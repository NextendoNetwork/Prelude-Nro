// Prelude — Nintendo Switch homebrew for the Nextendo Network.
// Copyright (C) 2026 Nextendo Network
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>.

// ============================================================
//  Nextendo .nro — rendu UI (style Nimbus/Pretendo).
//  Framebuffer libnx + texte FreeType (police Poppins, OFL, depuis le romfs)
//  + affichage des vraies images (logo Nextendo + icône Switch) en RGBA brut.
// ============================================================
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <switch.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "ui.h"
#include "ui_theme.h"
#include "nextendo_apply.h"
#include "nextendo_flag.h"
#include "lang.h"
#include "nextendo_update.h"   // NEXTENDO_VERSION_* pour le contexte de l en-tete

#define IMG 200                 // taille des .rgba (200x200)
#define IMG_BYTES (IMG * IMG * 4)

static Framebuffer s_fb;
static FT_Library  s_ft;
static FT_Face     s_bold, s_semi, s_reg;     // Poppins Bold / SemiBold / Regular
static u8         *s_bBuf, *s_sBuf, *s_rBuf;  // buffers TTF (gardés en vie)
static u8         *s_logo, *s_ninten;         // images RGBA 200x200

static inline u32 packColor(Color c) { return RGBA8(c.r, c.g, c.b, c.a); }

// ---- theme ----------------------------------------------------------------
// Deux jeux de valeurs, calques sur le systeme. Le theme clair n'est PAS
// l'inversion du sombre : le vert et l'ambre concus pour un fond noir tombent
// sous le seuil de lisibilite des qu'on eclaircit, donc ils sont assombris.
UiTheme g_theme = THEME_DARK;

#define THEMED(fn, dark, light) \
    Color fn(void) { return g_theme == THEME_LIGHT ? (light) : (dark); }

THEMED(theme_bg,    COL(0x2D,0x2D,0x2D), COL(0xEB,0xEB,0xEB))
THEMED(theme_pane,  COL(0x3D,0x3D,0x3D), COL(0xFF,0xFF,0xFF))
THEMED(theme_rail,  COL(0x35,0x35,0x35), COL(0xF7,0xF7,0xF7))
THEMED(theme_sep,   COL(0x4A,0x4A,0x4A), COL(0xDC,0xDC,0xDC))
THEMED(theme_sel,   COL(0x4A,0x4A,0x4A), COL(0xFF,0xFF,0xFF))
THEMED(theme_text,  COL(0xFF,0xFF,0xFF), COL(0x2D,0x2D,0x2D))
THEMED(theme_text2, COL(0xA8,0xA8,0xA8), COL(0x76,0x76,0x76))
THEMED(theme_ok,    COL(0x3F,0xBF,0x6A), COL(0x1E,0x8E,0x45))
THEMED(theme_warn,  COL(0xF5,0xA6,0x23), COL(0xB0,0x6E,0x00))
#undef THEMED

// ---- pixels ----
static inline void putPixel(u32 *b, u32 st, int x, int y, u32 c) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return;
    b[y * (st / sizeof(u32)) + x] = c;
}
static inline u32 getPixel(u32 *b, u32 st, int x, int y) {
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H) return 0;
    return b[y * (st / sizeof(u32)) + x];
}
static inline u32 blendPix(u32 dst, u8 sr, u8 sg, u8 sb, u8 a) {
    u8 dr = dst & 0xFF, dg = (dst >> 8) & 0xFF, db = (dst >> 16) & 0xFF;
    return RGBA8((sr * a + dr * (255 - a)) / 255,
                 (sg * a + dg * (255 - a)) / 255,
                 (sb * a + db * (255 - a)) / 255, 255);
}

// ---- formes ----
static void fillRect(u32 *b, u32 st, int x, int y, int w, int h, u32 c) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            putPixel(b, st, x + i, y + j, c);
}
static void roundedCard(u32 *b, u32 st, int x, int y, int w, int h, int r, u32 c) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    fillRect(b, st, x + r, y, w - 2 * r, h, c);
    fillRect(b, st, x, y + r, r, h - 2 * r, c);
    fillRect(b, st, x + w - r, y + r, r, h - 2 * r, c);
    int cx[4] = { x + r, x + w - r - 1, x + r, x + w - r - 1 };
    int cy[4] = { y + r, y + r, y + h - r - 1, y + h - r - 1 };
    for (int k = 0; k < 4; k++)
        for (int j = -r; j <= r; j++)
            for (int i = -r; i <= r; i++)
                if (i * i + j * j <= r * r) {
                    bool ok = true;
                    if (k == 0 && (i > 0 || j > 0)) ok = false;
                    if (k == 1 && (i < 0 || j > 0)) ok = false;
                    if (k == 2 && (i > 0 || j < 0)) ok = false;
                    if (k == 3 && (i < 0 || j < 0)) ok = false;
                    if (ok) putPixel(b, st, cx[k] + i, cy[k] + j, c);
                }
}

// ---- blit image RGBA (mise à l'échelle nearest + alpha) ----
static void blitImg(u32 *b, u32 st, int dx, int dy, int dw, int dh, u8 *img) {
    if (!img) return;
    for (int y = 0; y < dh; y++) {
        int sy = y * IMG / dh;
        for (int x = 0; x < dw; x++) {
            int sx = x * IMG / dw;
            u8 *p = img + (sy * IMG + sx) * 4;
            u8 a = p[3];
            if (!a) continue;
            u32 d = getPixel(b, st, dx + x, dy + y);
            putPixel(b, st, dx + x, dy + y, blendPix(d, p[0], p[1], p[2], a));
        }
    }
}

// ---- texte (FreeType, police choisie, blit alpha) ----
static int measureF(FT_Face fc, int px, const char *s) {
    if (FT_Set_Pixel_Sizes(fc, 0, px)) return 0;
    int w = 0; u32 i = 0, len = strlen(s); uint32_t cp; ssize_t u;
    while (i < len) {
        u = decode_utf8(&cp, (const uint8_t *)&s[i]); if (u <= 0) break; i += u;
        if (FT_Load_Char(fc, cp, FT_LOAD_DEFAULT)) continue;
        w += fc->glyph->advance.x >> 6;
    }
    return w;
}
static void drawF(u32 *b, u32 st, FT_Face fc, int x, int y, int px, u32 col, const char *s) {
    u8 cr = col & 0xFF, cg = (col >> 8) & 0xFF, cb = (col >> 16) & 0xFF;
    if (FT_Set_Pixel_Sizes(fc, 0, px)) return;
    int penX = x;
    u32 i = 0, len = strlen(s); uint32_t cp; ssize_t u;
    while (i < len) {
        u = decode_utf8(&cp, (const uint8_t *)&s[i]); if (u <= 0) break; i += u;
        if (FT_Load_Char(fc, cp, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot g = fc->glyph; FT_Bitmap *bm = &g->bitmap;
        if (bm->pixel_mode == FT_PIXEL_MODE_GRAY && bm->buffer) {
            int gx = penX + g->bitmap_left, gy = y - g->bitmap_top;
            u8 *src = bm->buffer;
            for (unsigned ry = 0; ry < bm->rows; ry++) {
                for (unsigned rx = 0; rx < bm->width; rx++) {
                    u8 a = src[rx]; if (!a) continue;
                    int px2 = gx + (int)rx, py2 = gy + (int)ry;
                    u32 d = getPixel(b, st, px2, py2);
                    putPixel(b, st, px2, py2, blendPix(d, cr, cg, cb, a));
                }
                src += bm->pitch;
            }
        }
        penX += g->advance.x >> 6;
    }
}
static void drawCF(u32 *b, u32 st, FT_Face fc, int cx, int y, int px, u32 col, const char *s) {
    drawF(b, st, fc, cx - measureF(fc, px, s) / 2, y, px, col, s);
}

// ===========================================================================
//  CROMO DEL SISTEMA — en-tete, barre de boutons, curseur, lignes d'option.
//
//  Ces primitives remplacent le dessin ad hoc de chaque ecran. Le but n'est pas
//  seulement l'aspect : c'est que la position d'un element ne soit plus un
//  nombre en dur recopie d'ecran en ecran. Tout part de HDR_H / FTR_H / ROW_H
//  et de l'echelle SP_*, donc ajouter une ligne ne demande plus de recalculer
//  les voisines.
// ===========================================================================

// Fond plein. Le systeme n'utilise pas de degrade : a plat, comme les Parametres.
static void chromeClear(u32 *b, u32 st) {
    u32 sw = st / sizeof(u32), bg = packColor(theme_bg());
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) b[y * sw + x] = bg;
}

// En-tete : pastille de marque, titre, et contexte a droite (console / version).
// Le filet du bas est la signature visuelle des ecrans systeme.
static void chromeHeader(u32 *b, u32 st, const char *title, const char *right) {
    int cy = HDR_H / 2 + FS_TITLE / 3;   // ligne de base optique du texte
    // Marque : le logo du romfs plutot qu'un carre de couleur. L'asset est deja
    // embarque (200x200 RGBA), donc c'est gratuit en taille, et un en-tete
    // systeme porte une identite, pas une pastille generique.
    if (s_logo) blitImg(b, st, SP_LG, HDR_H / 2 - 20, 40, 40, s_logo);
    else roundedCard(b, st, SP_LG, HDR_H / 2 - 16, 32, 32, 9, packColor(C_BLUE));
    drawF(b, st, s_semi, SP_LG + 40 + SP_SM, cy, FS_TITLE, packColor(theme_text()), title);
    if (right && right[0]) {
        int w = measureF(s_reg, FS_CAP, right);
        drawF(b, st, s_reg, FB_W - SP_LG - w, cy - 4, FS_CAP, packColor(theme_text2()), right);
    }
    fillRect(b, st, 0, HDR_H - 2, FB_W, 2, packColor(theme_sep()));
}

// Barre de boutons. Chaque indice = glyphe circulaire + verbe court, comme le
// systeme. On dessine le cercle avec roundedCard de rayon = moitie du cote :
// il n'y a pas de primitive cercle, et a 30 px la difference ne se voit pas.
typedef struct { const char *btn; const char *label; } Hint;

static void chromeFooter(u32 *b, u32 st, const Hint *hints, int n, const char *right) {
    int y = FB_H - FTR_H;
    fillRect(b, st, 0, y, FB_W, 2, packColor(theme_sep()));
    int cy = y + FTR_H / 2;
    int x  = SP_LG;
    for (int i = 0; i < n; i++) {
        int d = 30;
        roundedCard(b, st, x, cy - d / 2, d, d, d / 2, packColor(theme_text2()));
        int bw = measureF(s_semi, FS_CAP, hints[i].btn);
        drawF(b, st, s_semi, x + (d - bw) / 2, cy + FS_CAP / 3,
              FS_CAP, packColor(theme_bg()), hints[i].btn);
        x += d + SP_XS;
        drawF(b, st, s_reg, x, cy + FS_CAP / 3, FS_CAP, packColor(theme_text2()), hints[i].label);
        x += measureF(s_reg, FS_CAP, hints[i].label) + SP_LG;
    }
    if (right && right[0]) {
        int w = measureF(s_reg, FS_CAP, right);
        drawF(b, st, s_reg, FB_W - SP_LG - w, cy + FS_CAP / 3,
              FS_CAP, packColor(theme_text2()), right);
    }
}

// Curseur de selection : anneau cyan AUTOUR de la surface, qui garde sa couleur.
// Le systeme ne remplit pas l'element focalise — remplir est un reflexe de web,
// et ca fait perdre l'etat (actif/inactif) que la couleur de fond portait.
static void chromeCursor(u32 *b, u32 st, int x, int y, int w, int h) {
    u32 c = packColor(C_CYAN);
    roundedCard(b, st, x - 4, y - 4, w + 8, h + 8, RADIUS + 4, c);
}

// Ligne d'option : surface + titre + sous-titre optionnel. Renvoie le y de la
// ligne suivante, pour que l'appelant empile sans calculer d'offsets.
static int chromeRow(u32 *b, u32 st, int x, int y, int w,
                     bool focused, const char *label, const char *sub) {
    if (focused) chromeCursor(b, st, x, y, w, ROW_H);
    roundedCard(b, st, x, y, w, ROW_H, RADIUS, packColor(theme_pane()));
    if (sub && sub[0]) {
        drawF(b, st, s_semi, x + SP_MD, y + 34, FS_ITEM, packColor(theme_text()), label);
        drawF(b, st, s_reg,  x + SP_MD, y + 62, FS_CAP,  packColor(theme_text2()), sub);
    } else {
        drawF(b, st, s_semi, x + SP_MD, y + ROW_H / 2 + FS_ITEM / 3,
              FS_ITEM, packColor(theme_text()), label);
    }
    return y + ROW_H + SP_XS;
}

// Pastille d'etat, alignee a droite d'une ligne.
static void chromeBadge(u32 *b, u32 st, int rowX, int rowY, int rowW,
                        const char *text, Color bg, Color fg) {
    int tw = measureF(s_semi, FS_CAP, text);
    int pw = tw + SP_MD * 2, ph = 34;
    int px = rowX + rowW - SP_MD - pw, py = rowY + (ROW_H - ph) / 2;
    roundedCard(b, st, px, py, pw, ph, ph / 2, packColor(bg));
    drawF(b, st, s_semi, px + SP_MD, py + ph / 2 + FS_CAP / 3, FS_CAP, packColor(fg), text);
}

// Interrupteur systeme : piste + pastille, deux roundedCard. Un booleen se lit
// d'un coup d'oeil ainsi, alors que l'ancien "[X] Desactivar OC" obligeait a lire
// le libelle pour deduire l'etat courant.
static void chromeToggle(u32 *b, u32 st, int rowX, int rowY, int rowW, bool on) {
    int tw = 68, th = 36;
    int tx = rowX + rowW - SP_MD - tw, ty = rowY + (ROW_H - th) / 2;
    roundedCard(b, st, tx, ty, tw, th, th / 2, packColor(on ? C_CYAN : theme_sep()));
    int kd = th - 8;
    roundedCard(b, st, on ? tx + tw - kd - 4 : tx + 4, ty + 4, kd, kd, kd / 2,
                packColor(COL(0xFF, 0xFF, 0xFF)));
}

// Intitule de section (majuscules, discret) au-dessus d'un groupe de lignes.
static int chromeSection(u32 *b, u32 st, int x, int y, const char *text) {
    drawF(b, st, s_semi, x, y + FS_LABEL, FS_LABEL, packColor(theme_text2()), text);
    return y + FS_LABEL + SP_SM;
}

// ---- chargement police + image depuis le romfs ----
static bool loadFace(const char *path, FT_Face *face, u8 **buf) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    *buf = (u8 *)malloc(sz);
    if (!*buf) { fclose(f); return false; }
    bool ok = (fread(*buf, 1, sz, f) == (size_t)sz);
    fclose(f);
    if (!ok) return false;
    return FT_New_Memory_Face(s_ft, *buf, sz, 0, face) == 0;
}
static u8 *loadImg(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    u8 *b = (u8 *)malloc(IMG_BYTES);
    if (!b) { fclose(f); return NULL; }
    size_t n = fread(b, 1, IMG_BYTES, f);
    fclose(f);
    if (n != IMG_BYTES) { free(b); return NULL; }
    return b;
}

// ============================================================
bool ui_init(void) {
    if (FT_Init_FreeType(&s_ft)) return false;
    if (!loadFace("romfs:/Poppins-Bold.ttf", &s_bold, &s_bBuf)) return false;
    if (!loadFace("romfs:/Poppins-SemiBold.ttf", &s_semi, &s_sBuf)) s_semi = s_bold;
    if (!loadFace("romfs:/Poppins-Regular.ttf", &s_reg, &s_rBuf)) s_reg = s_bold;
    s_logo   = loadImg("romfs:/logo.rgba");
    s_ninten = loadImg("romfs:/nintendo.rgba");
    framebufferCreate(&s_fb, nwindowGetDefault(), FB_W, FB_H, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&s_fb);
    return true;
}

void ui_exit(void) {
    framebufferClose(&s_fb);
    if (s_logo) free(s_logo);
    if (s_ninten) free(s_ninten);
    FT_Done_FreeType(s_ft);
    if (s_bBuf) free(s_bBuf);
    if (s_sBuf) free(s_sBuf);
    if (s_rBuf) free(s_rBuf);
}

// ------- Ecran principal : rail de navigation + panneau -------
static const char *railLabel(int i) {
    switch (i) {
        case RAIL_MODE: return lang_str(STR_RAIL_MODE);
        case RAIL_SSBU: return lang_str(STR_RAIL_SSBU);
        case RAIL_S2:   return lang_str(STR_RAIL_S2);
        case RAIL_FLAG: return lang_str(STR_RAIL_FLAG);
        default:        return lang_str(STR_RAIL_LANG);
    }
}

// Nombre de lignes du panneau pour une section. main.c en a besoin pour borner
// le deplacement du focus : la logique de navigation ne doit pas deviner ce que
// le rendu affiche, sinon les deux divergent des qu'on ajoute une ligne.
int ui_pane_rows(int railSel, bool ssbuInstalled) {
    switch (railSel) {
        case RAIL_MODE: return 2;                        // Nextendo / Nintendo
        case RAIL_SSBU: return ssbuInstalled ? 2 : 1;    // mod (+ overclock si installe)
        case RAIL_LANG: return 4;                        // EN / ES / PT / FR
        default:        return 1;                        // S2, drapeau : une action
    }
}

static void drawRail(u32 *b, u32 st, int railSel, bool railFocused) {
    fillRect(b, st, 0, BODY_Y, RAIL_W, BODY_H, packColor(theme_rail()));
    fillRect(b, st, RAIL_W - 2, BODY_Y, 2, BODY_H, packColor(theme_sep()));

    int y = BODY_Y + SP_MD;
    for (int i = 0; i < RAIL_N; i++) {
        bool on = (railSel == i);
        if (on) {
            fillRect(b, st, 0, y, RAIL_W - 2, 56, packColor(theme_sel()));
            // Liseré plein quand le rail a le focus, attenue quand il l'a cede
            // au panneau : on voit d'un coup d'oeil OU vont les fleches.
            fillRect(b, st, 0, y, railFocused ? 6 : 3, 56,
                     packColor(railFocused ? C_CYAN : theme_sep()));
        }
        drawF(b, st, on ? s_semi : s_reg, SP_LG, y + 36, FS_ITEM,
              packColor(on ? theme_text() : theme_text2()), railLabel(i));
        y += 58;
    }
}

void ui_draw_picker(int railSel, int paneSel, bool paneFocused, int current,
                    const char *status, int updMaj, int updMin, int updPatch,
                    const char *flagCode, bool ssbuInstalled, bool ssbuOcDisabled) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    chromeClear(b, st);

    char ctx[64];
    snprintf(ctx, sizeof(ctx), "v%d.%d.%d", NEXTENDO_VERSION_MAJOR,
             NEXTENDO_VERSION_MINOR, NEXTENDO_VERSION_PATCH);
    chromeHeader(b, st, lang_str(STR_TITLE_PRELUDE), ctx);
    drawRail(b, st, railSel, !paneFocused);

    int x = PANE_X + SP_LG, w = PANE_W - SP_LG * 2;
    int y = BODY_Y + SP_MD;

    if (updMaj > 0) {
        char m[96];
        snprintf(m, sizeof(m), lang_str(STR_UPDATE_BANNER), updMaj, updMin, updPatch);
        roundedCard(b, st, x, y, w, 56, RADIUS, packColor(C_RED));
        drawF(b, st, s_semi, x + SP_MD, y + 36, FS_BODY, packColor(COL(0xFF,0xFF,0xFF)), m);
        y += 56 + SP_SM;
    }

    y = chromeSection(b, st, x, y, railLabel(railSel));
    // Le curseur n'apparait que si le panneau a le focus : sinon les fleches
    // agissent sur le rail, et montrer deux curseurs mentirait sur leur effet.
    #define FOC(i) (paneFocused && paneSel == (i))

    if (railSel == RAIL_MODE) {
        for (int i = 0; i < 2; i++) {
            bool isNx = (i == CHOICE_NEXTENDO);
            int rowY = y;
            y = chromeRow(b, st, x, y, w, FOC(i), isNx ? "Nextendo" : "Nintendo",
                          lang_str(isNx ? STR_DESC_NEXTENDO : STR_DESC_NINTENDO));
            roundedCard(b, st, x + SP_MD - 4, rowY + ROW_H / 2 - 6, 12, 12, 6,
                        packColor(isNx ? C_BLUE : C_RED));
            if (current == i)
                chromeBadge(b, st, x, rowY, w, lang_str(STR_BADGE_ACTIVE),
                            theme_ok(), COL(0xFF,0xFF,0xFF));
        }
    } else if (railSel == RAIL_SSBU) {
        int rowY = y;
        y = chromeRow(b, st, x, y, w, FOC(0), lang_str(STR_S2_TITLE_MOD),
                      lang_str(STR_SSBU_APPLIES));
        chromeBadge(b, st, x, rowY, w,
                    lang_str(ssbuInstalled ? STR_SSBU_INSTALLED : STR_SSBU_NOT_INSTALLED),
                    ssbuInstalled ? theme_ok() : theme_sep(),
                    ssbuInstalled ? COL(0xFF,0xFF,0xFF) : theme_text2());
        if (ssbuInstalled) {
            rowY = y;
            y = chromeRow(b, st, x, y, w, FOC(1), lang_str(STR_SSBU_OC),
                          lang_str(ssbuOcDisabled ? STR_SSBU_OC_OFF_DESC
                                                  : STR_SSBU_OC_ON_DESC));
            chromeToggle(b, st, x, rowY, w, !ssbuOcDisabled);
        }
    } else if (railSel == RAIL_S2) {
        y = chromeRow(b, st, x, y, w, FOC(0), lang_str(STR_RAIL_S2), lang_str(STR_DESC_S2));
    } else if (railSel == RAIL_FLAG) {
        int rowY = y;
        y = chromeRow(b, st, x, y, w, FOC(0), lang_str(STR_RAIL_FLAG), lang_str(STR_DESC_FLAG));
        if (flagCode && flagCode[0])
            chromeBadge(b, st, x, rowY, w, flagCode, theme_sep(), theme_text());
    } else {
        static const StringID ids[4] = { STR_LANG_EN, STR_LANG_ES, STR_LANG_PT, STR_LANG_FR };
        for (int i = 0; i < 4; i++) {
            int rowY = y;
            y = chromeRow(b, st, x, y, w, FOC(i), lang_str(ids[i]), NULL);
            if (i == (int)g_lang)
                chromeBadge(b, st, x, rowY, w, lang_str(STR_LANG_DEFAULT),
                            theme_ok(), COL(0xFF,0xFF,0xFF));
        }
    }
    #undef FOC

    if (status && status[0])
        drawF(b, st, s_semi, x, FB_H - FTR_H - SP_SM, FS_CAP, packColor(C_CYAN), status);

    // La barre de boutons dit ce que font les touches ICI et maintenant — pas une
    // liste fixe. C'est la moitie du travail d'une barre systeme.
    Hint h[4]; int n = 0;
    if (paneFocused) {
        h[n++] = (Hint){ "A", lang_str(railSel == RAIL_MODE ? STR_HINT_APPLY : STR_HINT_CHANGE) };
        h[n++] = (Hint){ "B", lang_str(STR_HINT_BACK) };
    } else {
        h[n++] = (Hint){ "A", lang_str(STR_HINT_OPEN) };
        h[n++] = (Hint){ "+", lang_str(STR_HINT_EXIT) };
    }
    chromeFooter(b, st, h, n, NULL);
    framebufferEnd(&s_fb);
}

// ------- Dialogue de confirmation (style systeme) -------
// Le systeme ne centre pas un mur de texte : boite compacte, et les ACTIONS en
// bas, separees par un filet. On garde les glyphes A/B dessus parce que l'entree
// reste A/B — annoncer des boutons cliquables qu'on ne peut pas parcourir serait
// mentir sur le modele d'interaction.
void ui_draw_confirm(int selection, bool warnNoEmummc) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    bool nx = (selection == CHOICE_NEXTENDO);

    chromeClear(b, st);
    chromeHeader(b, st, lang_str(STR_TITLE_PRELUDE), NULL);

    // Voile : le dialogue doit se lire comme pose PAR-DESSUS l'ecran, pas comme
    // un ecran de plus. C'est ce qui distingue une modale d'une navigation.
    u32 sw = st / sizeof(u32);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) {
            u32 d = b[y * sw + x];
            b[y * sw + x] = blendPix(d, 0, 0, 0, g_theme == THEME_LIGHT ? 90 : 130);
        }

    int dw = 780, dh = warnNoEmummc ? 470 : 300;
    int dx = (FB_W - dw) / 2, dy = (FB_H - dh) / 2;
    roundedCard(b, st, dx, dy, dw, dh, RADIUS + 6, packColor(theme_pane()));

    int y = dy + SP_XL;
    drawCF(b, st, s_semi, FB_W / 2, y, FS_BIG, packColor(theme_text()),
           lang_str(nx ? STR_CONFIRM_NEXTENDO : STR_CONFIRM_NINTENDO));
    y += SP_LG;
    drawCF(b, st, s_reg, FB_W / 2, y, FS_BODY, packColor(theme_text2()),
           lang_str(STR_CONFIRM_REBOOT));
    y += SP_MD + SP_XS;
    // warnNoEmummc == false => la console A un emuMMC (PRODINFO blanchi -> online HS).
    drawCF(b, st, s_reg, FB_W / 2, y, FS_CAP, packColor(theme_text2()),
           lang_str(nx ? STR_CONFIRM_RESTART_NEXTENDO
                       : (warnNoEmummc ? STR_CONFIRM_RESTART_NINTENDO
                                       : STR_CONFIRM_RESTART_NINTENDO_EMU)));

    if (warnNoEmummc) {
        y += SP_MD;
        int wx = dx + SP_LG, ww = dw - SP_LG * 2, wh = 150;
        roundedCard(b, st, wx, y, ww, wh, RADIUS, packColor(theme_sel()));
        fillRect(b, st, wx, y, 5, wh, packColor(theme_warn()));
        int ty = y + SP_MD + SP_XS;
        drawF(b, st, s_semi, wx + SP_MD, ty, FS_BODY, packColor(theme_warn()),
              lang_str(STR_WARN_TITLE));
        const StringID ln[3] = { STR_WARN_LINE1, STR_WARN_LINE2, STR_WARN_LINE3 };
        for (int i = 0; i < 3; i++) {
            ty += 30;
            drawF(b, st, s_reg, wx + SP_MD, ty, FS_CAP, packColor(theme_text2()),
                  lang_str(ln[i]));
        }
    }

    // Actions en bas, separees par un filet vertical : disposition du systeme.
    int ay = dy + dh - 64;
    fillRect(b, st, dx, ay, dw, 2, packColor(theme_sep()));
    fillRect(b, st, dx + dw / 2, ay, 2, 64, packColor(theme_sep()));
    drawCF(b, st, s_reg,  dx + dw / 4,     ay + 42, FS_BODY,
           packColor(theme_text2()), lang_str(STR_CONFIRM_B));
    drawCF(b, st, s_semi, dx + dw * 3 / 4, ay + 42, FS_BODY,
           packColor(C_CYAN), lang_str(STR_CONFIRM_A));

    framebufferEnd(&s_fb);
}

// ------- Ecran de progression -------
void ui_draw_progress(const char *line) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    chromeClear(b, st);
    chromeHeader(b, st, lang_str(STR_TITLE_PRELUDE), NULL);
    drawCF(b, st, s_semi, FB_W / 2, FB_H / 2 - SP_XS, FS_BIG, packColor(theme_text()), line);
    drawCF(b, st, s_reg, FB_W / 2, FB_H / 2 + SP_LG, FS_BODY, packColor(theme_text2()),
           lang_str(STR_PROGRESS_WAIT));
    framebufferEnd(&s_fb);
}

// ------- Ecran de resultat -------
// Le statut est porte par une pastille coloree ET par le texte : la couleur
// seule exclut ceux qui la distinguent mal, et sur un ecran de resultat le
// message est justement la seule chose qui compte.
void ui_draw_result(const char *title, const char *msg, bool ok) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    chromeClear(b, st);
    chromeHeader(b, st, lang_str(STR_TITLE_PRELUDE), NULL);

    int dw = 800, dh = 260, dx = (FB_W - dw) / 2, dy = (FB_H - dh) / 2;
    roundedCard(b, st, dx, dy, dw, dh, RADIUS + 6, packColor(theme_pane()));
    fillRect(b, st, dx, dy, dw, 6, packColor(ok ? theme_ok() : C_RED));

    drawCF(b, st, s_semi, FB_W / 2, dy + 84, FS_BIG, packColor(theme_text()), title);
    drawCF(b, st, s_reg, FB_W / 2, dy + 84 + SP_LG + SP_XS, FS_BODY,
           packColor(theme_text2()), msg);

    const Hint h[] = { { "A", lang_str(STR_HINT_BACK) } };
    chromeFooter(b, st, h, 1, NULL);
    framebufferEnd(&s_fb);
}

// ------- Ecran d'explication "Planning en ligne Splatoon 2" -------
void ui_draw_s2_info(void) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    chromeClear(b, st);
    chromeHeader(b, st, lang_str(STR_S2_TITLE), NULL);

    int x = SP_XL, y = BODY_Y + SP_LG;
    const StringID ln[4] = { STR_S2_DESC1, STR_S2_DESC2, STR_S2_DESC3, STR_S2_DESC4 };
    for (int i = 0; i < 4; i++) {
        drawF(b, st, s_reg, x, y + FS_BODY, FS_BODY, packColor(theme_text2()), lang_str(ln[i]));
        y += SP_LG;
    }

    const Hint h[] = { { "A", lang_str(STR_SSBU_INSTALL) }, { "B", lang_str(STR_HINT_BACK) } };
    chromeFooter(b, st, h, 2, NULL);
    framebufferEnd(&s_fb);
}

// ------- Confirmation avant mise a jour -------
void ui_draw_upd_confirm(int buildMaj, int buildMin, int buildPatch) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    chromeClear(b, st);
    chromeHeader(b, st, lang_str(STR_TITLE_PRELUDE), NULL);

    int dw = 780, dh = 300, dx = (FB_W - dw) / 2, dy = (FB_H - dh) / 2;
    roundedCard(b, st, dx, dy, dw, dh, RADIUS + 6, packColor(theme_pane()));

    char ver[64];
    snprintf(ver, sizeof(ver), lang_str(STR_UPD_CONFIRM_VERSION), buildMaj, buildMin, buildPatch);
    drawCF(b, st, s_semi, FB_W / 2, dy + SP_XL, FS_BIG, packColor(theme_text()),
           lang_str(STR_UPD_CONFIRM_TITLE));
    drawCF(b, st, s_semi, FB_W / 2, dy + SP_XL + SP_LG, FS_BODY, packColor(C_CYAN), ver);
    drawCF(b, st, s_reg, FB_W / 2, dy + SP_XL + SP_LG * 2, FS_CAP,
           packColor(theme_text2()), lang_str(STR_UPD_CONFIRM_DESC));

    int ay = dy + dh - 64;
    fillRect(b, st, dx, ay, dw, 2, packColor(theme_sep()));
    fillRect(b, st, dx + dw / 2, ay, 2, 64, packColor(theme_sep()));
    drawCF(b, st, s_reg,  dx + dw / 4,     ay + 42, FS_BODY,
           packColor(theme_text2()), lang_str(STR_UPD_CONFIRM_B));
    drawCF(b, st, s_semi, dx + dw * 3 / 4, ay + 42, FS_BODY,
           packColor(C_CYAN), lang_str(STR_UPD_CONFIRM_A));
    framebufferEnd(&s_fb);
}

// ------- Toast -------
void ui_draw_toast(const char *text) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    int tw = measureF(s_semi, FS_BODY, text) + SP_LG * 2, th = 56;
    int tx = (FB_W - tw) / 2, ty = FB_H - FTR_H - th - SP_MD;
    roundedCard(b, st, tx, ty, tw, th, th / 2, packColor(theme_sel()));
    drawCF(b, st, s_semi, FB_W / 2, ty + th / 2 + FS_BODY / 3, FS_BODY,
           packColor(theme_text()), text);
    framebufferEnd(&s_fb);
}

void ui_draw_loading(const char *text) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    chromeClear(b, st);
    chromeHeader(b, st, lang_str(STR_TITLE_PRELUDE), NULL);
    drawCF(b, st, s_semi, FB_W / 2, FB_H / 2, FS_BIG, packColor(theme_text2()), text);
    framebufferEnd(&s_fb);
}

// ------- Liste de pays MK8D (defilante) -------
// FLAG_ROWS reste le nombre de lignes visibles (main.c s'en sert pour le
// defilement) : on ne change QUE la presentation, pas le contrat.
void ui_draw_flag_menu(int sel, int scroll, const char *currentCode) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    chromeClear(b, st);

    char ctx[64] = {0};
    if (currentCode && currentCode[0]) {
        int idx = flag_find_index(currentCode);
        if (idx >= 0) snprintf(ctx, sizeof(ctx), "%s  %s", currentCode, g_flags[idx].name);
        else          snprintf(ctx, sizeof(ctx), "%s", currentCode);
    } else {
        snprintf(ctx, sizeof(ctx), "%s", lang_str(STR_FLAG_NONE));
    }
    chromeHeader(b, st, lang_str(STR_FLAG_MENU_TITLE), ctx);

    // Lignes plus compactes que ROW_H : c'est une liste a parcourir, pas un
    // ecran de reglages. Le pas vient d'une constante, pas d'un nombre en dur.
    const int rh = 58;
    int x = SP_XL, w = FB_W - SP_XL * 2, y = BODY_Y + SP_SM;

    for (int r = 0; r < FLAG_ROWS; r++) {
        int idx = scroll + r;
        if (idx >= FLAG_COUNT) break;
        int ry = y + r * rh;
        if (ry + rh > FB_H - FTR_H) break;          // ne jamais deborder sur la barre
        bool hov = (idx == sel);
        bool ins = (currentCode && currentCode[0]
                    && strncmp(g_flags[idx].code, currentCode, 2) == 0);

        if (hov) chromeCursor(b, st, x, ry, w, rh - 6);
        roundedCard(b, st, x, ry, w, rh - 6, RADIUS, packColor(theme_pane()));

        u32 col = packColor(hov ? theme_text() : theme_text2());
        drawF(b, st, s_semi, x + SP_MD,      ry + 36, FS_ITEM, col, g_flags[idx].code);
        drawF(b, st, s_reg,  x + SP_MD + 74, ry + 36, FS_BODY, col, g_flags[idx].name);
        if (ins) {
            const char *lbl = lang_str(STR_FLAG_INSTALLED);
            int tw = measureF(s_semi, FS_CAP, lbl);
            drawF(b, st, s_semi, x + w - SP_MD - tw, ry + 36, FS_CAP,
                  packColor(theme_ok()), lbl);
        }
    }

    const Hint h[] = { { "A", lang_str(STR_SSBU_INSTALL) }, { "B", lang_str(STR_HINT_BACK) } };
    chromeFooter(b, st, h, 2, NULL);
    framebufferEnd(&s_fb);
}

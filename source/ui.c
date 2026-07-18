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
#include "lang.h"

#define IMG 200                 // taille des .rgba (200x200)
#define IMG_BYTES (IMG * IMG * 4)

static Framebuffer s_fb;
static FT_Library  s_ft;
static FT_Face     s_bold, s_semi, s_reg;     // Poppins Bold / SemiBold / Regular
static u8         *s_bBuf, *s_sBuf, *s_rBuf;  // buffers TTF (gardés en vie)
static u8         *s_logo, *s_ninten;         // images RGBA 200x200

static inline u32 packColor(Color c) { return RGBA8(c.r, c.g, c.b, c.a); }

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

// ---- chargement police + image depuis le romfs ----
static bool loadFace(const char *path, FT_Face *face, u8 **buf) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
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

// sel = carte visee (bascule) ; isCurrent = mode deja charge (badge ACTUEL, non-basculable).
static void drawCard(u32 *b, u32 st, int x, bool sel, bool isCurrent, int choice) {
    int y = CARD_Y;
    Color acc = (choice == CHOICE_NEXTENDO) ? C_BLUE : C_RED;
    // Liseré coloré UNIQUEMENT sur la carte visée (celle vers laquelle on bascule).
    if (sel) roundedCard(b, st, x - 4, y - 4, CARD_W + 8, CARD_H + 8, 26, packColor(acc));
    // Carte actuelle = plus sombre (grisée, non-basculable) ; carte visée = claire.
    roundedCard(b, st, x, y, CARD_W, CARD_H, 22,
                packColor(sel ? C_CARD_SEL : (isCurrent ? C_BG : C_CARD)));

    int cx = x + CARD_W / 2;
    blitImg(b, st, cx - 75, y + 34, 150, 150, choice == CHOICE_NEXTENDO ? s_logo : s_ninten);
    drawCF(b, st, s_semi, cx, y + 235, 36,
           packColor(isCurrent ? C_SUBTLE : C_TITLE),
           choice == CHOICE_NEXTENDO ? "Nextendo" : "Nintendo");

    int by = y + 268;
    if (isCurrent) {
        // badge ACTUEL (vert) = le mode charge en ce moment
        const char *bd = lang_str(STR_CURRENT_BADGE);
        int bw = measureF(s_bold, 18, bd) + 30, bx = cx - bw / 2;
        roundedCard(b, st, bx, by, bw, 30, 15, packColor(C_GREEN));
        drawCF(b, st, s_bold, cx, by + 21, 18, packColor(COL(0x0C, 0x1A, 0x10)), bd);
    } else if (sel) {
        // badge BASCULER (couleur du mode) = la cible
        const char *bd = lang_str(STR_SWITCH_BADGE);
        int bw = measureF(s_bold, 18, bd) + 30, bx = cx - bw / 2;
        roundedCard(b, st, bx, by, bw, 30, 15, packColor(acc));
        drawCF(b, st, s_bold, cx, by + 21, 18, packColor(C_TITLE), bd);
    }
}

// Barre "Splatoon 2 — Planning en ligne" sous les deux cartes de mode.
static void drawS2Bar(u32 *b, u32 st, bool focused) {
    if (focused)
        roundedCard(b, st, S2BAR_X - 4, S2BAR_Y - 4, S2BAR_W + 8, S2BAR_H + 8, 20, packColor(C_S2));
    roundedCard(b, st, S2BAR_X, S2BAR_Y, S2BAR_W, S2BAR_H, 16,
                packColor(focused ? C_CARD_SEL : C_CARD));
    drawCF(b, st, s_semi, FB_W / 2, S2BAR_Y + 40, 25,
           packColor(focused ? C_TITLE : C_SUBTLE),
           lang_str(STR_S2_BAR));
}

void ui_draw_picker(int selection, int current, int focus, const char *status, int updVer) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    u32 bg = packColor(C_BG), sw = st / sizeof(u32);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) b[y * sw + x] = bg;

    // Bandeau de mise a jour OBLIGATOIRE (si une version plus recente existe).
    if (updVer > 0) {
        char m[96];
        snprintf(m, sizeof(m), lang_str(STR_UPDATE_BANNER), updVer);
        drawCF(b, st, s_semi, FB_W / 2, 32, 20, packColor(C_RED), m);
    }

    drawCF(b, st, s_bold, FB_W / 2, 84, 42, packColor(C_TITLE), lang_str(STR_TITLE_PRELUDE));
    // sous-titre centre : "Mode actuel : NEXTENDO" (le nom du mode en sa couleur)
    {
        const char *lbl = lang_str(STR_MODE_ACTUAL_PREFIX);
        const char *md  = (current == CHOICE_NEXTENDO) ? "NEXTENDO" : "NINTENDO";
        int wl = measureF(s_reg, 23, lbl), wm = measureF(s_semi, 23, md);
        int sx = FB_W / 2 - (wl + wm) / 2;
        drawF(b, st, s_reg,  sx,      120, 23, packColor(C_SUBTLE), lbl);
        drawF(b, st, s_semi, sx + wl, 120, 23,
              packColor(current == CHOICE_NEXTENDO ? C_BLUE : C_RED), md);
    }

    bool modeFocus = (focus == FOCUS_MODE);
    drawCard(b, st, CARD0_X, modeFocus && selection == CHOICE_NEXTENDO, current == CHOICE_NEXTENDO, CHOICE_NEXTENDO);
    drawCard(b, st, CARD1_X, modeFocus && selection == CHOICE_NINTENDO, current == CHOICE_NINTENDO, CHOICE_NINTENDO);
    drawS2Bar(b, st, focus == FOCUS_S2);

    // Ligne de contexte selon le focus.
    int cy = S2BAR_Y + S2BAR_H + 30;   // ~594
    if (focus == FOCUS_S2) {
        drawCF(b, st, s_reg, FB_W / 2, cy, 21, packColor(C_SUBTLE),
               lang_str(STR_DESC_S2));
    } else if (selection == CHOICE_NEXTENDO) {
        drawCF(b, st, s_reg, FB_W / 2, cy, 21, packColor(C_SUBTLE),
               lang_str(STR_DESC_NEXTENDO));
    } else {
        drawCF(b, st, s_reg, FB_W / 2, cy, 21, packColor(C_SUBTLE),
               lang_str(STR_DESC_NINTENDO));
    }

    drawCF(b, st, s_reg, FB_W / 2, FB_H - 50, 21, packColor(C_SUBTLE),
           focus == FOCUS_S2 ? lang_str(STR_HELP_S2) : lang_str(STR_HELP_MODE));
    if (status && status[0])
        drawCF(b, st, s_semi, FB_W / 2, FB_H - 22, 23, packColor(C_BLUE), status);

    framebufferEnd(&s_fb);
}

// Popup de CONFIRMATION plein ecran avant d'appliquer + redemarrer.
void ui_draw_confirm(int selection, bool warnNoEmummc) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    u32 sw = st / sizeof(u32), bg = packColor(C_BG);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) b[y * sw + x] = bg;

    bool nx = (selection == CHOICE_NEXTENDO);
    u32 acc = packColor(nx ? C_BLUE : C_RED);
    // L'avertissement ne concerne que le mode NINTENDO : c'est le seul ou la console parle aux
    // vrais serveurs. En mode NEXTENDO le DNS-MITM confine tout chez nous, l'identite ne fuit pas.
    bool warn = warnNoEmummc && !nx;

    // carte centrale avec liseré coloré ; agrandie pour loger l'avertissement
    int cw = 820, ch = warn ? 500 : 380, cxx = (FB_W - cw) / 2, cyy = (FB_H - ch) / 2;
    roundedCard(b, st, cxx - 4, cyy - 4, cw + 8, ch + 8, 28, acc);
    roundedCard(b, st, cxx, cyy, cw, ch, 24, packColor(C_CARD));

    int cx = FB_W / 2;
    drawCF(b, st, s_bold, cx, cyy + 78, 40, packColor(C_TITLE),
           lang_str(nx ? STR_CONFIRM_NEXTENDO : STR_CONFIRM_NINTENDO));
    drawCF(b, st, s_semi, cx, cyy + 138, 27, acc,
           lang_str(STR_CONFIRM_REBOOT));
    drawCF(b, st, s_reg, cx, cyy + 188, 22, packColor(C_SUBTLE),
           lang_str(nx ? STR_CONFIRM_RESTART_NEXTENDO : STR_CONFIRM_RESTART_NINTENDO));

    if (warn) {
        u32 wc = packColor(C_WARN);
        drawCF(b, st, s_bold, cx, cyy + 240, 25, wc,
               lang_str(STR_WARN_TITLE));
        drawCF(b, st, s_reg, cx, cyy + 278, 21, packColor(C_SUBTLE),
               lang_str(STR_WARN_LINE1));
        drawCF(b, st, s_reg, cx, cyy + 306, 21, packColor(C_SUBTLE),
               lang_str(STR_WARN_LINE2));
        drawCF(b, st, s_reg, cx, cyy + 334, 21, packColor(C_SUBTLE),
               lang_str(STR_WARN_LINE3));
        drawCF(b, st, s_reg, cx, cyy + 362, 21, packColor(C_SUBTLE),
               lang_str(STR_WARN_LINE4));
    }

    drawCF(b, st, s_reg, cx, cyy + (warn ? 400 : 218), 22, packColor(C_SUBTLE),
           lang_str(STR_CONFIRM_CLOSE_GAMES));

    // rappel des touches, en bas de la carte
    int by = cyy + ch - 46;
    drawCF(b, st, s_semi, cx - 150, by, 26, packColor(C_GREEN), lang_str(STR_CONFIRM_A));
    drawCF(b, st, s_semi, cx + 150, by, 26, packColor(C_SUBTLE), lang_str(STR_CONFIRM_B));

    framebufferEnd(&s_fb);
}

// ------- Ecran d'explication "Planning en ligne Splatoon 2" -------
void ui_draw_s2_info(void) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    u32 sw = st / sizeof(u32), bg = packColor(C_BG);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) b[y * sw + x] = bg;

    u32 acc = packColor(C_S2);
    int cw = 940, ch = 440, cxx = (FB_W - cw) / 2, cyy = (FB_H - ch) / 2;
    roundedCard(b, st, cxx - 4, cyy - 4, cw + 8, ch + 8, 28, acc);
    roundedCard(b, st, cxx, cyy, cw, ch, 24, packColor(C_CARD));

    int cx = FB_W / 2;
    drawCF(b, st, s_bold, cx, cyy + 74, 38, packColor(C_TITLE), lang_str(STR_S2_TITLE));
    drawCF(b, st, s_reg, cx, cyy + 132, 22, packColor(C_SUBTLE),
           lang_str(STR_S2_DESC1));
    drawCF(b, st, s_reg, cx, cyy + 162, 22, packColor(C_SUBTLE),
           lang_str(STR_S2_DESC2));
    drawCF(b, st, s_semi, cx, cyy + 214, 22, acc,
           lang_str(STR_S2_DESC3));
    drawCF(b, st, s_semi, cx, cyy + 256, 22, packColor(C_GREEN),
           lang_str(STR_S2_DESC4));

    int by = cyy + ch - 46;
    drawCF(b, st, s_semi, cx - 160, by, 26, acc, lang_str(STR_S2_A));
    drawCF(b, st, s_semi, cx + 160, by, 26, packColor(C_SUBTLE), lang_str(STR_S2_B));

    framebufferEnd(&s_fb);
}

// ------- Ecran de progression -------
void ui_draw_progress(const char *line) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    u32 sw = st / sizeof(u32), bg = packColor(C_BG);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) b[y * sw + x] = bg;

    drawCF(b, st, s_bold, FB_W / 2, FB_H / 2 - 24, 36, packColor(C_TITLE), lang_str(STR_PROGRESS_TITLE));
    drawCF(b, st, s_semi, FB_W / 2, FB_H / 2 + 30, 26, packColor(C_S2), line ? line : lang_str(STR_PROGRESS_WAIT));

    framebufferEnd(&s_fb);
}

// ------- Ecran de resultat (succes vert / erreur rouge) -------
void ui_draw_result(const char *title, const char *msg, bool ok) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    u32 sw = st / sizeof(u32), bg = packColor(C_BG);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) b[y * sw + x] = bg;

    u32 acc = packColor(ok ? C_GREEN : C_RED);
    int cw = 940, ch = 300, cxx = (FB_W - cw) / 2, cyy = (FB_H - ch) / 2;
    roundedCard(b, st, cxx - 4, cyy - 4, cw + 8, ch + 8, 28, acc);
    roundedCard(b, st, cxx, cyy, cw, ch, 24, packColor(C_CARD));

    int cx = FB_W / 2;
    drawCF(b, st, s_bold, cx, cyy + 92, 36, acc, title ? title : "");
    drawCF(b, st, s_reg, cx, cyy + 158, 22, packColor(C_TITLE), msg ? msg : "");
    drawCF(b, st, s_semi, cx, cyy + ch - 44, 24, packColor(C_SUBTLE), lang_str(STR_RESULT_RETURN));

    framebufferEnd(&s_fb);
}

// ------- Menu de sélection de langue (R depuis le picker) -------
void ui_draw_lang_menu(int sel) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    u32 sw = st / sizeof(u32), bg = packColor(C_BG);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) b[y * sw + x] = bg;

    u32 acc = packColor(C_BLUE);
    int cw = 700, ch = 460, cxx = (FB_W - cw) / 2, cyy = (FB_H - ch) / 2;
    roundedCard(b, st, cxx - 4, cyy - 4, cw + 8, ch + 8, 28, acc);
    roundedCard(b, st, cxx, cyy, cw, ch, 24, packColor(C_CARD));

    int cx = FB_W / 2;
    drawCF(b, st, s_bold, cx, cyy + 60, 36, packColor(C_TITLE), lang_str(STR_LANG_TITLE));

    static const StringID lang_ids[4] = { STR_LANG_EN, STR_LANG_ES, STR_LANG_PT, STR_LANG_FR };
    for (int i = 0; i < 4; i++) {
        int rowY = cyy + 120 + i * 70;
        bool hovered = (i == sel);
        bool current = (i == g_lang);

        if (hovered)
            roundedCard(b, st, cxx + 20, rowY - 4, cw - 40, 54, 12, packColor(C_CARD_SEL));

        drawF(b, st, s_semi, cxx + 50, rowY + 22, 28,
              packColor(hovered ? C_TITLE : (current ? C_GREEN : C_SUBTLE)),
              lang_str(lang_ids[i]));

        if (current) {
            const char *defLabel = lang_str(STR_LANG_DEFAULT);
            int dw = measureF(s_reg, 18, defLabel);
            drawF(b, st, s_reg, cxx + cw - 50 - dw, rowY + 24, 18,
                  packColor(C_GREEN), defLabel);
        }
    }

    drawCF(b, st, s_semi, cx, cyy + ch - 40, 22, packColor(C_GREEN),
           lang_str(STR_LANG_A_SELECT));
    drawCF(b, st, s_semi, cx, cyy + ch - 12, 20, packColor(C_SUBTLE),
           lang_str(STR_LANG_B_BACK));

    framebufferEnd(&s_fb);
}

// ------- Ecran de confirmation avant mise a jour -------
void ui_draw_upd_confirm(int buildVer) {
    u32 st;
    u32 *b = (u32 *)framebufferBegin(&s_fb, &st);
    u32 sw = st / sizeof(u32), bg = packColor(C_BG);
    for (int y = 0; y < FB_H; y++)
        for (int x = 0; x < FB_W; x++) b[y * sw + x] = bg;

    u32 acc = packColor(C_BLUE);
    int cw = 700, ch = 360, cxx = (FB_W - cw) / 2, cyy = (FB_H - ch) / 2;
    roundedCard(b, st, cxx - 4, cyy - 4, cw + 8, ch + 8, 28, acc);
    roundedCard(b, st, cxx, cyy, cw, ch, 24, packColor(C_CARD));

    int cx = FB_W / 2;
    drawCF(b, st, s_bold, cx, cyy + 70, 36, packColor(C_TITLE),
           lang_str(STR_UPD_CONFIRM_TITLE));

    char ver[64];
    snprintf(ver, sizeof(ver), lang_str(STR_UPD_CONFIRM_VERSION), buildVer);
    drawCF(b, st, s_semi, cx, cyy + 130, 24, acc, ver);

    drawCF(b, st, s_reg, cx, cyy + 180, 22, packColor(C_SUBTLE),
           lang_str(STR_UPD_CONFIRM_DESC));

    int by = cyy + ch - 50;
    drawCF(b, st, s_semi, cx - 150, by, 24, packColor(C_GREEN),
           lang_str(STR_UPD_CONFIRM_A));
    drawCF(b, st, s_semi, cx + 150, by, 24, packColor(C_SUBTLE),
           lang_str(STR_UPD_CONFIRM_B));

    framebufferEnd(&s_fb);
}

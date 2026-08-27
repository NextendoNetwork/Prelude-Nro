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

// Thème UI : palette du système (menu HOME / Paramètres) + deux couleurs de marque.
#ifndef UI_THEME_H
#define UI_THEME_H
#include <switch.h>

typedef struct { u8 r, g, b, a; } Color;
#define COL(R, G, B) ((Color){ (R), (G), (B), 255 })

// Les couleurs sont des accesseurs theme_*, pas des constantes : elles suivent le theme clair/sombre de la console.
// Les deux couleurs de MARQUE ne bougent pas : elles tiennent sur fond clair comme sombre.
#define C_RED      COL(0xE4, 0x00, 0x14)  // rouge marque (mode Nintendo)
#define C_BLUE     COL(0x1C, 0xA9, 0xE0)  // bleu marque (mode Nextendo)
#define C_S2       COL(0xF0, 0x2D, 0x7D)  // rose Splatoon
#define C_CYAN     COL(0x00, 0xC3, 0xE3)  // cyan systeme : curseur de selection, actifs

typedef enum { THEME_DARK = 0, THEME_LIGHT = 1 } UiTheme;
extern UiTheme g_theme;

Color theme_bg(void);        // fond de l'ecran
Color theme_pane(void);      // surface d'une ligne / carte posee sur le fond
Color theme_rail(void);      // colonne de navigation (legerement detachee du fond)
Color theme_sep(void);       // filets : sous l'en-tete, au-dessus de la barre de boutons
Color theme_sel(void);       // fond de l'entree active du rail

Color theme_text(void);      // texte principal
Color theme_text2(void);     // texte secondaire / libelles de boutons

// Re-teintes en clair : les valeurs concues pour un fond sombre tombent sous le seuil de contraste.
Color theme_ok(void);        // succes / "installe"
Color theme_warn(void);      // avertissement (console sans emuMMC)

// Anciens noms, a retirer quand ui.c n'en utilise plus.
#define C_BG       theme_bg()
#define C_TITLE    theme_text()
#define C_SUBTLE   theme_text2()
#define C_CARD     theme_pane()
#define C_CARD_SEL theme_sel()
#define C_GREEN    theme_ok()
#define C_WARN     theme_warn()

#define FB_W 1280
#define FB_H 720

// Echelle d'espacement (multiples de 8) : toute marge sort d'ici, plus aucun offset en dur.
#define SP_XS   8
#define SP_SM   16
#define SP_MD   24
#define SP_LG   40
#define SP_XL   64

#define HDR_H   96                 // en-tete : titre + contexte console
#define FTR_H   76                 // barre de boutons
#define RAIL_W  320                // colonne de navigation
#define BODY_Y  HDR_H              // haut de la zone centrale
#define BODY_H  (FB_H - HDR_H - FTR_H)
#define PANE_X  RAIL_W
#define PANE_W  (FB_W - RAIL_W)

// Echelle typographique.
#define FS_TITLE 34                // titre de l'en-tete
#define FS_BIG   30                // titre de dialogue
#define FS_ITEM  25                // libelle d'une ligne / entree du rail
#define FS_BODY  21                // texte courant
#define FS_CAP   18                // secondaire, badges, boutons
#define FS_LABEL 16                // intitule de section (majuscules)

#define ROW_H    84                // hauteur d'une ligne d'option
#define RADIUS   12                // rayon standard des surfaces

#define CHOICE_NEXTENDO 0
#define CHOICE_NINTENDO 1

// Navigation a deux colonnes : rail (sections) a gauche, panneau (lignes) a droite.
#define RAIL_MODE 0
#define RAIL_SSBU 1
#define RAIL_S2   2
#define RAIL_S3   3
#define RAIL_FLAG 4
#define RAIL_LANG 5
#define RAIL_N    6

#define COL_RAIL 0
#define COL_PANE 1

// Anciens noms, encore utilises par le journal de sortie et quelques traces.
#define FOCUS_MODE RAIL_MODE
#define FOCUS_S2   RAIL_S2
#define FOCUS_FLAG RAIL_FLAG

// Orange MK8D (accent de la section drapeau).
#define C_FLAG     COL(0xFF, 0x6B, 0x00)

#endif // UI_THEME_H

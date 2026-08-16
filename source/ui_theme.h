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
//  Nextendo .nro — thème UI (style Nimbus/Pretendo, marque rouge+bleu).
// ============================================================
#ifndef UI_THEME_H
#define UI_THEME_H
#include <switch.h>

typedef struct { u8 r, g, b, a; } Color;
#define COL(R, G, B) ((Color){ (R), (G), (B), 255 })

// ------------------------------------------------------------------
//  PALETTE : on adopte celle du systeme (menu HOME / Parametres) plutot qu'une
//  identite maison. Prelude EST une app de reglages — memes conventions, meme
//  vocabulaire visuel : l'utilisateur n'a rien de neuf a apprendre.
//
//  Deux themes, comme la console. Les couleurs ne sont plus des constantes mais
//  des accesseurs (theme_*) lisant le theme courant : une constante ne peut pas
//  changer a l'execution, et on veut suivre le reglage de la console.
//
//  Les deux couleurs de MARQUE (bleu Nextendo / rouge Nintendo) ne bougent pas :
//  c'est l'identite du projet, et elles tiennent sur fond clair comme sombre.
// ------------------------------------------------------------------

#define C_RED      COL(0xE4, 0x00, 0x14)  // rouge marque (mode Nintendo)
#define C_BLUE     COL(0x1C, 0xA9, 0xE0)  // bleu marque (mode Nextendo)
#define C_S2       COL(0xF0, 0x2D, 0x7D)  // rose Splatoon
#define C_CYAN     COL(0x00, 0xC3, 0xE3)  // cyan systeme : curseur de selection, actifs

typedef enum { THEME_DARK = 0, THEME_LIGHT = 1 } UiTheme;
extern UiTheme g_theme;

// Fonds et surfaces
Color theme_bg(void);        // fond de l'ecran
Color theme_pane(void);      // surface d'une ligne / carte posee sur le fond
Color theme_rail(void);      // colonne de navigation (legerement detachee du fond)
Color theme_sep(void);       // filets : sous l'en-tete, au-dessus de la barre de boutons
Color theme_sel(void);       // fond de l'entree active du rail

// Texte
Color theme_text(void);      // texte principal
Color theme_text2(void);     // texte secondaire / libelles de boutons

// Etats. Le vert et l'ambre sont RE-TEINTES en clair : les valeurs concues pour
// un fond sombre passent sous le seuil de contraste des qu'on eclaircit le fond.
Color theme_ok(void);        // succes / "installe"
Color theme_warn(void);      // avertissement (console sans emuMMC)

// --- Compatibilite : les anciens noms restent valides le temps que toutes les
//     pantallas passent au nouveau cromo. A retirer quand ui.c n'en utilise plus. ---
#define C_BG       theme_bg()
#define C_TITLE    theme_text()
#define C_SUBTLE   theme_text2()
#define C_CARD     theme_pane()
#define C_CARD_SEL theme_sel()
#define C_GREEN    theme_ok()
#define C_WARN     theme_warn()

#define FB_W 1280
#define FB_H 720

// ------------------------------------------------------------------
//  CROMO DEL SISTEMA. Trois elements structurels rendent une app "native" :
//  un en-tete titre, une colonne de navigation persistante, et une barre de
//  boutons fixe en bas. C'est ce qui manquait — pas la palette.
//
//  Echelle d'espacement (multiples de 8) : toute marge du nouveau cromo sort
//  d'ici. C'etait la vraie plaie de l'ancienne UI — des offsets en dur du genre
//  cyy+72 / cyy+128 / cyy+158, qu'il fallait tous recalculer pour ajouter UNE
//  ligne (vecu sur l'ecran du mod SSBU).
// ------------------------------------------------------------------
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

// Echelle typographique. Avant : 42/38/36/27/26/25/24/23/22/21 sans systeme.
#define FS_TITLE 34                // titre de l'en-tete
#define FS_BIG   30                // titre de dialogue
#define FS_ITEM  25                // libelle d'une ligne / entree du rail
#define FS_BODY  21                // texte courant
#define FS_CAP   18                // secondaire, badges, boutons
#define FS_LABEL 16                // intitule de section (majuscules)

#define ROW_H    84                // hauteur d'une ligne d'option
#define RADIUS   12                // rayon standard des surfaces
// NOTE : les constantes de l'ancienne maquette (CARD_*, S2BAR_*, BAR_*) ont ete
// retirees avec le redesign. Elles decrivaient deux cartes cote-a-cote et deux
// barres sous elles ; le nouvel ecran est un rail + un panneau, dont la geometrie
// sort de HDR_H / FTR_H / RAIL_W / ROW_H et de l'echelle SP_*.

#define CHOICE_NEXTENDO 0
#define CHOICE_NINTENDO 1

// ------------------------------------------------------------------
//  NAVIGATION A DEUX COLONNES (modele des Parametres).
//  Rail focalise  : haut/bas changent de section, droite ou A entre dans le
//                   panneau, B/+ quitte.
//  Panneau focalise : haut/bas changent de ligne, A active, B ou gauche
//                   revient au rail.
//  Avant, gauche/droite faisaient tourner une "zone" et le mode ne se
//  choisissait meme pas : sel etait fige sur "l'autre mode" au demarrage.
// ------------------------------------------------------------------
#define RAIL_MODE 0
#define RAIL_SSBU 1
#define RAIL_S2   2
#define RAIL_FLAG 3
#define RAIL_LANG 4
#define RAIL_N    5

#define COL_RAIL 0
#define COL_PANE 1

// Anciens noms, encore utilises par le journal de sortie et quelques traces.
#define FOCUS_MODE RAIL_MODE
#define FOCUS_S2   RAIL_S2
#define FOCUS_FLAG RAIL_FLAG

// Orange MK8D (accent de la section drapeau).
#define C_FLAG     COL(0xFF, 0x6B, 0x00)

#endif // UI_THEME_H

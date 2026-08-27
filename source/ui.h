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

// Rendu UI : framebuffer 1280x720 RGBA8888 + police partagee via FreeType.
#ifndef UI_H
#define UI_H
#include <switch.h>
#include "nextendo_apply.h"   // NextendoS3Status : la section Splatoon 3 affiche cet etat

bool ui_init(void);
void ui_exit(void);

// --- Tactile. Le framebuffer est en 1280x720, comme le tactile : les coordonnees se correspondent 1:1.
// Les zones sont enregistrees PENDANT le dessin, donc elles collent toujours a ce qui est affiche.
enum {
    UI_TAP_NONE   = 0,
    UI_TAP_RAIL   = 0x100,   // + index de section (RAIL_*)
    UI_TAP_ROW    = 0x200,   // + index de ligne du panneau / de la liste
    UI_TAP_BTN    = 0x300,   // + UI_BTN_* : la barre declenche EXACTEMENT la touche qu'elle affiche
    UI_TAP_UPDATE = 0x400,   // bandeau de mise a jour
    UI_TAP_YES    = 0x500,   // moitie droite de la barre d'actions d'une modale
    UI_TAP_NO     = 0x600,   // moitie gauche
};
enum { UI_BTN_A = 0, UI_BTN_B, UI_BTN_Y, UI_BTN_PLUS };

#define UI_TAP_KIND(t)  ((t) & ~0xFF)
#define UI_TAP_INDEX(t) ((t) & 0xFF)

// Zone touchee sur la DERNIERE frame dessinee, ou UI_TAP_NONE.
int ui_tap_at(int x, int y);

// Ecran principal. paneFocused oriente les fleches (panneau vs rail) et seul le cote focalise porte le curseur.
void ui_draw_picker(int railSel, int paneSel, bool paneFocused, int current,
                    const char *status, int updMaj, int updMin, int updPatch,
                    const char *flagCode, bool ssbuInstalled, bool ssbuOcDisabled,
                    const NextendoS3Status *s3);

// Nombre de lignes du panneau : main.c borne le focus avec ca.
int ui_pane_rows(int railSel, bool ssbuInstalled);

// warnNoEmummc : sans emuMMC le mode NINTENDO ne protege pas l'identite, l'ecran le dit franchement.
void ui_draw_confirm(int selection, bool warnNoEmummc);

void ui_draw_s2_info(void);

void ui_draw_progress(const char *line);

// `pct` est borne a 0..100 ; `detail` est la ligne sous la barre (NULL => "Patiente...").
void ui_draw_progress_bar(const char *line, int pct, const char *detail);

// Succes en vert, erreur en rouge.
void ui_draw_result(const char *title, const char *msg, bool ok);

// Toast en bas de l'ecran : pose dans la frame du picker, jamais dans une frame a lui.
void ui_set_toast(const char *text);

void ui_draw_loading(const char *text);

void ui_draw_upd_confirm(int buildMaj, int buildMin, int buildPatch);

// scroll = premier index visible ; currentCode = drapeau deja installe, ou "".
void ui_draw_flag_menu(int sel, int scroll, const char *currentCode);

// Question OUI/NON par-dessus l'ecran courant (A = oui, B = non).
void ui_draw_question(const char *title, const char *l1, const char *l2);

#endif // UI_H

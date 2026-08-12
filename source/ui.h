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
//  Nextendo .nro — rendu UI (framebuffer + police partagee via FreeType).
// ============================================================
#ifndef UI_H
#define UI_H
#include <switch.h>

// Init framebuffer (1280x720 RGBA8888) + police partagee (pl + FreeType).
bool ui_init(void);
void ui_exit(void);

// Dessine l'ecran de bascule. selection = carte visee (mode NON-actuel), current = mode ACTUEL
// (badge "ACTUEL"), focus = FOCUS_MODE / FOCUS_S2 / FOCUS_FLAG, status = optionnel.
// updMaj > 0 -> bandeau discret "MàJ dispo (vN) - Y pour installer".
// flagCode = code 2 lettres du drapeau installe, ou "" si aucun.
void ui_draw_picker(int selection, int current, int focus, const char *status,
                    int updMaj, int updMin, int updPatch, const char *flagCode);

// Popup de CONFIRMATION avant d'appliquer + redemarrer (A = confirmer, B = annuler).
// warnNoEmummc : console sans emuMMC (CFW sur la memoire interne). Le mode NINTENDO ne peut alors
// offrir AUCUNE protection d'identite -> l'ecran de confirmation le dit franchement. Passe par
// main.c (detecte une seule fois) : sonder spl a chaque frame serait absurde.
void ui_draw_confirm(int selection, bool warnNoEmummc);

// Ecran d'explication "Planning en ligne Splatoon 2" (A = installer, B = retour).
void ui_draw_s2_info(void);

// Ecran de progression pendant l'installation (une ligne d'etat centree).
void ui_draw_progress(const char *line);

// Ecran de resultat (succes vert / erreur rouge). A/B = retour.
void ui_draw_result(const char *title, const char *msg, bool ok);

// Menu de sélection de langue (R depuis le picker). sel = ligne survolée (0-2).
void ui_draw_lang_menu(int sel);

// Toast semi-transparent en bas de l'écran (texte centré).
void ui_draw_toast(const char *text);

// Ecran de chargement (titre "Prelude" + texte centree).
void ui_draw_loading(const char *text);

Framebuffer *ui_get_fb(void);

// Ecran de confirmation avant mise a jour (A = installer, B = annuler).
void ui_draw_upd_confirm(int buildMaj, int buildMin, int buildPatch);

// Menu de selection de drapeau MK8D (110 pays, scrollable).
// sel = index selectionne (0-109), scroll = premier index visible,
// currentCode = code 2 lettres installe (ou "" si aucun).
void ui_draw_flag_menu(int sel, int scroll, const char *currentCode);

// SSBU Online Deluxe mod: shows current install status + [A] install/remove,
// [X] toggle the mod's bundled overclock (ocDisabled), [B] back.
void ui_draw_ssbu_mod(bool installed, bool ocDisabled);

#endif // UI_H

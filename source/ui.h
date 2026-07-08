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
// (badge "ACTUEL"), focus = FOCUS_MODE (paire de cartes) ou FOCUS_S2 (barre planning), status = optionnel.
// updVer > 0 -> bandeau discret "MàJ dispo (vN) - Y pour installer".
void ui_draw_picker(int selection, int current, int focus, const char *status, int updVer);

// Popup de CONFIRMATION avant d'appliquer + redemarrer (A = confirmer, B = annuler).
void ui_draw_confirm(int selection);

// Ecran d'explication "Planning en ligne Splatoon 2" (A = installer, B = retour).
void ui_draw_s2_info(void);

// Ecran de progression pendant l'installation (une ligne d'etat centree).
void ui_draw_progress(const char *line);

// Ecran de resultat (succes vert / erreur rouge). A/B = retour.
void ui_draw_result(const char *title, const char *msg, bool ok);

#endif // UI_H

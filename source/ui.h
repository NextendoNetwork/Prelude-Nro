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
#include "nextendo_apply.h"   // NextendoS3Status: the Splatoon 3 section displays this state

bool ui_init(void);
void ui_exit(void);

// --- Touch. The framebuffer is 1280x720, same as the touchscreen, so coordinates map 1:1.
// Zones are registered WHILE drawing, so they always match what is on screen.
enum {
    UI_TAP_NONE   = 0,
    UI_TAP_RAIL   = 0x100,   // + section index (RAIL_*)
    UI_TAP_ROW    = 0x200,   // + row index within the panel / list
    UI_TAP_BTN    = 0x300,   // + UI_BTN_*: the bar fires EXACTLY the button it displays
    UI_TAP_UPDATE = 0x400,   // the update banner
    UI_TAP_YES    = 0x500,   // right half of a modal's action bar
    UI_TAP_NO     = 0x600,   // left half
};
enum { UI_BTN_A = 0, UI_BTN_B, UI_BTN_Y, UI_BTN_PLUS };

#define UI_TAP_KIND(t)  ((t) & ~0xFF)
#define UI_TAP_INDEX(t) ((t) & 0xFF)

// Zone hit on the LAST drawn frame, or UI_TAP_NONE.
int ui_tap_at(int x, int y);

// Main screen. paneFocused aims the d-pad (panel vs rail), and only the focused side draws a cursor.
void ui_draw_picker(int railSel, int paneSel, bool paneFocused, int current,
                    const char *status, int updMaj, int updMin, int updPatch,
                    const char *flagCode, bool ssbuInstalled, bool ssbuOcDisabled,
                    const NextendoS3Status *s3);

// Row count of the panel: main.c clamps the focus with it.
int ui_pane_rows(int railSel, bool ssbuInstalled);

// warnNoEmummc: without emuMMC, NINTENDO mode protects no identity, and the screen says so plainly.
void ui_draw_confirm(int selection, bool warnNoEmummc);

void ui_draw_s2_info(void);

void ui_draw_progress(const char *line);

// `pct` is clamped to 0..100; `detail` is the line under the bar (NULL => "Please wait...").
void ui_draw_progress_bar(const char *line, int pct, const char *detail);

// Success in green, failure in red.
void ui_draw_result(const char *title, const char *msg, bool ok);

// Toast at the bottom of the screen: drawn inside the picker's frame, never a frame of its own.
void ui_set_toast(const char *text);

void ui_draw_loading(const char *text);

void ui_draw_upd_confirm(int buildMaj, int buildMin, int buildPatch);

// scroll = first visible index; currentCode = the flag already installed, or "".
void ui_draw_flag_menu(int sel, int scroll, const char *currentCode);

// Generic YES/NO question laid over the current screen (A = yes, B = no).
void ui_draw_question(const char *title, const char *l1, const char *l2);

#endif // UI_H

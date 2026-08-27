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

// UI theme: the system palette (HOME menu / Settings) plus two brand colours.
#ifndef UI_THEME_H
#define UI_THEME_H
#include <switch.h>

typedef struct { u8 r, g, b, a; } Color;
#define COL(R, G, B) ((Color){ (R), (G), (B), 255 })

// Colours are theme_* accessors, not constants: they follow the console's light/dark setting.
// The two BRAND colours never move: they hold up on light and dark backgrounds alike.
#define C_RED      COL(0xE4, 0x00, 0x14)  // brand red (Nintendo mode)
#define C_BLUE     COL(0x1C, 0xA9, 0xE0)  // brand blue (Nextendo mode)
#define C_S2       COL(0xF0, 0x2D, 0x7D)  // Splatoon pink
#define C_CYAN     COL(0x00, 0xC3, 0xE3)  // system cyan: selection cursor, active items

typedef enum { THEME_DARK = 0, THEME_LIGHT = 1 } UiTheme;
extern UiTheme g_theme;

Color theme_bg(void);        // screen background
Color theme_pane(void);      // surface of a row / card sitting on the background
Color theme_rail(void);      // navigation column (slightly lifted off the background)
Color theme_sep(void);       // hairlines: under the header, above the button bar
Color theme_sel(void);       // background of the active rail entry

Color theme_text(void);      // primary text
Color theme_text2(void);     // secondary text / button labels

// Re-tinted in light mode: values designed for a dark background fall under the contrast threshold.
Color theme_ok(void);        // success / "installed"
Color theme_warn(void);      // warning (console without emuMMC)

// Legacy names, to drop once ui.c no longer uses any of them.
#define C_BG       theme_bg()
#define C_TITLE    theme_text()
#define C_SUBTLE   theme_text2()
#define C_CARD     theme_pane()
#define C_CARD_SEL theme_sel()
#define C_GREEN    theme_ok()
#define C_WARN     theme_warn()

#define FB_W 1280
#define FB_H 720

// Spacing scale (multiples of 8): every margin comes from here, no hardcoded offsets left.
#define SP_XS   8
#define SP_SM   16
#define SP_MD   24
#define SP_LG   40
#define SP_XL   64

#define HDR_H   96                 // header: title + console context
#define FTR_H   76                 // button bar
#define RAIL_W  320                // navigation column
#define BODY_Y  HDR_H              // top of the central area
#define BODY_H  (FB_H - HDR_H - FTR_H)
#define PANE_X  RAIL_W
#define PANE_W  (FB_W - RAIL_W)

// Type scale.
#define FS_TITLE 34                // header title
#define FS_BIG   30                // dialog title
#define FS_ITEM  25                // row label / rail entry
#define FS_BODY  21                // body text
#define FS_CAP   18                // secondary, badges, buttons
#define FS_LABEL 16                // section heading (uppercase)

#define ROW_H    84                // height of an option row
#define RADIUS   12                // standard corner radius

#define CHOICE_NEXTENDO 0
#define CHOICE_NINTENDO 1

// Two-column navigation: rail (sections) on the left, panel (rows) on the right.
#define RAIL_MODE 0
#define RAIL_SSBU 1
#define RAIL_S2   2
#define RAIL_S3   3
#define RAIL_FLAG 4
#define RAIL_LANG 5
#define RAIL_N    6

#define COL_RAIL 0
#define COL_PANE 1

// Legacy names, still used by the exit log and a few traces.
#define FOCUS_MODE RAIL_MODE
#define FOCUS_S2   RAIL_S2
#define FOCUS_FLAG RAIL_FLAG

// MK8D orange (accent for the flag section).
#define C_FLAG     COL(0xFF, 0x6B, 0x00)

#endif // UI_THEME_H

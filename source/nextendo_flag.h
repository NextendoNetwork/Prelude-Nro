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
//  Nextendo .nro — MK8D country flag installer.
//  Downloads the IPS ExeFS patch for the selected country from
//  https://github.com/alyeri/nextendo-mk8d-country-flags and
//  installs it at:
//      sdmc:/atmosphere/exefs_patches/Nextendo Country XX/
//  Title ID: 0100152000022000, Build ID: FE941ED5BA14BE5D505698DA1BBF4FE7
//  Only one flag can be active at a time; flag_install() removes
//  any previously installed patch before writing the new one.
// ============================================================
#ifndef NEXTENDO_FLAG_H
#define NEXTENDO_FLAG_H

typedef struct { char code[3]; const char *name; } FlagEntry;

extern const FlagEntry g_flags[];
#define FLAG_COUNT 110
#define FLAG_ROWS  9   // rows visible at once in the flag menu

// Scan sdmc:/atmosphere/exefs_patches/ for a "Nextendo Country XX" folder.
// Sets out_code to the 2-letter code (+ NUL) or "" if none found.
void flag_detect_current(char out_code[3]);

// Download and install the IPS patch for the given 2-letter code.
// Requires socketInitializeDefault() + sslInitialize() to have been called.
// Returns 0 on success, -1 on network error, -2 on write error.
int flag_install(const char *code);

// Remove all Nextendo Country flag patches from sdmc:/atmosphere/exefs_patches/.
void flag_remove(void);

// Return index into g_flags[] for the given 2-letter code, or -1.
int flag_find_index(const char *code);

#endif // NEXTENDO_FLAG_H

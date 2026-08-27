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

// MK8D country flag: one IPS patch at a time in sdmc:/atmosphere/exefs_patches/Nextendo Country XX/ (title 0100152000022000).
#ifndef NEXTENDO_FLAG_H
#define NEXTENDO_FLAG_H

typedef struct { char code[3]; const char *name; } FlagEntry;

extern const FlagEntry g_flags[];
#define FLAG_COUNT 110
#define FLAG_ROWS  9   // rows visible at once in the flag menu

// out_code = 2-letter code of the installed flag, or "" if none.
void flag_detect_current(char out_code[3]);

// Returns 0 on success, -1 on a bad country code, -2 on write error.
int flag_install(const char *code);

void flag_remove(void);

// Index into g_flags[], or -1 if the code is unknown.
int flag_find_index(const char *code);

#endif // NEXTENDO_FLAG_H

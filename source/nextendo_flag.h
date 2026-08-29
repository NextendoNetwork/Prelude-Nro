// Prelude — Nintendo Switch homebrew for the Nextendo Network.
// Copyright (C) 2026 Nextendo Network
//
// Licensed under the PolyForm Shield License 1.0.0.
//
// You may use, modify and distribute this software for any purpose EXCEPT providing a product
// that competes with Nextendo Network, or with any product Nextendo Network provides using it.
//
// See LICENSE.md for the full terms, or <https://polyformproject.org/licenses/shield/1.0.0>.
//
// Required Notice: Copyright 2026 Nextendo Network

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

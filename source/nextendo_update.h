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

// Self-update: /api/nro/latest at launch, then replacement of the currently running .nro.
#ifndef NEXTENDO_UPDATE_H
#define NEXTENDO_UPDATE_H
#include <switch.h>

// Version of THIS build. BUMP on every release (the server reports the latest).
#define NEXTENDO_BUILD 61

// Comes from APP_VERSION (Makefile) via -D. These values only apply to a build outside make.
#ifndef NEXTENDO_VERSION_MAJOR
#define NEXTENDO_VERSION_MAJOR 3
#define NEXTENDO_VERSION_MINOR 3
#define NEXTENDO_VERSION_PATCH 9
#endif

typedef struct {
    bool available;   // a semver newer than NEXTENDO_VERSION_* is available
    int  maj, min, patch;  // server version (from the semver tag)
    long size;        // expected .nro size (download integrity check)
} NextendoUpdate;

typedef enum {
    NUP_OK = 0,
    NUP_NET_FAIL,     // download failed
    NUP_SIZE_FAIL,    // received size != announced size (corrupt download)
    NUP_WRITE_FAIL    // could not write to the SD card
} nextendo_update_result;

// hbmenu's argv[0]: the file that will be replaced. Without this call, falls back to sdmc:/switch/nextendo.nro.
void nextendo_update_set_self_path(const char *argv0);

NextendoUpdate nextendo_update_check(void);

// The two slow phases: the download (17 MB) then writing it to the card. `total` is 0 when unknown.
typedef enum {
    NUP_PHASE_DOWNLOAD = 0,
    NUP_PHASE_INSTALL  = 1,
} nextendo_update_phase;

typedef void (*nextendo_progress_fn)(nextendo_update_phase phase, long done, long total);

// Replaces the running .nro via a .new + rename. `onProgress` may be NULL.
nextendo_update_result nextendo_update_apply(long expectedSize, nextendo_progress_fn onProgress);

#endif // NEXTENDO_UPDATE_H

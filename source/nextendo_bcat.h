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

// Splatoon 2 schedule: GET /api/bcat/<titleId> -> zip extracted into Atmosphere's LayeredFS (USA + EUR).
#ifndef NEXTENDO_BCAT_H
#define NEXTENDO_BCAT_H
#include <switch.h>

typedef enum {
    NB_OK = 0,           // installed
    NB_NET_FAIL,         // download failed (reason is in the log)
    NB_NET_CONNECT,      // server unreachable (timeout / connection refused)
    NB_NET_TIMEOUT,      // response cut short
    NB_NET_HTTP_ERR,     // server answered an HTTP code other than 200/204
    NB_NO_SCHEDULE,      // 204: nothing published
    NB_MOUNT_FAIL,       // (obsolete) kept for main.c compatibility
    NB_BAD_BUNDLE,       // unreadable bundle
    NB_WRITE_FAIL        // writing the file to the SD card failed
} nextendo_bcat_result;

// socketInitializeDefault() + sslInitialize() must be active before calling.
nextendo_bcat_result nextendo_bcat_install_s2(void);

extern Result g_last_rc;

#endif // NEXTENDO_BCAT_H

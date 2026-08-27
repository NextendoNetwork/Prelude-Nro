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

// Auto-mise a jour : /api/nro/latest au lancement, puis remplacement du .nro en cours d'execution.
#ifndef NEXTENDO_UPDATE_H
#define NEXTENDO_UPDATE_H
#include <switch.h>

// Version de CE build. A INCREMENTER a chaque release (le serveur renvoie la derniere).
#define NEXTENDO_BUILD 61

// Vient d'APP_VERSION (Makefile) via -D. Ces valeurs ne servent qu'a une compilation hors make.
#ifndef NEXTENDO_VERSION_MAJOR
#define NEXTENDO_VERSION_MAJOR 3
#define NEXTENDO_VERSION_MINOR 3
#define NEXTENDO_VERSION_PATCH 8
#endif

typedef struct {
    bool available;   // une version semver > NEXTENDO_VERSION_* est dispo
    int  maj, min, patch;  // version serveur (du tag semver)
    long size;        // taille attendue du .nro (verif du telechargement)
} NextendoUpdate;

typedef enum {
    NUP_OK = 0,
    NUP_NET_FAIL,     // telechargement impossible
    NUP_SIZE_FAIL,    // taille recue != taille annoncee (download corrompu)
    NUP_WRITE_FAIL    // ecriture SD impossible
} nextendo_update_result;

// argv[0] de hbmenu : le fichier qui sera remplace. Sans cet appel, repli sur sdmc:/switch/nextendo.nro.
void nextendo_update_set_self_path(const char *argv0);

NextendoUpdate nextendo_update_check(void);

// Les deux temps longs : le telechargement (17 Mo) puis la pose sur la carte. `total` vaut 0 si inconnu.
typedef enum {
    NUP_PHASE_DOWNLOAD = 0,
    NUP_PHASE_INSTALL  = 1,
} nextendo_update_phase;

typedef void (*nextendo_progress_fn)(nextendo_update_phase phase, long done, long total);

// Remplace le .nro en cours via un .new + rename. `onProgress` peut etre NULL.
nextendo_update_result nextendo_update_apply(long expectedSize, nextendo_progress_fn onProgress);

#endif // NEXTENDO_UPDATE_H

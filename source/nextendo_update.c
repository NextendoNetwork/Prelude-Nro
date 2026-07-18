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
//  Nextendo .nro — auto-mise a jour (via le serveur Nextendo).
//  Au lancement, on interroge /api/nro/latest ; si une version plus recente
//  existe, on affiche un bandeau. L'utilisateur peut telecharger le nouveau
//  .nro et remplacer /switch/nextendo.nro (le .nro courant tourne depuis la RAM).
// ============================================================
#include <switch.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#include "nextendo_update.h"
#include "nextendo_net.h"

#define UP_IP        "51.178.29.194"
#define UP_PORT      8095
#define UP_LATEST    "/api/nro/latest"
#define UP_DOWNLOAD  "/api/nro/download"
#define NRO_PATH     "sdmc:/switch/nextendo.nro"
#define NRO_TMP      "sdmc:/switch/nextendo.nro.new"

// Extrait la valeur entiere d'une cle JSON "key":N dans un buffer non NUL-termine.
static long json_int(const unsigned char *b, size_t len, const char *key) {
    size_t kl = strlen(key);
    for (size_t i = 0; i + kl < len; i++) {
        if (memcmp(b + i, key, kl) == 0) {
            size_t j = i + kl;
            while (j < len && (b[j] == ' ' || b[j] == ':' || b[j] == '"')) j++;
            long v = 0;
            bool got = false;
            while (j < len && b[j] >= '0' && b[j] <= '9') { v = v * 10 + (b[j] - '0'); j++; got = true; }
            if (got) return v;
        }
    }
    return -1;
}

NextendoUpdate nextendo_update_check(void) {
    NextendoUpdate u = { false, 0, 0 };
    socketInitializeDefault();
    size_t len = 0;
    int status = 0;
    unsigned char *body = net_http_get(UP_IP, UP_PORT, UP_LATEST, &len, &status);
    socketExit();
    if (body && status == 200) {
        long ver = json_int(body, len, "\"version\"");
        long sz  = json_int(body, len, "\"size\"");
        if (ver > NEXTENDO_BUILD) {
            u.available = true;
            u.latest = (int)ver;
            u.size = sz;
        }
    }
    if (body) free(body);
    return u;
}

nextendo_update_result nextendo_update_apply(long expectedSize) {
    socketInitializeDefault();

    // Ouvre le fichier temporaire AVANT l'appel HTTP (cible du streaming).
    FILE *f = fopen(NRO_TMP, "wb");
    if (!f) {
        mkdir("sdmc:/switch", 0777);
        f = fopen(NRO_TMP, "wb");
    }
    if (!f) { socketExit(); return NUP_WRITE_FAIL; }

    // Streame la reponse HTTP directement sur le disque (evite le buffer en RAM).
    int status = 0;
    long len = net_http_get_to_file(UP_IP, UP_PORT, UP_DOWNLOAD, f, &status);
    fclose(f);
    socketExit();

    if (len == -2) { remove(NRO_TMP); return NUP_WRITE_FAIL; }
    if (len < 0)   { remove(NRO_TMP); return NUP_NET_FAIL; }
    if (status != 200 || len < 4096) { remove(NRO_TMP); return NUP_NET_FAIL; }
    if (expectedSize > 0 && len != expectedSize) { remove(NRO_TMP); return NUP_SIZE_FAIL; }
    fsdevCommitDevice("sdmc");

    // Remplace l'ancien .nro (le courant tourne depuis la RAM -> ecrasement sans risque).
    // rename() echoue sur certaines cartes SD (fatfs) : dans ce cas on copie le fichier
    // temporaire vers NRO_PATH (sans buffer en RAM, on lit depuis le .new sur le disque).
    remove(NRO_PATH);
    if (rename(NRO_TMP, NRO_PATH) != 0) {
        FILE *src = fopen(NRO_TMP, "rb");
        if (!src) { remove(NRO_TMP); return NUP_WRITE_FAIL; }
        FILE *dst = fopen(NRO_PATH, "wb");
        if (!dst) { fclose(src); remove(NRO_TMP); return NUP_WRITE_FAIL; }
        char cbuf[16384];
        size_t n;
        bool ok = true;
        while ((n = fread(cbuf, 1, sizeof(cbuf), src)) > 0)
            if (fwrite(cbuf, 1, n, dst) != n) { ok = false; break; }
        fclose(src); fclose(dst);
        remove(NRO_TMP);
        if (!ok) return NUP_WRITE_FAIL;
    }

    fsdevCommitDevice("sdmc");
    return NUP_OK;
}

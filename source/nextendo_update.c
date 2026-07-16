// ============================================================
//  Nextendo .nro — auto-mise a jour.
// ============================================================
#include <switch.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#include "nextendo_update.h"
#include "nextendo_net.h"

#define UP_IP        "203.0.113.10"
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
    size_t len = 0;
    int status = 0;
    unsigned char *body = net_http_get(UP_IP, UP_PORT, UP_DOWNLOAD, &len, &status);
    socketExit();

    if (!body) return NUP_NET_FAIL;
    if (status != 200 || len < 4096) { free(body); return NUP_NET_FAIL; }   // un .nro fait > 4 Ko
    if (expectedSize > 0 && (long)len != expectedSize) { free(body); return NUP_SIZE_FAIL; }

    // Ecrit d'abord dans un .new temporaire (cree /switch si la carte ne l'a pas).
    FILE *f = fopen(NRO_TMP, "wb");
    if (!f) {
        mkdir("sdmc:/switch", 0777);
        f = fopen(NRO_TMP, "wb");
    }
    if (!f) { free(body); return NUP_WRITE_FAIL; }
    bool ok = (fwrite(body, 1, len, f) == len);
    fflush(f);
    fclose(f);
    if (!ok) { remove(NRO_TMP); free(body); return NUP_WRITE_FAIL; }
    fsdevCommitDevice("sdmc");

    // Remplace l'ancien .nro (le courant tourne depuis la RAM -> ecrasement sans risque).
    // rename() echoue sur certaines cartes SD (fatfs) : dans ce cas on ecrit NRO_PATH
    // directement (on garde 'body' jusqu'ici pour ce secours) au lieu de laisser
    // l'utilisateur sans .nro. C'etait la cause du "impossible d'ecrire sur la carte SD".
    remove(NRO_PATH);
    if (rename(NRO_TMP, NRO_PATH) != 0) {
        FILE *g = fopen(NRO_PATH, "wb");
        if (!g) { free(body); return NUP_WRITE_FAIL; }
        bool ok2 = (fwrite(body, 1, len, g) == len);
        fflush(g);
        fclose(g);
        remove(NRO_TMP);
        if (!ok2) { free(body); return NUP_WRITE_FAIL; }
    }
    free(body);
    fsdevCommitDevice("sdmc");
    return NUP_OK;
}

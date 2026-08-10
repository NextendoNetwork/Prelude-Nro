// Prelude -- Nintendo Switch homebrew for the Nextendo Network.
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
//  Nextendo .nro -- auto-update via GitHub Releases API.
//  Checks https://api.github.com/repos/NextendoNetwork/Prelude-Nro/releases/latest
//  for the latest version tag, compares semver with NEXTENDO_VERSION_*,
//  and downloads the .nro asset if a newer version is available.
// ============================================================
#include <switch.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#include "nextendo_update.h"
#include "nextendo_net.h"

// GitHub API for latest release
#define GH_API_HOST  "api.github.com"
#define GH_API_PATH  "/repos/NextendoNetwork/Prelude-Nro/releases/latest"
#define GH_API_PORT  443

#define NRO_PATH     "sdmc:/switch/nextendo.nro"
#define NRO_TMP      "sdmc:/switch/nextendo.nro.new"

static char g_download_url[512] = {0};
static long g_download_size = 0;

// Find a JSON string value for a given key, tolerating optional whitespace around ':'.
// Returns pointer to the first char inside the opening quote, or NULL.
static char *json_str_value(const char *haystack, const char *key) {
    char *p = strstr(haystack, key);
    if (!p) return NULL;
    p += strlen(key);
    while (*p == ' ' || *p == '\t') p++;  // skip whitespace after key (before ':')
    if (*p == ':') p++;
    while (*p == ' ' || *p == '\t') p++;  // skip whitespace after ':'
    if (*p != '"') return NULL;
    return p + 1;  // point inside the opening quote
}

// Parse the GitHub releases/latest JSON response.
// Tolerates both compact ("key":"val") and pretty-printed ("key": "val") formatting.
static bool parse_github_json(const unsigned char *b, size_t len, int *maj, int *min, int *patch,
                              char *url, size_t urlcap, long *size) {
    (void)len;

    // tag_name -> version
    char *tp = json_str_value((const char*)b, "\"tag_name\"");
    if (!tp) return false;
    if (*tp == 'v' || *tp == 'V') tp++;
    *maj = (int)strtol(tp, &tp, 10);
    if (*tp == '.') tp++;
    *min = (int)strtol(tp, &tp, 10);
    if (*tp == '.') tp++;
    *patch = (int)strtol(tp, NULL, 10);

    // browser_download_url -> NRO asset URL
    char *up = json_str_value((const char*)b, "\"browser_download_url\"");
    if (up) {
        char *ue = strchr(up, '"');
        if (ue) {
            size_t ul = (size_t)(ue - up);
            if (ul < urlcap) { memcpy(url, up, ul); url[ul] = '\0'; }
        }
    }

    // size -> asset byte count (strtol skips leading spaces automatically)
    char *sp = strstr((const char*)b, "\"size\":");
    if (sp) { sp += 7; *size = strtol(sp, NULL, 10); }

    return *maj > 0;
}

// Compare two semver triplets. Returns >0 if a>b, <0 if a<b, 0 if equal.
static int semver_cmp(int amaj, int amin, int apatch, int bmaj, int bmin, int bpatch) {
    if (amaj != bmaj) return amaj - bmaj;
    if (amin != bmin) return amin - bmin;
    return apatch - bpatch;
}

NextendoUpdate nextendo_update_check(void) {
    NextendoUpdate u = { false, 0, 0, 0, 0 };
    socketInitializeDefault();
    Result rc = sslInitialize(4);
    if (R_FAILED(rc)) { socketExit(); return u; }

    size_t len = 0;
    int status = 0;
    unsigned char *body = net_https_get(GH_API_HOST, GH_API_PATH, &len, &status);
    sslExit();
    socketExit();

    if (body && status == 200) {
        int maj = 0, min = 0, patch = 0; long sz = 0;
        if (parse_github_json(body, len, &maj, &min, &patch, g_download_url, sizeof(g_download_url), &sz)) {
            if (semver_cmp(maj, min, patch,
                           NEXTENDO_VERSION_MAJOR, NEXTENDO_VERSION_MINOR, NEXTENDO_VERSION_PATCH) > 0
                && sz > 4096) {
                u.available = true;
                u.maj = maj; u.min = min; u.patch = patch;
                u.size = sz;
                g_download_size = sz;
            }
        }
        free(body);
    }
    return u;
}

// Download and apply the update. Requires sslInitialize() before.
nextendo_update_result nextendo_update_apply(long expectedSize) {
    if (g_download_url[0] == '\0') return NUP_NET_FAIL;
    long expected = expectedSize > 0 ? expectedSize : g_download_size;

    FILE *f = fopen(NRO_TMP, "wb");
    if (!f) {
        mkdir("sdmc:/switch", 0777);
        f = fopen(NRO_TMP, "wb");
    }
    if (!f) return NUP_WRITE_FAIL;

    socketInitializeDefault();
    Result rc = sslInitialize(4);
    if (R_FAILED(rc)) { fclose(f); socketExit(); return NUP_NET_FAIL; }

    char host[256] = {0};
    char path[1024] = {0};
    if (sscanf(g_download_url, "https://%255[^/]%1023s", host, path) < 2) {
        sslExit(); fclose(f); remove(NRO_TMP); socketExit(); return NUP_NET_FAIL;
    }

    int status = 0;
    long len = net_https_get_to_file(host, path, f, &status);
    fclose(f);
    sslExit();
    socketExit();

    if (len == -2) { remove(NRO_TMP); return NUP_WRITE_FAIL; }
    if (len < 0)   { remove(NRO_TMP); return NUP_NET_FAIL; }
    if (status != 200 || len < 4096) { remove(NRO_TMP); return NUP_NET_FAIL; }
    if (expected > 0 && len != expected) { remove(NRO_TMP); return NUP_SIZE_FAIL; }
    fsdevCommitDevice("sdmc");

    // Replace the old .nro (current runs from RAM, safe to overwrite).
    // rename() can fail on FAT32; fallback to copy.
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

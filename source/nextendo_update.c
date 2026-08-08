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
//  Checks https://api.github.com/repos/Juanjo3222/Prelude-Nro/releases/latest
//  for the latest version tag, compares with NEXTENDO_BUILD, and downloads
//  the .nro asset if a newer version is available.
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
#define GH_API_PATH  "/repos/Juanjo3222/Prelude-Nro/releases/latest"
#define GH_API_PORT  443

#define NRO_PATH     "sdmc:/switch/nextendo.nro"
#define NRO_TMP      "sdmc:/switch/nextendo.nro.new"

static char g_download_url[512] = {0};
static long g_download_size = 0;

// Parse integer from JSON field like: "tag_name":"v3.0.3" -> extract build number
// Also handle "browser_download_url" and "size" fields
static bool parse_github_json(const unsigned char *b, size_t len, long *build, char *url, size_t urlcap, long *size) {
    // Extract tag_name: "tag_name":"vX.Y.Z" -> parse the version
    const char *tag_key = "\"tag_name\":\"";
    char *tp = strstr((const char*)b, tag_key);
    if (!tp) return false;
    tp += strlen(tag_key);
    // Parse vX.Y.Z — extract numbers after each dot
    int maj = 0, min = 0, patch = 0;
    if (*tp == 'v' || *tp == 'V') tp++;
    maj = (int)strtol(tp, &tp, 10);
    if (*tp == '.') tp++;
    min = (int)strtol(tp, &tp, 10);
    if (*tp == '.') tp++;
    patch = (int)strtol(tp, NULL, 10);
    // Use patch as build (or min*100+patch)
    *build = patch > 0 ? (long)patch : (long)(min * 100);

    // Extract browser_download_url
    const char *url_key = "\"browser_download_url\":\"";
    char *up = strstr((const char*)b, url_key);
    if (up) {
        up += strlen(url_key);
        char *ue = strchr(up, '"');
        if (ue) {
            size_t ul = (size_t)(ue - up);
            if (ul < urlcap) { memcpy(url, up, ul); url[ul] = '\0'; }
        }
    }

    // Extract size
    const char *size_key = "\"size\":";
    char *sp = strstr((const char*)b, size_key);
    if (sp) {
        sp += strlen(size_key);
        *size = strtol(sp, NULL, 10);
    }

    return *build > 0;
}

NextendoUpdate nextendo_update_check(void) {
    NextendoUpdate u = { false, 0, 0 };
    socketInitializeDefault();
    Result rc = sslInitialize(4);
    if (R_FAILED(rc)) { socketExit(); return u; }

    size_t len = 0;
    int status = 0;
    unsigned char *body = net_https_get(GH_API_HOST, GH_API_PATH, &len, &status);
    sslExit();
    socketExit();

    if (body && status == 200) {
        long build = 0; long sz = 0;
        if (parse_github_json(body, len, &build, g_download_url, sizeof(g_download_url), &sz)) {
            if (build > NEXTENDO_BUILD && sz > 4096) {
                u.available = true;
                u.latest = (int)build;
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

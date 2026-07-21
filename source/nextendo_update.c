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
//  Nextendo .nro -- auto-update via GitHub Releases (HTTPS).
//  Uses the Switch's native SSL service (no httpc dependency).
//  At startup, fetches the latest release info from GitHub API.
//  If a newer version exists, shows a banner. User can press Y
//  to download and replace /switch/nextendo.nro.
// ============================================================
#include <switch.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#include "nextendo_update.h"
#include "nextendo_net.h"

#define GH_API_HOST  "api.github.com"
#define GH_API_PATH  "/repos/Juanjo3222/Prelude-Nro/releases/latest"
#define NRO_PATH     "sdmc:/switch/nextendo.nro"
#define NRO_TMP      "sdmc:/switch/nextendo.nro.new"

// Stored by nextendo_update_check, consumed by nextendo_update_apply.
// Static because the apply function signature doesn't include the URL.
static char g_download_url[512] = {0};

// Parse a JSON string value: ,"key":"value"
static bool json_str(const unsigned char *b, size_t len, const char *key,
                     char *out, size_t out_max) {
    size_t kl = strlen(key);
    for (size_t i = 0; i + kl < len; i++) {
        if (memcmp(b + i, key, kl) == 0) {
            size_t j = i + kl;
            while (j < len && (b[j] == ' ' || b[j] == ':' || b[j] == '"')) j++;
            size_t start = j;
            while (j < len && b[j] != '"') j++;
            size_t slen = j - start;
            if (slen > 0 && slen < out_max) {
                memcpy(out, b + start, slen);
                out[slen] = '\0';
                return true;
            }
        }
    }
    return false;
}

// Parse a JSON long value: ,"key":NNN
static long json_long(const unsigned char *b, size_t len, const char *key) {
    size_t kl = strlen(key);
    for (size_t i = 0; i + kl < len; i++) {
        if (memcmp(b + i, key, kl) == 0) {
            size_t j = i + kl;
            while (j < len && (b[j] == ' ' || b[j] == ':' || b[j] == '"')) j++;
            long v = 0;
            if (j < len && b[j] == '-') { j++; }
            while (j < len && b[j] >= '0' && b[j] <= '9') {
                v = v * 10 + (b[j] - '0'); j++;
            }
            return v;
        }
    }
    return -1;
}

// Check for update. Returns parsed release info.
// Requires sslInitialize() called before, sslExit() will be called after.
NextendoUpdate nextendo_update_check(void) {
    NextendoUpdate u = { false, 0, 0 };
    // Get nifm ready for HTTPS
    socketInitializeDefault();

    Result rc = sslInitialize(4);
    if (R_FAILED(rc)) { socketExit(); return u; }

    size_t len = 0;
    int status = 0;
    unsigned char *body = net_https_get(GH_API_HOST, GH_API_PATH, &len, &status);

    if (body && status == 200) {
        char tag[32] = {0};
        if (json_str(body, len, "\"tag_name\"", tag, sizeof(tag))) {
            // Extract version number from tag like "v2.0.9" -> 209
            // Latest version has format vX.Y.Z where Z = NEXTENDO_BUILD
            long tagMajor = 0, tagMinor = 0, tagPatch = 0;
            // Parse "vMAJOR.MINOR.PATCH" format
            if (tag[0] == 'v') {
                int parsed = sscanf(tag + 1, "%ld.%ld.%ld",
                                    &tagMajor, &tagMinor, &tagPatch);
                if (parsed == 3 && tagPatch > NEXTENDO_BUILD) {
                    u.available = true;
                    u.latest = (int)tagPatch;
                    // Find the nextendo.nro asset
                    char name[64] = {0};
                    char url[512] = {0};
                    // Scan for assets array: find "browser_download_url" after
                    // "name":"nextendo.nro"
                    bool found = false;
                    for (size_t i = 0; i + 16 < len; i++) {
                        if (memcmp(body + i, "\"name\"", 6) == 0) {
                            if (json_str(body + i, len - i, "\"name\"",
                                         name, sizeof(name))) {
                                if (strcmp(name, "nextendo.nro") == 0) {
                                    // Found our asset -- get its download URL
                                    const char *rest = (const char *)body + i + strlen(name) + 4;
                                    size_t restLen = len - (i + strlen(name) + 4);
                                    if (json_str((const unsigned char *)rest,
                                                 restLen,
                                                 "\"browser_download_url\"",
                                                 url, sizeof(url))) {
                                        u.size = json_long(
                                            (const unsigned char *)rest,
                                            restLen, "\"size\"");
                                        strncpy(g_download_url, url,
                                                sizeof(g_download_url) - 1);
                                        g_download_url[
                                            sizeof(g_download_url) - 1] = '\0';
                                        found = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (!found) u.available = false;
                }
            }
        }
        free(body);
    }

    sslExit();
    socketExit();
    return u;
}

// Download and apply the update. Requires sslInitialize() before.
nextendo_update_result nextendo_update_apply(long expectedSize) {
    if (g_download_url[0] == '\0') return NUP_NET_FAIL;

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
    if (expectedSize > 0 && len != expectedSize) { remove(NRO_TMP); return NUP_SIZE_FAIL; }
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

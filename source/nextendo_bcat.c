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
//  Nextendo .nro — Splatoon 2 schedule installer via LayeredFS.
//
//  Copies pre-embedded payload files from the NRO's own romfs
//  (coopdata, vsdata, fesdata)
//  directly into Atmosphere's LayeredFS override path:
//    sdmc:/atmosphere/contents/<title_id>/romfs/
//  Covers JPN (01003C700009C000), USA (01003BC0000A0000) and
//  EUR (0100F8F0000A2000).
//
//  No network download, no NXBC bundle parsing. The files are shipped
//  inside the .nro and updated with each new release.
// ============================================================
#include <switch.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "nextendo_bcat.h"

#define ROMFS_BCAT_BASE "romfs:/bcatdata"
#define LAYEREDFS_BASE  "sdmc:/atmosphere/contents/%s/romfs"
#define LOG_PATH        "sdmc:/nextendo_bcat.log"

static FILE *g_log = NULL;
Result g_last_rc = 0;

static void logf_(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// mkdir -p for Switch SD paths (e.g. "sdmc:/a/b/c")
static bool ensureDir(const char *path) {
    char tmp[FS_MAX_PATH];
    size_t len = strnlen(path, sizeof(tmp) - 1);
    memcpy(tmp, path, len);
    tmp[len] = '\0';

    // Walk past device prefix (e.g. "sdmc:")
    char *p = strchr(tmp, ':');
    p = p ? p + 1 : tmp;
    if (*p == '/') p++;

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            int rc = mkdir(tmp, 0777);
            *p = '/';
            if (rc != 0 && errno != EEXIST) return false;
        }
    }
    int rc = mkdir(tmp, 0777);
    return rc == 0 || errno == EEXIST;
}

// Recursively remove a directory tree.
static void wipeTree(const char *path) {
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    char child[FS_MAX_PATH];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            wipeTree(child);
            rmdir(child);
        } else {
            remove(child);
        }
    }
    closedir(d);
}

// Copy a single file from romfs to sdmc.
static bool copyFile(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) { logf_("  ECHEC fopen source %s", src); return false; }

    // Ensure the parent directory exists (dst is a file path).
    char parent[FS_MAX_PATH];
    size_t plen = strnlen(dst, sizeof(parent) - 1);
    if (plen >= sizeof(parent)) plen = sizeof(parent) - 1;
    memcpy(parent, dst, plen);
    parent[plen] = '\0';
    char *slash = strrchr(parent, '/');
    if (slash) {
        *slash = '\0';
        if (!ensureDir(parent)) {
            logf_("  ECHEC ensureDir %s", parent);
            fclose(in);
            return false;
        }
    }

    FILE *out = fopen(dst, "wb");
    if (!out) { logf_("  ECHEC fopen dest %s", dst); fclose(in); return false; }

    char buf[8192];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            logf_("  ECHEC fwrite %s", dst);
            ok = false;
            break;
        }
    }
    fclose(in);
    fclose(out);
    return ok;
}

// Recursive copy of a directory tree. Mirrors the pattern used by
// nextendo_apply.c::copyTreeRomfs for the cert/patch stack.
static bool copyTree(const char *srcDir, const char *dstDir) {
    DIR *d = opendir(srcDir);
    if (!d) { logf_("  opendir ECHEC %s", srcDir); return false; }
    struct dirent *e;
    bool allOk = true;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sp[FS_MAX_PATH], dp[FS_MAX_PATH];
        snprintf(sp, sizeof(sp), "%s/%s", srcDir, e->d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dstDir, e->d_name);
        struct stat st;
        if (stat(sp, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!ensureDir(dp)) {
                logf_("  ECHEC mkdir %s", dp);
                allOk = false;
            } else if (!copyTree(sp, dp)) {
                allOk = false;
            }
        } else {
            if (!copyFile(sp, dp)) {
                allOk = false;
            } else {
                logf_("  %s", e->d_name);
            }
        }
    }
    closedir(d);
    return allOk;
}

nextendo_bcat_result nextendo_bcat_install_s2(void) {
    g_log = fopen(LOG_PATH, "w");
    logf_("=== Nextendo BCAT install S2 (v5 — romfs embarquee) ===");

    const char *regionIds[] = { "01003BC0000A0000", "0100F8F0000A2000", "01003C700009C000" };
    bool anyOk = false;

    for (int r = 0; r < 3; r++) {
        char srcBase[FS_MAX_PATH], dstBase[FS_MAX_PATH];
        snprintf(srcBase, sizeof(srcBase), "%s/%s/romfs", ROMFS_BCAT_BASE, regionIds[r]);
        snprintf(dstBase, sizeof(dstBase), LAYEREDFS_BASE,  regionIds[r]);

        logf_("--- region %s ---", regionIds[r]);
        logf_("  source romfs: %s", srcBase);
        logf_("  dest   sdmc:  %s", dstBase);

        struct stat st;
        if (stat(srcBase, &st) != 0 || !S_ISDIR(st.st_mode)) {
            logf_("  INEXISTANT dans la romfs — ce build ne couvre peut-etre pas cette region");
            continue;
        }

        char debugDir[FS_MAX_PATH];
        snprintf(debugDir, sizeof(debugDir), "%s/DebugUnderPilot", dstBase);
        wipeTree(debugDir); rmdir(debugDir);
        char sysDir[FS_MAX_PATH];
        snprintf(sysDir, sizeof(sysDir), "%s/System", dstBase);
        wipeTree(sysDir); rmdir(sysDir);

        if (copyTree(srcBase, dstBase)) {
            logf_("  region %s: OK", regionIds[r]);
            anyOk = true;
        } else {
            logf_("  region %s: ECHEC", regionIds[r]);
        }
    }

    logf_("=== resultat: %s ===", anyOk ? "OK" : "ECHEC");
    if (g_log) { fclose(g_log); g_log = NULL; }

    if (anyOk) return NB_OK;
    return NB_WRITE_FAIL;
}

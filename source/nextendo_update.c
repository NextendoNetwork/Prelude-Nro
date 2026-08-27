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

// Self-update: compares the releases/latest tag (GitHub API) against NEXTENDO_VERSION_*, then downloads the .nro.
#include <switch.h>
#include <string.h>
#include <strings.h>   // strcasecmp (case-insensitive .nro extension)
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>     // traces the exact reason a write failed

#include "nextendo_update.h"
#include "nextendo_net.h"
#include "nextendo_apply.h"   // nextendo_trace: updater diagnostics readable from the SD card
#include "audio.h"            // the BGM holds a FILE* open on the romfs (see releaseRomfs)

// GitHub API for latest release
#define GH_API_HOST  "api.github.com"
#define GH_API_PATH  "/repos/NextendoNetwork/Prelude-Nro/releases/latest"
#define GH_API_PORT  443

// Target = the .nro we are RUNNING (hbmenu's argv[0]). A fixed path dropped a 2nd copy and kept launching the old one.
#define LEGACY_NRO_FILE "sdmc:/switch/nextendo.nro"
#define LEGACY_TMP_FILE "sdmc:/switch/nextendo.nro.new"

static char g_self_nro[512] = {0};
static char g_self_tmp[520] = {0};

void nextendo_update_set_self_path(const char *argv0) {
    // With no usable argv: the historical path beats writing to a made-up location.
    if (!argv0 || !*argv0) return;
    size_t n = strlen(argv0);
    if (n < 5 || n >= sizeof(g_self_nro)) return;
    if (strcasecmp(argv0 + n - 4, ".nro") != 0) return;
    if (strncmp(argv0, "sdmc:/", 6) == 0)
        snprintf(g_self_nro, sizeof(g_self_nro), "%s", argv0);
    else if (argv0[0] == '/')
        snprintf(g_self_nro, sizeof(g_self_nro), "sdmc:%s", argv0);
    else
        return;
    // The .new goes NEXT TO the target, so rename() never crosses a boundary.
    snprintf(g_self_tmp, sizeof(g_self_tmp), "%s.new", g_self_nro);
}

static const char *nroPath(void) { return g_self_nro[0] ? g_self_nro : LEGACY_NRO_FILE; }
static const char *nroTmp(void)  { return g_self_tmp[0] ? g_self_tmp : LEGACY_TMP_FILE; }

// Without Content-Length the stream reports total=0: we fall back on the size the GitHub API already gave us.
static nextendo_progress_fn g_progress_cb    = NULL;
static long                 g_progress_total = 0;

static void progressRelay(long received, long total) {
    if (total <= 0) total = g_progress_total;
    if (g_progress_cb) g_progress_cb(NUP_PHASE_DOWNLOAD, received, total);
}

// Overwrites dst WITHOUT deleting it first: this is the building block of the three attempts below.
static bool copyOver(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    // 17 MB to write out: with no progress reported, the screen sits frozen at 100 % for the whole write.
    long copied = 0;
    if (g_progress_cb) g_progress_cb(NUP_PHASE_INSTALL, 0, g_progress_total);
    FILE *out = fopen(dst, "wb");
    // errno is saved before each fclose(), which is allowed to clobber it even when it succeeds.
    if (!out) { int e = errno; fclose(in); errno = e; return false; }
    char cbuf[16384];
    size_t n;
    bool ok = true;
    int err = 0;
    while ((n = fread(cbuf, 1, sizeof(cbuf), in)) > 0) {
        if (fwrite(cbuf, 1, n, out) != n) { err = errno; ok = false; break; }
        copied += (long)n;
        if (g_progress_cb) g_progress_cb(NUP_PHASE_INSTALL, copied, g_progress_total);
    }
    fclose(in);
    if (fclose(out) != 0) { if (ok) err = errno; ok = false; }   // deferred write
    if (!ok) errno = err;
    return ok;
}

// errno tells a lock (EBUSY / EACCES) apart from a full card (ENOSPC) or a missing path (ENOENT).
static void traceErr(const char *step) {
    char m[160];
    snprintf(m, sizeof(m), "%s (errno=%d)", step, errno);
    nextendo_trace(m);
}

// romfsInit() keeps an FS handle open on the running .nro: without releasing it, no replacement attempt can ever succeed.
// Audio goes first (mpg123 holds a FILE* on bgm.mp3); fonts and images are already in RAM, so the screen still draws.
static void releaseRomfs(void) {
    audio_exit();    // closes the FILE* mpg123 holds on romfs:/bgm.mp3
    romfsExit();     // closes the FS handle on the running .nro
    nextendo_trace("59 update: romfs relache (le .nro cible n'est plus ouvert)");
}

// Non-fatal, but a FAILURE must leave the app usable: mode, flags and BCAT all read the romfs.
static void restoreRomfs(void) {
    if (R_FAILED(romfsInit())) { nextendo_trace("69 WARN update: romfs non remonte"); return; }
    audio_init();
}

static char g_download_url[512] = {0};
static long g_download_size = 0;

// Tolerates whitespace around ':'. Returns the first character inside the opening quote, or NULL.
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

// Tolerates compact and pretty-printed JSON alike: the GitHub API returns the latter.
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

    // strtol skips leading whitespace by itself.
    char *sp = strstr((const char*)b, "\"size\":");
    if (sp) { sp += 7; *size = strtol(sp, NULL, 10); }

    return *maj > 0;
}

// >0 si a>b, <0 si a<b, 0 si egal.
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
        // parse_github_json works with strstr: the returned body has NO NUL, so we copy into a terminated buffer.
        char *json = (char *)malloc(len + 1);
        if (json) {
            memcpy(json, body, len);
            json[len] = '\0';
        }
        if (json && parse_github_json((const unsigned char *)json, len, &maj, &min, &patch,
                                      g_download_url, sizeof(g_download_url), &sz)) {
            if (semver_cmp(maj, min, patch,
                           NEXTENDO_VERSION_MAJOR, NEXTENDO_VERSION_MINOR, NEXTENDO_VERSION_PATCH) > 0
                && sz > 4096) {
                u.available = true;
                u.maj = maj; u.min = min; u.patch = patch;
                u.size = sz;
                g_download_size = sz;
            }
        }
        free(json);
        free(body);
    }
    return u;
}

// Requires sslInitialize() before calling.
nextendo_update_result nextendo_update_apply(long expectedSize, nextendo_progress_fn onProgress) {
    if (g_download_url[0] == '\0') return NUP_NET_FAIL;
    long expected = expectedSize > 0 ? expectedSize : g_download_size;

    g_progress_cb    = onProgress;
    g_progress_total = expected;

    FILE *f = fopen(nroTmp(), "wb");
    if (!f) {
        mkdir("sdmc:/switch", 0777);
        f = fopen(nroTmp(), "wb");
    }
    if (!f) { traceErr("57 ERREUR update: creation du .new impossible"); return NUP_WRITE_FAIL; }

    socketInitializeDefault();
    Result rc = sslInitialize(4);
    if (R_FAILED(rc)) { fclose(f); socketExit(); return NUP_NET_FAIL; }

    char host[256] = {0};
    char path[1024] = {0};
    if (sscanf(g_download_url, "https://%255[^/]%1023s", host, path) < 2) {
        sslExit(); fclose(f); remove(nroTmp()); socketExit(); return NUP_NET_FAIL;
    }

    int status = 0;
    long len = net_https_get_to_file(host, path, f, &status,
                                     onProgress ? progressRelay : NULL);
    fclose(f);
    sslExit();
    socketExit();

    if (len == -2) { traceErr("58 ERREUR update: ecriture du .new interrompue (carte pleine ?)");
                     remove(nroTmp()); return NUP_WRITE_FAIL; }
    if (len < 0)   { remove(nroTmp()); return NUP_NET_FAIL; }
    if (status != 200 || len < 4096) { remove(nroTmp()); return NUP_NET_FAIL; }
    if (expected > 0 && len != expected) { remove(nroTmp()); return NUP_SIZE_FAIL; }
    fsdevCommitDevice("sdmc");

    // Replacing the .nro: three attempts as a safety net, each tracing its own errno.
    bool placed = false;

    // The target is held open by our own romfs: without this release, all three attempts fail.
    releaseRomfs();
    { char m[600]; snprintf(m, sizeof(m), "59b update: cible = %s", nroPath()); nextendo_trace(m); }

    // 1) Overwrite IN PLACE: works when the file resists deletion but not writing.
    if (copyOver(nroTmp(), nroPath())) {
        placed = true;
        remove(nroTmp());
        nextendo_trace("60 update: ecrase en place");
    } else {
        traceErr("60 update: ecrasement en place refuse");
    }

    // 2) remove + rename: the historical path, and the cleanest when it works.
    if (!placed) {
        if (remove(nroPath()) != 0) traceErr("61 update: remove de la cible refuse");
        if (rename(nroTmp(), nroPath()) == 0) {
            placed = true;
            nextendo_trace("61 update: remplace par rename");
        } else if (copyOver(nroTmp(), nroPath())) {
            placed = true;
            remove(nroTmp());
            nextendo_trace("62 update: remplace par copie apres remove");
        } else {
            traceErr("62 update: copie apres remove refusee");
        }
    }

    // 3) Last resort, the historical location: a file to move by hand beats an error.
    if (!placed && strcmp(nroPath(), LEGACY_NRO_FILE) != 0) {
        mkdir("sdmc:/switch", 0777);
        if (copyOver(nroTmp(), LEGACY_NRO_FILE)) {
            placed = true;
            remove(nroTmp());
            nextendo_trace("63 WARN update: cible verrouillee -> ecrit dans switch/nextendo.nro");
        } else {
            traceErr("63 update: repli sur switch/nextendo.nro refuse");
        }
    } else if (!placed) {
        // Already at the historical location: no fallback left, so say so.
        nextendo_trace("63 update: pas de repli possible (deja switch/nextendo.nro)");
    }

    if (!placed) {
        remove(nroTmp());
        nextendo_trace("64 ERREUR update: aucune ecriture possible");
        restoreRomfs();
        return NUP_WRITE_FAIL;
    }

    // The app stays usable until the user closes and relaunches it.
    restoreRomfs();

    // Orphan left by an update from before this fix. We delete ONLY that path, never any other.
    if (strcmp(nroPath(), LEGACY_NRO_FILE) != 0) {
        remove(LEGACY_NRO_FILE);
        remove(LEGACY_TMP_FILE);
    }

    fsdevCommitDevice("sdmc");
    return NUP_OK;
}

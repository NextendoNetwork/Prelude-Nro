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

// System logic. Two verified traps: fsdevCommitDevice("sdmc") BEFORE any reboot or the writes are lost,
// and bpcRebootSystem() rather than appletRequestToReboot, which does not work from hbmenu.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>   // rmdir (purging files left behind by an older build)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <switch.h>

#include "nextendo_apply.h"
#include "nextendo_config.h"
#include "nextendo_hosts.h"
#include "nextendo_net.h"
#include "nextendo_update.h"  // NEXTENDO_BUILD: the backup question is re-asked on each build

char g_server_ip[NEXTENDO_SERVER_IP_MAX] = NEXTENDO_SERVER_IP_DEFAULT;

const char *server_display_name(void) {
    if (strcmp(g_server_ip, NEXTENDO_SERVER_IP_DEFAULT) == 0) return "VPS (51.178.29.194)";
    if (strcmp(g_server_ip, NEXTENDO_SERVER_IP_ALT) == 0)     return "Local (3.135.232.168)";
    return g_server_ip;
}

#define SETTINGS_DIR "sdmc:/atmosphere/config"
#define NEXTENDO_EXOSPHERE_INI "sdmc:/exosphere.ini"
#define NEXTENDO_TRACE NEXTENDO_TRACE_PATH

// main and the network thread trace in parallel: without a lock, two concurrent fopen/fputs/fclose interleave the lines.
static Mutex s_traceMtx;

void nextendo_trace(const char *step) {
    mutexLock(&s_traceMtx);
    FILE *f = fopen(NEXTENDO_TRACE, "a");
    if (f) { fputs(step, f); fputc('\n', f); fclose(f); }
    fsdevCommitDevice("sdmc");   // durable immediately: that is the whole point of the trace
    mutexUnlock(&s_traceMtx);
}

// mkdir -p: on fsdev, mkdir does not create intermediate directories.
static Result ensureDir(const char *path) {
    char tmp[FS_MAX_PATH];
    size_t len = strnlen(path, sizeof(tmp) - 1);
    memcpy(tmp, path, len);
    tmp[len] = '\0';

    char *p = strchr(tmp, ':');     // skip the "sdmc:" prefix
    p = p ? p + 1 : tmp;
    if (*p == '/') p++;

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
                return MAKERESULT(Module_Libnx, LibnxError_IoError);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    return 0;
}

// Console firmware version, read once. Zeroes if setsys is unavailable.
static void firmwareVersion(int *maj, int *min) {
    static bool done = false;
    static int  m = 0, n = 0;
    if (!done) {
        done = true;
        if (R_SUCCEEDED(setsysInitialize())) {
            SetSysFirmwareVersion fv;
            if (R_SUCCEEDED(setsysGetFirmwareVersion(&fv))) { m = fv.major; n = fv.minor; }
            setsysExit();
        }
    }
    *maj = m; *min = n;
}

// Our disable_ca_verification patches are indexed by build id and do NOT cover 22.5.0 and up, so on
// those firmwares TLS to our server is untrusted. A host we redirect but cannot serve over trusted TLS
// is worse off than one we leave alone: it fails instead of working. Only hosts a game strictly needs
// are worth that trade. Unknown firmware counts as NOT covered - losing a sync is recoverable, losing
// sign-in is what got reported on 22.5.0.
static bool caPatchesCoverThisFirmware(void) {
    int maj = 0, min = 0;
    firmwareVersion(&maj, &min);
    if (maj == 0) return false;
    return (maj < 22) || (maj == 22 && min < 5);
}

char *nextendo_hosts_build(const char *ip) {
    const char *nncs2_ip = NEXTENDO_SERVER_IP_NNCSD2;
    size_t cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    size_t olen = 0;
    #define EMIT_H(s) do { size_t sl = strlen(s); \
        if (olen + sl + 1 > cap) { cap = (olen + sl + 1) * 2; \
            char *nb = (char *)realloc(buf, cap); if (!nb) { free(buf); return NULL; } buf = nb; } \
        memcpy(buf + olen, (s), sl + 1); olen += sl; } while (0)

    EMIT_H("# ============================================================\n");
    EMIT_H("#  NEXTENDO NETWORK - Atmosphere DNS-MITM (mode NEXTENDO)\n");
    EMIT_H("#  Genere par l'app homebrew Nextendo. Derniere ligne qui matche gagne.\n");
    EMIT_H("# ============================================================\n\n");

    EMIT_H("# --- 1) Tout Nintendo -> serveurs Nextendo ---\n");
    char line[256];

    snprintf(line, sizeof(line), "%s    *.nintendo.com\n", ip);           EMIT_H(line);
    snprintf(line, sizeof(line), "%s    *.nintendo.co.jp\n", ip);         EMIT_H(line);
    snprintf(line, sizeof(line), "%s accounts.nintendo.com\n", ip);       EMIT_H(line);
    snprintf(line, sizeof(line), "%s api.accounts.nintendo.com\n", ip);   EMIT_H(line);
    snprintf(line, sizeof(line), "%s m-lp1.baas.nintendo.com\n", ip);    EMIT_H(line);
    snprintf(line, sizeof(line), "%s e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s cdn-image-e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s capi.lp1.op2.nintendo.net\n", ip);  EMIT_H(line);
    snprintf(line, sizeof(line), "%s storage.hac.lp1.scsi.srv.nintendo.net\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s val.hac.penne.srv.nintendo.net\n", ip);  EMIT_H(line);
    snprintf(line, sizeof(line), "%s god.hac.lp1.penne.srv.nintendo.net\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s dauth-lp1.ndas.srv.nintendo.net\n", ip);    EMIT_H(line);
    snprintf(line, sizeof(line), "%s aauth.hac.lp1.ndas.srv.nintendo.net\n", ip); EMIT_H(line);
    // Both forms are needed: *srv (no dot) matches the multi-label hosts that *.srv does not cover.
    snprintf(line, sizeof(line), "%s    *.srv.nintendo.net\n", ip);       EMIT_H(line);
    snprintf(line, sizeof(line), "%s    *srv.nintendo.net\n", ip);        EMIT_H(line);
    // g2* covers every NEX secure server, but a mid-label * is ignored on some builds: hence the explicit hosts.
    snprintf(line, sizeof(line), "%s g2*.s.n.srv.nintendo.net\n", ip);         EMIT_H(line);
    // MK8 to production, where the players are. Must stay AFTER the g2* wildcard: last matching line wins.
    snprintf(line, sizeof(line), "%s g2b309e01-lp1.s.n.srv.nintendo.net\n", NEXTENDO_SERVER_IP_NNCSD2); EMIT_H(line); // MK8 -> 164
    snprintf(line, sizeof(line), "%s g23380901-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // SSBU
    snprintf(line, sizeof(line), "%s g2ee2e300-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // ACNH
    snprintf(line, sizeof(line), "%s g26cfaf00-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // Strikers
    snprintf(line, sizeof(line), "%s g20de2100-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // LM3
    // Ids read off the production containers, not guessed.
    snprintf(line, sizeof(line), "%s g23932a00-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // Mario Tennis Aces
    snprintf(line, sizeof(line), "%s g25c08801-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // ARMS
    snprintf(line, sizeof(line), "%s g2df33d01-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // Splatoon 2
    // Splatoon 3 speaks NPLN (gRPC over HTTP/2), not NEX: no g2*.s.n host at all. Explicit for the same reason as above.
    snprintf(line, sizeof(line), "%s t-dce9377b-lp1.lp1.t.npln.srv.nintendo.net\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s t-adf89f68-lp1.lp1.t.npln.srv.nintendo.net\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s gw.hac.lp1.vermillion.srv.nintendo.net\n", ip);     EMIT_H(line);
    snprintf(line, sizeof(line), "%s val.hac.lp1.penne.srv.nintendo.net\n", ip);         EMIT_H(line);
    snprintf(line, sizeof(line), "%s fro-3.hac.lp1.penne.srv.nintendo.net\n", ip);       EMIT_H(line);
    // dragons ends in .nintendo.net, not srv.nintendo.net: no wildcard catches it, and without this line it goes to Nintendo.
    snprintf(line, sizeof(line), "%s dragons.hac.lp1.dragons.nintendo.net\n", ip);       EMIT_H(line);
    // gamesync carries the lobby itself (KeepUserSession over TCP/7575) and escapes the *srv wildcard: without it the tenant answers but no match ever starts.
    snprintf(line, sizeof(line), "%s    *.npln.nintendo.net\n", ip);                     EMIT_H(line);
    snprintf(line, sizeof(line), "%s gamesync.npln.nintendo.net\n", ip);                 EMIT_H(line);

    // BCAT (Splatoon 3 schedule sync). These end in .cdn.nintendo.net, which no wildcard above
    // covers, so before v3.3.9 they resolved to the real Nintendo. Gated: see caPatchesCoverThisFirmware.
    if (caPatchesCoverThisFirmware()) {
        snprintf(line, sizeof(line), "%s bcat-data-lp1.cdn.nintendo.net\n", ip);   EMIT_H(line);
        snprintf(line, sizeof(line), "%s bcat-list-lp1.cdn.nintendo.net\n", ip);   EMIT_H(line);
        snprintf(line, sizeof(line), "%s bcat-topics-lp1.cdn.nintendo.net\n", ip); EMIT_H(line);
    }
    // *.op2.nintendo.net removed: too broad, it caught subdomains the VPS does not serve -> 2219-4001 on ACNH.

    EMIT_H("\n# --- 2) NAT-check #2 : IP differente de nncs1 (sinon MK8 test-103) ---\n");
    snprintf(line, sizeof(line), "%s  nncs2-*.n.n.srv.nintendo.net\n", nncs2_ip); EMIT_H(line);

    EMIT_H("\n# --- 3) ANTI-BAN : telemetrie -> trou noir ---\n");
    EMIT_H("0.0.0.0          receive-%.dg.srv.nintendo.net\n");
    EMIT_H("0.0.0.0          receive-%.er.srv.nintendo.net\n");

    EMIT_H("\n# --- 4) d4c (MAJ systeme) -> NON REDIRIGE ---\n");
    EMIT_H("# NE PAS null-router : nim stocke un flag persistant.\n\n");

    EMIT_H("\n# --- 5) conntest (browser connectivity check) -> serveur conntest Nextendo ---\n");
    EMIT_H("# Le vrai Nintendo bloque parfois le conntest → \"This feature is not available\".\n");
    EMIT_H("# On le redirige vers notre serveur qui repond X-Organization: Nintendo + 200 OK.\n");
    snprintf(line, sizeof(line), "%s conntest.nintendowifi.net\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s ctest.cdn.nintendo.net\n", ip);    EMIT_H(line);

    #undef EMIT_H
    return buf;
}

static bool writeTextFile(const char *path, const char *contents) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    size_t n = strlen(contents);
    bool ok = (fwrite(contents, 1, n, f) == n);
    fclose(f);
    return ok;
}

// Line-by-line parser: every other key and section of system_settings.ini is preserved.
static bool iniSetDnsMitm(bool enable, bool addDefaults) {
    static const char *K1 = "enable_dns_mitm";
    static const char *K2 = "add_defaults_to_dns_hosts";
    const char *V1 = enable ? "enable_dns_mitm = u8!0x1\n"
                            : "enable_dns_mitm = u8!0x0\n";
    const char *V2 = addDefaults ? "add_defaults_to_dns_hosts = u8!0x1\n"
                                 : "add_defaults_to_dns_hosts = u8!0x0\n";

    ensureDir(SETTINGS_DIR);

    char *buf = NULL; long sz = 0;
    FILE *f = fopen(NEXTENDO_SETTINGS_INI, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        buf = (char *)malloc(sz + 1);
        if (!buf) { fclose(f); return false; }
        if (sz > 0) {
            size_t nr = fread(buf, 1, sz, f);
            if (nr != (size_t)sz) {
                // Partial read -> corrupt file, so we treat it as if it did not exist.
                free(buf); buf = NULL; sz = 0;
            }
        }
        if (buf) buf[sz] = '\0';
        fclose(f);
    }

    size_t cap = (size_t)sz + 256;
    char *out = (char *)malloc(cap);
    if (!out) { free(buf); return false; }
    size_t olen = 0;
    #define EMIT(s, n) do { \
        if (olen + (n) + 1 > cap) { cap = (olen + (n) + 1) * 2; \
            char *nb = (char *)realloc(out, cap); if (!nb) { free(out); free(buf); return false; } out = nb; } \
        memcpy(out + olen, (s), (n)); olen += (n); out[olen] = '\0'; } while (0)

    bool inAtmos = false, sawAtmos = false, setK1 = false, setK2 = false;

    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        size_t llen = nl ? (size_t)(nl - line + 1) : strlen(line);
        char *t = line; while (*t == ' ' || *t == '\t') t++;

        if (*t == '[') {
            if (inAtmos) {
                if (!setK1) { EMIT(V1, strlen(V1)); setK1 = true; }
                if (!setK2) { EMIT(V2, strlen(V2)); setK2 = true; }
            }
            inAtmos = (strncmp(t, "[atmosphere]", 12) == 0);
            if (inAtmos) sawAtmos = true;
            EMIT(line, llen);
        } else if (inAtmos) {
            char *k = t;
            if (*k == ';' || *k == '#') { k++; while (*k == ' ' || *k == '\t') k++; }
            if (strncmp(k, K1, strlen(K1)) == 0) { EMIT(V1, strlen(V1)); setK1 = true; }
            else if (strncmp(k, K2, strlen(K2)) == 0) { EMIT(V2, strlen(V2)); setK2 = true; }
            else EMIT(line, llen);
        } else {
            EMIT(line, llen);
        }
        line = nl ? nl + 1 : NULL;
    }

    if (inAtmos) {
        if (olen > 0 && out[olen - 1] != '\n') EMIT("\n", 1);
        if (!setK1) { EMIT(V1, strlen(V1)); setK1 = true; }
        if (!setK2) { EMIT(V2, strlen(V2)); setK2 = true; }
    }
    if (!sawAtmos) {
        if (olen > 0 && out[olen - 1] != '\n') EMIT("\n", 1);
        EMIT("[atmosphere]\n", 13);
        EMIT(V1, strlen(V1));
        EMIT(V2, strlen(V2));
    }
    free(buf);

    f = fopen(NEXTENDO_SETTINGS_INI, "wb");
    if (!f) { free(out); return false; }
    bool ok = (fwrite(out, 1, olen, f) == olen);
    fclose(f);
    free(out);
    #undef EMIT
    return ok;
}

// blank_prodinfo_emummc: 0 in NEXTENDO mode (real device cert, confined by DNS-MITM -> fixes 2123-0011), 1 in NINTENDO mode (blank identity, anti-ban).
// Read by exosphere at BOOT and affects ONLY emuMMC boots: blank_prodinfo_sysmmc is never touched.
static bool iniSetBlankProdinfoEmummc(bool blank) {
    static const char *K = "blank_prodinfo_emummc";
    const char *V = blank ? "blank_prodinfo_emummc=1\n" : "blank_prodinfo_emummc=0\n";

    char *buf = NULL; long sz = 0;
    FILE *f = fopen(NEXTENDO_EXOSPHERE_INI, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        buf = (char *)malloc(sz + 1);
        if (!buf) { fclose(f); return false; }
        if (sz > 0) {
            size_t nr = fread(buf, 1, sz, f);
            if (nr != (size_t)sz) {
                // Partial read -> corrupt file, so we treat it as if it did not exist.
                free(buf); buf = NULL; sz = 0;
            }
        }
        if (buf) buf[sz] = '\0';
        fclose(f);
    }

    size_t cap = (size_t)sz + 128;
    char *out = (char *)malloc(cap);
    if (!out) { free(buf); return false; }
    size_t olen = 0;
    #define EMITX(s, n) do { \
        if (olen + (n) + 1 > cap) { cap = (olen + (n) + 1) * 2; \
            char *nb = (char *)realloc(out, cap); if (!nb) { free(out); free(buf); return false; } out = nb; } \
        memcpy(out + olen, (s), (n)); olen += (n); out[olen] = '\0'; } while (0)

    bool inExo = false, sawExo = false, setK = false;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        size_t llen = nl ? (size_t)(nl - line + 1) : strlen(line);
        char *t = line; while (*t == ' ' || *t == '\t') t++;

        if (*t == '[') {
            if (inExo && !setK) { EMITX(V, strlen(V)); setK = true; }
            inExo = (strncmp(t, "[exosphere]", 11) == 0);
            if (inExo) sawExo = true;
            EMITX(line, llen);
        } else if (inExo) {
            char *k = t;
            if (*k == ';' || *k == '#') { k++; while (*k == ' ' || *k == '\t') k++; }
            if (strncmp(k, K, strlen(K)) == 0) { EMITX(V, strlen(V)); setK = true; }
            else EMITX(line, llen);
        } else {
            EMITX(line, llen);
        }
        line = nl ? nl + 1 : NULL;
    }
    if (inExo && !setK) {
        if (olen > 0 && out[olen - 1] != '\n') EMITX("\n", 1);
        EMITX(V, strlen(V)); setK = true;
    }
    if (!sawExo) {
        if (olen > 0 && out[olen - 1] != '\n') EMITX("\n", 1);
        EMITX("[exosphere]\n", 12);
        EMITX(V, strlen(V));
    }
    free(buf);

    f = fopen(NEXTENDO_EXOSPHERE_INI, "wb");
    if (!f) { free(out); return false; }
    bool ok = (fwrite(out, 1, olen, f) == olen);
    fclose(f);
    free(out);
    #undef EMITX
    return ok;
}

// NEXTENDO if a hosts file still redirects to our VPS, otherwise NINTENDO.
static bool fileHas(const char *path, const char *needle) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

int nextendo_current_mode(void) {
    if (fileHas(NEXTENDO_HOSTS_SYSMMC, NEXTENDO_SERVER_IP_DEFAULT) ||
        fileHas(NEXTENDO_HOSTS_EMUMMC, NEXTENDO_SERVER_IP_DEFAULT) ||
        fileHas(NEXTENDO_HOSTS_SYSMMC, NEXTENDO_SERVER_IP_ALT) ||
        fileHas(NEXTENDO_HOSTS_EMUMMC, NEXTENDO_SERVER_IP_ALT))
        return 0;   // CHOICE_NEXTENDO
    return 1;       // CHOICE_NINTENDO
}

#define SplConfigItem_ExosphereEmummcType ((SplConfigItem)65007)

BootType nextendo_detect_boot(void) {
    if (R_FAILED(splInitialize())) return BOOT_UNKNOWN;
    u64 val = 0;
    Result rc = splGetConfig(SplConfigItem_ExosphereEmummcType, &val);
    splExit();
    if (R_FAILED(rc)) return BOOT_UNKNOWN;
    return (val != 0) ? BOOT_EMUMMC : BOOT_SYSMMC;
}

// File copy, romfs -> SD.
static bool copyFile(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    static char cbuf[16384];
    size_t n; bool ok = true;
    while ((n = fread(cbuf, 1, sizeof(cbuf), in)) > 0)
        if (fwrite(cbuf, 1, n, out) != n) { ok = false; break; }
    fclose(in); fclose(out);
    return ok;
}

// Recursive copy, romfs -> SD. We OVERWRITE (the patches are idempotent) so a .nro update really propagates the latest ones.
static bool copyTreeRomfs(const char *srcDir, const char *dstDir) {
    DIR *d = opendir(srcDir);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sp[FS_MAX_PATH], dp[FS_MAX_PATH];
        snprintf(sp, sizeof(sp), "%s/%s", srcDir, e->d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dstDir, e->d_name);
        struct stat st;
        if (stat(sp, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (R_FAILED(ensureDir(dp))) return false;
            if (!copyTreeRomfs(sp, dp)) return false;
        } else {
            if (!copyFile(sp, dp)) return false;
        }
    }
    closedir(d);
    return true;
}

// Exact mirror of copyTreeRomfs: keying off the romfs rather than a hardcoded list keeps the purge in sync with what we install.
// rmdir fails on a non-empty directory, which PRESERVES any folder where the user put something else.
static bool removeTreeRomfs(const char *srcDir, const char *dstDir) {
    DIR *d = opendir(srcDir);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sp[FS_MAX_PATH], dp[FS_MAX_PATH];
        snprintf(sp, sizeof(sp), "%s/%s", srcDir, e->d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dstDir, e->d_name);
        struct stat st;
        if (stat(sp, &st) == 0 && S_ISDIR(st.st_mode)) {
            removeTreeRomfs(sp, dp);
            rmdir(dp);                  // only when empty -> otherwise kept
        } else {
            remove(dp);
        }
    }
    closedir(d);
    return true;
}

// Removes whatever leaves the VPS IP readable on the card: both DNS-MITM logs (Atmosphere writes the redirect table and our IP on every query there) and the *.txt.bak files, which are never read back.
static void nextendo_purge_leaks(void) {
    remove("sdmc:/atmosphere/logs/dns_mitm_startup.log");
    remove("sdmc:/atmosphere/logs/dns_mitm_debug.log");
    remove(NEXTENDO_HOSTS_SYSMMC ".bak");
    remove(NEXTENDO_HOSTS_EMUMMC ".bak");
}

static bool fileExists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Provisions the cert-trust stack (romfs -> SD) so Nextendo mode works with no manual install.
// Everything is gated by firmware build id: only what matches the console applies, the rest lies dormant.

// Orphans from OLD builds: copyTreeRomfs only writes the CURRENT romfs, so anything we stopped shipping stays on the SD card forever.
// The worst is network_mitm, which intercepts all SSL at boot: an old Prelude therefore leaves a live MITM facing the REAL Nintendo servers (2137-7403).
// Files first, directories after; all best-effort, and an ENOENT is the NORMAL case on a fresh install.
static const char *const NEXTENDO_STALE_FILES[] = {
    // network_mitm (MITM on ssl/ssl:s at boot), dropped in build 4.
    "sdmc:/atmosphere/contents/4200000000000666/flags/boot2.flag",
    "sdmc:/atmosphere/contents/4200000000000666/mitm.lst",
    "sdmc:/atmosphere/contents/4200000000000666/exefs.nsp",
    // Old locations of the browser CA bundle.
    "sdmc:/atmosphere/contents/0100000000000803/romfs/openssl_peer/cacerts.pem",
    "sdmc:/atmosphere/contents/0100000000000803/romfs/nro/netfront/openssl_peer/cacerts.pem",
    // Browser patch for a build id we no longer target.
    "sdmc:/atmosphere/nro_patches/disable_browser_ca_verification/C338171E72636A2D77418E02F45E75D9F3090B.ips",
};

static const char *const NEXTENDO_STALE_DIRS[] = {
    "sdmc:/atmosphere/contents/4200000000000666/flags",
    "sdmc:/atmosphere/contents/4200000000000666",
    "sdmc:/atmosphere/contents/0100000000000803/romfs/openssl_peer",
    "sdmc:/atmosphere/contents/0100000000000803/romfs/nro/netfront/openssl_peer",
    "sdmc:/atmosphere/contents/0100000000000803/romfs/nro/netfront",
    "sdmc:/atmosphere/contents/0100000000000803/romfs/nro",
};

// Returns the number of files actually removed (0 = already clean, or a fresh install).
static int nextendo_purge_stale(void) {
    int removed = 0;
    for (size_t i = 0; i < sizeof(NEXTENDO_STALE_FILES) / sizeof(NEXTENDO_STALE_FILES[0]); i++)
        if (remove(NEXTENDO_STALE_FILES[i]) == 0) removed++;
    for (size_t i = 0; i < sizeof(NEXTENDO_STALE_DIRS) / sizeof(NEXTENDO_STALE_DIRS[0]); i++)
        rmdir(NEXTENDO_STALE_DIRS[i]);   // echoue si non vide -> volontaire
    return removed;
}

static bool nextendo_provision_all(void) {
    nextendo_purge_stale();
    if (!copyTreeRomfs("romfs:/sd", "sdmc:")) return false;
    return true;
}

// Backup of the user's dns.mitm hosts: NINTENDO mode DELETES them, so we copy them once before writing anything.
// The copy lives in sdmc:/switch/, not in atmosphere/hosts/, which purge_leaks cleans and Atmosphere reads.
// SECURITY RULE: NEVER back up a file carrying the VPS IP, or the copy would do the very thing purge_leaks exists to prevent.
#define NEXTENDO_BACKUP_DIR    "sdmc:/switch/prelude_hosts_backup"
#define NEXTENDO_BACKUP_SYSMMC NEXTENDO_BACKUP_DIR "/sysmmc.txt"
#define NEXTENDO_BACKUP_EMUMMC NEXTENDO_BACKUP_DIR "/emummc.txt"
#define NEXTENDO_BACKUP_CFG    "sdmc:/switch/prelude_backup.cfg"

static bool copyFileRaw(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char buf[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    fclose(in);
    fclose(out);
    if (!ok) remove(dst);   // no half-written copy left on the card
    return ok;
}

// Backup-worthy = the file exists AND is not one WE wrote.
// Recognised first by the HEADER, present in EVERY file Prelude generates: a filter on the current IPs alone would let through a hosts file from a very old version, which we would then restore towards a dead server.
// The IPs are checked afterwards, for a hand-edited file that lost the header.
static bool backupCandidateOk(const char *path) {
    if (!fileExists(path)) return false;
    if (fileHas(path, NEXTENDO_HOSTS_HEADER_MARK)) return false;
    if (fileHas(path, NEXTENDO_SERVER_IP_DEFAULT)) return false;
    if (fileHas(path, NEXTENDO_SERVER_IP_ALT)) return false;
    return true;
}

bool nextendo_hosts_backup_exists(void) {
    return fileExists(NEXTENDO_BACKUP_SYSMMC) || fileExists(NEXTENDO_BACKUP_EMUMMC);
}

int nextendo_hosts_backup_create(void) {
    if (R_FAILED(ensureDir("sdmc:/switch"))) return 0;
    if (R_FAILED(ensureDir(NEXTENDO_BACKUP_DIR))) return 0;
    int n = 0;
    if (backupCandidateOk(NEXTENDO_HOSTS_SYSMMC) &&
        copyFileRaw(NEXTENDO_HOSTS_SYSMMC, NEXTENDO_BACKUP_SYSMMC)) n++;
    if (backupCandidateOk(NEXTENDO_HOSTS_EMUMMC) &&
        copyFileRaw(NEXTENDO_HOSTS_EMUMMC, NEXTENDO_BACKUP_EMUMMC)) n++;
    fsdevCommitDevice("sdmc");
    nextendo_trace(n ? "50 backup hosts cree" : "50 backup hosts: rien a sauvegarder");
    return n;
}

int nextendo_hosts_backup_restore(void) {
    if (R_FAILED(ensureDir(NEXTENDO_HOSTS_DIR))) return 0;
    int n = 0;
    // Re-validated BEFORE restoring, not only at creation: that is what repairs copies an earlier version already made, without asking the user anything.
    if (backupCandidateOk(NEXTENDO_BACKUP_SYSMMC) &&
        copyFileRaw(NEXTENDO_BACKUP_SYSMMC, NEXTENDO_HOSTS_SYSMMC)) n++;
    if (backupCandidateOk(NEXTENDO_BACKUP_EMUMMC) &&
        copyFileRaw(NEXTENDO_BACKUP_EMUMMC, NEXTENDO_HOSTS_EMUMMC)) n++;
    return n;
}

// prelude_backup.cfg: one key per line.
//   prompt_build     last build that ASKED the question (0 = never)
//   use_for_nintendo 1 = restore the backup when switching to NINTENDO mode
static void backupCfgRead(int *promptBuild, int *useForNintendo) {
    *promptBuild = 0;
    *useForNintendo = 0;
    FILE *f = fopen(NEXTENDO_BACKUP_CFG, "rb");
    if (!f) return;
    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    const char *p = strstr(buf, "prompt_build=");
    if (p) *promptBuild = atoi(p + 13);
    p = strstr(buf, "use_for_nintendo=");
    if (p) *useForNintendo = atoi(p + 17);
}

static bool backupCfgWrite(int promptBuild, int useForNintendo) {
    if (R_FAILED(ensureDir("sdmc:/switch"))) return false;
    char buf[128];
    snprintf(buf, sizeof(buf), "prompt_build=%d\nuse_for_nintendo=%d\n",
             promptBuild, useForNintendo);
    bool ok = writeTextFile(NEXTENDO_BACKUP_CFG, buf);
    fsdevCommitDevice("sdmc");
    return ok;
}

// Asked ONCE ONLY: after the answer the original hosts are either saved or overwritten, and asking again would only ever show "nothing to back up".
bool nextendo_backup_prompt_needed(void) {
    int pb, u;
    backupCfgRead(&pb, &u);
    return pb == 0;
}

void nextendo_backup_prompt_done(void) {
    int pb, u;
    backupCfgRead(&pb, &u);
    (void)pb;
    backupCfgWrite(NEXTENDO_BUILD, u);
}

bool nextendo_backup_use_for_nintendo(void) {
    int pb, u;
    backupCfgRead(&pb, &u);
    (void)pb;
    return u != 0;
}

void nextendo_backup_set_use_for_nintendo(bool on) {
    int pb, u;
    backupCfgRead(&pb, &u);
    (void)u;
    backupCfgWrite(pb, on ? 1 : 0);
}

// Counts the .ips files on the CARD and in the ROMFS: two different numbers mean the card still holds a previous version's.
// A limit worth stating plainly: this reports what is on the card, not what Atmosphere APPLIED — an unknown build id gets nothing, silently.
static int countIps(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (dot && strcmp(dot, ".ips") == 0) n++;
    }
    closedir(d);
    return n;
}

// provision_all is internal, but startup must be able to refresh the card without a full mode switch.
bool nextendo_provision_all_public(void) { return nextendo_provision_all(); }

void nextendo_s3_status(NextendoS3Status *out) {
    if (!out) return;
    out->onSd = countIps("sdmc:/atmosphere/exefs_patches/s3certbypass") +
                countIps("sdmc:/atmosphere/exefs_patches/s3peername");
    out->inRomfs = countIps("romfs:/sd/atmosphere/exefs_patches/s3certbypass") +
                   countIps("romfs:/sd/atmosphere/exefs_patches/s3peername");
    // dns.mitm off = the console talks to the REAL Nintendo, and no patch can help with that.
    out->dnsMitmOn = fileHas(NEXTENDO_SETTINGS_INI, "enable_dns_mitm = u8!0x1");
    out->hostsOk   = (nextendo_current_mode() == 0);
}

bool nextendo_apply_nextendo_ip(const char *ip) {
    if (R_FAILED(ensureDir(NEXTENDO_HOSTS_DIR))) return false;
    if (!nextendo_provision_all()) {
        nextendo_trace("30 WARN: provision_all a echoue -> annulation");
        return false;
    }
    char *hosts = nextendo_hosts_build(ip);
    if (!hosts) return false;
    bool a = writeTextFile(NEXTENDO_HOSTS_SYSMMC, hosts);
    bool b = writeTextFile(NEXTENDO_HOSTS_EMUMMC, hosts);
    free(hosts);
    // add_defaults=1 as in NINTENDO mode: we do not maintain our own telemetry list, Atmosphere's is tracked upstream.
    // No conflict with our redirects: the defaults only cover receive-%, never accounts.nintendo.com nor the game hosts.
    bool i = iniSetDnsMitm(true, true);
    bool p = iniSetBlankProdinfoEmummc(false);
    if (!p) nextendo_trace("29 WARN: iniSetBlankProdinfoEmummc(false) a echoue -> risque 2123-0011");
    nextendo_purge_leaks();
    fsdevCommitDevice("sdmc");
    return a && b && i && p;
}

bool nextendo_apply_nextendo(void) {
    return nextendo_apply_nextendo_ip(g_server_ip);
}

bool nextendo_apply_nintendo(void) {
    nextendo_trace("20 apply_nintendo: entree");
    // DELETED, not renamed: the .bak was never read back and only kept the VPS IP readable on the card.
    remove(NEXTENDO_HOSTS_SYSMMC);
    remove(NEXTENDO_HOSTS_EMUMMC);
    nextendo_trace("21 hosts supprimes");
    // Restored AFTER the deletion. Since the backup was refused if it carried one of our IPs, nothing of Nextendo comes back this way.
    if (nextendo_backup_use_for_nintendo() && nextendo_hosts_backup_exists()) {
        int nb = nextendo_hosts_backup_restore();
        nextendo_trace(nb ? "21b hosts utilisateur restaures"
                          : "21b WARN: restauration hosts utilisateur echouee");
    }
    nextendo_purge_leaks();
    nextendo_trace("22 purge_leaks ok");

    // Turning dns_mitm off is not enough: an old build's network_mitm starts via boot2.flag and would leave an SSL MITM facing the REAL servers (2137-7403).
    nextendo_purge_stale();
    nextendo_trace("23 purge_stale ok");

    // The cert-trust stack survived the switch: certificate checking stayed off and our CA stayed trusted, facing the REAL Nintendo. provision_all() puts it all back on return.
    if (!removeTreeRomfs("romfs:/sd", "sdmc:")) {
        nextendo_trace("24b removeTreeRomfs a echoue");
        return false;
    }
    nextendo_trace("24 removeTreeRomfs ok");
    removeTreeRomfs("romfs:/ssbu_quickplay", "sdmc:"); // SSBU online-deluxe mod
    nextendo_trace("24c ssbu_quickplay retire");

    // DNS-MITM left ACTIVE with add_defaults=1: turning dns_mitm off would also disable the native telemetry blocking, leaving the console LESS protected than a stock install.
    // The default entries are COMPILED INTO the DNS.mitm sysmodule, not read from a file: do NOT write our own default.txt, it belongs to the user and would drift behind Atmosphere's.
    // Safety net: if our hosts resist deletion, we fall back to dns_mitm=0, which neutralises them for certain.
    bool hostsGone = !fileExists(NEXTENDO_HOSTS_SYSMMC) && !fileExists(NEXTENDO_HOSTS_EMUMMC);
    bool i = hostsGone ? iniSetDnsMitm(true, true) : iniSetDnsMitm(false, false);
    nextendo_trace(hostsGone ? "25 ini ok (hosts partis, dns_mitm garde actif)"
                             : "25 ini ok (SECOURS : hosts resistants, dns_mitm coupe)");

    iniSetBlankProdinfoEmummc(true);     // emuMMC : PRODINFO blanchi -> anti-ban si online vrai Nintendo
                                         // (no effect on a sysNAND-only console: the UI says so)
    nextendo_trace("26 blank_prodinfo ok");
    fsdevCommitDevice("sdmc");
    nextendo_trace("27 apply_nintendo: TERMINE");
    return i;
}

// Network diagnostics: traces the information that matters for 2123-0011 / 2810-1224.
void nextendo_diag_network(void) {
    char buf[128];
    
    // Only checks that the address is valid: a real Pia test would require speaking the NEX protocol.
    {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_port = htons(10025);
            sa.sin_addr.s_addr = inet_addr("164.132.111.120");
            int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
            close(fd);
            snprintf(buf, sizeof(buf), "36 diag: nncs2:10025 (UDP) -> %s", rc == 0 ? "socket ok" : "socket echec");
        } else {
            snprintf(buf, sizeof(buf), "36 diag: nncs2:10025 (UDP) -> SOCKET_ECHEC");
        }
        nextendo_trace(buf);
    }
    {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_port = htons(10125);
            sa.sin_addr.s_addr = inet_addr("164.132.111.120");
            int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
            close(fd);
            snprintf(buf, sizeof(buf), "37 diag: nncs2:10125 (UDP) -> %s", rc == 0 ? "socket ok" : "socket echec");
        } else {
            snprintf(buf, sizeof(buf), "37 diag: nncs2:10125 (UDP) -> SOCKET_ECHEC");
        }
        nextendo_trace(buf);
    }

    // nncs1 PIA connectivity test (UDP to the main VPS, ports 10024 + 10025).
    {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_port = htons(10024);
            sa.sin_addr.s_addr = inet_addr(g_server_ip);
            int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
            close(fd);
            snprintf(buf, sizeof(buf), "39 diag: nncs1(PIA):10024 -> %s", rc == 0 ? "socket ok" : "refuse/timeout");
        } else {
            snprintf(buf, sizeof(buf), "39 diag: nncs1(PIA):10024 -> SOCKET_ECHEC");
        }
        nextendo_trace(buf);
    }
    {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd >= 0) {
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_port = htons(10124);
            sa.sin_addr.s_addr = inet_addr(g_server_ip);
            int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
            close(fd);
            snprintf(buf, sizeof(buf), "40 diag: nncs1(PIA):10124 -> %s", rc == 0 ? "socket ok" : "refuse/timeout");
        } else {
            snprintf(buf, sizeof(buf), "40 diag: nncs1(PIA):10124 -> SOCKET_ECHEC");
        }
        nextendo_trace(buf);
    }

    // BCAT connectivity test (HTTP to the server on :8095)
    if (nextendo_current_mode() == 0) {
        size_t blen = 0;
        int httpStatus = 0;
        unsigned char *body = net_http_get(g_server_ip, 8095, "/api/bcat/0100f8f0000a2000/cache", &blen, &httpStatus);
        snprintf(buf, sizeof(buf), "41 diag: BCAT %s:%d -> HTTP %d (%zu o)", g_server_ip, 8095, httpStatus, blen);
        nextendo_trace(buf);
        if (body) free(body);
    }

    // Presence of the hosts files: if absent, that simply means Nintendo mode is active.
    struct stat st;
    bool hasSys = stat(NEXTENDO_HOSTS_SYSMMC, &st) == 0;
    bool hasEmu = stat(NEXTENDO_HOSTS_EMUMMC, &st) == 0;
    snprintf(buf, sizeof(buf), "42 diag: hosts sysmmc=%d emummc=%d", hasSys, hasEmu);
    nextendo_trace(buf);
}

Result nextendo_reboot(void) {
    Result rc = bpcInitialize();
    if (R_FAILED(rc)) return rc;
    rc = bpcRebootSystem();              // does not return on success
    bpcExit();
    return rc;
}

// SSBU Online Deluxe mod: lives in romfs:/ssbu_quickplay/ and copies to sdmc:. Installing it is optional.

#define SSBU_MOD_SENTINEL \
    "sdmc:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libssbu_online_deluxe.nro"

bool nextendo_ssbu_is_installed(void) {
    struct stat st;
    return stat(SSBU_MOD_SENTINEL, &st) == 0;
}

bool nextendo_ssbu_install(void) {
    bool ok = copyTreeRomfs("romfs:/ssbu_quickplay", "sdmc:");
    if (ok) fsdevCommitDevice("sdmc");
    return ok;
}

void nextendo_ssbu_remove(void) {
    removeTreeRomfs("romfs:/ssbu_quickplay", "sdmc:");
    fsdevCommitDevice("sdmc");
}

// The SSBU mod's bundled overclock (libnx_over.nro plugin + sysmodule 00FF0000A11CE0FF, loaded by boot2.flag).
// With Horizon OC / sys-clk already in place, both drive the same PCV rails and the console FREEZES on launching Smash.
// Disabling = config.toml overclocker=false, then deleting the plugin and the sysmodule (procedure confirmed by saad-script, the mod's author).

#define SSBU_OC_CONFIG_DIR  "sdmc:/ultimate/ssbu_online_deluxe"
#define SSBU_OC_CONFIG      SSBU_OC_CONFIG_DIR "/config.toml"
#define SSBU_OC_PLUGIN      "sdmc:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libnx_over.nro"
#define SSBU_OC_SYSMOD      "sdmc:/atmosphere/contents/00FF0000A11CE0FF"
#define SSBU_OC_SYSMOD_ROMFS "romfs:/ssbu_quickplay/atmosphere/contents/00FF0000A11CE0FF"
#define SSBU_OC_PLUGIN_ROMFS "romfs:/ssbu_quickplay/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libnx_over.nro"

// boot2.flag is the reliable marker, not config.toml: the player may have hand-edited the latter.
bool nextendo_ssbu_oc_is_disabled(void) {
    return !fileExists(SSBU_OC_SYSMOD "/flags/boot2.flag");
}

bool nextendo_ssbu_oc_set(bool enabled) {
    bool ok;
    if (enabled) {
        if (R_FAILED(ensureDir(SSBU_OC_SYSMOD))) return false;
        ok = copyTreeRomfs(SSBU_OC_SYSMOD_ROMFS, SSBU_OC_SYSMOD);
        // Only copied back if the mod is installed: otherwise we would recreate an orphan in a tree the user removed.
        if (ok && nextendo_ssbu_is_installed())
            ok = copyFile(SSBU_OC_PLUGIN_ROMFS, SSBU_OC_PLUGIN);
        if (ok) {
            ensureDir(SSBU_OC_CONFIG_DIR);
            writeTextFile(SSBU_OC_CONFIG, "overclocker = true\n");
        }
    } else {
        // The setting first: that way the mod starts without overclock even if a deletion fails halfway.
        ok = (R_SUCCEEDED(ensureDir(SSBU_OC_CONFIG_DIR)) &&
              writeTextFile(SSBU_OC_CONFIG, "overclocker = false\n"));
        remove(SSBU_OC_PLUGIN);
        remove(SSBU_OC_SYSMOD "/flags/boot2.flag");
        rmdir(SSBU_OC_SYSMOD "/flags");
        remove(SSBU_OC_SYSMOD "/exefs.nsp");
        rmdir(SSBU_OC_SYSMOD);
    }
    fsdevCommitDevice("sdmc");
    return ok;
}

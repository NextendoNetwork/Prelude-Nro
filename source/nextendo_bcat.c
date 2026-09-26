// Prelude — Nintendo Switch homebrew for the Nextendo Network.
// Copyright (C) 2026 Nextendo Network
//
// Licensed under the PolyForm Shield License 1.0.0.
//
// You may use, modify and distribute this software for any purpose EXCEPT providing a product
// that competes with Nextendo Network, or with any product Nextendo Network provides using it.
//
// See LICENSE.md for the full terms, or <https://polyformproject.org/licenses/shield/1.0.0>.
//
// Required Notice: Copyright 2026 Nextendo Network

// ============================================================
//  Nextendo .nro — Splatoon 2 schedule installer via LayeredFS.
//
//  Downloads the per-title schedule zip from the account API:
//      GET https://nextendo.network/api/bcat/<titleId>
//  (streams bcat_store/<titleId>.zip, entries at the zip root:
//   coopdata/ vsdata/ fesdata/) and extracts it into Atmosphere's
//  LayeredFS override path:
//      sdmc:/atmosphere/contents/<title_id>/romfs/DebugUnderPilot/bcat/
//  for USA (01003BC0000A0000), EUR (0100F8F0000A2000) and JPN
//  (01003C700009C800).
//
//  System/GameConfigSetting.xml (static game config, NOT in the zip) and
//  dummy/ are still copied from the NRO's own romfs.
// ============================================================
#include <switch.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "nextendo_bcat.h"
#include "nextendo_net.h"
#include "miniz.h"

#define BCAT_HOST       "nextendo.network"
#define BCAT_API_PATH   "/api/bcat/%s"
#define LOG_PATH_SMB35  "sdmc:/nextendo_bcat_smb35.log"
#define ZIP_TMP         "sdmc:/nextendo_bcat_tmp.zip"
#define ROMFS_BCAT_BASE "romfs:/bcatdata"
#define LAYEREDFS_BASE  "sdmc:/atmosphere/contents/%s/romfs"
#define LOG_PATH        "sdmc:/nextendo_bcat.log"
#define LOG_PATH_S3     "sdmc:/nextendo_bcat_s3.log"

#define BCAT_ERR_WRITE  (-100)  // downloadZip: SD write failure (distinct from NET_ERR_*)

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

static void toLowerInPlace(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static bool ensureDir(const char *path) {
    char tmp[FS_MAX_PATH];
    size_t len = strnlen(path, sizeof(tmp) - 1);
    memcpy(tmp, path, len);
    tmp[len] = '\0';

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

static bool copyFile(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) { logf_("  ECHEC fopen source %s", src); return false; }

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

static bool copyTreeSimple(const char *srcDir, const char *dstDir) {
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
            } else if (!copyTreeSimple(sp, dp)) {
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

static bool safeEntryPath(const char *p) {
    if (!p || !*p) return false;
    size_t len = strlen(p);
    if (len > 512) return false;
    if (strchr(p, ':') || p[0] == '/' || strchr(p, '\\')) return false;
    const char *seg = p;
    while (*seg) {
        const char *slash = strchr(seg, '/');
        size_t slen = slash ? (size_t)(slash - seg) : strlen(seg);
        if (slen == 1 && seg[0] == '.') return false;
        if (slen == 2 && seg[0] == '.' && seg[1] == '.') return false;
        if (!slash) break;
        seg = slash + 1;
    }
    return true;
}

static int downloadZip(const char *titleIdLower) {
    char apiPath[64];
    snprintf(apiPath, sizeof(apiPath), BCAT_API_PATH, titleIdLower);

    FILE *f = fopen(ZIP_TMP, "wb");
    if (!f) { logf_("  ECHEC fopen tmp %s", ZIP_TMP); return NET_ERR_SOCKET; }

    int status = 0;
    long len = net_https_get_to_file(BCAT_HOST, apiPath, f, &status, NULL);
    fclose(f);
    fsdevCommitDevice("sdmc");  // rend le zip visible/immediat pour miniz (sinon lecture tronquee)

    if (status == 204) { remove(ZIP_TMP); return 204; }
    if (status != 200) { remove(ZIP_TMP); return status ? status : NET_ERR_TIMEOUT; }  // status=0 = sin respuesta HTTP (fallo pre-read)
    if (len == -2)     { remove(ZIP_TMP); return BCAT_ERR_WRITE; }
    if (len < 0)       { remove(ZIP_TMP); return NET_ERR_TIMEOUT; }
    if (len < 100)     { remove(ZIP_TMP); return NET_ERR_PROTO; }
    return 0;
}

static bool extractZip(const char *dstBase) {
    // Diagnostique : qu'y a-t-il reellement sur la SD ? (taille + signature PK)
    struct stat zst;
    if (stat(ZIP_TMP, &zst) == 0) {
        logf_("  ZIP_TMP taille=%lld", (long long)zst.st_size);
    } else {
        logf_("  ZIP_TMP introuvable (stat ECHEC)");
    }
    {
        FILE *dbg = fopen(ZIP_TMP, "rb");
        if (dbg) {
            unsigned char sig[4] = {0};
            size_t rn = fread(sig, 1, 4, dbg);
            logf_("  ZIP_TMP 4 premiers octets: %02x %02x %02x %02x (lus=%u)",
                  sig[0], sig[1], sig[2], sig[3], (unsigned)rn);
            fclose(dbg);
        } else {
            logf_("  ZIP_TMP fopen ECHEC (illisible)");
        }
    }

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    // Lecture en MEMOIRE (pas via fopen/fsdev) : le lecteur fichier de miniz depend de
    // fseek/ftell sur la SD (cache fsdev) qui est une source connue d'echec illisible.
    // 191 Ko ca passe facilement en heap; on extrait puis on libere.
    FILE *zr = fopen(ZIP_TMP, "rb");
    if (!zr) { logf_("  ECHEC fopen zip (mem)"); return false; }
    fseek(zr, 0, SEEK_END);
    long zsize = ftell(zr);
    fseek(zr, 0, SEEK_SET);
    if (zsize < 22) { fclose(zr); logf_("  ECHEC zip trop petit (%ld)", zsize); return false; }
    unsigned char *zbuf = (unsigned char *)malloc((size_t)zsize);
    if (!zbuf) { fclose(zr); logf_("  ECHEC malloc zip"); return false; }
    size_t got = fread(zbuf, 1, (size_t)zsize, zr);
    fclose(zr);
    if (got != (size_t)zsize) { free(zbuf); logf_("  ECHEC fread zip (%u/%ld)", (unsigned)got, zsize); return false; }

    if (!mz_zip_reader_init_mem(&zip, zbuf, (size_t)zsize, 0)) {
        free(zbuf);
        logf_("  ECHEC mz_zip_reader_init_mem (zip invalide)");
        return false;
    }

    mz_uint n = mz_zip_reader_get_num_files(&zip);
    logf_("  zip: %u entree(s)", (unsigned)n);
    if (n == 0) { mz_zip_reader_end(&zip); return false; }

    bool allOk = true;
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) { allOk = false; continue; }
        if (st.m_is_directory) continue;

        if (!safeEntryPath(st.m_filename)) {
            logf_("  ENTREE REFUSEE (path invalide): %s", st.m_filename);
            allOk = false;
            continue;
        }

        char dst[FS_MAX_PATH];
        snprintf(dst, sizeof(dst), "%s/%s", dstBase, st.m_filename);

        char parent[FS_MAX_PATH];
        size_t plen = strnlen(dst, sizeof(parent) - 1);
        memcpy(parent, dst, plen);
        parent[plen] = '\0';
        char *slash = strrchr(parent, '/');
        if (slash) {
            *slash = '\0';
            if (!ensureDir(parent)) {
                logf_("  ECHEC ensureDir %s", parent);
                allOk = false;
                continue;
            }
        }

        if (!mz_zip_reader_extract_to_file(&zip, i, dst, 0)) {
            logf_("  ECHEC extraction %s", st.m_filename);
            allOk = false;
        } else {
            logf_("  + %s", st.m_filename);
        }
    }
    mz_zip_reader_end(&zip);
    free(zbuf);   // apres end : miniz lit le buffer jusqu'au end
    return allOk;
}

static bool copyStatic(const char *titleId, const char *romfsBase) {
    char srcBase[FS_MAX_PATH];
    snprintf(srcBase, sizeof(srcBase), "%s/%s/romfs", ROMFS_BCAT_BASE, titleId);

    struct stat st;
    bool ok = true;

    char sysSrc[FS_MAX_PATH], sysDst[FS_MAX_PATH];
    snprintf(sysSrc, sizeof(sysSrc), "%s/System", srcBase);
    snprintf(sysDst, sizeof(sysDst), "%s/System", romfsBase);
    if (stat(sysSrc, &st) == 0 && S_ISDIR(st.st_mode))
        ok = copyTreeSimple(sysSrc, sysDst) && ok;

    char dumSrc[FS_MAX_PATH], dumDst[FS_MAX_PATH];
    snprintf(dumSrc, sizeof(dumSrc), "%s/DebugUnderPilot/bcat/dummy", srcBase);
    snprintf(dumDst, sizeof(dumDst), "%s/DebugUnderPilot/bcat/dummy", romfsBase);
    if (stat(dumSrc, &st) == 0 && S_ISDIR(st.st_mode))
        ok = copyTreeSimple(dumSrc, dumDst) && ok;

    return ok;
}

// ------------------------------------------------------------------
//  Super Mario Bros. 35 — Batailles Speciales.
//
//  MEME MECANISME QUE S2, destination differente. Le jeu lit son evenement a
//  DEUX endroits et fusionne les deux listes :
//      Contents:/Dat/sp_battle.dat   (son propre romfs — c'est ici qu'on ecrit)
//      sp_battle_info/master         (le vrai BCAT)
//  Comme S2, on ne touche pas au save de distribution : on pose le fichier la ou
//  le jeu ira le chercher dans son romfs, et LayeredFS fait le reste.
//
//  Le fichier livre par le jeu est UN SEUL octet nul : le parseur exige '{' en
//  premiere position et abandonne sinon. Un contenu qu'il ne comprend pas ne
//  casse donc rien — il n'y a simplement pas d'evenement.
nextendo_bcat_result nextendo_bcat_install_smb35(void) {
    g_log = fopen(LOG_PATH_SMB35, "w");
    logf_("=== Nextendo BCAT install SMB35 ===");

    static const char SMB35_ID[] = "0100277011F1A000";
    char lower[32];
    snprintf(lower, sizeof(lower), "%s", SMB35_ID);
    toLowerInPlace(lower);

    logf_("  GET /api/bcat/%s", lower);
    int rc = downloadZip(lower);
    if (rc == 204) {
        logf_("  204 : aucun evenement publie");
        if (g_log) { fclose(g_log); g_log = NULL; }
        return NB_NO_SCHEDULE;
    }
    if (rc != 0) {
        logf_("  ECHEC telechargement (status=%d ssl_rc=0x%x)", rc, (unsigned)g_net_ssl_rc);
        if (g_log) { fclose(g_log); g_log = NULL; }
        if (rc == NET_ERR_TIMEOUT)  return NB_NET_TIMEOUT;
        if (rc == NET_ERR_CONNECT)  return NB_NET_CONNECT;
        if (rc == BCAT_ERR_WRITE)   return NB_WRITE_FAIL;
        if (rc > 0)                 return NB_NET_HTTP_ERR;
        return NB_NET_FAIL;
    }

    // Destination : la RACINE du romfs, pas un sous-dossier. Le zip porte deja
    // "Dat/sp_battle.dat", donc extraire a la racine met le fichier au bon endroit.
    char romfsBase[FS_MAX_PATH];
    snprintf(romfsBase, sizeof(romfsBase), LAYEREDFS_BASE, SMB35_ID);
    logf_("  dest: %s", romfsBase);

    bool ok = extractZip(romfsBase);
    fsdevCommitDevice("sdmc");
    remove(ZIP_TMP);
    fsdevCommitDevice("sdmc");

    logf_("=== resultat: %s ===", ok ? "OK" : "ECHEC");
    if (g_log) { fclose(g_log); g_log = NULL; }
    return ok ? NB_OK : NB_WRITE_FAIL;
}

nextendo_bcat_result nextendo_bcat_install_s2(void) {
    g_log = fopen(LOG_PATH, "w");
    logf_("=== Nextendo BCAT install S2 (v7 — zip EUR installe pour toutes les regions) ===");

    { struct hostent *he = gethostbyname(BCAT_HOST);
      if (he && he->h_addr_list[0])
          logf_("  DNS %s -> %s", BCAT_HOST, inet_ntoa(*(struct in_addr *)he->h_addr_list[0]));
      else
          logf_("  DNS %s -> ECHEC (h_errno=%d)", BCAT_HOST, h_errno); }

    // Download the EUR zip once and install it to every region's LayeredFS.
    static const char EUR_ID[] = "0100F8F0000A2000";
    static const char *regionIds[] = { "01003BC0000A0000", "0100F8F0000A2000" };

    char eurLower[32];
    snprintf(eurLower, sizeof(eurLower), "%s", EUR_ID);
    toLowerInPlace(eurLower);

    logf_("--- telechargement EUR (%s) ---", EUR_ID);
    logf_("  GET /api/bcat/%s", eurLower);

    int rc = downloadZip(eurLower);
    if (rc == 204) {
        logf_("  204 : aucun planning publie");
        if (g_log) { fclose(g_log); g_log = NULL; }
        return NB_NO_SCHEDULE;
    }
    if (rc != 0) {
        logf_("  ECHEC telechargement (status=%d ssl_rc=0x%x)", rc, (unsigned)g_net_ssl_rc);
        if (g_log) { fclose(g_log); g_log = NULL; }
        if (rc == NET_ERR_TIMEOUT)  return NB_NET_TIMEOUT;
        if (rc == NET_ERR_CONNECT)  return NB_NET_CONNECT;
        if (rc == BCAT_ERR_WRITE)   return NB_WRITE_FAIL;
        if (rc > 0)                 return NB_NET_HTTP_ERR;
        return NB_NET_FAIL;
    }

    bool anyOk = false;
    for (int r = 0; r < 2; r++) {
        char romfsBase[FS_MAX_PATH];
        snprintf(romfsBase, sizeof(romfsBase), LAYEREDFS_BASE, regionIds[r]);

        char dstBase[FS_MAX_PATH];
        snprintf(dstBase, sizeof(dstBase), "%s/DebugUnderPilot/bcat", romfsBase);

        logf_("--- install region %s ---", regionIds[r]);
        logf_("  dest: %s", dstBase);

        char tree[FS_MAX_PATH];
        snprintf(tree, sizeof(tree), "%s/DebugUnderPilot", romfsBase);
        wipeTree(tree); rmdir(tree);
        snprintf(tree, sizeof(tree), "%s/System", romfsBase);
        wipeTree(tree); rmdir(tree);

        bool ok = extractZip(dstBase);
        ok = copyStatic(EUR_ID, romfsBase) && ok;  // EUR static files for all regions
        fsdevCommitDevice("sdmc");

        logf_("  region %s: %s", regionIds[r], ok ? "OK" : "ECHEC extraction");
        if (ok) anyOk = true;
    }

    remove(ZIP_TMP);
    fsdevCommitDevice("sdmc");

    logf_("=== resultat: %s ===", anyOk ? "OK" : "ECHEC");
    if (g_log) { fclose(g_log); g_log = NULL; }

    return anyOk ? NB_OK : NB_WRITE_FAIL;
}

nextendo_bcat_result nextendo_bcat_install_s3(void) {
    g_log = fopen(LOG_PATH_S3, "w");
    logf_("=== Nextendo BCAT install S3 ===");

    { struct hostent *he = gethostbyname(BCAT_HOST);
      if (he && he->h_addr_list[0])
          logf_("  DNS %s -> %s", BCAT_HOST, inet_ntoa(*(struct in_addr *)he->h_addr_list[0]));
      else
          logf_("  DNS %s -> ECHEC (h_errno=%d)", BCAT_HOST, h_errno); }

    // Title ID Splatoon 3 (Global: 0100C2500FC20000)
    // Puedes personalizar la URL o Title ID aqui:
    static const char S3_ID[] = "0100C2500FC20000";

    char s3Lower[32];
    snprintf(s3Lower, sizeof(s3Lower), "%s", S3_ID);
    toLowerInPlace(s3Lower);

    logf_("--- telechargement S3 (%s) ---", S3_ID);
    logf_("  GET /api/bcat/%s", s3Lower);

    int rc = downloadZip(s3Lower);
    if (rc == 204) {
        logf_("  204 : aucun planning publie");
        if (g_log) { fclose(g_log); g_log = NULL; }
        return NB_NO_SCHEDULE;
    }
    if (rc != 0) {
        logf_("  ECHEC telechargement (status=%d ssl_rc=0x%x)", rc, (unsigned)g_net_ssl_rc);
        if (g_log) { fclose(g_log); g_log = NULL; }
        if (rc == NET_ERR_TIMEOUT)  return NB_NET_TIMEOUT;
        if (rc == NET_ERR_CONNECT)  return NB_NET_CONNECT;
        if (rc == BCAT_ERR_WRITE)   return NB_WRITE_FAIL;
        if (rc > 0)                 return NB_NET_HTTP_ERR;
        return NB_NET_FAIL;
    }

    char romfsBase[FS_MAX_PATH];
    snprintf(romfsBase, sizeof(romfsBase), LAYEREDFS_BASE, S3_ID);

    char dstBase[FS_MAX_PATH];
    snprintf(dstBase, sizeof(dstBase), "%s/DebugUnderPilot/bcat", romfsBase);

    logf_("--- install S3 ---");
    logf_("  dest: %s", dstBase);

    char tree[FS_MAX_PATH];
    snprintf(tree, sizeof(tree), "%s/DebugUnderPilot", romfsBase);
    wipeTree(tree); rmdir(tree);

    bool ok = extractZip(dstBase);
    fsdevCommitDevice("sdmc");

    remove(ZIP_TMP);
    fsdevCommitDevice("sdmc");

    logf_("=== resultat S3: %s ===", ok ? "OK" : "ECHEC");
    if (g_log) { fclose(g_log); g_log = NULL; }

    return ok ? NB_OK : NB_WRITE_FAIL;
}

// ============================================================
//  Nextendo .nro — installation du planning BCAT de Splatoon 2.
//
//  Le serveur (nextendo-account, bcat_cache.go) construit tout le save delivery-cache
//  (directories.meta + files.meta + payloads bruts + digests reverse-MD5) et le serialise
//  en un bundle "NXBC". Ici on ne fait que : monter le save BCAT de S2, remplacer NOTRE
//  partie (directories.meta + directories/) en PRESERVANT les fichiers propres a S2
//  (passphrase.bin / list.msgpack / etag.bin / na_required), ecrire, committer.
//
//  Permissions : un .nro lance via HBL herite des droits FS d'Atmosphere (comme JKSV).
//  Un log est ecrit sur sdmc:/nextendo_bcat.log pour diagnostic.
// ============================================================
#include <switch.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "nextendo_bcat.h"
#include "nextendo_net.h"

#define S2_TITLE_ID 0x0100F8F0000A2000ULL
#define BCAT_IP     "203.0.113.10"
#define BCAT_PORT   8095
#define BCAT_PATH   "/api/bcat/0100f8f0000a2000/cache"
#define LOG_PATH    "sdmc:/nextendo_bcat.log"

static FILE *g_log = NULL;
static void logf_(const char *fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// mkdir -p sur le DOSSIER PARENT d'un chemin de fichier (fonctionne avec le prefixe "bcat:").
static void ensureParent(const char *filePath) {
    char dir[FS_MAX_PATH];
    size_t len = strnlen(filePath, sizeof(dir) - 1);
    memcpy(dir, filePath, len);
    dir[len] = '\0';
    char *slash = strrchr(dir, '/');
    if (!slash) return;
    *slash = '\0';
    char *p = strchr(dir, ':');
    p = p ? p + 1 : dir;
    if (*p == '/') p++;
    for (; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(dir, 0777); *p = '/'; }
    }
    mkdir(dir, 0777);
}

// Supprime recursivement le CONTENU d'un dossier monte (le dossier lui-meme reste).
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

// Efface UNIQUEMENT ce qu'on gere : directories.meta + l'arbre directories/.
// On NE TOUCHE PAS a passphrase.bin / list.msgpack / etag.bin / na_required (crees par S2).
static void clearManaged(void) {
    remove("bcat:/directories.meta");
    wipeTree("bcat:/directories");
    rmdir("bcat:/directories");
}

static bool writeFileB(const char *path, const unsigned char *data, u32 len) {
    ensureParent(path);
    FILE *f = fopen(path, "wb");
    if (!f) { logf_("  ECHEC fopen %s", path); return false; }
    bool ok = (len == 0) || (fwrite(data, 1, len, f) == len);
    fclose(f);
    if (!ok) logf_("  ECHEC fwrite %s (%u o)", path, len);
    return ok;
}

// Parse le bundle NXBC et ecrit chaque blob dans bcat:/<path>.
//   "NXBC" | u32 count | count * [ u16 pathLen | path | u32 dataLen | data ]   (tout little-endian)
static bool writeBundle(const unsigned char *b, size_t len) {
    if (len < 8 || memcmp(b, "NXBC", 4) != 0) { logf_("bundle: magic invalide"); return false; }
    u32 count;
    memcpy(&count, b + 4, 4);
    logf_("bundle: %u blobs", count);
    size_t off = 8;
    u32 totalBytes = 0;
    for (u32 i = 0; i < count; i++) {
        if (off + 2 > len) return false;
        u16 pl;
        memcpy(&pl, b + off, 2);
        off += 2;
        if (off + (size_t)pl + 4 > len) return false;
        char rel[FS_MAX_PATH];
        size_t n = (pl < sizeof(rel) - 1) ? pl : sizeof(rel) - 1;
        memcpy(rel, b + off, n);
        rel[n] = '\0';
        off += pl;
        u32 dl;
        memcpy(&dl, b + off, 4);
        off += 4;
        if (off + (size_t)dl > len) return false;
        char path[FS_MAX_PATH];
        snprintf(path, sizeof(path), "bcat:/%s", rel);
        if (!writeFileB(path, b + off, dl)) return false;
        logf_("  ok %s (%u o)", rel, dl);
        totalBytes += dl;
        off += dl;
    }
    logf_("bundle: %u o ecrits au total", totalBytes);
    return true;
}

nextendo_bcat_result nextendo_bcat_install_s2(void) {
    g_log = fopen(LOG_PATH, "w");
    logf_("=== Nextendo BCAT install S2 (v2) ===");

    size_t blen = 0;
    int status = 0;
    unsigned char *bundle = net_http_get(BCAT_IP, BCAT_PORT, BCAT_PATH, &blen, &status);
    logf_("http: status=%d body=%zu o", status, blen);
    if (!bundle) { if (g_log) fclose(g_log); return NB_NET_FAIL; }
    if (status == 204) { free(bundle); logf_("204 : rien de publie"); if (g_log) fclose(g_log); return NB_NO_SCHEDULE; }
    if (status != 200 || blen < 8) { free(bundle); if (g_log) fclose(g_log); return NB_NET_FAIL; }

    FsFileSystem fs;
    Result rc = fsOpen_BcatSaveData(&fs, S2_TITLE_ID);
    logf_("fsOpen_BcatSaveData: rc=0x%x", rc);
    if (R_FAILED(rc)) { free(bundle); if (g_log) fclose(g_log); return NB_MOUNT_FAIL; }
    if (fsdevMountDevice("bcat", fs) < 0) {
        fsFsClose(&fs);
        free(bundle);
        logf_("fsdevMountDevice: echec");
        if (g_log) fclose(g_log);
        return NB_MOUNT_FAIL;
    }

    // Journalise ce qui EXISTE deja dans le save (avant modif) — diagnostic.
    logf_("--- contenu save AVANT ---");
    DIR *d = opendir("bcat:/");
    if (d) { struct dirent *e; while ((e = readdir(d))) if (strcmp(e->d_name,".")&&strcmp(e->d_name,"..")) logf_("  %s", e->d_name); closedir(d); }

    clearManaged();      // efface seulement directories.meta + directories/ (garde passphrase.bin etc.)
    bool ok = writeBundle(bundle, blen);
    free(bundle);

    Result crc = fsdevCommitDevice("bcat");
    logf_("commit: rc=0x%x", crc);
    if (ok && R_FAILED(crc)) ok = false;

    fsdevUnmountDevice("bcat"); // ferme aussi fs
    logf_("=== resultat: %s ===", ok ? "OK" : "ECHEC");
    if (g_log) { fclose(g_log); g_log = NULL; }

    return ok ? NB_OK : NB_WRITE_FAIL;
}

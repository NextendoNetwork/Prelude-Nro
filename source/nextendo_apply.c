// ============================================================
//  Nextendo .nro — logique systeme.
//  API verifiees (workflow) contre libnx master + docs Atmosphere :
//   - SD via fopen "sdmc:/..." + fsdevCommitDevice("sdmc") AVANT reboot (sinon perte)
//   - mkdir ne cree pas les dirs intermediaires -> ensureDir (mkdir -p)
//   - detection emuMMC : splGetConfig(65007) ; on ecrit les 2 fichiers (robuste)
//   - reboot : bpcInitialize()/bpcRebootSystem() (PAS appletRequestToReboot depuis hbmenu)
//   - ini [atmosphere] enable_dns_mitm = u8!0x1 / add_defaults_to_dns_hosts = u8!0x0
// ============================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <switch.h>

#include "nextendo_apply.h"
#include "nextendo_hosts.h"

#define SETTINGS_DIR "sdmc:/atmosphere/config"

// --- mkdir -p (mkdir ne cree pas les dirs intermediaires sur fsdev) ---
static Result ensureDir(const char *path) {
    char tmp[FS_MAX_PATH];
    size_t len = strnlen(path, sizeof(tmp) - 1);
    memcpy(tmp, path, len);
    tmp[len] = '\0';

    char *p = strchr(tmp, ':');     // sauter le prefixe "sdmc:"
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

static bool writeTextFile(const char *path, const char *contents) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    size_t n = strlen(contents);
    bool ok = (fwrite(contents, 1, n, f) == n);
    fclose(f);
    return ok;
}

// Renomme path -> path.bak (ok si le fichier n'existait pas).
static bool renameToBak(const char *path) {
    char bak[FS_MAX_PATH];
    snprintf(bak, sizeof(bak), "%s.bak", path);
    remove(bak);                        // enleve un .bak precedent
    if (rename(path, bak) == 0) return true;
    return (errno == ENOENT);           // pas de fichier a renommer = ok
}

// --- Edite system_settings.ini : pose enable_dns_mitm a 0/1 + add_defaults=0,
//     en preservant toutes les autres cles/sections. (parser ligne a ligne verifie) ---
static bool iniSetDnsMitm(bool enable) {
    static const char *K1 = "enable_dns_mitm";
    static const char *K2 = "add_defaults_to_dns_hosts";
    const char *V1 = enable ? "enable_dns_mitm = u8!0x1\n"
                            : "enable_dns_mitm = u8!0x0\n";
    static const char *V2 = "add_defaults_to_dns_hosts = u8!0x0\n";

    ensureDir(SETTINGS_DIR);

    char *buf = NULL; long sz = 0;
    FILE *f = fopen(NEXTENDO_SETTINGS_INI, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        buf = (char *)malloc(sz + 1);
        if (!buf) { fclose(f); return false; }
        if (sz > 0) { if (fread(buf, 1, sz, f) != (size_t)sz) { /* tolere */ } }
        buf[sz] = '\0';
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

// --- Detection du MODE ACTUELLEMENT CHARGE (pour l'UI) ---
// NEXTENDO si un fichier hosts redirige encore vers notre VPS (51.178.29.194) ;
// sinon NINTENDO (apply_nintendo les renomme en .bak -> plus de redirection).
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
    if (fileHas(NEXTENDO_HOSTS_SYSMMC, "51.178.29.194") ||
        fileHas(NEXTENDO_HOSTS_EMUMMC, "51.178.29.194"))
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

// --- Copie fichier (romfs -> SD) ---
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

// --- Copie recursive romfs -> SD (miroir de l'arbo). Ecrase (patches gates par build-id,
//     idempotents) pour qu'une MAJ du .nro propage les derniers patches sur la SD. ---
static void copyTreeRomfs(const char *srcDir, const char *dstDir) {
    DIR *d = opendir(srcDir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sp[FS_MAX_PATH], dp[FS_MAX_PATH];
        snprintf(sp, sizeof(sp), "%s/%s", srcDir, e->d_name);
        snprintf(dp, sizeof(dp), "%s/%s", dstDir, e->d_name);
        struct stat st;
        if (stat(sp, &st) == 0 && S_ISDIR(st.st_mode)) {
            ensureDir(dp);
            copyTreeRomfs(sp, dp);
        } else {
            copyFile(sp, dp);
        }
    }
    closedir(d);
}

// --- Provisionne le stack cert-trust Nextendo (romfs du .nro -> SD) : disable_ca_verification
//     (ssl : jeux / auth / BAAS -> notre VPS) + disable_browser_ca_verification + cabundle + rootCA.pem
//     (WebView "Lier un compte"). Tout est gate par build-id firmware -> seul ce qui matche la console
//     s'applique, le reste dort (zero risque de brick). But : mode Nextendo fonctionnel SANS install
//     manuelle. (Pas de network_mitm : inutile sur 22.5.0 + ne se charge pas -> le stack SD qui marche
//     ne l'a pas ; disable_ca_verification suffit pour le ssl.) ---
static void nextendo_provision_all(void) {
    copyTreeRomfs("romfs:/sd", "sdmc:");
}

bool nextendo_apply_nextendo(void) {
    if (R_FAILED(ensureDir(NEXTENDO_HOSTS_DIR))) return false;
    nextendo_provision_all();            // pose TOUT le stack cert-trust (1ere fois ou MAJ)
    bool a = writeTextFile(NEXTENDO_HOSTS_SYSMMC, NEXTENDO_HOSTS);
    bool b = writeTextFile(NEXTENDO_HOSTS_EMUMMC, NEXTENDO_HOSTS);
    bool i = iniSetDnsMitm(true);
    fsdevCommitDevice("sdmc");           // flush SD avant tout reboot
    return a && b && i;
}

bool nextendo_apply_nintendo(void) {
    renameToBak(NEXTENDO_HOSTS_SYSMMC);
    renameToBak(NEXTENDO_HOSTS_EMUMMC);
    bool i = iniSetDnsMitm(false);
    fsdevCommitDevice("sdmc");
    return i;
}

Result nextendo_reboot(void) {
    Result rc = bpcInitialize();
    if (R_FAILED(rc)) return rc;
    rc = bpcRebootSystem();              // ne revient pas si succes
    bpcExit();
    return rc;
}

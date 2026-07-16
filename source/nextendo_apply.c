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
#include <unistd.h>   // rmdir (purge des fichiers laisses par un ancien build)
#include <switch.h>

#include "nextendo_apply.h"
#include "nextendo_hosts.h"

#define SETTINGS_DIR "sdmc:/atmosphere/config"
#define NEXTENDO_EXOSPHERE_INI "sdmc:/exosphere.ini"
#define NEXTENDO_TRACE NEXTENDO_TRACE_PATH

void nextendo_trace(const char *step) {
    FILE *f = fopen(NEXTENDO_TRACE, "a");
    if (f) { fputs(step, f); fputc('\n', f); fclose(f); }
    fsdevCommitDevice("sdmc");   // durable tout de suite : c'est le point de la trace
}

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

// --- Edite system_settings.ini : pose enable_dns_mitm a 0/1 + add_defaults=0,
//     en preservant toutes les autres cles/sections. (parser ligne a ligne verifie) ---
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

// --- Edite exosphere.ini : blank_prodinfo_emummc a 0 (mode NEXTENDO : le vrai PRODINFO est servi
//     a l'emuMMC -> cert device valide -> plus de 2123-0011, l'auth compte/online marche ; c'est
//     confine par le DNS-MITM donc l'identite reelle ne fuit JAMAIS vers Nintendo = safe) ou a 1
//     (mode NINTENDO : PRODINFO blanchi -> si l'utilisateur va sur le VRAI Nintendo en emuMMC,
//     il presente une identite blanche = pas de ban de la vraie console). Lu par exosphere au
//     BOOT ; le reboot du .nro l'applique. N'affecte QUE les boots emuMMC (blank_prodinfo_sysmmc
//     n'est jamais touche -> le sysNAND garde son vrai PRODINFO). ---
static bool iniSetBlankProdinfoEmummc(bool blank) {
    static const char *K = "blank_prodinfo_emummc";
    const char *V = blank ? "blank_prodinfo_emummc=1\n" : "blank_prodinfo_emummc=0\n";

    char *buf = NULL; long sz = 0;
    FILE *f = fopen(NEXTENDO_EXOSPHERE_INI, "rb");
    if (f) {
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        buf = (char *)malloc(sz + 1);
        if (!buf) { fclose(f); return false; }
        if (sz > 0) { if (fread(buf, 1, sz, f) != (size_t)sz) { /* tolere */ } }
        buf[sz] = '\0';
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

// --- Detection du MODE ACTUELLEMENT CHARGE (pour l'UI) ---
// NEXTENDO si un fichier hosts redirige encore vers notre VPS (203.0.113.10) ;
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
    if (fileHas(NEXTENDO_HOSTS_SYSMMC, "203.0.113.10") ||
        fileHas(NEXTENDO_HOSTS_EMUMMC, "203.0.113.10"))
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

// --- Miroir exact de copyTreeRomfs : parcourt l'arbre romfs et retire de la SD les chemins
//     correspondants, puis rmdir les dossiers devenus vides (rmdir echoue si non vide, donc
//     un dossier qui contient aussi des fichiers a l'utilisateur est PRESERVE : /atmosphere,
//     /atmosphere/contents, exefs_patches partage avec d'autres patches, etc.).
//     On se cale sur le romfs plutot que sur une liste codee en dur : la purge reste ainsi
//     automatiquement synchronisee avec ce qu'on installe, sans liste a maintenir. ---
static void removeTreeRomfs(const char *srcDir, const char *dstDir) {
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
            removeTreeRomfs(sp, dp);
            rmdir(dp);                  // vide seulement -> sinon conserve
        } else {
            remove(dp);
        }
    }
    closedir(d);
}

// --- Retire les traces qui laissent l'IP du VPS lisible sur la carte SD.
//     dns_mitm_startup.log : Atmosphere y ecrit la table COMPLETE des redirections a chaque
//       demarrage, sans reglage pour l'en empecher -> on ne peut que l'effacer. Il reviendra
//       au prochain boot en mode Nextendo (inevitable), mais le mode Nintendo ne laisse rien.
//     dns_mitm_debug.log : alimente par enable_dns_mitm_debug_log, il enregistre notre IP a
//       CHAQUE requete DNS (mesure sur une carte de test : 608 Ko / 7507 lignes). Jamais
//       nettoye par personne, il survivait indefiniment.
//     *.txt.bak : le retour en mode Nextendo reecrit les hosts depuis NEXTENDO_HOSTS et ne
//       relit jamais le .bak -> c'etait un residu qui gardait l'IP en clair. ---
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

// --- Provisionne le stack cert-trust Nextendo (romfs du .nro -> SD) : disable_ca_verification
//     (ssl : jeux / auth / BAAS -> notre VPS) + disable_browser_ca_verification + cabundle + rootCA.pem
//     (WebView "Lier un compte"). Tout est gate par build-id firmware -> seul ce qui matche la console
//     s'applique, le reste dort (zero risque de brick). But : mode Nextendo fonctionnel SANS install
//     manuelle. (Pas de network_mitm : inutile sur 22.5.0 + ne se charge pas -> le stack SD qui marche
//     ne l'a pas ; disable_ca_verification suffit pour le ssl.) ---
// --- Fichiers poses par d'ANCIENNES versions du .nro et que l'on ne livre plus.
//     copyTreeRomfs n'ecrit que ce que le romfs COURANT contient : tout ce qu'un build
//     precedent a pose et qu'on a depuis retire reste orphelin sur la SD, indefiniment.
//
//     Le plus grave est le sysmodule network_mitm (4200000000000666) : son mitm.lst
//     intercepte `ssl` + `ssl:s` (TOUT le SSL de la console) et son boot2.flag le lance
//     a chaque demarrage. Un utilisateur d'un ancien Prelude qui repasse en mode Nintendo
//     garde donc un MITM SSL actif pendant qu'il parle aux VRAIS serveurs Nintendo :
//     c'est ce qui donne 2137-7403, et c'est un vecteur de ban evident. Il ne suffit pas
//     de ne plus le livrer, il faut l'effacer.
//
//     Les cacerts.pem sont des chemins de l'ancien layout du navigateur (romfs/openssl_peer
//     et romfs/nro/netfront/openssl_peer) ; le stack courant utilise romfs/0/browser +
//     romfs/browser. Laisser les anciens en place fait cohabiter deux bundles CA.
//
//     Ordre : les fichiers d'abord, puis les dossiers (rmdir echoue si non vide, ce qui
//     est volontaire : on ne supprime jamais un dossier ou l'utilisateur aurait mis autre
//     chose). Tout est best-effort : un ENOENT est le cas NORMAL (installation neuve).
static const char *const NEXTENDO_STALE_FILES[] = {
    // network_mitm (MITM ssl/ssl:s au boot) — retire au build 4.
    "sdmc:/atmosphere/contents/4200000000000666/flags/boot2.flag",
    "sdmc:/atmosphere/contents/4200000000000666/mitm.lst",
    "sdmc:/atmosphere/contents/4200000000000666/exefs.nsp",
    // Anciens emplacements du bundle CA du navigateur.
    "sdmc:/atmosphere/contents/0100000000000803/romfs/openssl_peer/cacerts.pem",
    "sdmc:/atmosphere/contents/0100000000000803/romfs/nro/netfront/openssl_peer/cacerts.pem",
    // Patch navigateur d'un build-id qui n'est plus cible.
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

// --- Efface ce qu'un ancien .nro a pose et qu'on ne livre plus. Renvoie le nombre de
//     fichiers reellement supprimes (0 = installation deja propre / neuve).
static int nextendo_purge_stale(void) {
    int removed = 0;
    for (size_t i = 0; i < sizeof(NEXTENDO_STALE_FILES) / sizeof(NEXTENDO_STALE_FILES[0]); i++)
        if (remove(NEXTENDO_STALE_FILES[i]) == 0) removed++;
    for (size_t i = 0; i < sizeof(NEXTENDO_STALE_DIRS) / sizeof(NEXTENDO_STALE_DIRS[0]); i++)
        rmdir(NEXTENDO_STALE_DIRS[i]);   // echoue si non vide -> volontaire
    return removed;
}

static void nextendo_provision_all(void) {
    nextendo_purge_stale();              // d'abord retirer l'ancien...
    copyTreeRomfs("romfs:/sd", "sdmc:"); // ...puis poser le courant
}

bool nextendo_apply_nextendo(void) {
    if (R_FAILED(ensureDir(NEXTENDO_HOSTS_DIR))) return false;
    nextendo_provision_all();            // pose TOUT le stack cert-trust (1ere fois ou MAJ)
    bool a = writeTextFile(NEXTENDO_HOSTS_SYSMMC, NEXTENDO_HOSTS);
    bool b = writeTextFile(NEXTENDO_HOSTS_EMUMMC, NEXTENDO_HOSTS);
    // add_defaults=0 : nos redirections couvrent deja *.nintendo.net en entier, y compris tout
    // serveur de telemetrie que Nintendo ajouterait plus tard -> le filet d'Atmosphere n'apporte
    // rien ici, et la telemetrie est de toute facon null-routee par nos propres lignes.
    bool i = iniSetDnsMitm(true, false);
    iniSetBlankProdinfoEmummc(false);    // emuMMC : vrai PRODINFO -> cert device OK (fix 2123-0011)
    nextendo_purge_leaks();              // logs DNS-MITM + .bak : l'IP du VPS n'a rien a y faire
    fsdevCommitDevice("sdmc");           // flush SD avant tout reboot
    return a && b && i;
}

bool nextendo_apply_nintendo(void) {
    nextendo_trace("20 apply_nintendo: entree");
    // Nos hosts sont SUPPRIMES, pas renommes : le retour en mode Nextendo les reecrit depuis
    // NEXTENDO_HOSTS et n'a jamais relu le .bak, qui ne servait donc qu'a garder l'IP du VPS
    // lisible sur la carte.
    remove(NEXTENDO_HOSTS_SYSMMC);
    remove(NEXTENDO_HOSTS_EMUMMC);
    nextendo_trace("21 hosts supprimes");
    nextendo_purge_leaks();
    nextendo_trace("22 purge_leaks ok");

    // Retirer ce qu'un ANCIEN .nro a laisse. Couper dns_mitm ne suffit pas : le sysmodule
    // network_mitm d'un vieux build intercepte ssl/ssl:s et demarre via boot2.flag, donc en mode
    // Nintendo la console parlerait aux VRAIS serveurs a travers un MITM SSL — avec un PRODINFO
    // blanchi par-dessus. C'est precisement ce qu'il ne faut pas, et ca donne 2137-7403.
    nextendo_purge_stale();
    nextendo_trace("23 purge_stale ok");

    // Retirer AUSSI le stack cert-trust courant (patches disable_ca_verification, CA Nextendo
    // injectee dans le navigateur, rootCA.pem). Mesure sur une console de test : il survivait a
    // la bascule, donc en mode Nintendo la verification des certificats restait desactivee a
    // l'echelle du systeme et notre CA restait de confiance — n'importe qui sur le reseau
    // pouvait intercepter le trafic vers le VRAI Nintendo. Sans risque : nextendo_provision_all()
    // repose tout au retour en mode Nextendo.
    removeTreeRomfs("romfs:/sd", "sdmc:");
    nextendo_trace("24 removeTreeRomfs ok");

    // TELEMETRIE. L'ancien code posait enable_dns_mitm=0, ce qui desactivait du meme coup le
    // blocage de telemetrie natif d'Atmosphere : la console se retrouvait MOINS protegee qu'une
    // install d'origine. Doc Atmosphere : "By default, atmosphere redirects resolution requests
    // for official telemetry servers, redirecting them to a loopback address." Nos hosts etant
    // partis, DNS.mitm retombe sur /atmosphere/hosts/default.txt (qu'Atmosphere cree lui-meme
    // avec receive-%.dg/er.srv.nintendo.net -> 127.0.0.1). On laisse donc le DNS-MITM ACTIF et
    // on remet add_defaults=1 : la telemetrie est bloquee, tout le reste resout normalement,
    // l'eShop continue de marcher (la raison d'etre du mode Nintendo).
    // Garde-fou : si nos hosts resistent a l'effacement, garder le DNS-MITM actif redirigerait
    // en douce vers nos serveurs une console censee etre sur Nintendo. Dans ce cas seulement, on
    // retombe sur l'ancien comportement (dns_mitm=0), qui neutralise nos hosts a coup sur.
    bool hostsGone = !fileExists(NEXTENDO_HOSTS_SYSMMC) && !fileExists(NEXTENDO_HOSTS_EMUMMC);
    bool i = hostsGone ? iniSetDnsMitm(true, true) : iniSetDnsMitm(false, false);
    nextendo_trace(hostsGone ? "25 ini ok (hosts partis, dns_mitm garde actif)"
                             : "25 ini ok (SECOURS : hosts resistants, dns_mitm coupe)");

    iniSetBlankProdinfoEmummc(true);     // emuMMC : PRODINFO blanchi -> anti-ban si online vrai Nintendo
                                         // (sans effet sur une console sysNAND-only : l'UI previent)
    nextendo_trace("26 blank_prodinfo ok");
    fsdevCommitDevice("sdmc");
    nextendo_trace("27 apply_nintendo: TERMINE");
    return i;
}

Result nextendo_reboot(void) {
    Result rc = bpcInitialize();
    if (R_FAILED(rc)) return rc;
    rc = bpcRebootSystem();              // ne revient pas si succes
    bpcExit();
    return rc;
}

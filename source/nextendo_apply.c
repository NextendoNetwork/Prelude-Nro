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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <switch.h>

#include "nextendo_apply.h"
#include "nextendo_config.h"
#include "nextendo_hosts.h"
#include "nextendo_net.h"

char g_server_ip[NEXTENDO_SERVER_IP_MAX] = NEXTENDO_SERVER_IP_DEFAULT;

const char *server_display_name(void) {
    if (strcmp(g_server_ip, NEXTENDO_SERVER_IP_DEFAULT) == 0) return "VPS (51.178.29.194)";
    if (strcmp(g_server_ip, NEXTENDO_SERVER_IP_ALT) == 0)     return "Local (3.135.232.168)";
    return g_server_ip;
}

#define SETTINGS_DIR "sdmc:/atmosphere/config"
#define NEXTENDO_EXOSPHERE_INI "sdmc:/exosphere.ini"
#define NEXTENDO_TRACE NEXTENDO_TRACE_PATH

// Verrou : depuis que le travail reseau du demarrage tourne dans un thread a part
// (pour supprimer l'ecran de chargement), main ET ce thread tracent en parallele.
// Sans ca, deux fopen/fputs/fclose concurrents entrelacent les lignes — et une
// trace illisible ne vaut rien le jour ou c'est notre seul temoin.
static Mutex s_traceMtx;

void nextendo_trace(const char *step) {
    mutexLock(&s_traceMtx);
    FILE *f = fopen(NEXTENDO_TRACE, "a");
    if (f) { fputs(step, f); fputc('\n', f); fclose(f); }
    fsdevCommitDevice("sdmc");   // durable tout de suite : c'est le point de la trace
    mutexUnlock(&s_traceMtx);
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
    // *.srv.nintendo.net et *srv.nintendo.net (sans point) etaient dans v2.0.9/v2.1.0
    // et couvraient TOUS les jeux. Le deuxieme pattern (*srv) matche les hotes multi-label
    // comme g2b309e01-lp1.s.n.srv.nintendo.net que le premier (*.srv) ne couvre pas.
    snprintf(line, sizeof(line), "%s    *.srv.nintendo.net\n", ip);       EMIT_H(line);
    snprintf(line, sizeof(line), "%s    *srv.nintendo.net\n", ip);        EMIT_H(line);
    // Wildcard g2* couvre TOUS les secure-servers NEX. En plus, on ajoute les
    // hotes EXPLICITES de chaque jeu au cas ou le wildcard ne matche pas dans
    // Atmosphere (le * mid-label peut etre ignore sur certains builds).
    snprintf(line, sizeof(line), "%s g2*.s.n.srv.nintendo.net\n", ip);         EMIT_H(line);
    // MK8 -> production (164) : les joueurs y sont ; le VPS dev n'a pas de joueurs.
    // Doit rester APRES le wildcard g2* : "derniere ligne qui matche gagne".
    snprintf(line, sizeof(line), "%s g2b309e01-lp1.s.n.srv.nintendo.net\n", NEXTENDO_SERVER_IP_NNCSD2); EMIT_H(line); // MK8 -> 164
    snprintf(line, sizeof(line), "%s g23380901-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // SSBU
    snprintf(line, sizeof(line), "%s g2ee2e300-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // ACNH
    snprintf(line, sizeof(line), "%s g26cfaf00-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // Strikers
    snprintf(line, sizeof(line), "%s g20de2100-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // LM3
    // --- Splatoon 3 / NPLN ---
    // Splatoon 3 n'utilise PAS NEX : il parle NPLN (gRPC sur HTTP/2), donc aucun
    // hote g2*.s.n. Les quatre premiers finissent par srv.nintendo.net et sont deja
    // pris par le wildcard *srv ; on les met quand meme en explicite, meme raison que
    // pour les jeux NEX ci-dessus (le * mid-label peut etre ignore sur certains builds).
    snprintf(line, sizeof(line), "%s t-dce9377b-lp1.lp1.t.npln.srv.nintendo.net\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s t-adf89f68-lp1.lp1.t.npln.srv.nintendo.net\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s gw.hac.lp1.vermillion.srv.nintendo.net\n", ip);     EMIT_H(line);
    snprintf(line, sizeof(line), "%s val.hac.lp1.penne.srv.nintendo.net\n", ip);         EMIT_H(line);
    snprintf(line, sizeof(line), "%s fro-3.hac.lp1.penne.srv.nintendo.net\n", ip);       EMIT_H(line);
    // dragons : celui-la n'est couvert par AUCUN wildcard existant — il finit par
    // .nintendo.net et non par srv.nintendo.net, et il n'y a pas de *.nintendo.net
    // generique (retire en v3.0.2 pour op2). Sans cette ligne il part chez Nintendo.
    snprintf(line, sizeof(line), "%s dragons.hac.lp1.dragons.nintendo.net\n", ip);       EMIT_H(line);
    // *.op2.nintendo.net RETIRÉ (v3.0.2): trop large — attrapait des sous-domaines
    // op2 non gérés par le VPS (authorization server, entitlement check) → 404 → erreurs
    // 2219-4001 (ACNH). On garde capi.lp1.op2.nintendo.net (ligne au-dessus) qui suffit.

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
        if (sz > 0) {
            size_t nr = fread(buf, 1, sz, f);
            if (nr != (size_t)sz) {
                // Lecture partielle -> fichier corrompu, on le traite comme inexistant.
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
        if (sz > 0) {
            size_t nr = fread(buf, 1, sz, f);
            if (nr != (size_t)sz) {
                // Lecture partielle -> fichier corrompu, on le traite comme inexistant.
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

// --- Miroir exact de copyTreeRomfs : parcourt l'arbre romfs et retire de la SD les chemins
//     correspondants, puis rmdir les dossiers devenus vides (rmdir echoue si non vide, donc
//     un dossier qui contient aussi des fichiers a l'utilisateur est PRESERVE : /atmosphere,
//     /atmosphere/contents, exefs_patches partage avec d'autres patches, etc.).
//     On se cale sur le romfs plutot que sur une liste codee en dur : la purge reste ainsi
//     automatiquement synchronisee avec ce qu'on installe, sans liste a maintenir. ---
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
            rmdir(dp);                  // vide seulement -> sinon conserve
        } else {
            remove(dp);
        }
    }
    closedir(d);
    return true;
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

static bool nextendo_provision_all(void) {
    nextendo_purge_stale();
    if (!copyTreeRomfs("romfs:/sd", "sdmc:")) return false;
    return true;
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
    bool i = iniSetDnsMitm(true, false);
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
    if (!removeTreeRomfs("romfs:/sd", "sdmc:")) {
        nextendo_trace("24b removeTreeRomfs a echoue");
        return false;
    }
    nextendo_trace("24 removeTreeRomfs ok");
    removeTreeRomfs("romfs:/ssbu_quickplay", "sdmc:"); // SSBU online-deluxe mod
    nextendo_trace("24c ssbu_quickplay retire");

    // TELEMETRIE. L'ancien code posait enable_dns_mitm=0, ce qui desactivait du meme coup le
    // blocage de telemetrie natif d'Atmosphere : la console se retrouvait MOINS protegee qu'une
    // install d'origine. Doc Atmosphere : "By default, atmosphere redirects resolution requests
    // for official telemetry servers, redirecting them to a loopback address." Nos hosts etant
    // partis, ce sont les entrees par defaut d'Atmosphere qui s'appliquent. Elles sont COMPILEES
    // DANS le sysmodule DNS.mitm (receive-%.dg/er.srv.nintendo.net -> 127.0.0.1), pas lues depuis
    // un fichier : add_defaults_to_dns_hosts=1 les fusionne avec la config hosts active, donc le
    // blocage vaut des le boot meme si /atmosphere/hosts/ est vide. Ne PAS ecrire nous-memes un
    // default.txt "de securite" : ce fichier appartient a l'utilisateur (beaucoup y gardent leurs
    // propres entrees), il survivrait a la desinstallation de Prelude sur une carte censee etre
    // propre, et notre copie de la liste deriverait derriere celle d'Atmosphere.
    // On laisse donc le DNS-MITM ACTIF et
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

// --- Diagnostic reseau (trace les infos utiles pour 2123-0011 / 2810-1224) ---
void nextendo_diag_network(void) {
    char buf[128];
    
    // nncs2 UDP connect (ne bloque pas, ne verifie que la validite de l'adresse).
    // Le test reel de connectivite Pia necessite d'echanger le protocole NEX, hors de
    // portee ici. On logge au moins la cible configuree.
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

    // nncs1 PIA connectivity test (UDP vers le VPS principal, port 10024 + 10025).
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

    // Test connectivite BCAT (HTTP au serveur :8095)
    if (nextendo_current_mode() == 0) {
        size_t blen = 0;
        int httpStatus = 0;
        unsigned char *body = net_http_get(g_server_ip, 8095, "/api/bcat/0100f8f0000a2000/cache", &blen, &httpStatus);
        snprintf(buf, sizeof(buf), "41 diag: BCAT %s:%d -> HTTP %d (%zu o)", g_server_ip, 8095, httpStatus, blen);
        nextendo_trace(buf);
        if (body) free(body);
    }

    // Verifie que les fichiers hosts sont presents (confirme que le mode Nextendo
    // a bien ses redirections ; si absent, le mode Nintendo est actif sans surprise).
    struct stat st;
    bool hasSys = stat(NEXTENDO_HOSTS_SYSMMC, &st) == 0;
    bool hasEmu = stat(NEXTENDO_HOSTS_EMUMMC, &st) == 0;
    snprintf(buf, sizeof(buf), "42 diag: hosts sysmmc=%d emummc=%d", hasSys, hasEmu);
    nextendo_trace(buf);
}

Result nextendo_reboot(void) {
    Result rc = bpcInitialize();
    if (R_FAILED(rc)) return rc;
    rc = bpcRebootSystem();              // ne revient pas si succes
    bpcExit();
    return rc;
}

// ------------------------------------------------------------------
//  SSBU Online Deluxe mod — install / remove / detect.
//  Le mod vit dans romfs:/ssbu_quickplay/ et se copie dans sdmc:.
//  L'installation est optionnelle : le joueur l'active via le bouton L.
// ------------------------------------------------------------------

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

// ------------------------------------------------------------------
//  Overclock embarque du mod SSBU — activation / desactivation.
//
//  Le mod embarque SON PROPRE overclock : le plugin skyline libnx_over.nro
//  et le sysmodule 00FF0000A11CE0FF (charge au boot via flags/boot2.flag).
//  Si le joueur utilise DEJA un gestionnaire d'horloge (Horizon OC, sys-clk),
//  les deux pilotent les memes rails PCV et la console GELE — au lancement de
//  Smash ou quelques secondes apres. Signe caracteristique : renommer le
//  dossier de Horizon OC fait remarcher le mod.
//
//  On desactive donc l'overclock DU MOD en gardant le reste du mod intact :
//    1. config.toml  -> overclocker = false  (le mod lit ce reglage au demarrage)
//    2. suppression du plugin libnx_over.nro
//    3. suppression du sysmodule 00FF0000A11CE0FF (c'est boot2.flag qui le charge)
//  Procedure confirmee par saad-script (auteur du mod).
//
//  La reactivation recopie les deux depuis le romfs du .nro : aucun telechargement,
//  donc l'operation reste reversible hors ligne.
// ------------------------------------------------------------------

#define SSBU_OC_CONFIG_DIR  "sdmc:/ultimate/ssbu_online_deluxe"
#define SSBU_OC_CONFIG      SSBU_OC_CONFIG_DIR "/config.toml"
#define SSBU_OC_PLUGIN      "sdmc:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libnx_over.nro"
#define SSBU_OC_SYSMOD      "sdmc:/atmosphere/contents/00FF0000A11CE0FF"
#define SSBU_OC_SYSMOD_ROMFS "romfs:/ssbu_quickplay/atmosphere/contents/00FF0000A11CE0FF"
#define SSBU_OC_PLUGIN_ROMFS "romfs:/ssbu_quickplay/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libnx_over.nro"

// C'est boot2.flag qui fait charger le sysmodule au demarrage : son absence est
// donc le marqueur fiable de "overclock du mod desactive", et pas le config.toml
// (que le joueur peut avoir edite a la main sans toucher aux fichiers).
bool nextendo_ssbu_oc_is_disabled(void) {
    return !fileExists(SSBU_OC_SYSMOD "/flags/boot2.flag");
}

bool nextendo_ssbu_oc_set(bool enabled) {
    bool ok;
    if (enabled) {
        if (R_FAILED(ensureDir(SSBU_OC_SYSMOD))) return false;
        ok = copyTreeRomfs(SSBU_OC_SYSMOD_ROMFS, SSBU_OC_SYSMOD);
        // Le plugin n'est recopie que si le mod lui-meme est installe : sinon on
        // recreerait un fichier orphelin dans une arbo que l'utilisateur a retiree.
        if (ok && nextendo_ssbu_is_installed())
            ok = copyFile(SSBU_OC_PLUGIN_ROMFS, SSBU_OC_PLUGIN);
        if (ok) {
            ensureDir(SSBU_OC_CONFIG_DIR);
            writeTextFile(SSBU_OC_CONFIG, "overclocker = true\n");
        }
    } else {
        // Ordre volontaire : on ecrit d'abord le reglage, pour que le mod parte en
        // mode "sans overclock" meme si une suppression echoue a mi-chemin.
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

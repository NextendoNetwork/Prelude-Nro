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

// Logique systeme. Deux pieges verifies : fsdevCommitDevice("sdmc") AVANT tout reboot sinon les ecritures sont perdues,
// et bpcRebootSystem() plutot que appletRequestToReboot, qui ne marche pas depuis hbmenu.
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
#include "nextendo_update.h"  // NEXTENDO_BUILD : la question de sauvegarde se repose a chaque build

char g_server_ip[NEXTENDO_SERVER_IP_MAX] = NEXTENDO_SERVER_IP_DEFAULT;

const char *server_display_name(void) {
    if (strcmp(g_server_ip, NEXTENDO_SERVER_IP_DEFAULT) == 0) return "VPS (51.178.29.194)";
    if (strcmp(g_server_ip, NEXTENDO_SERVER_IP_ALT) == 0)     return "Local (3.135.232.168)";
    return g_server_ip;
}

#define SETTINGS_DIR "sdmc:/atmosphere/config"
#define NEXTENDO_EXOSPHERE_INI "sdmc:/exosphere.ini"
#define NEXTENDO_TRACE NEXTENDO_TRACE_PATH

// main et le thread reseau tracent en parallele : sans verrou, deux fopen/fputs/fclose concurrents entrelacent les lignes.
static Mutex s_traceMtx;

void nextendo_trace(const char *step) {
    mutexLock(&s_traceMtx);
    FILE *f = fopen(NEXTENDO_TRACE, "a");
    if (f) { fputs(step, f); fputc('\n', f); fclose(f); }
    fsdevCommitDevice("sdmc");   // durable tout de suite : c'est le point de la trace
    mutexUnlock(&s_traceMtx);
}

// mkdir -p : sur fsdev, mkdir ne cree pas les dossiers intermediaires.
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
    // Les deux formes sont necessaires : *srv (sans point) matche les hotes multi-label que *.srv ne couvre pas.
    snprintf(line, sizeof(line), "%s    *.srv.nintendo.net\n", ip);       EMIT_H(line);
    snprintf(line, sizeof(line), "%s    *srv.nintendo.net\n", ip);        EMIT_H(line);
    // g2* couvre tous les secure-servers NEX, mais le * mid-label est ignore sur certains builds : d'ou les hotes explicites.
    snprintf(line, sizeof(line), "%s g2*.s.n.srv.nintendo.net\n", ip);         EMIT_H(line);
    // MK8 vers la production, ou sont les joueurs. Doit rester APRES le wildcard g2* : derniere ligne qui matche gagne.
    snprintf(line, sizeof(line), "%s g2b309e01-lp1.s.n.srv.nintendo.net\n", NEXTENDO_SERVER_IP_NNCSD2); EMIT_H(line); // MK8 -> 164
    snprintf(line, sizeof(line), "%s g23380901-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // SSBU
    snprintf(line, sizeof(line), "%s g2ee2e300-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // ACNH
    snprintf(line, sizeof(line), "%s g26cfaf00-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // Strikers
    snprintf(line, sizeof(line), "%s g20de2100-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // LM3
    // Ids releves sur les conteneurs en production, pas devines.
    snprintf(line, sizeof(line), "%s g23932a00-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // Mario Tennis Aces
    snprintf(line, sizeof(line), "%s g25c08801-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // ARMS
    snprintf(line, sizeof(line), "%s g2df33d01-lp1.s.n.srv.nintendo.net\n", ip); EMIT_H(line); // Splatoon 2
    // Splatoon 3 parle NPLN (gRPC sur HTTP/2), pas NEX : aucun hote g2*.s.n. Explicites pour la meme raison que ci-dessus.
    snprintf(line, sizeof(line), "%s t-dce9377b-lp1.lp1.t.npln.srv.nintendo.net\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s t-adf89f68-lp1.lp1.t.npln.srv.nintendo.net\n", ip); EMIT_H(line);
    snprintf(line, sizeof(line), "%s gw.hac.lp1.vermillion.srv.nintendo.net\n", ip);     EMIT_H(line);
    snprintf(line, sizeof(line), "%s val.hac.lp1.penne.srv.nintendo.net\n", ip);         EMIT_H(line);
    snprintf(line, sizeof(line), "%s fro-3.hac.lp1.penne.srv.nintendo.net\n", ip);       EMIT_H(line);
    // dragons finit par .nintendo.net et non srv.nintendo.net : aucun wildcard ne le prend, sans cette ligne il part chez Nintendo.
    snprintf(line, sizeof(line), "%s dragons.hac.lp1.dragons.nintendo.net\n", ip);       EMIT_H(line);
    // gamesync porte le lobby lui-meme (KeepUserSession sur TCP/7575) et echappe au wildcard *srv : sans lui le tenant repond mais aucune partie ne demarre.
    snprintf(line, sizeof(line), "%s    *.npln.nintendo.net\n", ip);                     EMIT_H(line);
    snprintf(line, sizeof(line), "%s gamesync.npln.nintendo.net\n", ip);                 EMIT_H(line);
    // *.op2.nintendo.net retire : trop large, il attrapait des sous-domaines non geres par le VPS -> 2219-4001 sur ACNH.

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

// Parser ligne a ligne : toutes les autres cles et sections de system_settings.ini sont preservees.
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

// blank_prodinfo_emummc : 0 en mode NEXTENDO (vrai cert device, confine par le DNS-MITM -> fix 2123-0011), 1 en mode NINTENDO (identite blanche, anti-ban).
// Lu par exosphere au BOOT et n'affecte QUE les boots emuMMC : blank_prodinfo_sysmmc n'est jamais touche.
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

// NEXTENDO si un fichier hosts redirige encore vers notre VPS, sinon NINTENDO.
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

// Copie fichier romfs -> SD.
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

// Copie recursive romfs -> SD. On ECRASE (les patches sont idempotents) pour qu'une MAJ du .nro propage bien les derniers.
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

// Miroir exact de copyTreeRomfs : se caler sur le romfs plutot qu'une liste en dur garde la purge synchronisee avec ce qu'on installe.
// rmdir echoue si le dossier n'est pas vide, ce qui PRESERVE tout dossier ou l'utilisateur a mis autre chose.
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

// Retire ce qui laisse l'IP du VPS lisible sur la carte : les deux logs DNS-MITM (Atmosphere y ecrit la table des redirections et notre IP a chaque requete) et les *.txt.bak, jamais relus.
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

// Provisionne le stack cert-trust (romfs -> SD) pour un mode Nextendo fonctionnel sans install manuelle.
// Tout est gate par build-id firmware : seul ce qui matche la console s'applique, le reste dort.

// Orphelins d'ANCIENS builds : copyTreeRomfs n'ecrit que le romfs COURANT, donc ce qu'on a cesse de livrer reste sur la SD indefiniment.
// Le plus grave est network_mitm, qui intercepte tout le SSL au boot : un vieux Prelude laisse donc un MITM actif face aux VRAIS serveurs Nintendo (2137-7403).
// Fichiers d'abord, dossiers ensuite ; tout est best-effort, un ENOENT est le cas NORMAL sur une installation neuve.
static const char *const NEXTENDO_STALE_FILES[] = {
    // network_mitm (MITM ssl/ssl:s au boot), retire au build 4.
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

// Renvoie le nombre de fichiers reellement supprimes (0 = installation deja propre ou neuve).
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

// Sauvegarde des hosts dns.mitm de l'utilisateur : le mode NINTENDO les SUPPRIME, donc on les copie une fois avant d'ecrire quoi que ce soit.
// La copie vit dans sdmc:/switch/ et non dans atmosphere/hosts/, que purge_leaks nettoie et qu'Atmosphere lit.
// REGLE DE SECURITE : on ne sauvegarde JAMAIS un fichier portant l'IP du VPS, sinon la copie ferait ce que purge_leaks existe pour empecher.
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
    if (!ok) remove(dst);   // pas de demi-copie sur la carte
    return ok;
}

// Sauvegardable = le fichier existe ET n'est pas un fichier que NOUS avons ecrit.
// Reconnu d'abord a l'EN-TETE, presente dans TOUT fichier genere par Prelude : un filtre sur les seules IP courantes laisserait passer un hosts d'une version tres ancienne, qu'on restaurerait vers un serveur mort.
// Les IP sont testees ensuite, pour un fichier bricole a la main qui aurait perdu l'en-tete.
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
    // Revalidee AVANT restauration, pas seulement a la creation : c'est ce qui repare les copies deja fabriquees par une version anterieure, sans rien demander a l'utilisateur.
    if (backupCandidateOk(NEXTENDO_BACKUP_SYSMMC) &&
        copyFileRaw(NEXTENDO_BACKUP_SYSMMC, NEXTENDO_HOSTS_SYSMMC)) n++;
    if (backupCandidateOk(NEXTENDO_BACKUP_EMUMMC) &&
        copyFileRaw(NEXTENDO_BACKUP_EMUMMC, NEXTENDO_HOSTS_EMUMMC)) n++;
    return n;
}

// prelude_backup.cfg : une cle par ligne.
//   prompt_build     dernier build ayant POSE la question (0 = jamais)
//   use_for_nintendo 1 = remettre la sauvegarde au passage en mode NINTENDO
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

// Posee UNE SEULE FOIS : apres la reponse, les hosts d'origine sont soit sauvegardes soit ecrases, et redemander n'afficherait plus que "rien a sauvegarder".
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

// Compte les .ips cote CARTE et cote ROMFS : deux nombres differents = la carte porte encore ceux d'une version precedente.
// Limite a annoncer honnetement : ceci dit ce qui est sur la carte, pas ce qu'Atmosphere a APPLIQUE — un build id inconnu ne recoit rien, en silence.
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

// provision_all est interne, mais le demarrage doit pouvoir rafraichir la carte sans bascule de mode complete.
bool nextendo_provision_all_public(void) { return nextendo_provision_all(); }

void nextendo_s3_status(NextendoS3Status *out) {
    if (!out) return;
    out->onSd = countIps("sdmc:/atmosphere/exefs_patches/s3certbypass") +
                countIps("sdmc:/atmosphere/exefs_patches/s3peername");
    out->inRomfs = countIps("romfs:/sd/atmosphere/exefs_patches/s3certbypass") +
                   countIps("romfs:/sd/atmosphere/exefs_patches/s3peername");
    // dns.mitm coupe = la console parle au VRAI Nintendo, et aucun correctif n'y peut rien.
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
    // add_defaults=1 comme en mode NINTENDO : on ne maintient pas notre propre liste de telemetrie, celle d'Atmosphere est suivie en amont.
    // Sans conflit avec nos redirections : les defauts ne couvrent que receive-%, jamais accounts.nintendo.com ni les hotes de jeu.
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
    // SUPPRIMES, pas renommes : le .bak n'etait jamais relu et ne servait qu'a garder l'IP du VPS lisible sur la carte.
    remove(NEXTENDO_HOSTS_SYSMMC);
    remove(NEXTENDO_HOSTS_EMUMMC);
    nextendo_trace("21 hosts supprimes");
    // Remises APRES la suppression. La sauvegarde ayant ete refusee si elle portait une de nos IP, rien de Nextendo ne revient par ce chemin.
    if (nextendo_backup_use_for_nintendo() && nextendo_hosts_backup_exists()) {
        int nb = nextendo_hosts_backup_restore();
        nextendo_trace(nb ? "21b hosts utilisateur restaures"
                          : "21b WARN: restauration hosts utilisateur echouee");
    }
    nextendo_purge_leaks();
    nextendo_trace("22 purge_leaks ok");

    // Couper dns_mitm ne suffit pas : le network_mitm d'un vieux build demarre via boot2.flag et laisserait un MITM SSL face aux VRAIS serveurs (2137-7403).
    nextendo_purge_stale();
    nextendo_trace("23 purge_stale ok");

    // Le stack cert-trust survivait a la bascule : la verif des certificats restait coupee et notre CA de confiance face au VRAI Nintendo. provision_all() repose tout au retour.
    if (!removeTreeRomfs("romfs:/sd", "sdmc:")) {
        nextendo_trace("24b removeTreeRomfs a echoue");
        return false;
    }
    nextendo_trace("24 removeTreeRomfs ok");
    removeTreeRomfs("romfs:/ssbu_quickplay", "sdmc:"); // SSBU online-deluxe mod
    nextendo_trace("24c ssbu_quickplay retire");

    // DNS-MITM laisse ACTIF avec add_defaults=1 : couper dns_mitm desactiverait aussi le blocage de telemetrie natif, rendant la console MOINS protegee qu'une install d'origine.
    // Les entrees par defaut sont COMPILEES dans le sysmodule DNS.mitm, pas lues d'un fichier : ne PAS ecrire notre propre default.txt, il appartient a l'utilisateur et deriverait derriere celui d'Atmosphere.
    // Garde-fou : si nos hosts resistent a l'effacement, on retombe sur dns_mitm=0, qui les neutralise a coup sur.
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

// Diagnostic reseau : trace les infos utiles pour 2123-0011 / 2810-1224.
void nextendo_diag_network(void) {
    char buf[128];
    
    // Ne verifie que la validite de l'adresse : un vrai test Pia demanderait d'echanger le protocole NEX.
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

    // Presence des fichiers hosts : absents, c'est simplement que le mode Nintendo est actif.
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

// Mod SSBU Online Deluxe : vit dans romfs:/ssbu_quickplay/ et se copie dans sdmc:. Installation optionnelle.

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

// Overclock embarque du mod SSBU (plugin libnx_over.nro + sysmodule 00FF0000A11CE0FF, charges par boot2.flag).
// Avec Horizon OC / sys-clk deja en place, les deux pilotent les memes rails PCV et la console GELE au lancement de Smash.
// Desactiver = config.toml overclocker=false, puis suppression du plugin et du sysmodule (procedure confirmee par saad-script, auteur du mod).

#define SSBU_OC_CONFIG_DIR  "sdmc:/ultimate/ssbu_online_deluxe"
#define SSBU_OC_CONFIG      SSBU_OC_CONFIG_DIR "/config.toml"
#define SSBU_OC_PLUGIN      "sdmc:/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libnx_over.nro"
#define SSBU_OC_SYSMOD      "sdmc:/atmosphere/contents/00FF0000A11CE0FF"
#define SSBU_OC_SYSMOD_ROMFS "romfs:/ssbu_quickplay/atmosphere/contents/00FF0000A11CE0FF"
#define SSBU_OC_PLUGIN_ROMFS "romfs:/ssbu_quickplay/atmosphere/contents/01006A800016E000/romfs/skyline/plugins/libnx_over.nro"

// boot2.flag est le marqueur fiable, pas config.toml : le joueur peut avoir edite ce dernier a la main.
bool nextendo_ssbu_oc_is_disabled(void) {
    return !fileExists(SSBU_OC_SYSMOD "/flags/boot2.flag");
}

bool nextendo_ssbu_oc_set(bool enabled) {
    bool ok;
    if (enabled) {
        if (R_FAILED(ensureDir(SSBU_OC_SYSMOD))) return false;
        ok = copyTreeRomfs(SSBU_OC_SYSMOD_ROMFS, SSBU_OC_SYSMOD);
        // Recopie seulement si le mod est installe : sinon on recreerait un orphelin dans une arbo retiree.
        if (ok && nextendo_ssbu_is_installed())
            ok = copyFile(SSBU_OC_PLUGIN_ROMFS, SSBU_OC_PLUGIN);
        if (ok) {
            ensureDir(SSBU_OC_CONFIG_DIR);
            writeTextFile(SSBU_OC_CONFIG, "overclocker = true\n");
        }
    } else {
        // Le reglage d'abord : le mod part ainsi sans overclock meme si une suppression echoue a mi-chemin.
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

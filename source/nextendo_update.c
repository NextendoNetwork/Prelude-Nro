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
//  Nextendo .nro -- auto-update via GitHub Releases API.
//  Checks https://api.github.com/repos/NextendoNetwork/Prelude-Nro/releases/latest
//  for the latest version tag, compares semver with NEXTENDO_VERSION_*,
//  and downloads the .nro asset if a newer version is available.
// ============================================================
#include <switch.h>
#include <string.h>
#include <strings.h>   // strcasecmp (extension .nro insensible a la casse)
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

#include "nextendo_update.h"
#include "nextendo_net.h"
#include "nextendo_apply.h"   // nextendo_trace : diagnostic de l'updater depuis la carte

// GitHub API for latest release
#define GH_API_HOST  "api.github.com"
#define GH_API_PATH  "/repos/NextendoNetwork/Prelude-Nro/releases/latest"
#define GH_API_PORT  443

// --- Chemin du .nro : celui qu'on EXECUTE, pas un chemin devine. ---
//  L'updater ecrivait TOUJOURS dans sdmc:/switch/nextendo.nro. Pour tous ceux dont le
//  Prelude vit ailleurs (autre nom, sous-dossier, racine de la carte), la mise a jour
//  deposait donc un fichier EN PLUS : l'ancien restait celui qu'ils lancaient, et la
//  carte se retrouvait avec deux versions. C'est la cause des DEUX rapports recus —
//  « l'updater ne marche pas » (on relance l'ancien) et « il devrait remplacer Prelude »
//  (les deux sont conservees). Le seul utilisateur pour qui ca marchait etait celui dont
//  le chemin coincidait avec la constante.
//  hbmenu passe le chemin REEL du .nro dans argv[0] : on ecrit LA, donc le fichier
//  remplace est exactement celui que l'utilisateur vient de lancer.
#define LEGACY_NRO_FILE "sdmc:/switch/nextendo.nro"
#define LEGACY_TMP_FILE "sdmc:/switch/nextendo.nro.new"

static char g_self_nro[512] = {0};
static char g_self_tmp[520] = {0};

void nextendo_update_set_self_path(const char *argv0) {
    // Sans argv exploitable on garde le chemin historique : mieux vaut l'ancien
    // comportement qu'une ecriture a un endroit invente.
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
    // Le fichier temporaire va A COTE de la cible : rename() ne traverse alors aucune
    // frontiere et le .new ne traine pas dans un dossier qui n'est pas le sien.
    snprintf(g_self_tmp, sizeof(g_self_tmp), "%s.new", g_self_nro);
}

static const char *nroPath(void) { return g_self_nro[0] ? g_self_nro : LEGACY_NRO_FILE; }
static const char *nroTmp(void)  { return g_self_tmp[0] ? g_self_tmp : LEGACY_TMP_FILE; }

// Copie src -> dst en ECRASANT dst sans le supprimer d'abord. Renvoie false des que
// l'ouverture ou une ecriture echoue, en laissant le soin a l'appelant d'essayer autre
// chose : c'est la brique des trois tentatives de remplacement ci-dessous.
static bool copyOver(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    char cbuf[16384];
    size_t n;
    bool ok = true;
    while ((n = fread(cbuf, 1, sizeof(cbuf), in)) > 0)
        if (fwrite(cbuf, 1, n, out) != n) { ok = false; break; }
    fclose(in);
    if (fclose(out) != 0) ok = false;   // erreur d'ecriture differee (carte pleine)
    return ok;
}

static char g_download_url[512] = {0};
static long g_download_size = 0;

// Find a JSON string value for a given key, tolerating optional whitespace around ':'.
// Returns pointer to the first char inside the opening quote, or NULL.
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

// Parse the GitHub releases/latest JSON response.
// Tolerates both compact ("key":"val") and pretty-printed ("key": "val") formatting.
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

    // size -> asset byte count (strtol skips leading spaces automatically)
    char *sp = strstr((const char*)b, "\"size\":");
    if (sp) { sp += 7; *size = strtol(sp, NULL, 10); }

    return *maj > 0;
}

// Compare two semver triplets. Returns >0 if a>b, <0 if a<b, 0 if equal.
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
        if (parse_github_json(body, len, &maj, &min, &patch, g_download_url, sizeof(g_download_url), &sz)) {
            if (semver_cmp(maj, min, patch,
                           NEXTENDO_VERSION_MAJOR, NEXTENDO_VERSION_MINOR, NEXTENDO_VERSION_PATCH) > 0
                && sz > 4096) {
                u.available = true;
                u.maj = maj; u.min = min; u.patch = patch;
                u.size = sz;
                g_download_size = sz;
            }
        }
        free(body);
    }
    return u;
}

// Download and apply the update. Requires sslInitialize() before.
nextendo_update_result nextendo_update_apply(long expectedSize) {
    if (g_download_url[0] == '\0') return NUP_NET_FAIL;
    long expected = expectedSize > 0 ? expectedSize : g_download_size;

    FILE *f = fopen(nroTmp(), "wb");
    if (!f) {
        mkdir("sdmc:/switch", 0777);
        f = fopen(nroTmp(), "wb");
    }
    if (!f) return NUP_WRITE_FAIL;

    socketInitializeDefault();
    Result rc = sslInitialize(4);
    if (R_FAILED(rc)) { fclose(f); socketExit(); return NUP_NET_FAIL; }

    char host[256] = {0};
    char path[1024] = {0};
    if (sscanf(g_download_url, "https://%255[^/]%1023s", host, path) < 2) {
        sslExit(); fclose(f); remove(nroTmp()); socketExit(); return NUP_NET_FAIL;
    }

    int status = 0;
    long len = net_https_get_to_file(host, path, f, &status);
    fclose(f);
    sslExit();
    socketExit();

    if (len == -2) { remove(nroTmp()); return NUP_WRITE_FAIL; }
    if (len < 0)   { remove(nroTmp()); return NUP_NET_FAIL; }
    if (status != 200 || len < 4096) { remove(nroTmp()); return NUP_NET_FAIL; }
    if (expected > 0 && len != expected) { remove(nroTmp()); return NUP_SIZE_FAIL; }
    fsdevCommitDevice("sdmc");

    // --- Remplacement du .nro. ---
    //  Le build 55 a fait passer la cible de « un fichier a un chemin fixe » a « le
    //  fichier qu'on est en train d'executer ». Le commentaire d'origine disait que
    //  l'ecrasement etait sans risque parce que le code tourne depuis la RAM ; c'etait
    //  gratuit tant que le fichier remplace n'etait PAS celui qu'on executait, et ca a
    //  cesse de l'etre. Selon le chargeur de homebrew, le .nro en cours peut rester
    //  ouvert : remove() echoue, rename() ne peut pas ecraser sur FAT32, l'ouverture en
    //  ecriture est refusee, et l'utilisateur voit « impossible d'ecrire sur la SD »
    //  avec une mise a jour pourtant deja telechargee. Rapporte par Andrei depuis 3.3.3.
    //
    //  On ne renonce donc plus a la premiere resistance : ecrasement en place, puis
    //  remove+rename, puis en dernier recours l'ancien emplacement fixe. Ce dernier
    //  recours recree le doublon que le build 55 corrigeait — c'est assume : une mise a
    //  jour installee ailleurs vaut mieux qu'une mise a jour perdue, et la trace dit
    //  exactement ce qui s'est passe.
    bool placed = false;

    // 1) Ecrasement EN PLACE, sans supprimer d'abord : si le fichier est verrouille en
    //    suppression mais ouvrable en ecriture, ce chemin passe la ou l'ancien echouait.
    if (copyOver(nroTmp(), nroPath())) {
        placed = true;
        remove(nroTmp());
        nextendo_trace("60 update: ecrase en place");
    }

    // 2) remove + rename : le chemin historique, le plus propre quand il fonctionne.
    if (!placed) {
        remove(nroPath());
        if (rename(nroTmp(), nroPath()) == 0) {
            placed = true;
            nextendo_trace("61 update: remplace par rename");
        } else if (copyOver(nroTmp(), nroPath())) {
            placed = true;
            remove(nroTmp());
            nextendo_trace("62 update: remplace par copie apres remove");
        }
    }

    // 3) Dernier recours : l'emplacement historique. L'utilisateur devra deplacer le
    //    fichier a la main, mais il A la mise a jour au lieu d'une erreur.
    if (!placed && strcmp(nroPath(), LEGACY_NRO_FILE) != 0) {
        mkdir("sdmc:/switch", 0777);
        if (copyOver(nroTmp(), LEGACY_NRO_FILE)) {
            placed = true;
            remove(nroTmp());
            nextendo_trace("63 WARN update: cible verrouillee -> ecrit dans switch/nextendo.nro");
        }
    }

    if (!placed) {
        remove(nroTmp());
        nextendo_trace("64 ERREUR update: aucune ecriture possible");
        return NUP_WRITE_FAIL;
    }

    // Une mise a jour PRECEDENTE (avant ce correctif) a pu deposer une copie a l'ancien
    // emplacement fixe. Si ce n'est pas le fichier qu'on vient de remplacer, c'est un
    // orphelin que nous avons cree nous-memes, et il ne sert qu'a laisser croire qu'il
    // reste deux versions installees. On ne supprime QUE ce chemin-la, jamais un autre.
    if (strcmp(nroPath(), LEGACY_NRO_FILE) != 0) {
        remove(LEGACY_NRO_FILE);
        remove(LEGACY_TMP_FILE);
    }

    fsdevCommitDevice("sdmc");
    return NUP_OK;
}

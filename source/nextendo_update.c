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
#include <errno.h>     // trace du motif exact d'un echec d'ecriture

#include "nextendo_update.h"
#include "nextendo_net.h"
#include "nextendo_apply.h"   // nextendo_trace : diagnostic de l'updater depuis la carte
#include "audio.h"            // la BGM tient un FILE* ouvert sur le romfs (cf. releaseRomfs)

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

// --- Relais de progression. ---
//  net_https_get_to_file ne connait que ce que le serveur annonce ; si Content-Length
//  manque il rapporte total=0 et la barre resterait plate pendant 17 Mo. On retombe
//  alors sur la taille que l'API GitHub nous a donnee, qu'on a de toute facon deja.
static nextendo_progress_fn g_progress_cb    = NULL;
static long                 g_progress_total = 0;

static void progressRelay(long received, long total) {
    if (total <= 0) total = g_progress_total;
    if (g_progress_cb) g_progress_cb(NUP_PHASE_DOWNLOAD, received, total);
}

// Copie src -> dst en ECRASANT dst sans le supprimer d'abord. Renvoie false des que
// l'ouverture ou une ecriture echoue, en laissant le soin a l'appelant d'essayer autre
// chose : c'est la brique des trois tentatives de remplacement ci-dessous.
static bool copyOver(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    // La pose du fichier copie 17 Mo sur la carte : sans signalement, l'ecran reste
    // fige sur 100 % de telechargement le temps de l'ecriture.
    long copied = 0;
    if (g_progress_cb) g_progress_cb(NUP_PHASE_INSTALL, 0, g_progress_total);
    FILE *out = fopen(dst, "wb");
    // errno est la seule chose qui distingue « cible verrouillee » de « carte pleine »
    // dans la trace : on le met de cote avant chaque fclose(), qui a le droit de
    // l'ecraser meme en reussissant.
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
    if (fclose(out) != 0) { if (ok) err = errno; ok = false; }   // ecriture differee
    if (!ok) errno = err;
    return ok;
}

// Trace + motif exact de l'echec. Les trois tentatives de remplacement echouaient
// toutes de la meme facon dans le rapport utilisateur (« impossible d'ecrire sur la
// carte SD ») sans jamais dire POURQUOI : errno distingue un verrou (EBUSY / EACCES)
// d'une carte pleine (ENOSPC) ou d'un chemin absent (ENOENT).
static void traceErr(const char *step) {
    char m[160];
    snprintf(m, sizeof(m), "%s (errno=%d)", step, errno);
    nextendo_trace(m);
}

// --- Liberation du romfs le temps du remplacement. ---
//  romfsInit() (main) ne monte pas un fichier a part : libnx lit argv[0] et garde un
//  handle FS OUVERT sur le .nro en cours pour toute la session, parce que le romfs est
//  lu a la demande (patches .ips, donnees BCAT, bgm). Le fichier que l'updater doit
//  remplacer est donc tenu ouvert par nous-memes : la FS refuse alors l'ouverture en
//  ecriture, remove() et rename() echouent, les trois tentatives tombent d'affilee et
//  l'utilisateur voit « impossible d'ecrire sur la carte SD » avec une mise a jour
//  pourtant deja telechargee et verifiee.
//
//  C'est la vraie cause du rapport d'Andrei depuis 3.3.3 : le build 55 a fait passer la
//  cible de « un chemin fixe » a « le fichier qu'on execute », et c'est precisement ce
//  fichier-la que le romfs tient. Avant 55 ca marchait par accident — on ecrasait un
//  AUTRE fichier que celui qui tournait (d'ou les doublons signales a l'epoque).
//
//  L'audio part en premier : mpg123 detient un FILE* ouvert sur romfs:/bgm.mp3 pendant
//  toute la session. Les polices et les images, elles, sont deja entierement en RAM
//  (ui_init lit dans un buffer puis ferme), donc l'ecran de resultat s'affiche sans
//  romfs monte.
static void releaseRomfs(void) {
    audio_exit();    // ferme le FILE* que mpg123 tient sur romfs:/bgm.mp3
    romfsExit();     // ferme le handle FS sur le .nro courant
    nextendo_trace("59 update: romfs relache (le .nro cible n'est plus ouvert)");
}

// Remonte le romfs — depuis le .nro remplace s'il l'a ete, sinon l'ancien — et relance
// la musique. Non-fatal : une MAJ reussie demande de toute facon de fermer et relancer
// Prelude, mais un ECHEC doit laisser l'app entierement utilisable (changement de mode,
// drapeaux, BCAT lisent tous le romfs).
static void restoreRomfs(void) {
    if (R_FAILED(romfsInit())) { nextendo_trace("69 WARN update: romfs non remonte"); return; }
    audio_init();
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
        // parse_github_json travaille au strstr : le corps DOIT etre termine par un NUL.
        // net_https_get renvoie exactement les octets du corps, sans terminateur — on
        // lisait donc au-dela de l'allocation, avec le resultat que le tas voulait bien
        // donner ce jour-la. On recopie dans un tampon terminé plutot que de faire
        // confiance a ce qui suit le buffer.
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

// Download and apply the update. Requires sslInitialize() before.
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

    // --- Remplacement du .nro. ---
    //  Le build 55 a fait passer la cible de « un fichier a un chemin fixe » a « le
    //  fichier qu'on est en train d'executer ». Le commentaire d'origine disait que
    //  l'ecrasement etait sans risque parce que le code tourne depuis la RAM ; c'etait
    //  gratuit tant que le fichier remplace n'etait PAS celui qu'on executait, et ca a
    //  cesse de l'etre : ce n'est PAS le chargeur de homebrew qui tient le fichier, c'est
    //  NOUS. romfsInit() garde un handle FS ouvert sur le .nro courant pendant toute la
    //  session (le romfs est lu a la demande), donc remove() echoue, rename() ne peut pas
    //  ecraser, l'ouverture en ecriture est refusee, et l'utilisateur voit « impossible
    //  d'ecrire sur la SD » avec une mise a jour pourtant deja telechargee et verifiee.
    //  Rapporte par Andrei depuis 3.3.3 — soit exactement depuis le build 55.
    //
    //  La correction tient dans releaseRomfs() : on lache le romfs le temps de poser le
    //  fichier. Les trois tentatives restent en place derriere, comme filet, et chacune
    //  trace desormais son errno — un echec qui subsisterait dirait enfin lequel.
    bool placed = false;

    // Le .nro cible est OUVERT par notre propre romfs tant qu'on ne le relache pas :
    // sans ca les trois tentatives ci-dessous echouent toutes, quelle que soit la carte.
    releaseRomfs();
    { char m[600]; snprintf(m, sizeof(m), "59b update: cible = %s", nroPath()); nextendo_trace(m); }

    // 1) Ecrasement EN PLACE, sans supprimer d'abord : si le fichier est verrouille en
    //    suppression mais ouvrable en ecriture, ce chemin passe la ou l'ancien echouait.
    if (copyOver(nroTmp(), nroPath())) {
        placed = true;
        remove(nroTmp());
        nextendo_trace("60 update: ecrase en place");
    } else {
        traceErr("60 update: ecrasement en place refuse");
    }

    // 2) remove + rename : le chemin historique, le plus propre quand il fonctionne.
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

    // 3) Dernier recours : l'emplacement historique. L'utilisateur devra deplacer le
    //    fichier a la main, mais il A la mise a jour au lieu d'une erreur.
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
        // Prelude EST deja a l'emplacement historique : il n'y a pas d'autre endroit ou
        // se replier. Le dire, plutot que de sortir sans avoir rien tente de plus.
        nextendo_trace("63 update: pas de repli possible (deja switch/nextendo.nro)");
    }

    if (!placed) {
        remove(nroTmp());
        nextendo_trace("64 ERREUR update: aucune ecriture possible");
        restoreRomfs();
        return NUP_WRITE_FAIL;
    }

    // Remonte le romfs sur le .nro qui vient d'etre remplace : l'app reste utilisable
    // jusqu'a ce que l'utilisateur la ferme et la relance comme le lui dit l'ecran.
    restoreRomfs();

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

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

// Auto-mise a jour : /api/nro/latest au lancement, puis remplacement du .nro en cours d'execution.
#ifndef NEXTENDO_UPDATE_H
#define NEXTENDO_UPDATE_H
#include <switch.h>

// Version de CE build. A INCREMENTER a chaque release (le serveur renvoie la derniere).
// build 4  : mode Nintendo coupe network_mitm (fix eShop 2137-7403) + auto-provisioning du stack cert-trust.
// build 5  : release Prelude v1 (rebrand Nextendo -> Prelude ; MAJ desormais OBLIGATOIRE).
// build 6  : fix "impossible d'ecrire sur la SD" a la MAJ (rename() echouait sur certaines cartes fatfs).
// build 7  : alignement des versions — APP_VERSION (Makefile) = X.Y.N ou N = NEXTENDO_BUILD.
// build 8  : PRODINFO par mode — NEXTENDO blank_prodinfo_emummc=0 (fix 2123-0011), NINTENDO =1. sysNAND jamais touche.
// build 9  : nncs2 vers le VPS OVH (l'ancienne machine etait morte -> NAT incomplet -> 2618-201) + purge des fichiers des anciens builds.
// build 10 : audit de securite — telemetrie (dns_mitm reste actif avec add_defaults=1), retrait du cert-trust en mode Nintendo, purge des logs qui fuyaient notre IP, avertissement sans emuMMC.
// build 11 : build de diagnostic — nextendo_trace() ecrit chaque etape dans prelude_trace.txt avec commit par ligne.
// build 12 : release de l'audit, verifie sur console. La trace est gardee : le gel du build 10 n'a jamais ete explique.
// build 13 : ecran de revue avant application (revert au build 14 : il embrouillait plus qu'il n'aidait).
// build 14 : revert du config review screen + workflow GitHub Actions.
// v2.0.1   : hosts a jour (penne_ids, dauth, srv.nintendo.net). v2.0.0 avait NEXTENDO_BUILD 0 -> faux "MAJ obligatoire".
// v2.0.3   : retrait du wildcard *.nintendo.net pour que d4c resolve vers le vrai Nintendo (fix popup fantasme de MAJ).
// build 24 : fix browser conntest (retrait *.nintendowifi.net) + codes d'erreur reseau distincts pour le BCAT.
// build 25 : creation auto de la save BCAT + ecran de confirmation avant de telecharger une MAJ.
// build 28 : v2.0.9. Fix du gel des entrees (consoleUpdate(NULL) dans la boucle principale).
// build 29 : v2.1.0. Auto-MAJ via l'API GitHub en HTTPS, sans dependre du VPS.
// build 30 : v2.1.1. Verif de MAJ au lancement en HTTP : sslInit/sslExit cassait le service SSL systeme.
// build 31 : v2.1.2. Retrait des wildcards *srv.nintendo.net pour que le conntest du navigateur (FW 18.0+) marche.
// build 32 : v2.1.3. Planning S2 via LayeredFS Atmosphere au lieu du montage BCAT SaveData (USA + EUR).
// build 33 : v2.1.4. Vraie IP du serveur Nextendo (51.178.29.194) a la place du placeholder.
// build 34 : v2.1.5. Donnees du planning S2 embarquees dans la romfs du .nro, plus aucun telechargement.
// build 35 : v3.0.0. Nouveaux plannings + wildcard g2*.s.n.srv.nintendo.net couvrant tous les serveurs NEX.
// build 36 : v3.0.2. Retrait de *.op2.nintendo.net (causait 2219-4001 sur ACNH) + redirections conntest.
// build 40 : v3.0.6. MK8 secure-server route vers l'IP de production, place APRES le wildcard g2* (derniere ligne gagnante).
// build 41 : v3.0.7. Fix updater : comparaison en semver complet (le patch du tag etait compare a NEXTENDO_BUILD) + depot amont.
// build 42 : v3.0.8. Fix telechargement BCAT : sslConnectionSetSocketDescriptor brut echouait, il faut la version socket* de libnx.
// build 43 : v3.0.9. BCAT : un seul zip EUR telecharge puis installe sur toutes les regions.
// build 44 : v3.1.0. Fix updater : le JSON GitHub est indente, "tag_name":"" ne matchait jamais -> json_str_value() tolere les espaces.
// build 45 : v3.2.0. Installeur de drapeau de pays MK8D (110 pays, menu deroulant).
// build 46 : v3.2.2. Mod SSBU online-deluxe embarque dans la romfs du .nro.
// build 47 : v3.2.3. Hote de jeu explicite pour Luigi's Mansion 3 (deja couvert par g2*, ajoute par fiabilite).
// build 48 : v3.2.4. Fix updater : les URL de release GitHub redirigent (302) vers un CDN, on suit desormais une redirection.
// build 49 : v3.2.5. Mod SSBU rendu optionnel, avec son propre ecran d'installation.
// build 50 : v3.2.6. Bascule pour l'overclock du mod SSBU : conflit avec Horizon OC / sys-clk (memes rails PCV) -> gel au lancement de Smash.
// build 51 : v3.2.7. Formulation honnete du mode NINTENDO : sur emuMMC blank_prodinfo=1 coupe l'authentification, donc ni eShop ni jeu en ligne.
// build 52 : v3.3.0. UI au look de la console, SDL2 remplace par mpg123 (.nro 25.97 -> 17.01 Mo), et hotes NPLN de Splatoon 3.
// build 53 : v3.3.1. Splatoon 3 : patches IPS s3certbypass + s3peername. Le jeu lie son propre BoringSSL et ne passe jamais par le service ssl: de la console.
// build 54 : v3.3.2. Le mode NEXTENDO ne fusionnait pas les entrees de telemetrie par defaut d'Atmosphere (add_defaults etait a false). Signale par TherealJaw.
// build 55 : v3.3.3. L'updater ecrit desormais dans le .nro LANCE (argv[0]) et non un chemin fixe + sauvegarde des hosts que l'utilisateur avait avant Prelude.
// build 56 : v3.3.4. On reconnait nos propres hosts a leur EN-TETE, pas a leurs deux IP courantes, et on revalide la sauvegarde avant de la restaurer.
// build 57 : v3.3.5. Trois tentatives de remplacement du .nro (ecrasement en place, remove+rename, ancien chemin) + trace de l'updater.
// build 58 : v3.3.6. Splatoon 3 sur 11.2.0 / 11.3.0 : identifiant de build 28C4287A, livre sous ses deux formes de nom (40 et 64 hex).
// build 59 : v3.3.7. Rafraichissement de la carte au lancement, section Splatoon 3, trois hotes de jeu manquants, drapeaux MK8D fabriques localement.
// build 60 : v3.3.8. Retrait de CN / HK / TW : MK8D n'a pas ces textures de drapeau, un patch ne peut pas les creer.
// build 61 : v3.3.9. romfsInit() gardait un handle ouvert sur le .nro en cours -> l'updater ne pouvait jamais le remplacer. releaseRomfs() avant la pose.
#define NEXTENDO_BUILD 61

// Doit rester alignee avec APP_VERSION (Makefile) : l'updater compare des semver, pas NEXTENDO_BUILD.
#define NEXTENDO_VERSION_MAJOR 3
#define NEXTENDO_VERSION_MINOR 3
#define NEXTENDO_VERSION_PATCH 9

typedef struct {
    bool available;   // une version semver > NEXTENDO_VERSION_* est dispo
    int  maj, min, patch;  // version serveur (du tag semver)
    long size;        // taille attendue du .nro (verif du telechargement)
} NextendoUpdate;

typedef enum {
    NUP_OK = 0,
    NUP_NET_FAIL,     // telechargement impossible
    NUP_SIZE_FAIL,    // taille recue != taille annoncee (download corrompu)
    NUP_WRITE_FAIL    // ecriture SD impossible
} nextendo_update_result;

// argv[0] de hbmenu : le fichier qui sera remplace. Sans cet appel, repli sur sdmc:/switch/nextendo.nro.
void nextendo_update_set_self_path(const char *argv0);

NextendoUpdate nextendo_update_check(void);

// Les deux temps longs : le telechargement (17 Mo) puis la pose sur la carte. `total` vaut 0 si inconnu.
typedef enum {
    NUP_PHASE_DOWNLOAD = 0,
    NUP_PHASE_INSTALL  = 1,
} nextendo_update_phase;

typedef void (*nextendo_progress_fn)(nextendo_update_phase phase, long done, long total);

// Remplace le .nro en cours via un .new + rename. `onProgress` peut etre NULL.
nextendo_update_result nextendo_update_apply(long expectedSize, nextendo_progress_fn onProgress);

#endif // NEXTENDO_UPDATE_H

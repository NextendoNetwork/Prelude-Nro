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
//  Nextendo .nro — auto-mise a jour (via le serveur Nextendo).
//  Au lancement, on interroge /api/nro/latest ; si une version plus recente
//  existe, on affiche un bandeau. L'utilisateur peut telecharger le nouveau
//  .nro et remplacer /switch/nextendo.nro (le .nro courant tourne depuis la RAM).
// ============================================================
#ifndef NEXTENDO_UPDATE_H
#define NEXTENDO_UPDATE_H
#include <switch.h>

// Version de CE build. A INCREMENTER a chaque release (le serveur renvoie la derniere).
// build 4 : mode Nintendo coupe network_mitm (fix eShop 2137-7403) + auto-provisioning
//           du stack cert-trust complet au 1er passage en mode Nextendo (zero install manuelle).
// build 5 : release Prelude v1 (rebrand du homebrew Nextendo -> Prelude ; MAJ desormais OBLIGATOIRE).
// build 6 : fix "impossible d'ecrire sur la carte SD" a la MAJ (rename() qui echoue sur
//           certaines cartes fatfs -> secours par ecriture directe + mkdir defensif).
// build 7 : ALIGNEMENT DES VERSIONS. La version NACP affichee dans hbmenu etait restee
//           "1.0.0" (jamais bumpee) alors que le build interne etait deja 6 -> confusion.
//           Desormais APP_VERSION (Makefile) = 1.0.N ou N = NEXTENDO_BUILD. Build 7 = 1.0.7.
// build 8 : PRODINFO dynamique par mode (emuMMC). Mode NEXTENDO -> blank_prodinfo_emummc=0
//           (vrai cert device -> fix du 2123-0011 qui frappait tous les emuMMC incognito ;
//           confine par DNS-MITM donc l'identite ne fuit jamais = safe). Mode NINTENDO ->
//           blank_prodinfo_emummc=1 (identite blanche -> anti-ban si online sur le vrai Nintendo
//           en emuMMC). Le sysNAND (blank_prodinfo_sysmmc) n'est JAMAIS touche.
// build 9 : (a) FIX NAT : l'IP nncs2 codee en dur (178.105.220.158) pointait sur l'ancienne
//           machine nncs2, desormais hors service -> Pia n'obtenait jamais la 2e sonde -> NAT
//           jamais complete -> MK8/S2 tombaient en 2618-201. Remplacee par le VPS OVH
//           164.132.111.120 qui fait tourner le meme responder. (b) PURGE des fichiers laisses par les anciens builds (network_mitm
//           4200000000000666, anciens emplacements du bundle CA navigateur, IPS d'un build-id
//           qui n'est plus cible) : appliquee DANS LES DEUX MODES, donc une console qui a un
//           vieux Prelude est nettoyee des le 1er lancement. En mode NINTENDO c'est critique :
//           couper dns_mitm ne suffisait pas, un network_mitm residuel continuait d'intercepter
//           ssl/ssl:s.
// build 10 : AUDIT DE SECURITE, mesure sur une vraie console. Quatre trous, tous constates :
//           (a) TELEMETRIE. Le mode Nintendo posait enable_dns_mitm=0 + add_defaults=0, soit les
//               deux opt-out du blocage de telemetrie natif d'Atmosphere ("By default, atmosphere
//               redirects resolution requests for official telemetry servers") : la console etait
//               MOINS protegee qu'une install d'origine, alors que c'est justement le mode ou elle
//               parle aux vrais serveurs. Desormais dns_mitm reste ACTIF avec add_defaults=1 ; nos
//               hosts etant supprimes, DNS.mitm retombe sur default.txt et bloque la telemetrie,
//               l'eShop continuant de marcher. Secours dns_mitm=0 si nos hosts resistent.
//           (b) CERT-TRUST. Les patches disable_ca_verification, la CA Nextendo injectee dans le
//               navigateur et rootCA.pem survivaient a la bascule -> en mode Nintendo la verif des
//               certificats restait desactivee et notre CA de confiance, face au VRAI Nintendo.
//               Le mode Nintendo les retire maintenant (removeTreeRomfs, miroir de l'install).
//           (c) FUITE D'IP. dns_mitm_debug.log gardait notre IP a chaque requete (608 Ko / 7507
//               lignes sur la carte de test), dns_mitm_startup.log la table complete des
//               redirections, et les .txt.bak les hosts en clair — alors que le .bak n'etait
//               JAMAIS relu. Tout est purge dans les deux modes.
//           (d) PRODINFO. blank_prodinfo_emummc est sans effet sur une console sans emuMMC (CFW
//               sur la memoire interne) : aucune protection n'y est possible sans casser l'eShop,
//               raison d'etre du mode Nintendo. On ne peut pas corriger, donc on previent :
//               l'ecran de confirmation le dit franchement (splGetConfig ExosphereEmummcType).
// build 11 : build de DIAGNOSTIC. Le build 10 gelait la console au moment de basculer en mode
//           NINTENDO : l'ecran s'affichait, A ne repondait plus, et la carte restait pourtant
//           intacte (donc apply_nintendo n'a rien commit). Impossible a reproduire (Ryujinx ne
//           lit pas les .nro), donc on fait parler la console : nextendo_trace() ecrit chaque
//           etape dans sdmc:/prelude_trace.txt et COMMIT a chaque ligne — sans ce commit les
//           ecritures restent en cache et un arret force les perd, ce qui est exactement ce qui
//           rendait le blocage invisible. La derniere ligne du fichier = la derniere etape
//           franchie. Suspect principal : le build 10 a introduit le PREMIER appel a
//           splInitialize() de l'appli (nextendo_detect_boot existait mais n'avait jamais ete
//           appele) ; si spl casse la session sm en mode applet, l'entree meurt et tout colle.
// build 12 : RELEASE de l'audit (= build 10 verifie sur console + trace conservee). Le build 11
//           a prouve sur la vraie console que les 4 correctifs passent de bout en bout :
//           detect_boot=SYSMMC, hosts supprimes, purges ok, "apply_nintendo: TERMINE". Constate
//           apres bascule : plus un seul patch IPS, plus de CA Nextendo, plus de rootCA.pem, plus
//           aucun log DNS-MITM ni .bak -> l'IP du VPS n'est plus nulle part sur la carte, sauf
//           dans ce .nro (inevitable : c'est lui qui ecrit les hosts).
//           La trace est GARDEE : le gel du build 10 n'a jamais ete explique (le suspect spl a ete
//           innocente par la trace elle-meme), donc si un joueur le revit, prelude_trace.txt est
//           notre seul temoin. Elle repart de zero a chaque lancement.
// build 13 : ECRAN DE REVUE AVANT APPLICATION (revertido en build 14). Mostraba los cambios
//           de config antes de aplicar. Causaba confusión y se revirtió al confirm estático.
// build 14 : REVERT del config review screen. Vuelve al ui_draw_confirm original.
//           Agregado GitHub Actions workflow para builds automáticos.
// v2.0.0 : Major bump. Hosts actualizados con redirecciones a VPS para penne_ids, dauth, srv.nintendo.net.
// fix: NEXTENDO_BUILD 20 para evitar que el servidor (v12) bloquee el homebrew con falso "Mise à jour OBLIGATOIRE".
#define NEXTENDO_BUILD 20

typedef struct {
    bool available;   // une version > NEXTENDO_BUILD est dispo
    int  latest;      // numero de version serveur
    long size;        // taille attendue du .nro (verif du telechargement)
} NextendoUpdate;

typedef enum {
    NUP_OK = 0,
    NUP_NET_FAIL,     // telechargement impossible
    NUP_SIZE_FAIL,    // taille recue != taille annoncee (download corrompu)
    NUP_WRITE_FAIL    // ecriture SD impossible
} nextendo_update_result;

// Interroge le serveur (rapide, timeout court). socketInitializeDefault géré en interne.
NextendoUpdate nextendo_update_check(void);

// Telecharge le nouveau .nro et remplace /switch/nextendo.nro (via un .new + rename).
nextendo_update_result nextendo_update_apply(long expectedSize);

#endif // NEXTENDO_UPDATE_H

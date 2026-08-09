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
//  Nextendo .nro — installation du planning Splatoon 2 via LayeredFS.
//
//  Le planning (schedule) est TELECHARGE depuis l'API du compte :
//      GET https://nextendo.network/api/bcat/<titleId>
//  qui streame bcat_store/<titleId>.zip (coopdata/ vsdata/ fesdata/ a la racine).
//  Les entrees du zip sont extraites dans le dossier LayeredFS d'Atmosphere :
//      sdmc:/atmosphere/contents/<titleId>/romfs/DebugUnderPilot/bcat/
//  pour les regions USA (01003BC0000A0000) et EUR (0100F8F0000A2000).
//  System/GameConfigSetting.xml (config STATIQUE du jeu, absente du zip) et le
//  dossier dummy/ restent copies depuis la romfs du .nro. JPN a le meme pattern
//  d'URL (/api/bcat/01003c700009c800) mais n'a pas de config System embarque.
// ============================================================
#ifndef NEXTENDO_BCAT_H
#define NEXTENDO_BCAT_H
#include <switch.h>

typedef enum {
    NB_OK = 0,           // installe
    NB_NET_FAIL,         // telechargement impossible (raison dans le log)
    NB_NET_CONNECT,      // serveur injoignable (timeout / connexion refusee)
    NB_NET_TIMEOUT,      // reponse interrompue
    NB_NET_HTTP_ERR,     // le serveur a repondu un code HTTP different de 200/204
    NB_NO_SCHEDULE,      // 204 : rien de publie
    NB_MOUNT_FAIL,       // (obsolete) conserve pour compatibilite main.c
    NB_BAD_BUNDLE,       // bundle illisible
    NB_WRITE_FAIL        // ecriture fichier sur la SD echouee
} nextendo_bcat_result;

// Installe le planning S2 dans le dossier LayeredFS d'Atmosphere.
// socketInitializeDefault() + sslInitialize() doivent etre actifs avant l'appel.
nextendo_bcat_result nextendo_bcat_install_s2(void);

extern Result g_last_rc;

#endif // NEXTENDO_BCAT_H

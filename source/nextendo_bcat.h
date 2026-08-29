// Prelude — Nintendo Switch homebrew for the Nextendo Network.
// Copyright (C) 2026 Nextendo Network
//
// Licensed under the PolyForm Shield License 1.0.0.
//
// You may use, modify and distribute this software for any purpose EXCEPT providing a product
// that competes with Nextendo Network, or with any product Nextendo Network provides using it.
//
// See LICENSE.md for the full terms, or <https://polyformproject.org/licenses/shield/1.0.0>.
//
// Required Notice: Copyright 2026 Nextendo Network

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

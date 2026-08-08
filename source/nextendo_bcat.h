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
//  Copie les fichiers de donnees (coopdata/*.byaml, vsdata/*.byaml, fesdata/*)
//  depuis la romfs embarque du .nro vers le dossier
//  LayeredFS d'Atmosphere sur la carte SD pour les regions JPN, USA et EUR.
//  Aucune connexion reseau requise : les donnees sont livrees avec le .nro.
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
// socketInitializeDefault() doit etre actif avant l'appel.
nextendo_bcat_result nextendo_bcat_install_s2(void);

extern Result g_last_rc;

#endif // NEXTENDO_BCAT_H

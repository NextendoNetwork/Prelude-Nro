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
//  Nextendo .nro — configuration centralisée du serveur.
//  Tous les autres fichiers .c incluent ce header au lieu de
//  hardcoder des IP. Définir NEXTENDO_SERVER_HOST dans le
//  Makefile / CFLAGS pour pointer vers un autre serveur.
// ============================================================
#ifndef NEXTENDO_CONFIG_H
#define NEXTENDO_CONFIG_H

// Hôte du serveur Nextendo (HTTPS). Peut être un nom DNS ou
// une IP littérale utilisée par resolve_host().
// Définir NEXTENDO_SERVER_HOST dans les CFLAGS pour surcharger :
//   make CFLAGS="-DNEXTENDO_SERVER_HOST=\"51.178.29.194\""
#ifndef NEXTENDO_SERVER_HOST
#define NEXTENDO_SERVER_HOST "51.178.29.194"
#endif

// IPs des serveurs disponibles (défaut + alternatif via code ↑↓←→).
#define NEXTENDO_SERVER_IP_DEFAULT  "51.178.29.194"
#define NEXTENDO_SERVER_IP_ALT      "3.135.232.168"
#define NEXTENDO_SERVER_IP_NNCSD2   "164.132.111.120"

// IP courante utilisée par les hosts dns.mitm (modifiable via ↑↓←→).
extern char g_server_ip[];
#define NEXTENDO_SERVER_IP_MAX 64

// Renvoie le nom d'affichage du serveur courant.
const char *server_display_name(void);

#endif // NEXTENDO_CONFIG_H

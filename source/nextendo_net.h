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
//  Nextendo .nro — mini client HTTP (sockets libnx, pas de dependance externe).
// ============================================================
#ifndef NEXTENDO_NET_H
#define NEXTENDO_NET_H
#include <switch.h>
#include <stddef.h>
#include <stdio.h>

// GET http://ip:port/path -> renvoie le CORPS (malloc, a free par l'appelant) + longueur + code HTTP.
// HTTP/1.1 "Connection: close" -> lecture jusqu'a EOF. Retourne NULL si echec reseau/connexion.
// Necessite socketInitializeDefault() appele au prealable.
unsigned char *net_http_get(const char *ip, int port, const char *path, size_t *out_len, int *out_status);

// Idem mais STREAME le corps directement dans un FICHIER (faible memoire : pas de gros malloc,
// marche meme en mode applet ou le tas est reduit). Retourne le nombre d'octets du CORPS ecrits,
// -1 si echec reseau/connexion, -2 si l'ecriture fichier echoue. *out_status = code HTTP.
long net_http_get_to_file(const char *ip, int port, const char *path, FILE *out, int *out_status);

#endif // NEXTENDO_NET_H

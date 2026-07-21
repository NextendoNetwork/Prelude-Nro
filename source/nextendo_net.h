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

// Codes d'erreur reseau (retournes dans *out_status quand la fonction retourne NULL).
#define NET_ERR_UNKNOWN  -1   // erreur non specifiee
#define NET_ERR_CONNECT  -2   // connexion echouee (timeout / refuse / host injoignable)
#define NET_ERR_SOCKET   -3   // echec creation socket
#define NET_ERR_TIMEOUT  -4   // timeout reponse (select/recv)
#define NET_ERR_PROTO    -5   // reponse HTTP invalide (pas de status-line ou headers malformes)
#define NET_ERR_OOM      -6   // allocation memoire

// GET http://ip:port/path -> renvoie le CORPS (malloc, a free par l'appelant) + longueur + code HTTP.
// HTTP/1.1 "Connection: close" -> lecture jusqu'a EOF. Retourne NULL si echec reseau/connexion.
// *out_status : >0 = code HTTP (200, 204...), <0 = code d'erreur NET_ERR_*.
// Necessite socketInitializeDefault() appele au prealable.
unsigned char *net_http_get(const char *ip, int port, const char *path, size_t *out_len, int *out_status);

// Idem mais STREAME le corps directement dans un FICHIER (faible memoire : pas de gros malloc,
// marche meme en mode applet ou le tas est reduit). Retourne le nombre d'octets du CORPS ecrits,
// -1 si echec reseau/connexion, -2 si l'ecriture fichier echoue. *out_status = code HTTP.
long net_http_get_to_file(const char *ip, int port, const char *path, FILE *out, int *out_status);

// HTTPS GET vers host:443/path. Necessite socketInitializeDefault() + sslInitialize() avant.
// Retourne le body (malloc) + longueur + code HTTP. NULL si echec.
unsigned char *net_https_get(const char *host, const char *path,
                              size_t *out_len, int *out_status);

// HTTPS GET streaming fichier. Necessite socketInitializeDefault() + sslInitialize().
// Retourne le nombre d'octets ecrits, -1 si reseau, -2 si ecriture.
long net_https_get_to_file(const char *host, const char *path,
                            FILE *out, int *out_status);

#endif // NEXTENDO_NET_H

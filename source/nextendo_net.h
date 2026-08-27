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

// Mini client HTTP/HTTPS sur sockets libnx, sans dependance externe.
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

// Corps a free() par l'appelant, NULL si echec. *out_status : >0 = code HTTP, <0 = NET_ERR_*.
unsigned char *net_http_get(const char *ip, int port, const char *path, size_t *out_len, int *out_status);

// Idem en streaming fichier (pas de gros malloc : tient en mode applet). -1 = reseau, -2 = ecriture.
long net_http_get_to_file(const char *ip, int port, const char *path, FILE *out, int *out_status);

// HTTPS vers host:443. Necessite socketInitializeDefault() + sslInitialize() avant.
unsigned char *net_https_get(const char *host, const char *path,
                              size_t *out_len, int *out_status);

// Appele depuis la boucle de lecture : a garder court. `total` vaut 0 si le serveur ne l'annonce pas.
typedef void (*net_progress_fn)(long received, long total);

// `onProgress` peut etre NULL et reste muet pendant une redirection : compter son corps ferait reculer la barre.
long net_https_get_to_file(const char *host, const char *path,
                            FILE *out, int *out_status,
                            net_progress_fn onProgress);

// Dernier Result libnx d'un appel SSL ayant echoue (diagnostic).
extern Result g_net_ssl_rc;

#endif // NEXTENDO_NET_H

// ============================================================
//  Nextendo .nro — mini client HTTP (sockets libnx, pas de dependance externe).
// ============================================================
#ifndef NEXTENDO_NET_H
#define NEXTENDO_NET_H
#include <switch.h>
#include <stddef.h>

// GET http://ip:port/path -> renvoie le CORPS (malloc, a free par l'appelant) + longueur + code HTTP.
// HTTP/1.1 "Connection: close" -> lecture jusqu'a EOF. Retourne NULL si echec reseau/connexion.
// Necessite socketInitializeDefault() appele au prealable.
unsigned char *net_http_get(const char *ip, int port, const char *path, size_t *out_len, int *out_status);

#endif // NEXTENDO_NET_H

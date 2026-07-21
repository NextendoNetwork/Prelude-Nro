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
//  Nextendo .nro — mini client HTTP(S) (sockets BSD libnx).
//  Pour HTTP simple : connexion directe au VPS BCAT.
//  Pour HTTPS : utilise le service SSL natif de la Switch.
// ============================================================
#include <switch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "nextendo_net.h"

// Taille maximale d'une reponse HTTP en memoire (4 MiB).
// Au-dela, on considere que le serveur envoie trop de donnees (DoS ou erreur).
#define MAX_RESPONSE_SIZE (4 * 1024 * 1024)

// ------------------------------------------------------------------
//  Utilitaires partages (connexion TCP + timeout).
// ------------------------------------------------------------------

// Connecte un socket TCP a ip:port avec timeout non-bloquant (6s).
// Renvoie le fd, ou -1 si echec. Le socket est en mode bloquant au retour.
static int tcp_connect(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct timeval tv = { .tv_sec = 20, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u16)port);
    sa.sin_addr.s_addr = inet_addr(ip);

    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc < 0 && errno == EINPROGRESS) {
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(fd, &wf);
        struct timeval ct = { .tv_sec = 6, .tv_usec = 0 };
        if (select(fd + 1, NULL, &wf, NULL, &ct) <= 0) { close(fd); return -1; }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr != 0) { close(fd); return -1; }
    } else if (rc < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, fl);
    return fd;
}

// Envoie une requete HTTP brute sur un fd (socket ou SSL).
// Retourne 0 si tous les octets envoyes, -1 si erreur.
static int send_request(int fd, const char *method, const char *path,
                        const char *host, int port) {
    char req[768];
    int rl = snprintf(req, sizeof(req),
                      "%s %s HTTP/1.1\r\nHost: %s:%d\r\n"
                      "User-Agent: NextendoHomebrew\r\n"
                      "Accept: */*\r\nConnection: close\r\n\r\n",
                      method, path, host, port);
    int sent = 0;
    while (sent < rl) {
        ssize_t n = send(fd, req + sent, rl - sent, 0);
        if (n <= 0) return -1;
        sent += (int)n;
    }
    return 0;
}

// Parse le code HTTP depuis le debut d'une reponse (status-line).
// Renvoie le code (ex: 200) ou NET_ERR_PROTO.
static int parse_status(const unsigned char *buf, size_t len) {
    for (size_t i = 0; i + 3 < len && i < 64; i++) {
        if (buf[i] == ' ') {
            return (buf[i + 1] - '0') * 100
                 + (buf[i + 2] - '0') * 10
                 + (buf[i + 3] - '0');
        }
    }
    return NET_ERR_PROTO;
}

// Trouve le debut du corps (apres \r\n\r\n).
// Renvoie l'offset, ou 0 si pas trouve (met *found=false).
static size_t find_body(const unsigned char *buf, size_t len, bool *found) {
    *found = false;
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n'
            && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            *found = true;
            return i + 4;
        }
    }
    return 0;
}

// ------------------------------------------------------------------
//  HTTP simple (nextendo_net.h - deja existant).
// ------------------------------------------------------------------

unsigned char *net_http_get(const char *ip, int port, const char *path, size_t *out_len, int *out_status) {
    *out_len = 0;
    *out_status = NET_ERR_UNKNOWN;

    int fd = tcp_connect(ip, port);
    if (fd < 0) { *out_status = NET_ERR_CONNECT; return NULL; }

    if (send_request(fd, "GET", path, ip, port) < 0) { *out_status = NET_ERR_CONNECT; close(fd); return NULL; }

    size_t cap = 1 << 16, len = 0;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { close(fd); *out_status = NET_ERR_OOM; return NULL; }
    for (;;) {
        if (len + 8192 > cap) {
            if (cap >= MAX_RESPONSE_SIZE) { free(buf); close(fd); *out_status = NET_ERR_PROTO; return NULL; }
            cap *= 2;
            if (cap > MAX_RESPONSE_SIZE) cap = MAX_RESPONSE_SIZE;
            unsigned char *nb = (unsigned char *)realloc(buf, cap);
            if (!nb) { free(buf); close(fd); *out_status = NET_ERR_OOM; return NULL; }
            buf = nb;
        }
        ssize_t n = recv(fd, buf + len, cap - len, 0);
        if (n < 0) { *out_status = NET_ERR_TIMEOUT; free(buf); close(fd); return NULL; }
        if (n == 0) break;
        len += (size_t)n;
    }
    close(fd);

    int status = parse_status(buf, len);
    *out_status = status;

    bool found = false;
    size_t bodyOff = find_body(buf, len, &found);
    if (!found) { free(buf); return NULL; }

    size_t blen = len - bodyOff;
    unsigned char *body = (unsigned char *)malloc(blen ? blen : 1);
    if (!body) { free(buf); return NULL; }
    memcpy(body, buf + bodyOff, blen);
    free(buf);

    *out_len = blen;
    return body;
}

// Streame la reponse depuis un fd (socket ou SSL) vers un fichier,
// parse les en-tetes en cours de route. Retourne le nombre d'octets du corps
// ecrits, -1 si reseau, -2 si ecriture fichier.
typedef ssize_t (*net_recv_fn)(int fd, void *buf, size_t count);

static long stream_to_file(int fd, FILE *out, int *out_status, net_recv_fn recv_fn) {
    unsigned char rbuf[32768];
    unsigned char hdr[8192];
    size_t hlen = 0;
    int status = 0;
    int inBody = 0;
    long bodyBytes = 0;
    for (;;) {
        ssize_t n = recv_fn(fd, rbuf, sizeof(rbuf));
        if (n <= 0) break;
        if (inBody) {
            if (fwrite(rbuf, 1, (size_t)n, out) != (size_t)n) { *out_status = status; return -2; }
            bodyBytes += n;
            continue;
        }
        size_t i = 0;
        while (i < (size_t)n && hlen < sizeof(hdr)) {
            hdr[hlen++] = rbuf[i++];
            if (hlen >= 4 && hdr[hlen-4]=='\r' && hdr[hlen-3]=='\n' && hdr[hlen-2]=='\r' && hdr[hlen-1]=='\n')
                break;
        }
        if (hlen >= 4 && hdr[hlen-4]=='\r' && hdr[hlen-3]=='\n' && hdr[hlen-2]=='\r' && hdr[hlen-1]=='\n') {
            status = parse_status(hdr, hlen);
            inBody = 1;
            if ((size_t)n > i) {
                size_t rem = (size_t)n - i;
                if (fwrite(rbuf + i, 1, rem, out) != rem) { *out_status = status; return -2; }
                bodyBytes += (long)rem;
            }
        }
    }
    *out_status = status;
    if (!inBody) return -1;
    return bodyBytes;
}

static ssize_t plain_recv(int fd, void *buf, size_t count) {
    return recv(fd, buf, count, 0);
}

long net_http_get_to_file(const char *ip, int port, const char *path, FILE *out, int *out_status) {
    *out_status = 0;
    int fd = tcp_connect(ip, port);
    if (fd < 0) return -1;
    if (send_request(fd, "GET", path, ip, port) < 0) { close(fd); return -1; }

    long res = stream_to_file(fd, out, out_status, plain_recv);
    close(fd);
    return res;
}

// ------------------------------------------------------------------
//  HTTPS via le service SSL natif de la Switch.
//  Utilise switch/services/ssl.h (inclus via <switch.h>).
// ------------------------------------------------------------------

// Resout un nom d'hote en IP (string). Renvoie le buffer statique ou NULL.
static const char *resolve_host(const char *host) {
    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr_list[0]) return NULL;
    return inet_ntoa(*(struct in_addr *)he->h_addr_list[0]);
}

// Effectue une requete HTTPS GET. Retourne le corps (malloc), la longueur
// et le code HTTP. Necessite socketInitializeDefault() + sslInitialize() avant.
// L'appelant doit liberer le pointeur retourne avec free().
unsigned char *net_https_get(const char *host, const char *path,
                              size_t *out_len, int *out_status) {
    *out_len = 0;
    *out_status = NET_ERR_UNKNOWN;

    // Resoudre le nom d'hote.
    const char *ip = resolve_host(host);
    if (!ip) { *out_status = NET_ERR_CONNECT; return NULL; }

    // Connexion TCP.
    int fd = tcp_connect(ip, 443);
    if (fd < 0) { *out_status = NET_ERR_CONNECT; return NULL; }

    // Initialiser SSL.
    SslContext sslCtx;
    SslConnection sslConn;
    Result rc = sslCreateContext(&sslCtx, SslVersion_Auto);
    if (R_FAILED(rc)) { close(fd); return NULL; }

    rc = sslContextCreateConnection(&sslCtx, &sslConn);
    if (R_FAILED(rc)) { sslContextClose(&sslCtx); close(fd); return NULL; }

    int out_fd = -1;
    rc = sslConnectionSetSocketDescriptor(&sslConn, fd, &out_fd);
    if (R_FAILED(rc)) { sslConnectionClose(&sslConn); sslContextClose(&sslCtx); close(fd); return NULL; }

    rc = sslConnectionSetHostName(&sslConn, host, strlen(host));
    if (R_FAILED(rc)) { sslConnectionClose(&sslConn); sslContextClose(&sslCtx); if (out_fd >= 0) close(out_fd); return NULL; }

    rc = sslConnectionDoHandshake(&sslConn, NULL, NULL, NULL, 0);
    if (R_FAILED(rc)) { sslConnectionClose(&sslConn); sslContextClose(&sslCtx); if (out_fd >= 0) close(out_fd); return NULL; }

    // Envoyer la requete via SSL.
    char req[768];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\nHost: %s\r\n"
                      "User-Agent: NextendoHomebrew\r\n"
                      "Accept: */*\r\nConnection: close\r\n\r\n",
                      path, host);
    u32 written = 0;
    rc = sslConnectionWrite(&sslConn, req, rl, &written);
    if (R_FAILED(rc) || written != (u32)rl) {
        sslConnectionClose(&sslConn); sslContextClose(&sslCtx);
        if (out_fd >= 0) close(out_fd);
        *out_status = NET_ERR_CONNECT; return NULL;
    }

    // Lire la reponse.
    size_t cap = 1 << 16, len = 0;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) {
        sslConnectionClose(&sslConn); sslContextClose(&sslCtx);
        if (out_fd >= 0) close(out_fd);
        *out_status = NET_ERR_OOM; return NULL;
    }
    for (;;) {
        if (len + 8192 > cap) {
            if (cap >= MAX_RESPONSE_SIZE) { free(buf); sslConnectionClose(&sslConn); sslContextClose(&sslCtx); if (out_fd >= 0) close(out_fd); *out_status = NET_ERR_PROTO; return NULL; }
            cap *= 2;
            if (cap > MAX_RESPONSE_SIZE) cap = MAX_RESPONSE_SIZE;
            unsigned char *nb = (unsigned char *)realloc(buf, cap);
            if (!nb) { free(buf); sslConnectionClose(&sslConn); sslContextClose(&sslCtx); if (out_fd >= 0) close(out_fd); *out_status = NET_ERR_OOM; return NULL; }
            buf = nb;
        }
        u32 read = 0;
        rc = sslConnectionRead(&sslConn, buf + len, (u32)(cap - len), &read);
        if (R_FAILED(rc) || read == 0) break;
        len += read;
    }

    sslConnectionClose(&sslConn);
    sslContextClose(&sslCtx);
    if (out_fd >= 0) close(out_fd);

    int status = parse_status(buf, len);
    *out_status = status;

    bool found = false;
    size_t bodyOff = find_body(buf, len, &found);
    if (!found) { free(buf); *out_status = NET_ERR_PROTO; return NULL; }

    size_t blen = len - bodyOff;
    unsigned char *body = (unsigned char *)malloc(blen ? blen : 1);
    if (!body) { free(buf); return NULL; }
    memcpy(body, buf + bodyOff, blen);
    free(buf);

    *out_len = blen;
    return body;
}

// Version streaming fichier de HTTPS GET.
// Necessite socketInitializeDefault() + sslInitialize() avant.
// Retourne le nombre d'octets du corps ecrits, -1 si reseau, -2 si ecriture.
long net_https_get_to_file(const char *host, const char *path,
                            FILE *out, int *out_status) {
    *out_status = 0;

    const char *ip = resolve_host(host);
    if (!ip) return -1;

    int fd = tcp_connect(ip, 443);
    if (fd < 0) return -1;

    SslContext sslCtx;
    SslConnection sslConn;
    Result rc = sslCreateContext(&sslCtx, SslVersion_Auto);
    if (R_FAILED(rc)) { close(fd); return -1; }
    rc = sslContextCreateConnection(&sslCtx, &sslConn);
    if (R_FAILED(rc)) { sslContextClose(&sslCtx); close(fd); return -1; }

    int out_fd = -1;
    rc = sslConnectionSetSocketDescriptor(&sslConn, fd, &out_fd);
    if (R_FAILED(rc)) { sslConnectionClose(&sslConn); sslContextClose(&sslCtx); close(fd); return -1; }

    rc = sslConnectionSetHostName(&sslConn, host, strlen(host));
    if (R_FAILED(rc)) { sslConnectionClose(&sslConn); sslContextClose(&sslCtx); if (out_fd >= 0) close(out_fd); return -1; }

    rc = sslConnectionDoHandshake(&sslConn, NULL, NULL, NULL, 0);
    if (R_FAILED(rc)) { sslConnectionClose(&sslConn); sslContextClose(&sslCtx); if (out_fd >= 0) close(out_fd); return -1; }

    char req[768];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\nHost: %s\r\n"
                      "User-Agent: NextendoHomebrew\r\n"
                      "Accept: */*\r\nConnection: close\r\n\r\n",
                      path, host);
    u32 written = 0;
    rc = sslConnectionWrite(&sslConn, req, rl, &written);
    if (R_FAILED(rc) || written != (u32)rl) {
        sslConnectionClose(&sslConn); sslContextClose(&sslCtx);
        if (out_fd >= 0) close(out_fd);
        return -1;
    }

    // Streaming: lire via SSL, ecrire dans le fichier.
    unsigned char rbuf[32768];
    unsigned char hdr[8192];
    size_t hlen = 0;
    int status = 0;
    int inBody = 0;
    long bodyBytes = 0;
    for (;;) {
        u32 read = 0;
        rc = sslConnectionRead(&sslConn, rbuf, sizeof(rbuf), &read);
        if (R_FAILED(rc)) break;
        if (read == 0) break;
        if (inBody) {
            if (fwrite(rbuf, 1, read, out) != read) {
                sslConnectionClose(&sslConn); sslContextClose(&sslCtx);
                if (out_fd >= 0) close(out_fd);
                *out_status = status; return -2;
            }
            bodyBytes += read;
            continue;
        }
        size_t i = 0;
        while (i < read && hlen < sizeof(hdr)) {
            hdr[hlen++] = rbuf[i++];
            if (hlen >= 4 && hdr[hlen-4]=='\r' && hdr[hlen-3]=='\n' && hdr[hlen-2]=='\r' && hdr[hlen-1]=='\n')
                break;
        }
        if (hlen >= 4 && hdr[hlen-4]=='\r' && hdr[hlen-3]=='\n' && hdr[hlen-2]=='\r' && hdr[hlen-1]=='\n') {
            status = parse_status(hdr, hlen);
            inBody = 1;
            if (read > i) {
                size_t rem = read - i;
                if (fwrite(rbuf + i, 1, rem, out) != rem) {
                    sslConnectionClose(&sslConn); sslContextClose(&sslCtx);
                    if (out_fd >= 0) close(out_fd);
                    *out_status = status; return -2;
                }
                bodyBytes += (long)rem;
            }
        }
    }
    sslConnectionClose(&sslConn);
    sslContextClose(&sslCtx);
    if (out_fd >= 0) close(out_fd);
    *out_status = status;
    if (!inBody) return -1;
    return bodyBytes;
}

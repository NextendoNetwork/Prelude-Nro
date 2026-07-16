// ============================================================
//  Nextendo .nro — mini client HTTP en clair (sockets BSD libnx).
//  Suffisant pour telecharger le bundle BCAT depuis http://203.0.113.10:8095.
//  Pas de TLS (l'API Nextendo est en HTTP simple) -> aucune lib crypto necessaire.
// ============================================================
#include <switch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "nextendo_net.h"

unsigned char *net_http_get(const char *ip, int port, const char *path, size_t *out_len, int *out_status) {
    *out_len = 0;
    *out_status = 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    // Timeouts recv/send : evite un blocage si le serveur cesse de repondre.
    struct timeval tv = { .tv_sec = 20, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((u16)port);
    sa.sin_addr.s_addr = inet_addr(ip);

    // Connexion avec timeout (non-bloquant + select 6s) : pas de freeze au demarrage si hors-ligne.
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc < 0 && errno == EINPROGRESS) {
        fd_set wf;
        FD_ZERO(&wf);
        FD_SET(fd, &wf);
        struct timeval ct = { .tv_sec = 6, .tv_usec = 0 };
        if (select(fd + 1, NULL, &wf, NULL, &ct) <= 0) { close(fd); return NULL; }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr != 0) { close(fd); return NULL; }
    } else if (rc < 0) {
        close(fd);
        return NULL;
    }
    fcntl(fd, F_SETFL, fl);   // retour en mode bloquant

    char req[512];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: NextendoHomebrew\r\n"
                      "Accept: */*\r\nConnection: close\r\n\r\n",
                      path, ip, port);
    int sent = 0;
    while (sent < rl) {
        ssize_t n = send(fd, req + sent, rl - sent, 0);
        if (n <= 0) { close(fd); return NULL; }
        sent += (int)n;
    }

    // Lit toute la reponse jusqu'a EOF (le serveur ferme apres le corps).
    size_t cap = 1 << 16, len = 0;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) { close(fd); return NULL; }
    for (;;) {
        if (len + 8192 > cap) {
            cap *= 2;
            unsigned char *nb = (unsigned char *)realloc(buf, cap);
            if (!nb) { free(buf); close(fd); return NULL; }
            buf = nb;
        }
        ssize_t n = recv(fd, buf + len, cap - len, 0);
        if (n <= 0) break;
        len += (size_t)n;
    }
    close(fd);

    // Code HTTP = 3 chiffres apres le 1er espace de la status-line.
    int status = 0;
    for (size_t i = 0; i + 3 < len && i < 64; i++) {
        if (buf[i] == ' ') {
            status = (buf[i + 1] - '0') * 100 + (buf[i + 2] - '0') * 10 + (buf[i + 3] - '0');
            break;
        }
    }
    *out_status = status;

    // Separateur headers/body = "\r\n\r\n".
    size_t bodyOff = 0;
    bool found = false;
    for (size_t i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            bodyOff = i + 4;
            found = true;
            break;
        }
    }
    if (!found) { free(buf); return NULL; }

    size_t blen = len - bodyOff;
    unsigned char *body = (unsigned char *)malloc(blen ? blen : 1);
    if (!body) { free(buf); return NULL; }
    memcpy(body, buf + bodyOff, blen);
    free(buf);

    *out_len = blen;
    return body;
}

// Ouvre une connexion + envoie la requete GET. Renvoie le fd (>=0) ou -1. (factorise pour le stream.)
static int net_open_get(const char *ip, int port, const char *path) {
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
        fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf);
        struct timeval ct = { .tv_sec = 6, .tv_usec = 0 };
        if (select(fd + 1, NULL, &wf, NULL, &ct) <= 0) { close(fd); return -1; }
        int soerr = 0; socklen_t sl = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr != 0) { close(fd); return -1; }
    } else if (rc < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, fl);
    char req[512];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: NextendoHomebrew\r\n"
                      "Accept: */*\r\nConnection: close\r\n\r\n", path, ip, port);
    int sent = 0;
    while (sent < rl) {
        ssize_t n = send(fd, req + sent, rl - sent, 0);
        if (n <= 0) { close(fd); return -1; }
        sent += (int)n;
    }
    return fd;
}

long net_http_get_to_file(const char *ip, int port, const char *path, FILE *out, int *out_status) {
    *out_status = 0;
    int fd = net_open_get(ip, port, path);
    if (fd < 0) return -1;

    unsigned char rbuf[32768];
    unsigned char hdr[8192];
    size_t hlen = 0;
    int status = 0;
    int inBody = 0;
    long bodyBytes = 0;
    for (;;) {
        ssize_t n = recv(fd, rbuf, sizeof(rbuf), 0);
        if (n <= 0) break;
        if (inBody) {
            if (fwrite(rbuf, 1, (size_t)n, out) != (size_t)n) { close(fd); *out_status = status; return -2; }
            bodyBytes += n;
            continue;
        }
        // Toujours dans les en-tetes : on accumule dans hdr jusqu'a "\r\n\r\n".
        size_t i = 0;
        while (i < (size_t)n && hlen < sizeof(hdr)) {
            hdr[hlen++] = rbuf[i++];
            if (hlen >= 4 && hdr[hlen-4]=='\r' && hdr[hlen-3]=='\n' && hdr[hlen-2]=='\r' && hdr[hlen-1]=='\n')
                break;
        }
        if (hlen >= 4 && hdr[hlen-4]=='\r' && hdr[hlen-3]=='\n' && hdr[hlen-2]=='\r' && hdr[hlen-1]=='\n') {
            for (size_t k = 0; k + 3 < hlen && k < 64; k++) {
                if (hdr[k] == ' ') { status = (hdr[k+1]-'0')*100 + (hdr[k+2]-'0')*10 + (hdr[k+3]-'0'); break; }
            }
            inBody = 1;
            // Les octets restants de rbuf (a partir de i) sont le DEBUT du corps -> on les ecrit.
            if ((size_t)n > i) {
                size_t rem = (size_t)n - i;
                if (fwrite(rbuf + i, 1, rem, out) != rem) { close(fd); *out_status = status; return -2; }
                bodyBytes += (long)rem;
            }
        }
    }
    close(fd);
    *out_status = status;
    if (!inBody) return -1;
    return bodyBytes;
}

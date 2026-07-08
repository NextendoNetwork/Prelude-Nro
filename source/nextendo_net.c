// ============================================================
//  Nextendo .nro — mini client HTTP en clair (sockets BSD libnx).
//  Suffisant pour telecharger le bundle BCAT depuis http://51.178.29.194:8095.
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

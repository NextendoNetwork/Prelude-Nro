#include "nextendo_time.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NTP_TIMESTAMP_DELTA 2208988800ULL
#define NTP_ERA_SECONDS     4294967296ULL
#define NTP_MIN_UNIX_TIME   1577836800ULL // 2020-01-01 00:00:00 UTC
#define NTP_MAX_UNIX_TIME   2524608000ULL // 2050-01-01 00:00:00 UTC

extern TimeServiceType __nx_time_service_type;

typedef struct {
    uint8_t li_vn_mode;
    uint8_t stratum;
    uint8_t poll;
    uint8_t precision;
    uint32_t rootDelay;
    uint32_t rootDispersion;
    uint32_t refId;
    uint32_t refTm_s;
    uint32_t refTm_f;
    uint32_t origTm_s;
    uint32_t origTm_f;
    uint32_t rxTm_s;
    uint32_t rxTm_f;
    uint32_t txTm_s;
    uint32_t txTm_f;
} ntp_packet;

static bool ntp_to_unix_windowed(uint32_t ntp_sec_32, uint64_t *out_unix_time) {
    uint64_t candidates[2] = {
        (uint64_t)ntp_sec_32,
        (uint64_t)ntp_sec_32 + NTP_ERA_SECONDS,
    };

    for (int i = 0; i < 2; i++) {
        if (candidates[i] < NTP_TIMESTAMP_DELTA) continue;
        uint64_t unix_time = candidates[i] - NTP_TIMESTAMP_DELTA;
        if (unix_time >= NTP_MIN_UNIX_TIME && unix_time <= NTP_MAX_UNIX_TIME) {
            *out_unix_time = unix_time;
            return true;
        }
    }
    return false;
}

Result nextendo_time_sync(void) {
    Result rs = socketInitializeDefault();
    if (R_FAILED(rs)) return rs;

    int sockfd = -1;
    Result rc = 0;
    const char *server_name = "time.cloudflare.com";
    struct hostent *server = gethostbyname(server_name);
    if (server == NULL || server->h_addr_list == NULL || server->h_addr_list[0] == NULL ||
        server->h_addrtype != AF_INET || server->h_length != (int)sizeof(struct in_addr)) {
        rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
        goto cleanup_socket;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], sizeof(struct in_addr));
    serv_addr.sin_port = htons(123);

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto cleanup_socket;
    }

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto cleanup_socket;
    }

    ntp_packet req_packet;
    memset(&req_packet, 0, sizeof(req_packet));
    req_packet.li_vn_mode = (0 << 6) | (4 << 3) | 3;
    req_packet.txTm_s = htonl((uint32_t)(NTP_TIMESTAMP_DELTA + (uint64_t)time(NULL)));
    req_packet.txTm_f = htonl(0x4E585444); // "NXTD"

    if (send(sockfd, (char *)&req_packet, sizeof(req_packet), 0) < 0) {
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto cleanup_socket;
    }

    ntp_packet resp_packet;
    memset(&resp_packet, 0, sizeof(resp_packet));
    ssize_t n = recv(sockfd, (char *)&resp_packet, sizeof(resp_packet), 0);
    if (n < (ssize_t)sizeof(resp_packet)) {
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto cleanup_socket;
    }

    uint8_t mode = resp_packet.li_vn_mode & 0x07;
    if (mode != 4) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    uint8_t li = (resp_packet.li_vn_mode >> 6) & 0x03;
    if (li == 3) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    if (resp_packet.stratum < 1 || resp_packet.stratum > 15) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    if (resp_packet.origTm_s != req_packet.txTm_s || resp_packet.origTm_f != req_packet.txTm_f) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    uint64_t unix_time = 0;
    if (!ntp_to_unix_windowed(ntohl(resp_packet.txTm_s), &unix_time)) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    close(sockfd);
    sockfd = -1;
    socketExit();

    timeExit();
    __nx_time_service_type = TimeServiceType_System;
    rc = timeInitialize();
    if (R_FAILED(rc)) {
        __nx_time_service_type = TimeServiceType_User;
        timeInitialize();
        return rc;
    }

    Result rc_net = timeSetCurrentTime(TimeType_NetworkSystemClock, unix_time);
    Result rc_user = timeSetCurrentTime(TimeType_UserSystemClock, unix_time);

    timeExit();
    __nx_time_service_type = TimeServiceType_User;
    timeInitialize();

    if (R_FAILED(rc_net)) return rc_net;
    if (R_FAILED(rc_user)) return rc_user;
    return 0;

cleanup_socket:
    if (sockfd >= 0) close(sockfd);
    socketExit();
    return rc;
}

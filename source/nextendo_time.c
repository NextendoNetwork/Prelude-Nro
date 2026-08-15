#include "nextendo_time.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NTP_TIMESTAMP_DELTA 2208988800ULL
#define NTP_MIN_TIMESTAMP   3786825600ULL // 2020-01-01 00:00:00 UTC in NTP seconds
#define NTP_MIN_UNIX_TIME   1577836800ULL // 2020-01-01 00:00:00 UTC in Unix seconds
#define NTP_MAX_UNIX_TIME   2524608000ULL // 2050-01-01 00:00:00 UTC in Unix seconds

extern TimeServiceType __nx_time_service_type;

typedef struct {
    uint8_t li_vn_mode;      // LI (2 bits), VN (3 bits), Mode (3 bits)
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

Result nextendo_time_sync(void) {
    Result rs = socketInitializeDefault();
    if (R_FAILED(rs)) {
        return rs;
    }

    int sockfd = -1;
    const char *server_name = "time.cloudflare.com";
    const uint16_t port = 123;
    struct hostent *server = NULL;
    struct sockaddr_in serv_addr;
    ntp_packet req_packet;
    ntp_packet resp_packet;
    Result rc = 0;

    // Resolve NTP server hostname with validation
    server = gethostbyname(server_name);
    if (server == NULL || server->h_addr_list == NULL || server->h_addr_list[0] == NULL ||
        server->h_addrtype != AF_INET || server->h_length != (int)sizeof(struct in_addr)) {
        rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
        goto cleanup_socket;
    }

    memset(&serv_addr, 0, sizeof(struct sockaddr_in));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], sizeof(struct in_addr));
    serv_addr.sin_port = htons(port);

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto cleanup_socket;
    }

    // Set socket receive and send timeout to 5 seconds
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto cleanup_socket;
    }

    // Build client NTP request (LI=0, VN=4, Mode=3)
    memset(&req_packet, 0, sizeof(ntp_packet));
    req_packet.li_vn_mode = (0 << 6) | (4 << 3) | 3;
    uint32_t req_s = (uint32_t)(NTP_TIMESTAMP_DELTA + (uint64_t)time(NULL));
    uint32_t req_f = 0x4E585444; // Nonce "NXTD"
    req_packet.txTm_s = htonl(req_s);
    req_packet.txTm_f = htonl(req_f);

    if (send(sockfd, (char *)&req_packet, sizeof(ntp_packet), 0) < 0) {
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto cleanup_socket;
    }

    memset(&resp_packet, 0, sizeof(ntp_packet));
    ssize_t n = recv(sockfd, (char *)&resp_packet, sizeof(ntp_packet), 0);
    if (n < (ssize_t)sizeof(ntp_packet)) {
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto cleanup_socket;
    }

    // 1. Reply mode must be 4 (Server)
    uint8_t mode = resp_packet.li_vn_mode & 0x07;
    if (mode != 4) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    // 2. LI (Leap Indicator) must not be 3 (Alarm condition / unsynchronized)
    uint8_t li = (resp_packet.li_vn_mode >> 6) & 0x03;
    if (li == 3) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    // 3. Stratum must be between 1 and 15 (0 is Kiss-o'-Death, 16 is unsynchronized)
    if (resp_packet.stratum < 1 || resp_packet.stratum > 15) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    // 4. origTm in the response must match our sent txTm
    if (resp_packet.origTm_s != req_packet.txTm_s || resp_packet.origTm_f != req_packet.txTm_f) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    // 5. Plausibility sanity check on timestamp
    uint32_t tx_sec = ntohl(resp_packet.txTm_s);
    if (tx_sec < NTP_MIN_TIMESTAMP) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    uint64_t unix_time = (uint64_t)tx_sec - NTP_TIMESTAMP_DELTA;
    if (unix_time < NTP_MIN_UNIX_TIME || unix_time > NTP_MAX_UNIX_TIME) {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto cleanup_socket;
    }

    // Network operation successful; close socket before IPC operations
    close(sockfd);
    sockfd = -1;
    socketExit();

    // Dynamically switch time service to System (time:s) to update system clocks
    timeExit();
    __nx_time_service_type = TimeServiceType_System;
    rc = timeInitialize();
    if (R_FAILED(rc)) {
        // Restore user time service session before returning
        __nx_time_service_type = TimeServiceType_User;
        timeInitialize();
        return rc;
    }

    Result rc_net = timeSetCurrentTime(TimeType_NetworkSystemClock, unix_time);
    Result rc_user = timeSetCurrentTime(TimeType_UserSystemClock, unix_time);

    // Always restore the default user time service session
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

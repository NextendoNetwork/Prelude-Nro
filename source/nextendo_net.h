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

// Minimal HTTP/HTTPS client on libnx sockets, no external dependency.
#ifndef NEXTENDO_NET_H
#define NEXTENDO_NET_H
#include <switch.h>
#include <stddef.h>
#include <stdio.h>

// Network error codes (returned in *out_status when the function returns NULL).
#define NET_ERR_UNKNOWN  -1   // unspecified failure
#define NET_ERR_CONNECT  -2   // connect failed (timeout / refused / host unreachable)
#define NET_ERR_SOCKET   -3   // socket creation failed
#define NET_ERR_TIMEOUT  -4   // response timed out (select/recv)
#define NET_ERR_PROTO    -5   // malformed HTTP response (no status line or bad headers)
#define NET_ERR_OOM      -6   // out of memory

// Body must be free()d by the caller, NULL on failure. *out_status: >0 = HTTP code, <0 = NET_ERR_*.
unsigned char *net_http_get(const char *ip, int port, const char *path, size_t *out_len, int *out_status);

// Same, streamed to a file (no large malloc, so it survives applet mode). -1 = network, -2 = write.
long net_http_get_to_file(const char *ip, int port, const char *path, FILE *out, int *out_status);

// HTTPS to host:443. Requires socketInitializeDefault() + sslInitialize() beforehand.
unsigned char *net_https_get(const char *host, const char *path,
                              size_t *out_len, int *out_status);

// Called from the read loop, so keep it short. `total` is 0 when the server does not announce it.
typedef void (*net_progress_fn)(long received, long total);

// `onProgress` may be NULL, and stays silent during a redirect: counting its body would rewind the bar.
long net_https_get_to_file(const char *host, const char *path,
                            FILE *out, int *out_status,
                            net_progress_fn onProgress);

// Last libnx Result from a failed SSL call (diagnostics).
extern Result g_net_ssl_rc;

#endif // NEXTENDO_NET_H

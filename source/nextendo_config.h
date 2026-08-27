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

// Central server configuration: no other .c file hardcodes an IP.
#ifndef NEXTENDO_CONFIG_H
#define NEXTENDO_CONFIG_H

// DNS name or literal IP. Override: make CFLAGS="-DNEXTENDO_SERVER_HOST=\"1.2.3.4\""
#ifndef NEXTENDO_SERVER_HOST
#define NEXTENDO_SERVER_HOST "51.178.29.194"
#endif

// Available servers (default + alternate, toggled by the ↑↓←→ code).
#define NEXTENDO_SERVER_IP_DEFAULT  "51.178.29.194"
#define NEXTENDO_SERVER_IP_ALT      "3.135.232.168"
#define NEXTENDO_SERVER_IP_NNCSD2   "164.132.111.120"

// IP currently written into the dns.mitm hosts (switchable with ↑↓←→).
extern char g_server_ip[];
#define NEXTENDO_SERVER_IP_MAX 64

const char *server_display_name(void);

#endif // NEXTENDO_CONFIG_H

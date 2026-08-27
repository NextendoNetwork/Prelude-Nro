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

// Contenu des hosts Atmosphere DNS-MITM (sysmmc.txt + emummc.txt), genere par l'app.
// Regle Atmosphere : la DERNIERE ligne qui matche gagne ('*' = 0+ caracteres, '%' = lp1).
// nncs1/nncs2 doivent porter DEUX IP DISTINCTES, sinon Pia dedup la 2e sonde -> NAT incomplet -> 2618-201.
// ATTENTION : le litteral ci-dessous part tel quel sur la carte SD, commentaires '#' compris.
#ifndef NEXTENDO_HOSTS_H
#define NEXTENDO_HOSTS_H

#include "nextendo_config.h"

// Buffer alloué dynamiquement — l'appelant doit le free().
char *nextendo_hosts_build(const char *ip);

// Signature de NOS hosts : reconnait nos fichiers sans dependre de l'IP du moment.
#define NEXTENDO_HOSTS_HEADER_MARK "NEXTENDO NETWORK - Atmosphere DNS-MITM"

#define NEXTENDO_HOSTS_SYSMMC "sdmc:/atmosphere/hosts/sysmmc.txt"
#define NEXTENDO_HOSTS_EMUMMC "sdmc:/atmosphere/hosts/emummc.txt"
#define NEXTENDO_HOSTS_DIR    "sdmc:/atmosphere/hosts"
#define NEXTENDO_SETTINGS_INI "sdmc:/atmosphere/config/system_settings.ini"

#endif // NEXTENDO_HOSTS_H

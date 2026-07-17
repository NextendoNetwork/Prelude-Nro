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
//  Nextendo .nro — installation du planning en ligne (BCAT) de Splatoon 2.
//  Telecharge un "bundle" pret-a-ecrire depuis le serveur Nextendo et le pose dans le
//  save delivery-cache BCAT du jeu (titre 0100F8F0000A2000). Ecrase l'install precedente.
// ============================================================
#ifndef NEXTENDO_BCAT_H
#define NEXTENDO_BCAT_H
#include <switch.h>

typedef enum {
    NB_OK = 0,        // installe
    NB_NET_FAIL,      // telechargement impossible
    NB_NO_SCHEDULE,   // 204 : rien de publie
    NB_MOUNT_FAIL,    // save BCAT introuvable (lancer S2 une fois)
    NB_BAD_BUNDLE,    // bundle illisible
    NB_WRITE_FAIL     // ecriture / commit save echoues
} nextendo_bcat_result;

// Installe le planning S2 dans son cache BCAT. socketInitializeDefault() doit etre actif.
nextendo_bcat_result nextendo_bcat_install_s2(void);

#endif // NEXTENDO_BCAT_H

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
//  Nextendo Network — contenu du fichier hosts Atmosphere DNS-MITM
//  Mode NEXTENDO : redirige TOUT Nintendo -> nos serveurs, bloque le reste.
//  Ecrit par l'app dans /atmosphere/hosts/sysmmc.txt ET /atmosphere/hosts/emummc.txt.
//
//  Regle Atmosphere : la DERNIERE ligne qui matche gagne.
//    '*' = 0+ caracteres   '%' = lp1
//  Les wildcards *.nintendo.* couvrent deja TOUS les hotes compte/jeu decouverts
//  (accounts, m-lp1.baas, capi.lp1.op2, dragons, scsi, god.penne, e0d67c509...baas).
// ============================================================
//  nncs2 NOTE : Pia exige DEUX IP DISTINCTES pour nncs1/nncs2, sinon il dedup et n'envoie
//  jamais la 2e sonde -> le NAT ne se termine pas -> MK8/S2 tombent en 2618-201. L'ancienne
//  box distincte est morte (ne repond plus) ; le serveur la remplace et fait tourner le meme
//  responder nncs2 (UDP 10025 + 10125) avec NNCS_SERVER_IP regle sur sa propre IP. Doit
//  rester aligne avec DnsMitmResolver.cs cote emulateur, qui redirige nncs2 vers la meme IP.
//
//  ATTENTION : tout ce qui est DANS le litteral ci-dessous est ecrit tel quel sur la carte SD
//  de l'utilisateur, commentaires '#' compris. Les explications vont ici, pas la-dedans.
#ifndef NEXTENDO_HOSTS_H
#define NEXTENDO_HOSTS_H

// CONFIGURATION : 203.0.113.10 / 203.0.113.11 sont des adresses d'EXEMPLE (RFC 5737).
// Avant de compiler, remplace-les par l'IP de TON serveur Nextendo et de ton second
// repondeur NAT. Idem BCAT_IP / UP_IP dans nextendo_bcat.c / nextendo_update.c.
static const char NEXTENDO_HOSTS[] =
    "# ============================================================\n"
    "#  NEXTENDO NETWORK - Atmosphere DNS-MITM (mode NEXTENDO)\n"
    "#  Genere par l'app homebrew Nextendo. Derniere ligne qui matche gagne.\n"
    "# ============================================================\n"
    "\n"
    "# --- 1) Tout Nintendo -> serveurs Nextendo (VPS) ---\n"
    "203.0.113.10    *.nintendo.net\n"
    "203.0.113.10    *.nintendo.com\n"
    "203.0.113.10    *.nintendowifi.net\n"
    "203.0.113.10    *.nintendo.co.jp\n"
    "\n"
    "# --- 2) NAT-check #2 : IP differente de nncs1 (sinon MK8 test-103) ---\n"
    "203.0.113.11  nncs2-*.n.n.srv.nintendo.net\n"
    "\n"
    "# --- 3) ANTI-BAN : telemetrie / rapport d'erreur -> trou noir (en dernier) ---\n"
    "0.0.0.0          receive-%.dg.srv.nintendo.net\n"
    "0.0.0.0          receive-%.er.srv.nintendo.net\n"
    "# d4c (MAJ systeme) -> NOTRE VPS : le handler nx-account repond \"aucune MAJ\"\n"
    "# (version 1073742904 <= console) -> plus de popup. (IP reelle Nintendo repondait\n"
    "# \"MAJ dispo\" -> popup recurrent ; null-route 0.0.0.0 -> erreur de connexion MAJ.)\n"
    "203.0.113.10    *.d4c.nintendo.net\n";

// Chemins cibles sur la carte SD.
#define NEXTENDO_HOSTS_SYSMMC "sdmc:/atmosphere/hosts/sysmmc.txt"
#define NEXTENDO_HOSTS_EMUMMC "sdmc:/atmosphere/hosts/emummc.txt"
#define NEXTENDO_HOSTS_DIR    "sdmc:/atmosphere/hosts"
#define NEXTENDO_SETTINGS_INI "sdmc:/atmosphere/config/system_settings.ini"

#endif // NEXTENDO_HOSTS_H

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
//  Mode NEXTENDO : redirige la plupart des domaines Nintendo -> nos serveurs,
//  bloque la telemetrie. Exceptions : d4c (MAJ systeme), ctest (browser conntest)
//  et *.nintendowifi.net resolvent vers le vrai Nintendo pour eviter popups.
//  Ecrit par l'app dans /atmosphere/hosts/sysmmc.txt ET /atmosphere/hosts/emummc.txt.
//
//  Regle Atmosphere : la DERNIERE ligne qui matche gagne.
//    '*' = 0+ caracteres   '%' = lp1
//  Les wildcards *.nintendo.* couvrent deja TOUS les hotes compte/jeu decouverts
//  (accounts, m-lp1.baas, capi.lp1.op2, dragons, scsi, god.penne, e0d67c509...baas).
// ============================================================
//  nncs2 NOTE : Pia exige DEUX IP DISTINCTES pour nncs1/nncs2, sinon il dedup et n'envoie
//  jamais la 2e sonde -> le NAT ne se termine pas -> MK8/S2 tombent en 2618-201. L'ancienne
//  box distincte est morte (ne repond plus) ; le VPS OVH la remplace et fait tourner le meme
//  responder nncs2 (UDP 10025 + 10125) avec NNCS_SERVER_IP regle sur sa propre IP. Doit
//  rester aligne avec DnsMitmResolver.cs cote emulateur, qui redirige nncs2 vers la meme IP.
//
//  ATTENTION : tout ce qui est DANS le litteral ci-dessous est ecrit tel quel sur la carte SD
//  de l'utilisateur, commentaires '#' compris. Les explications vont ici, pas la-dedans.
#ifndef NEXTENDO_HOSTS_H
#define NEXTENDO_HOSTS_H

static const char NEXTENDO_HOSTS[] =
    "# ============================================================\n"
    "#  NEXTENDO NETWORK - Atmosphere DNS-MITM (mode NEXTENDO)\n"
    "#  Genere par l'app homebrew Nextendo. Derniere ligne qui matche gagne.\n"
    "# ============================================================\n"
    "\n"
    "# --- 1) Tout Nintendo -> serveurs Nextendo (VPS) ---\n"
    "# ATTENTION: on n'utilise PAS de wildcard *.nintendo.net car il attraperait\n"
    "# d4c.nintendo.net (MAJ systeme). d4c DOIT resoudre vers le vrai Nintendo pour\n"
    "# eviter le popup phantom sur 22.5.0 (le handler VPS necessite un cert SSL\n"
    "# valide, et le patch disable_ca_verification ne couvre pas 22.5.0).\n"
    "# Le vrai Nintendo repond \"pas de MAJ\" (22.5.0 est le plus recent) -> pas de popup.\n"
    "# Chaque service critique a son entree explicite ci-dessous.\n"
    "# NOTE: *.nintendowifi.net a ete RETIRE volontairement car le browser conntest\n"
    "# (conntest.nintendowifi.net) ne passe pas via le VPS (cherche X-Organization:\n"
    "# Nintendo). En FW 19.0+, si ce check echoue -> \"This feature is not available\"\n"
    "# et le browser ne s'ouvre pas. Si Kazu ajoute un handler cote serveur pour\n"
    "# conntest.nintendowifi.net, on peut remettre le wildcard.\n"
    "51.178.29.194    *.nintendo.com\n"
    "51.178.29.194    *.nintendo.co.jp\n"
    "# Entrees explicites pour les hotes critiques du linking de compte\n"
    "# et des services online (par securite, pour les wildcards multi-niveaux).\n"
    "51.178.29.194 accounts.nintendo.com\n"
    "51.178.29.194 api.accounts.nintendo.com\n"
    "51.178.29.194 m-lp1.baas.nintendo.com\n"
"51.178.29.194 e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com\n"
"51.178.29.194 cdn-image-e0d67c509fb203858ebcb2fe3f88c2aa.baas.nintendo.com\n"
"51.178.29.194 capi.lp1.op2.nintendo.net\n"
"51.178.29.194 storage.hac.lp1.scsi.srv.nintendo.net\n"
    "51.178.29.194 val.hac.penne.srv.nintendo.net\n"
    "51.178.29.194 god.hac.lp1.penne.srv.nintendo.net\n"
    "51.178.29.194 dauth-lp1.ndas.srv.nintendo.net\n"
    "51.178.29.194 api.hac.lp1.ctest.srv.nintendo.net\n"
    "51.178.29.194 *.srv.nintendo.net\n"
    "51.178.29.194 *srv.nintendo.net\n"
    "# Entrees explicites pour les hotes .nintendo.net critiques\n"
    "# qui NE SONT PAS couverts par *.srv.nintendo.net.\n"
    "# (Le wildcard *.nintendo.net a ete retire car il attrape d4c.)\n"
    "51.178.29.194    *.op2.nintendo.net\n"
    "\n"
    "# --- 2) NAT-check #2 : IP differente de nncs1 (sinon MK8 test-103) ---\n"
    "164.132.111.120  nncs2-*.n.n.srv.nintendo.net\n"
    "\n"
    "# --- 3) ANTI-BAN : telemetrie -> trou noir ---\n"
    "0.0.0.0          receive-%.dg.srv.nintendo.net\n"
    "0.0.0.0          receive-%.er.srv.nintendo.net\n"
    "# --- 4) d4c (MAJ systeme) -> NON REDIRIGE ---\n"
    "# d4c.nintendo.net n'est PAS dans les wildcards ci-dessus (pas de *.nintendo.net).\n"
    "# Il resout donc vers le vrai Nintendo via DNS normal. Comme 22.5.0 est le firmware\n"
    "# le plus recent, le vrai Nintendo repond \"pas de MAJ disponible\" -> pas de popup.\n"
    "# L'ancienne redirection vers le VPS cassait en 22.5.0 car le SSL auto-signe n'était\n"
    "# pas accepte par le module SSL systeme (disable_ca_verification ne couvre pas 22.5.0).\n"
    "# NE PAS null-router : nim stocke un flag persistant dans la savedata systeme si la\n"
    "# reference version est absente, et le popup ne disparait plus meme apres correction.\n"
    "\n"
    "# --- 5) ctest.cdn.nintendo.net (browser conntest FW 19+) -> NON REDIRIGE ---\n"
    "# Ce domaine N'est PAS dans les wildcards. Il resout vers le vrai Nintendo.\n"
    "# Le browser l'utilise pour verifier la connectivite. S'il echoue ->\n"
    "# \"This feature is not available\" et le browser ne s'ouvre pas.\n"
    "# NE JAMAIS ajouter *.nintendo.net ou un wildcard qui attrape ctest.cdn.nintendo.net.\n";

// Chemins cibles sur la carte SD.
#define NEXTENDO_HOSTS_SYSMMC "sdmc:/atmosphere/hosts/sysmmc.txt"
#define NEXTENDO_HOSTS_EMUMMC "sdmc:/atmosphere/hosts/emummc.txt"
#define NEXTENDO_HOSTS_DIR    "sdmc:/atmosphere/hosts"
#define NEXTENDO_SETTINGS_INI "sdmc:/atmosphere/config/system_settings.ini"

#endif // NEXTENDO_HOSTS_H

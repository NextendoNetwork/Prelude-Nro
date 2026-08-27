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

// Logique systeme : ecrire les hosts, editer system_settings.ini, redemarrer.
#ifndef NEXTENDO_APPLY_H
#define NEXTENDO_APPLY_H
#include <switch.h>

typedef enum { BOOT_UNKNOWN = -1, BOOT_SYSMMC = 0, BOOT_EMUMMC = 1 } BootType;

// Commit a chaque ligne : sans lui un arret force perd le cache SD et la trace ne dit plus rien.
void nextendo_trace(const char *step);
#define NEXTENDO_TRACE_PATH "sdmc:/prelude_trace.txt"

// Cosmetique : les deux fichiers hosts sont ecrits de toute facon.
BootType nextendo_detect_boot(void);

// 0 = NEXTENDO, 1 = NINTENDO (memes valeurs que CHOICE_*), lu depuis les fichiers hosts.
int nextendo_current_mode(void);

// Ecrit sysmmc.txt + emummc.txt et pose enable_dns_mitm=1.
bool nextendo_apply_nextendo_ip(const char *ip);

// Idem avec NEXTENDO_SERVER_IP_DEFAULT.
bool nextendo_apply_nextendo(void);

// Retire nos hosts et repasse enable_dns_mitm=0.
bool nextendo_apply_nintendo(void);

// Loggue la connectivite dans la trace (diagnostic 2123-0011 / 2810-1224).
void nextendo_diag_network(void);

// Ne revient pas si succes (bpcRebootSystem).
Result nextendo_reboot(void);

// Mod SSBU Online Deluxe : copie romfs:/ssbu_quickplay/ vers sdmc:, et l'inverse.
bool nextendo_ssbu_is_installed(void);
bool nextendo_ssbu_install(void);
void nextendo_ssbu_remove(void);

// Overclock du mod : a couper si Horizon OC / sys-clk tourne deja, sinon la console gele au lancement de Smash.
bool nextendo_ssbu_oc_is_disabled(void);
bool nextendo_ssbu_oc_set(bool enabled);

// Correctifs Splatoon 3 : indexes par build id, ignores EN SILENCE si aucun ne correspond (-> 2122-2403).
typedef struct {
    int  onSd;        // correctifs presents sur la carte
    int  inRomfs;     // correctifs livres par ce .nro
    bool dnsMitmOn;   // enable_dns_mitm = 1 dans system_settings.ini
    bool hostsOk;     // les hosts portent bien notre IP
} NextendoS3Status;

void nextendo_s3_status(NextendoS3Status *out);
bool nextendo_provision_all_public(void);

// Hosts dns.mitm d'AVANT Prelude. create/restore renvoient le nombre de fichiers traites (0 = rien a faire).
bool nextendo_hosts_backup_exists(void);
int  nextendo_hosts_backup_create(void);
int  nextendo_hosts_backup_restore(void);
// La question ne se pose qu'au premier lancement et apres chaque mise a jour.
bool nextendo_backup_prompt_needed(void);
void nextendo_backup_prompt_done(void);
bool nextendo_backup_use_for_nintendo(void);
void nextendo_backup_set_use_for_nintendo(bool on);

#endif // NEXTENDO_APPLY_H

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

// System logic: write the hosts files, edit system_settings.ini, reboot.
#ifndef NEXTENDO_APPLY_H
#define NEXTENDO_APPLY_H
#include <switch.h>

typedef enum { BOOT_UNKNOWN = -1, BOOT_SYSMMC = 0, BOOT_EMUMMC = 1 } BootType;

// Commit on every line: without it a forced power-off loses the SD cache and the trace says nothing.
void nextendo_trace(const char *step);
#define NEXTENDO_TRACE_PATH "sdmc:/prelude_trace.txt"

// Cosmetic: both hosts files get written either way.
BootType nextendo_detect_boot(void);

// 0 = NEXTENDO, 1 = NINTENDO (same values as CHOICE_*), read back from the hosts files.
int nextendo_current_mode(void);

// Writes sysmmc.txt + emummc.txt and sets enable_dns_mitm=1.
bool nextendo_apply_nextendo_ip(const char *ip);

// Same, using NEXTENDO_SERVER_IP_DEFAULT.
bool nextendo_apply_nextendo(void);

// Removes our hosts files and puts enable_dns_mitm back to 0.
bool nextendo_apply_nintendo(void);

// Logs connectivity into the trace (diagnostics for 2123-0011 / 2810-1224).
void nextendo_diag_network(void);

// Does not return on success (bpcRebootSystem).
Result nextendo_reboot(void);

// SSBU Online Deluxe mod: copies romfs:/ssbu_quickplay/ to sdmc:, and the reverse.
bool nextendo_ssbu_is_installed(void);
bool nextendo_ssbu_install(void);
void nextendo_ssbu_remove(void);

// The mod's own overclock: turn it off if Horizon OC / sys-clk is already running, or Smash freezes on launch.
bool nextendo_ssbu_oc_is_disabled(void);
bool nextendo_ssbu_oc_set(bool enabled);

// Splatoon 3 patches: indexed by build id, and SILENTLY ignored when none matches (-> 2122-2403).
typedef struct {
    int  onSd;        // patches present on the SD card
    int  inRomfs;     // patches shipped by this .nro
    bool dnsMitmOn;   // enable_dns_mitm = 1 in system_settings.ini
    bool hostsOk;     // the hosts files really do carry our IP
} NextendoS3Status;

void nextendo_s3_status(NextendoS3Status *out);
bool nextendo_provision_all_public(void);

// The user's dns.mitm hosts from BEFORE Prelude. create/restore return the file count handled (0 = nothing to do).
bool nextendo_hosts_backup_exists(void);
int  nextendo_hosts_backup_create(void);
int  nextendo_hosts_backup_restore(void);
// The question is only asked on first launch and after each update.
bool nextendo_backup_prompt_needed(void);
void nextendo_backup_prompt_done(void);
bool nextendo_backup_use_for_nintendo(void);
void nextendo_backup_set_use_for_nintendo(bool on);

#endif // NEXTENDO_APPLY_H

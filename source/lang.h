// Prelude — Nintendo Switch homebrew for the Nextendo Network.
// Copyright (C) 2026 Nextendo Network
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version.
//
// You should have received a copy of the GNU Affero General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>.

// ============================================================
//  Prelude — internationalisation (EN / ES / PT).
//  Les chaînes sont indexées par StringID. La langue courante
//  est persistée dans sdmc:/switch/prelude_lang.txt (1 octet).
// ============================================================
#ifndef LANG_H
#define LANG_H

typedef enum {
    LANG_EN,    // English (default)
    LANG_ES,    // Español
    LANG_PT,    // Português
    LANG_FR,    // Français
} Lang;

typedef enum {
    // --- Picker screen ---
    STR_TITLE_PRELUDE,          // "Prelude" (brand — same in all langs)
    STR_MODE_ACTUAL_PREFIX,     // "Current mode:" / "Modo actual:" / "Modo atual:"
    STR_CURRENT_BADGE,          // "CURRENT" / "ACTUAL" / "ATUAL"
    STR_SWITCH_BADGE,           // "SWITCH HERE" / "CAMBIAR AQUÍ" / "MUDAR AQUI"

    // --- Context descriptions ---
    STR_DESC_NEXTENDO,          // "Nextendo servers: custom online..."
    STR_DESC_NINTENDO,          // "Official Nintendo servers..."
    STR_DESC_S2,                // "Downloads Splatoon 2 schedule..."

    // --- Help lines ---
    STR_HELP_MODE,              // "A: switch mode   < >: change   B: quit"
    STR_HELP_S2,                // "A: install schedule   < >: change   B: quit"

    // --- Update banner ---
    STR_UPDATE_BANNER,          // "MANDATORY update (v%d) - press Y to install"

    // --- Confirm screen ---
    STR_CONFIRM_NEXTENDO,       // "Switch to NEXTENDO mode?"
    STR_CONFIRM_NINTENDO,       // "Switch to NINTENDO mode?"
    STR_CONFIRM_REBOOT,         // "The console will REBOOT now."
    STR_CONFIRM_RESTART_NEXTENDO, // "After reboot: connected to Nextendo servers."
    STR_CONFIRM_RESTART_NINTENDO, // "After reboot: back to official Nintendo servers."
    STR_CONFIRM_CLOSE_GAMES,    // "Close any running games before confirming."
    STR_CONFIRM_A,              // "A: Confirm"
    STR_CONFIRM_B,              // "B: Cancel"

    // --- No-emuMMC warnings ---
    STR_WARN_TITLE,             // "WARNING: this console has no emuMMC."
    STR_WARN_LINE1,             // "CFW runs on internal memory with your real console ID."
    STR_WARN_LINE2,             // "No identity protection is possible without breaking eShop."
    STR_WARN_LINE3,             // "Going ONLINE on real Nintendo remains at your own risk."
    STR_WARN_LINE4,             // "(Telemetry is still blocked.)"

    // --- S2 bar label (picker screen) ---
    STR_S2_BAR,                 // "Splatoon 2 - Install online schedule"

    // --- S2 info screen ---
    STR_S2_TITLE,               // "Splatoon 2 online schedule"
    STR_S2_DESC1,               // "Downloads rotation schedule..."
    STR_S2_DESC2,               // "from Nextendo servers and installs..."
    STR_S2_DESC3,               // "Launch Splatoon 2 at least once first."
    STR_S2_DESC4,               // "Each installation REPLACES the previous schedule."
    STR_S2_A,                   // "A: Install"
    STR_S2_B,                   // "B: Back"

    // --- Progress screen ---
    STR_PROGRESS_TITLE,         // "Installation"
    STR_PROGRESS_WAIT,          // "Please wait..."

    // --- Result screen ---
    STR_RESULT_RETURN,           // "A / B: Return"

    // --- Status messages (main.c) ---
    STR_STATUS_NEXTENDO_OK,     // "NEXTENDO mode applied - rebooting..."
    STR_STATUS_NINTENDO_OK,     // "NINTENDO mode applied - rebooting..."
    STR_STATUS_REBOOT_FAIL,     // "Reboot failed"
    STR_STATUS_SD_ERROR,        // "ERROR: cannot write to SD card"
    STR_STATUS_DOWNLOAD_SCHEDULE, // "Downloading and installing schedule..."
    STR_STATUS_SCHEDULE_OK,     // "Schedule installed"
    STR_STATUS_SCHEDULE_OK_DESC,  // "Old schedule replaced. Relaunch Splatoon 2 to apply."
    STR_STATUS_NO_SCHEDULE,     // "No schedule"
    STR_STATUS_NO_SCHEDULE_DESC,  // "Nothing published yet."
    STR_STATUS_MOUNT_FAIL,      // "Save data not found"
    STR_STATUS_MOUNT_FAIL_DESC, // "Launch Splatoon 2 once, then try again."
    STR_STATUS_NET_FAIL,        // "Network error"
    STR_STATUS_NET_FAIL_DESC,   // "Download failed (check connection / mode)."
    STR_STATUS_WRITE_FAIL,      // "Write error"
    STR_STATUS_WRITE_FAIL_DESC, // "Cannot write to save data."
    STR_STATUS_NET_CONNECT,     // "Server unreachable"
    STR_STATUS_NET_CONNECT_DESC, // "Cannot connect to server (check internet / mode)."
    STR_STATUS_NET_TIMEOUT,     // "Connection timeout"
    STR_STATUS_NET_TIMEOUT_DESC, // "Server took too long to respond. Try again."
    STR_STATUS_NET_HTTP_ERR,    // "Server error"
    STR_STATUS_NET_HTTP_ERR_DESC, // "Server returned an unexpected response."
    STR_STATUS_DOWNLOAD_UPDATE, // "Downloading update..."
    STR_STATUS_UPDATE_OK,       // "Update installed"
    STR_STATUS_UPDATE_OK_DESC,  // "CLOSE and relaunch Prelude to apply v%d."
    STR_STATUS_UPDATE_SIZE_FAIL,    // "Download corrupted"
    STR_STATUS_UPDATE_SIZE_FAIL_DESC, // "Unexpected size. Try again."
    STR_STATUS_UPDATE_WRITE_FAIL,   // "Write failed"
    STR_STATUS_UPDATE_WRITE_FAIL_DESC, // "Cannot write to SD card."
    STR_STATUS_UPDATE_NET_FAIL,     // "Network error"
    STR_STATUS_UPDATE_NET_FAIL_DESC, // "Download failed."

    // --- Update confirmation ---
    STR_UPD_CONFIRM_TITLE,      // "Update available"
    STR_UPD_CONFIRM_VERSION,    // "New version: build %d"
    STR_UPD_CONFIRM_DESC,       // "Prelude will download and replace itself."
    STR_UPD_CONFIRM_A,          // "A: Download and install"
    STR_UPD_CONFIRM_B,          // "B: Cancel"

    // --- Language menu ---
    STR_LANG_TITLE,             // "Language" / "Idioma"
    STR_LANG_EN,                // "English"
    STR_LANG_ES,                // "Español"
    STR_LANG_PT,                // "Português"
    STR_LANG_FR,                // "Français"
    STR_LANG_DEFAULT,           // "(Default)"
    STR_LANG_A_SELECT,          // "A: Select"
    STR_LANG_B_BACK,            // "B: Back"

    STR_COUNT
} StringID;

extern Lang g_lang;

// Initialise la langue (lecture depuis SD si disponible, sinon LANG_EN).
void lang_init(void);

// Sauvegarde la langue choisie sur la SD.
void lang_save(void);

// Renvoie la chaîne localisée pour un ID donné.
const char *lang_str(StringID id);

#endif // LANG_H

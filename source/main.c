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

// Entry point: two-column screen (section rail + panel), update check at launch.
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <netdb.h>

#include "ui.h"
#include "audio.h"
#include "nextendo_apply.h"
#include "nextendo_config.h"
#include <stdlib.h>
#include "nextendo_bcat.h"
#include "nextendo_update.h"
#include "nextendo_flag.h"
#include "ui_theme.h"
#include "lang.h"

enum {
    SCREEN_PICKER, SCREEN_S2_INFO, SCREEN_S2_PROGRESS, SCREEN_S2_RESULT,
    SCREEN_UPD_CONFIRM, SCREEN_UPD_PROGRESS, SCREEN_UPD_RESULT,
    SCREEN_FLAG_MENU, SCREEN_FLAG_PROGRESS, SCREEN_FLAG_RESULT,
    SCREEN_BACKUP_ASK, SCREEN_USEBAK_ASK,
    // Only confirm / progress / result stay modal, plus the 110-country list, too long for a panel.
};

// Exit log (sdmc:/prelude_exit.log): context + last screen + the full trace and BCAT log. The single entry point for debugging.
#define EXIT_LOG_PATH "sdmc:/prelude_exit.log"

static void appendFileToLog(FILE *out, const char *path) {
    FILE *in = fopen(path, "r");
    if (!in) { fprintf(out, "(absent)\n"); return; }
    char line[512];
    while (fgets(line, sizeof(line), in)) fputs(line, out);
    fclose(in);
}

static void writeExitLog(int lastScreen, const char *lastTitle, const char *lastMsg,
                         bool lastOk, BootType boot, bool noEmummc, int currentMode,
                         const NextendoUpdate *upd) {
    FILE *f = fopen(EXIT_LOG_PATH, "w");
    if (!f) return;
    fprintf(f, "=== Prelude exit log ===\n");
    fprintf(f, "build : %d (v%d.%d.%d)\n", NEXTENDO_BUILD,
            NEXTENDO_VERSION_MAJOR, NEXTENDO_VERSION_MINOR, NEXTENDO_VERSION_PATCH);
    fprintf(f, "boot : %s\n", boot == BOOT_SYSMMC ? "SYSMMC"
                            : boot == BOOT_EMUMMC ? "EMUMMC" : "UNKNOWN");
    fprintf(f, "no_emummc : %d\n", noEmummc);
    fprintf(f, "current mode : %s\n", currentMode == CHOICE_NEXTENDO ? "NEXTENDO" : "NINTENDO");
    fprintf(f, "server ip : %s\n", g_server_ip);
    fprintf(f, "update available : %s", upd->available ? "YES" : "no");
    if (upd->available) fprintf(f, " (v%d.%d.%d)", upd->maj, upd->min, upd->patch);
    fprintf(f, "\n");
    fprintf(f, "last screen : %d\n", lastScreen);
    fprintf(f, "last result ok : %d\n", lastOk);
    fprintf(f, "last result title : %s\n", lastTitle ? lastTitle : "");
    fprintf(f, "last result msg : %s\n", lastMsg ? lastMsg : "");
    fprintf(f, "\n--- nextendo_bcat.log ---\n");
    appendFileToLog(f, "sdmc:/nextendo_bcat.log");
    fprintf(f, "\n--- prelude_trace.txt ---\n");
    appendFileToLog(f, NEXTENDO_TRACE_PATH);
    fclose(f);
    fsdevCommitDevice("sdmc");
}

// Called on the main thread, so we draw directly. We only redraw when the percentage changes: otherwise 17 MB means ~550 vsync-locked presents.
static int s_updLastPct = -1;
static nextendo_update_phase s_updLastPhase = NUP_PHASE_DOWNLOAD;

static void updateProgress(nextendo_update_phase phase, long done, long total) {
    int pct = (total > 0) ? (int)((done * 100) / total) : 0;
    if (pct > 100) pct = 100;
    if (pct == s_updLastPct && phase == s_updLastPhase) return;
    s_updLastPct   = pct;
    s_updLastPhase = phase;

    // Tenths of a MiB in integers: no floating point for a status line.
    char detail[64];
    if (total > 0)
        snprintf(detail, sizeof(detail), "%ld%%  -  %ld.%ld / %ld.%ld MiB", (long)pct,
                 done / (1024 * 1024), (done * 10 / (1024 * 1024)) % 10,
                 total / (1024 * 1024), (total * 10 / (1024 * 1024)) % 10);
    else
        snprintf(detail, sizeof(detail), "%ld.%ld MiB",
                 done / (1024 * 1024), (done * 10 / (1024 * 1024)) % 10);

    ui_draw_progress_bar(lang_str(phase == NUP_PHASE_INSTALL ? STR_STATUS_INSTALL_UPDATE
                                                            : STR_STATUS_DOWNLOAD_UPDATE),
                         pct, detail);
}

// Startup network work, off the main thread: it took several seconds and blocked rendering.
// `done` is the only rendezvous: the thread writes `upd` THEN sets done=1, and the barrier guarantees main never sees a half-written `upd`.
// The mandatory-update lock depends on it: while done is 0, we DO NOT KNOW whether a newer version exists.
static struct {
    NextendoUpdate  upd;
    int             mode;
    volatile bool   done;      // the updater's answer is readable
    volatile bool   netDone;   // the thread has given the sockets back
} s_boot;

// socketInitializeDefault()/socketExit() are not refcounted: the startup thread's socketExit()
// would close the network stack out from under a download started from the menu. Ever since
// `done` is published BEFORE the diagnostics, that window is real — it is exactly what was
// cutting an update off at a few percent.
static void waitBootNet(void) {
    while (!s_boot.netDone) svcSleepThread(10000000ULL);   // 10 ms
}

static Thread s_bootThread;

static void bootWorker(void *arg) {
    (void)arg;
    NextendoUpdate u = nextendo_update_check();
    nextendo_trace(u.available ? "14 update_check: MAJ DISPO -> homebrew verrouille (A inactif)"
                               : "14 update_check: a jour -> A actif");

    // Published AS SOON AS the answer is known. Everything below is diagnostics: keeping it
    // ahead of publication made the UI wait up to ~10 s (the BCAT probe has two 5 s timeouts)
    // for a trace the user never reads.
    s_boot.upd  = u;
    __asm__ __volatile__("dmb ish" ::: "memory");  // upd visible BEFORE done
    s_boot.done = true;

    // Network diagnostics: nncs2 + hosts state (traced for 2123-0011 / 2810-1224).
    socketInitializeDefault();
    nextendo_diag_network();
    // Atmosphere's DNS-MITM is lazily loaded: without this priming query, nnAccount linking fails.
    if (s_boot.mode == CHOICE_NEXTENDO) {
        struct hostent *he = gethostbyname("accounts.nintendo.com");
        nextendo_trace(he ? "15a dns warmup: accounts.nintendo.com OK"
                          : "15a dns warmup: accounts.nintendo.com FAIL");
    }
    socketExit();
    __asm__ __volatile__("dmb ish" ::: "memory");
    s_boot.netDone = true;
}

int main(int argc, char **argv) {
    // argv[0]: the updater must replace THIS file, or the update lands beside it and the old version keeps launching.
    nextendo_update_set_self_path((argc > 0 && argv) ? argv[0] : NULL);

    romfsInit();
    audio_init();
    lang_init();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();

    if (!ui_init()) {
        audio_exit();
        romfsExit();
        return 1;
    }

    // The trace restarts from scratch on every launch: it documents THE session the user is telling us about, and never grows.
    remove(NEXTENDO_TRACE_PATH);
    nextendo_trace("10 main: ui_init ok");

    // Without emuMMC, blank_prodinfo_emummc does nothing: we warn rather than imply a protection that is not there. Detected ONCE.
    nextendo_trace("11 avant detect_boot (splInitialize)");
    BootType boot = nextendo_detect_boot();
    nextendo_trace(boot == BOOT_SYSMMC  ? "12 detect_boot = SYSMMC (pas d emuMMC)"
                 : boot == BOOT_EMUMMC  ? "12 detect_boot = EMUMMC"
                                        : "12 detect_boot = INCONNU (spl a echoue)");
    bool noEmummc = (boot == BOOT_SYSMMC);

    int  current = nextendo_current_mode();
    // Without this, updating Prelude left the previous version's patches on the card. Nothing is touched in Nintendo mode.
    if (current == CHOICE_NEXTENDO) nextendo_provision_all_public();

    NextendoS3Status s3;
    nextendo_s3_status(&s3);
    int  sel    = (current == CHOICE_NEXTENDO) ? CHOICE_NINTENDO : CHOICE_NEXTENDO;
    int  railSel = RAIL_MODE;   // rail section
    int  paneSel = 0;           // panel row
    bool paneFocus = false;     // false = the d-pad drives the rail
    int  screen = SCREEN_PICKER;
    // The only chance to save the hosts from BEFORE us: once a mode is applied, the originals are overwritten.
    if (nextendo_backup_prompt_needed()) screen = SCREEN_BACKUP_ASK;
    int  state  = 0;
    char status[160] = {0};
    char rTitle[64] = {0}, rMsg[192] = {0};
    bool rOk = false;

    int  flagSel    = 0;
    int  flagScroll = 0;
    char flagCurrent[3] = {0};
    flag_detect_current(flagCurrent);

    bool ssbuInstalled = nextendo_ssbu_is_installed();
    // Read from the SD card (boot2.flag present), not assumed: the player may have turned it off on an earlier run.
    bool ssbuOcDisabled = nextendo_ssbu_oc_is_disabled();

    // ↑↓←→ sequence toggles the server IP.
    enum { SEQ_IDLE, SEQ_UP, SEQ_UP_DOWN, SEQ_UP_DOWN_LEFT };
    int seqState = SEQ_IDLE;
    bool touchHeld = false;   // rising edge: a finger held down counts only once

    // On a thread: the picker shows immediately and stays navigable meanwhile.
    s_boot.mode = current;
    nextendo_trace("13 demarrage du thread reseau (picker deja affiche)");
    bool bootThreadOn = false;
    if (R_SUCCEEDED(threadCreate(&s_bootThread, bootWorker, NULL, NULL, 0x20000, 0x2C, -2))
        && R_SUCCEEDED(threadStart(&s_bootThread))) {
        bootThreadOn = true;
    } else {
        // Synchronous fallback rather than leaving upd unset: the mandatory-update lock depends on it.
        nextendo_trace("13b threadCreate KO -> repli synchrone");
        bootWorker(NULL);
    }
    NextendoUpdate upd = (NextendoUpdate){0};
    bool bootPublished = false;   // upd is published once (a successful update resets available=0)
    nextendo_trace("15 entree dans la boucle principale");

    bool tracedLoop = false, tracedConfirm = false;
    while (appletMainLoop()) {
        consoleUpdate(NULL);
        // One-shot: a successful update resets upd.available to 0, and the copy must not revive the banner next frame.
        if (!bootPublished && s_boot.done) { upd = s_boot.upd; bootPublished = true; }
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);

        // The tag targets the LAST drawn frame, which is exactly what the user is looking at.
        int tap = UI_TAP_NONE;
        HidTouchScreenState ts = {0};
        if (hidGetTouchScreenStates(&ts, 1) && ts.count > 0) {
            if (!touchHeld) tap = ui_tap_at((int)ts.touches[0].x, (int)ts.touches[0].y);
            touchHeld = true;
        } else {
            touchHeld = false;
        }
        // The button bar fires the button it displays, modals their A / B half.
        // The rest (rail, rows, banner) is screen-specific and handled in each branch.
        if (UI_TAP_KIND(tap) == UI_TAP_BTN) {
            switch (UI_TAP_INDEX(tap)) {
            case UI_BTN_A:    k |= HidNpadButton_A;    break;
            case UI_BTN_B:    k |= HidNpadButton_B;    break;
            case UI_BTN_Y:    k |= HidNpadButton_Y;    break;
            case UI_BTN_PLUS: k |= HidNpadButton_Plus; break;
            }
        } else if (tap == UI_TAP_YES) k |= HidNpadButton_A;
        else if (tap == UI_TAP_NO)    k |= HidNpadButton_B;
        // Proves the loop runs AND input arrives: if absent, padUpdate/HID is dead, not the logic.
        if (!tracedLoop && k) { nextendo_trace("16 premiere touche detectee dans la boucle"); tracedLoop = true; }

        // --- ↑↓←→ sequence: toggle the server IP ---
        if (screen == SCREEN_PICKER && state == 0) {
            if (seqState == SEQ_IDLE && (k & HidNpadButton_Up))            seqState = SEQ_UP;
            else if (seqState == SEQ_UP && (k & HidNpadButton_Down))       seqState = SEQ_UP_DOWN;
            else if (seqState == SEQ_UP_DOWN && (k & HidNpadButton_Left))  seqState = SEQ_UP_DOWN_LEFT;
            else if (seqState == SEQ_UP_DOWN_LEFT && (k & HidNpadButton_Right)) {
                if (strcmp(g_server_ip, NEXTENDO_SERVER_IP_DEFAULT) == 0)
                    strncpy(g_server_ip, NEXTENDO_SERVER_IP_ALT, NEXTENDO_SERVER_IP_MAX - 1);
                else
                    strncpy(g_server_ip, NEXTENDO_SERVER_IP_DEFAULT, NEXTENDO_SERVER_IP_MAX - 1);
                g_server_ip[NEXTENDO_SERVER_IP_MAX - 1] = '\0';
                ui_set_toast(server_display_name());
                seqState = SEQ_IDLE;
            } else if (k) {
                seqState = SEQ_IDLE;
            }
        } else {
            seqState = SEQ_IDLE;
        }

        if (screen == SCREEN_PICKER) {
            if (state == 0) {
                // B only quits from the rail: inside the panel it goes back, and quitting on a back action would be a trap.
                if (k & HidNpadButton_Plus) break;
                if ((k & HidNpadButton_B) && !paneFocus) break;

                // Only a button that ACTIVATES consults the update lock, and only it waits.
                // Navigation and exit do not depend on it: making them wait was free of charge.
                if ((k & (HidNpadButton_A | HidNpadButton_Y)) && !bootPublished) {
                    ui_draw_loading(lang_str(STR_CHECKING_UPDATE));
                    while (!s_boot.done) svcSleepThread(10000000ULL);  // 10 ms
                    upd = s_boot.upd;
                    bootPublished = true;
                }

                // Tapping a section goes there; tapping a row selects AND activates it, like the
                // console's own home screen. Under the update lock only the banner gets through.
                if (upd.available) {
                    if (tap == UI_TAP_UPDATE) k |= HidNpadButton_Y;
                } else if (UI_TAP_KIND(tap) == UI_TAP_RAIL) {
                    railSel = UI_TAP_INDEX(tap);
                    paneFocus = false;
                    status[0] = 0;
                } else if (UI_TAP_KIND(tap) == UI_TAP_ROW) {
                    int rows = ui_pane_rows(railSel, ssbuInstalled);
                    int r = UI_TAP_INDEX(tap);
                    if (r < rows) { paneSel = r; paneFocus = true; k |= HidNpadButton_A; }
                }

                if (upd.available) {
                    // MANDATORY UPDATE: everything is locked except install (Y) and exit (+/B).
                    if (k & HidNpadButton_Y) { screen = SCREEN_UPD_CONFIRM; }
                } else if (!paneFocus) {
                    // Left column: we move through the sections.
                    if (k & HidNpadButton_AnyUp)   { railSel = (railSel + RAIL_N - 1) % RAIL_N; status[0] = 0; }
                    if (k & HidNpadButton_AnyDown) { railSel = (railSel + 1) % RAIL_N;          status[0] = 0; }
                    // Right AND A enter the panel: the d-pad is the expected gesture, and A must not sit inert here.
                    if (k & (HidNpadButton_AnyRight | HidNpadButton_A)) {
                        paneFocus = true;
                        paneSel = 0;
                        // We land on the mode that is NOT current: that is the one you can switch to.
                        if (railSel == RAIL_MODE)
                            paneSel = (current == CHOICE_NEXTENDO) ? CHOICE_NINTENDO : CHOICE_NEXTENDO;
                    }
                } else {
                    // Right column: we move through the section's rows.
                    int rows = ui_pane_rows(railSel, ssbuInstalled);
                    if (k & HidNpadButton_AnyUp)   paneSel = (paneSel + rows - 1) % rows;
                    if (k & HidNpadButton_AnyDown) paneSel = (paneSel + 1) % rows;
                    if (k & (HidNpadButton_AnyLeft | HidNpadButton_B)) paneFocus = false;

                    if (k & HidNpadButton_A) {
                        switch (railSel) {
                        case RAIL_MODE:
                            // paneSel IS the chosen mode.
                            sel = paneSel;
                            nextendo_trace("17 A picker -> ecran de confirmation");
                            state = 1; status[0] = 0;
                            break;
                        case RAIL_S3:
                            // Rewrites the romfs files without a full mode switch. Forbidden in Nintendo mode, which strips the cert stack on purpose.
                            if (current != CHOICE_NINTENDO) {
                                nextendo_provision_all_public();
                                nextendo_s3_status(&s3);   // re-read: the panel must reflect the card
                                snprintf(status, sizeof(status), "%s", lang_str(STR_S3_DONE));
                            }
                            break;
                        case RAIL_S2:   screen = SCREEN_S2_INFO;   break;
                        case RAIL_FLAG: screen = SCREEN_FLAG_MENU; break;
                        case RAIL_LANG:
                            if (paneSel != (int)g_lang) { g_lang = (Lang)paneSel; lang_save(); }
                            break;
                        case RAIL_SSBU:
                            if (paneSel == 0) {
                                if (ssbuInstalled) {
                                    nextendo_ssbu_remove();
                                    ssbuInstalled = false;
                                    ssbuOcDisabled = nextendo_ssbu_oc_is_disabled();
                                    snprintf(status, sizeof(status), "%s", lang_str(STR_SSBU_NOT_INSTALLED));
                                } else {
                                    ssbuInstalled = nextendo_ssbu_install();
                                    if (ssbuInstalled) ssbuOcDisabled = nextendo_ssbu_oc_is_disabled();
                                    snprintf(status, sizeof(status), "%s",
                                             lang_str(ssbuInstalled ? STR_SSBU_INSTALLED
                                                                    : STR_STATUS_SD_ERROR));
                                }
                                // The row count depends on ssbuInstalled: without this clamp, paneSel points at a vanished row.
                                int r = ui_pane_rows(railSel, ssbuInstalled);
                                if (paneSel >= r) paneSel = r - 1;
                            } else {
                                bool enable = ssbuOcDisabled;
                                nextendo_ssbu_oc_set(enable);
                                ssbuOcDisabled = nextendo_ssbu_oc_is_disabled();
                            }
                            break;
                        }
                    }
                }
                if (screen == SCREEN_PICKER && state == 0) {
                    // The picker shows IMMEDIATELY and stays navigable: the check reports itself in
                    // the status line instead of taking over the screen. The banner appears on its own
                    // when the thread publishes, which is what passive publication is for.
                    const char *ligne = status[0] ? status
                                      : (!bootPublished ? lang_str(STR_CHECKING_UPDATE) : NULL);
                    ui_draw_picker(railSel, paneSel, paneFocus, current, ligne,
                                   upd.available ? upd.maj : 0,
                                   upd.available ? upd.min : 0,
                                   upd.available ? upd.patch : 0,
                                   flagCurrent, ssbuInstalled, ssbuOcDisabled, &s3);
                }
            } else {
                if (k & (HidNpadButton_B | HidNpadButton_Plus)) {
                    state = 0;
                } else if (k & HidNpadButton_A) {
                    nextendo_trace("19 A confirmation -> appel de apply_*");
                    bool ok = (sel == CHOICE_NEXTENDO) ? nextendo_apply_nextendo()
                                                       : nextendo_apply_nintendo();
                    nextendo_trace(ok ? "28 apply a renvoye OK -> reboot"
                                      : "28 apply a renvoye ECHEC -> message d erreur");
                    if (ok) {
                        snprintf(status, sizeof(status), "%s",
                                 lang_str(sel == CHOICE_NEXTENDO
                                     ? STR_STATUS_NEXTENDO_OK
                                     : STR_STATUS_NINTENDO_OK));
                        ui_draw_picker(railSel, paneSel, paneFocus, current, status,
                                       upd.available ? upd.maj : 0,
                                       upd.available ? upd.min : 0,
                                       upd.available ? upd.patch : 0,
                                       flagCurrent, ssbuInstalled, ssbuOcDisabled, &s3);
                        svcSleepThread(1200000000ULL);
                        audio_exit();
                        nextendo_reboot();
                        snprintf(status, sizeof(status), "%s", lang_str(STR_STATUS_REBOOT_FAIL));
                        state = 0;
                    } else {
                        snprintf(status, sizeof(status), "%s", lang_str(STR_STATUS_SD_ERROR));
                        state = 0;
                    }
                }
                if (state == 1) {
                    if (!tracedConfirm) { nextendo_trace("18 avant ui_draw_confirm"); }
                    ui_draw_confirm(sel, noEmummc);
                    if (!tracedConfirm) { nextendo_trace("18b ui_draw_confirm rendu ok"); tracedConfirm = true; }
                }
            }

        } else if (screen == SCREEN_BACKUP_ASK) {
            if (k & HidNpadButton_A) {
                int nb = nextendo_hosts_backup_create();
                snprintf(status, sizeof(status), "%s",
                         lang_str(nb ? STR_BACKUP_SAVED : STR_BACKUP_NONE));
                // With no copy made, the second question has no subject.
                if (nb) {
                    screen = SCREEN_USEBAK_ASK;
                } else {
                    nextendo_backup_prompt_done();
                    screen = SCREEN_PICKER;
                }
            } else if (k & (HidNpadButton_B | HidNpadButton_Plus)) {
                nextendo_backup_prompt_done();
                screen = SCREEN_PICKER;
            }
            if (screen == SCREEN_BACKUP_ASK)
                ui_draw_question(lang_str(STR_BACKUP_TITLE),
                                 lang_str(STR_BACKUP_BODY1),
                                 lang_str(STR_BACKUP_BODY2));

        } else if (screen == SCREEN_USEBAK_ASK) {
            if (k & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus)) {
                nextendo_backup_set_use_for_nintendo((k & HidNpadButton_A) != 0);
                nextendo_backup_prompt_done();
                screen = SCREEN_PICKER;
            }
            if (screen == SCREEN_USEBAK_ASK)
                ui_draw_question(lang_str(STR_USEBAK_TITLE),
                                 lang_str(STR_USEBAK_BODY1),
                                 lang_str(STR_USEBAK_BODY2));

        } else if (screen == SCREEN_S2_INFO) {
            if (k & (HidNpadButton_B | HidNpadButton_Plus)) {
                screen = SCREEN_PICKER;
            } else if (k & HidNpadButton_A) {
                screen = SCREEN_S2_PROGRESS;
            }
            if (screen == SCREEN_S2_INFO) ui_draw_s2_info();

        } else if (screen == SCREEN_UPD_CONFIRM) {
            if (k & HidNpadButton_A) {
                screen = SCREEN_UPD_PROGRESS;
            } else if (k & (HidNpadButton_B | HidNpadButton_Plus)) {
                screen = SCREEN_PICKER;
            }
            if (screen == SCREEN_UPD_CONFIRM) ui_draw_upd_confirm(upd.maj, upd.min, upd.patch);

        } else if (screen == SCREEN_S2_PROGRESS) {
            ui_draw_progress(lang_str(STR_STATUS_DOWNLOAD_SCHEDULE));
            svcSleepThread(150000000ULL);
            waitBootNet();
            socketInitializeDefault();
            Result sslrc = sslInitialize(4);
            nextendo_bcat_result res = R_SUCCEEDED(sslrc) ? nextendo_bcat_install_s2() : NB_NET_FAIL;
            if (R_SUCCEEDED(sslrc)) sslExit();
            socketExit();
            rOk = (res == NB_OK);
            switch (res) {
                case NB_OK:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_SCHEDULE_OK));
                    snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_SCHEDULE_OK_DESC));
                    break;
                case NB_NO_SCHEDULE:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_NO_SCHEDULE));
                    snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_NO_SCHEDULE_DESC));
                    break;
                case NB_MOUNT_FAIL:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_MOUNT_FAIL));
                    snprintf(rMsg, sizeof(rMsg), "%s (rc=0x%x)", lang_str(STR_STATUS_MOUNT_FAIL_DESC), g_last_rc);
                    break;
                case NB_NET_CONNECT:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_NET_CONNECT));
                    snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_NET_CONNECT_DESC));
                    break;
                case NB_NET_TIMEOUT:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_NET_TIMEOUT));
                    snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_NET_TIMEOUT_DESC));
                    break;
                case NB_NET_HTTP_ERR:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_NET_HTTP_ERR));
                    snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_NET_HTTP_ERR_DESC));
                    break;
                case NB_NET_FAIL:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_NET_FAIL));
                    snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_NET_FAIL_DESC));
                    break;
                default:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_WRITE_FAIL));
                    snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_WRITE_FAIL_DESC));
                    break;
            }
            screen = SCREEN_S2_RESULT;

        } else if (screen == SCREEN_UPD_PROGRESS) {
            ui_draw_progress_bar(lang_str(STR_STATUS_DOWNLOAD_UPDATE), 0, NULL);
            svcSleepThread(150000000ULL);
            waitBootNet();
            s_updLastPct = -1;   // a 2nd attempt must restart from zero, not from the last %
            s_updLastPhase = NUP_PHASE_DOWNLOAD;
            nextendo_update_result res = nextendo_update_apply(upd.size, updateProgress);
            rOk = (res == NUP_OK);
            switch (res) {
                case NUP_OK: {
                    upd.available = false;   // done: drop the banner
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_UPDATE_OK));
                    // Format comes from lang_str: developer-controlled, therefore safe.
                    char updFmt[64];
                    strncpy(updFmt, lang_str(STR_STATUS_UPDATE_OK_DESC), sizeof(updFmt) - 1);
                    updFmt[sizeof(updFmt) - 1] = '\0';
                    snprintf(rMsg, sizeof(rMsg), updFmt, upd.maj, upd.min, upd.patch);
                    break;
                }
                case NUP_SIZE_FAIL:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_UPDATE_SIZE_FAIL));
                    snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_UPDATE_SIZE_FAIL_DESC));
                    break;
                case NUP_WRITE_FAIL:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_UPDATE_WRITE_FAIL));
                    snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_UPDATE_WRITE_FAIL_DESC));
                    break;
                default:
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_UPDATE_NET_FAIL));
                    snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_UPDATE_NET_FAIL_DESC));
                    break;
            }
            screen = SCREEN_UPD_RESULT;

        } else if (screen == SCREEN_FLAG_MENU) {
            if (UI_TAP_KIND(tap) == UI_TAP_ROW) {
                int idx = flagScroll + UI_TAP_INDEX(tap);
                if (idx >= 0 && idx < FLAG_COUNT) flagSel = idx;
            }
            if (k & (HidNpadButton_B | HidNpadButton_Plus)) {
                screen = SCREEN_PICKER;
            } else if (k & HidNpadButton_A) {
                screen = SCREEN_FLAG_PROGRESS;
            } else {
                // AnyUp/AnyDown, not Up/Down: the latter ignore the stick. L and R jump a page through the 110 countries.
                int avant = flagSel;
                if (k & HidNpadButton_AnyUp)   flagSel--;
                if (k & HidNpadButton_AnyDown) flagSel++;
                if (k & HidNpadButton_L)       flagSel -= FLAG_ROWS;
                if (k & HidNpadButton_R)       flagSel += FLAG_ROWS;
                if (flagSel < 0)               flagSel = 0;
                if (flagSel > FLAG_COUNT - 1)  flagSel = FLAG_COUNT - 1;
                if (flagSel != avant) {
                    // Scrolling follows the selection, without ever leaving the list.
                    if (flagSel < flagScroll) flagScroll = flagSel;
                    if (flagSel >= flagScroll + FLAG_ROWS) flagScroll = flagSel - (FLAG_ROWS - 1);
                    if (flagScroll < 0) flagScroll = 0;
                }
            }
            if (screen == SCREEN_FLAG_MENU)
                ui_draw_flag_menu(flagSel, flagScroll, flagCurrent);

        } else if (screen == SCREEN_FLAG_PROGRESS) {
            ui_draw_progress(lang_str(STR_STATUS_DOWNLOAD_FLAG));
            svcSleepThread(150000000ULL);
            waitBootNet();
            socketInitializeDefault();
            Result sslrc = sslInitialize(4);
            int frc = R_SUCCEEDED(sslrc) ? flag_install(g_flags[flagSel].code) : -1;
            if (R_SUCCEEDED(sslrc)) sslExit();
            socketExit();
            rOk = (frc == 0);
            if (frc == 0) {
                flagCurrent[0] = g_flags[flagSel].code[0];
                flagCurrent[1] = g_flags[flagSel].code[1];
                flagCurrent[2] = '\0';
                snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_FLAG_OK));
                snprintf(rMsg,   sizeof(rMsg),   "%s", lang_str(STR_STATUS_FLAG_OK_DESC));
            } else if (frc == -2) {
                snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_FLAG_WRITE_FAIL));
                snprintf(rMsg,   sizeof(rMsg),   "%s", lang_str(STR_STATUS_FLAG_WRITE_FAIL_DESC));
            } else {
                snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_FLAG_NET_FAIL));
                snprintf(rMsg,   sizeof(rMsg),   "%s", lang_str(STR_STATUS_FLAG_NET_FAIL_DESC));
            }
            screen = SCREEN_FLAG_RESULT;

        } else { // SCREEN_S2_RESULT / SCREEN_UPD_RESULT / SCREEN_FLAG_RESULT / SCREEN_SSBU_RESULT (fallback)
            if (k & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))
                screen = (screen == SCREEN_FLAG_RESULT) ? SCREEN_FLAG_MENU
                                                        : SCREEN_PICKER;
            ui_draw_result(rTitle, rMsg, rOk);
        }
    }

    ui_exit();
    // The thread must finish before we leave: it still traces during the diagnostics.
    // Waiting on `done` is no longer enough: it is published BEFORE them, not at thread end.
    if (bootThreadOn) { threadWaitForExit(&s_bootThread); threadClose(&s_bootThread); }
    if (!bootPublished && s_boot.done) upd = s_boot.upd;   // the log must see the real state
    writeExitLog(screen, rTitle, rMsg, rOk, boot, noEmummc, current, &upd);
    audio_exit();
    romfsExit();
    return 0;
}

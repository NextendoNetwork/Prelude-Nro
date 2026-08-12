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
//  Nextendo .nro — point d'entree.
//  Ecran principal :
//    [ NEXTENDO ]   [ NINTENDO ]           <- bascule de mode (A = appliquer + REDEMARRER)
//    [ Splatoon 2 - Installer le planning ] <- installe le byaml BCAT de S2 (sans redemarrage)
//  Navigation : < > pour passer de la paire de modes a la barre S2.
//  Au lancement : verif de mise a jour -> bandeau discret + Y pour installer.
// ============================================================
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <netdb.h>

#include "ui.h"
#include "audio.h"
#include "nextendo_apply.h"
#include "nextendo_config.h"
#include <turbojpeg.h>
#include <stdlib.h>
#include "nextendo_bcat.h"
#include "nextendo_update.h"
#include "nextendo_flag.h"
#include "ui_theme.h"
#include "lang.h"

enum {
    SCREEN_PICKER, SCREEN_S2_INFO, SCREEN_S2_PROGRESS, SCREEN_S2_RESULT,
    SCREEN_UPD_CONFIRM, SCREEN_UPD_PROGRESS, SCREEN_UPD_RESULT,
    SCREEN_LANG,
    SCREEN_FLAG_MENU, SCREEN_FLAG_PROGRESS, SCREEN_FLAG_RESULT,
    SCREEN_SSBU_MOD, SCREEN_SSBU_RESULT
};

// --- Log de sortie : consolide l'etat de la session dans sdmc:/prelude_exit.log ---
// Ecrit au moment de quitter l'appli (apres la boucle principale) : resume du contexte
// (build, boot, mode, IP, MAJ) + le resultat du dernier ecran + le contenu integral de
// prelude_trace.txt et de nextendo_bcat.log. C'est le point d'entree unique du debug :
// si un joueur signale "Fallo la descarga", ce fichier dit exactement ou ca a casse.
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

// --- Easter egg: 10% chance video on startup ---
static void easteregg_video(void) {
    Framebuffer *fb = ui_get_fb();
    if (!fb) return;
    tjhandle tj = tjInitDecompress();
    if (!tj) return;

    audio_egg_play();

    int w = 320, h = 180;
    int fbW = 1280, fbH = 720;
    PadState pad; padInitializeDefault(&pad);

    for (int loops = 0; loops < 2; loops++) {
        for (int frame = 1; frame <= 9999; frame++) {
            padUpdate(&pad);
            if (padGetButtons(&pad) & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))
                goto ee_done;
            char path[64];
            snprintf(path, sizeof(path), "romfs:/easteregg/frame_%04d.jpg", frame);
            FILE *f = fopen(path, "rb");
            if (!f) break;
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            unsigned char *jbuf = malloc(sz);
            if (!jbuf) { fclose(f); break; }
            fread(jbuf, 1, sz, f); fclose(f);

            u32 *buf = (u32*)framebufferBegin(fb, NULL);
            if (!buf) { free(jbuf); break; }
            unsigned char *rgb = malloc(w * h * 3);
            if (rgb && tjDecompress2(tj, jbuf, sz, rgb, w, 0, h, TJPF_RGB, TJFLAG_FASTDCT) == 0) {
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) {
                        int si = (y * w + x) * 3;
                        buf[y * fbW + x] = 0xFF000000 | (rgb[si] << 16) | (rgb[si+1] << 8) | rgb[si+2];
                    }
            }
            free(rgb); free(jbuf);
            framebufferEnd(fb);
            svcSleepThread(100000000ULL / 10);
        }
    }
ee_done:
    audio_egg_stop();
    tjDestroy(tj);
}

int main(int argc, char **argv) {
    romfsInit();
    audio_init();
    lang_init();

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    if (!ui_init()) {
        audio_exit();
        romfsExit();
        return 1;
    }

    // La trace repart de zero a chaque lancement : elle documente LA session courante (celle dont
    // l'utilisateur nous parle) et ne grossit pas indefiniment. Conservee en release : le gel du
    // build 10 n'a jamais ete explique, donc si un joueur le revit, ce fichier est notre seul temoin.
    remove(NEXTENDO_TRACE_PATH);
    nextendo_trace("10 main: ui_init ok");

    if ((rand() % 10) == 0) easteregg_video();

    // Une console sans emuMMC fait tourner le CFW sur la memoire interne, donc avec son vrai
    // identifiant : blank_prodinfo_emummc, la protection posee par le mode NINTENDO, n'a alors
    // aucun effet. On previent au lieu de laisser croire a une protection inexistante.
    // Detecte UNE fois (splInitialize/splExit), pas a chaque frame.
    nextendo_trace("11 avant detect_boot (splInitialize)");
    BootType boot = nextendo_detect_boot();
    nextendo_trace(boot == BOOT_SYSMMC  ? "12 detect_boot = SYSMMC (pas d emuMMC)"
                 : boot == BOOT_EMUMMC  ? "12 detect_boot = EMUMMC"
                                        : "12 detect_boot = INCONNU (spl a echoue)");
    bool noEmummc = (boot == BOOT_SYSMMC);

    int  current = nextendo_current_mode();
    int  sel    = (current == CHOICE_NEXTENDO) ? CHOICE_NINTENDO : CHOICE_NEXTENDO;
    int  focus  = FOCUS_MODE;
    int  screen = SCREEN_PICKER;
    int  state  = 0;
    int  langSel = g_lang;
    char status[160] = {0};
    char rTitle[64] = {0}, rMsg[192] = {0};
    bool rOk = false;

    // Flag state
    int  flagSel    = 0;
    int  flagScroll = 0;
    char flagCurrent[3] = {0};
    flag_detect_current(flagCurrent);

    // SSBU mod state
    bool ssbuInstalled = nextendo_ssbu_is_installed();

    // Séquence ↑↓←→ pour basculer l'IP du serveur.
    enum { SEQ_IDLE, SEQ_UP, SEQ_UP_DOWN, SEQ_UP_DOWN_LEFT };
    int seqState = SEQ_IDLE;
    int toastFrames = 0;  // frames restantes d'affichage du toast

    // Verif de mise a jour au demarrage (affiche d'abord le picker pour ne pas rester noir).
    ui_draw_loading("Cargando...");
    nextendo_trace("13 loading, antes de update_check (reseau)");
    NextendoUpdate upd = nextendo_update_check();
    nextendo_trace(upd.available ? "14 update_check: MAJ DISPO -> homebrew verrouille (A inactif)"
                                 : "14 update_check: a jour -> A actif");
    // Diagnostic reseau : nncs2 + etat hosts (trace pour 2123-0011 / 2810-1224).
    ui_draw_loading("Verificando conexion...");
    socketInitializeDefault();
    nextendo_diag_network();
    // DNS warmup : Atmosphere's DNS-MITM is lazy-loaded (reads hosts on first DNS query).
    // nnAccount linking fails if DNS-MITM hasn't loaded when it resolves accounts.nintendo.com.
    // Clover's workaround (BrowseNX from DBI title override) confirms any DNS query forces init;
    // we do it here so linking works without user workarounds.
    if (current == CHOICE_NEXTENDO) {
        ui_draw_loading("Inicializando DNS...");
        struct hostent *he = gethostbyname("accounts.nintendo.com");
        nextendo_trace(he ? "15a dns warmup: accounts.nintendo.com OK"
                          : "15a dns warmup: accounts.nintendo.com FAIL");
    }
    socketExit();
    nextendo_trace("15 entree dans la boucle principale");

    bool tracedLoop = false, tracedConfirm = false;
    while (appletMainLoop()) {
        consoleUpdate(NULL);
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);
        // Une seule fois : prouve que la boucle tourne ET que l'entree remonte (si A ne fait rien
        // alors que cette ligne est absente, c'est padUpdate/HID qui est mort, pas la logique).
        if (!tracedLoop && k) { nextendo_trace("16 premiere touche detectee dans la boucle"); tracedLoop = true; }

        // --- Séquence ↑↓←→ : bascule l'IP du serveur ---
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
                toastFrames = 120;  // ~2 secondes à 60fps
                seqState = SEQ_IDLE;
            } else if (k) {
                seqState = SEQ_IDLE;
            }
        } else {
            seqState = SEQ_IDLE;
        }

        if (screen == SCREEN_PICKER) {
            if (state == 0) {
                if (k & (HidNpadButton_B | HidNpadButton_Plus)) break;
                if (k & HidNpadButton_R) { screen = SCREEN_LANG; langSel = g_lang; }
                if (k & HidNpadButton_L) { screen = SCREEN_SSBU_MOD; }
                if (upd.available) {
                    // MAJ OBLIGATOIRE : tant qu'une version plus recente existe, le homebrew
                    // est verrouille -> seules l'installation (Y) et la sortie (+/B) sont possibles.
                    if (k & HidNpadButton_Y) { screen = SCREEN_UPD_CONFIRM; }
                } else {
                    if (k & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
                        focus = (focus == FOCUS_MODE) ? FOCUS_S2
                              : (focus == FOCUS_S2)   ? FOCUS_FLAG
                                                      : FOCUS_MODE;
                        status[0] = 0;
                    }
                    if (k & HidNpadButton_A) {
                        if (focus == FOCUS_MODE) { nextendo_trace("17 A picker -> ecran de confirmation"); state = 1; status[0] = 0; }
                        else if (focus == FOCUS_S2)   { screen = SCREEN_S2_INFO; }
                        else                          { screen = SCREEN_FLAG_MENU; }
                    }
                }
                if (screen == SCREEN_PICKER && state == 0)
                    ui_draw_picker(sel, current, focus, status[0] ? status : NULL,
                                   upd.available ? upd.maj : 0,
                                   upd.available ? upd.min : 0,
                                   upd.available ? upd.patch : 0,
                                   flagCurrent);

                // Toast du serveur
                if (toastFrames > 0) {
                    ui_draw_toast(server_display_name());
                    toastFrames--;
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
                        ui_draw_picker(sel, current, focus, status,
                                       upd.available ? upd.maj : 0,
                                       upd.available ? upd.min : 0,
                                       upd.available ? upd.patch : 0,
                                       flagCurrent);
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

        } else if (screen == SCREEN_S2_INFO) {
            if (k & (HidNpadButton_B | HidNpadButton_Plus)) {
                screen = SCREEN_PICKER;
            } else if (k & HidNpadButton_A) {
                screen = SCREEN_S2_PROGRESS;
            }
            if (screen == SCREEN_S2_INFO) ui_draw_s2_info();

        } else if (screen == SCREEN_LANG) {
            if (k & HidNpadButton_B) {
                screen = SCREEN_PICKER;
            } else if (k & HidNpadButton_A) {
                if (langSel != g_lang) {
                    g_lang = langSel;
                    lang_save();
                }
                screen = SCREEN_PICKER;
            } else if (k & HidNpadButton_Up) {
                if (langSel > 0) langSel--;
            } else if (k & HidNpadButton_Down) {
                if (langSel < 3) langSel++;
            }
            if (screen == SCREEN_LANG) ui_draw_lang_menu(langSel);

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
            ui_draw_progress(lang_str(STR_STATUS_DOWNLOAD_UPDATE));
            svcSleepThread(150000000ULL);
            nextendo_update_result res = nextendo_update_apply(upd.size);
            rOk = (res == NUP_OK);
            switch (res) {
                case NUP_OK: {
                    upd.available = false;   // faite : on retire le bandeau
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_UPDATE_OK));
                    // lang_str fournit un format avec %%d — controlee par le developpeur, safe
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
            if (k & (HidNpadButton_B | HidNpadButton_Plus)) {
                screen = SCREEN_PICKER;
            } else if (k & HidNpadButton_A) {
                screen = SCREEN_FLAG_PROGRESS;
            } else if (k & HidNpadButton_Up) {
                if (flagSel > 0) {
                    flagSel--;
                    if (flagSel < flagScroll) flagScroll = flagSel;
                }
            } else if (k & HidNpadButton_Down) {
                if (flagSel < FLAG_COUNT - 1) {
                    flagSel++;
                    if (flagSel >= flagScroll + FLAG_ROWS) flagScroll = flagSel - (FLAG_ROWS - 1);
                }
            }
            if (screen == SCREEN_FLAG_MENU)
                ui_draw_flag_menu(flagSel, flagScroll, flagCurrent);

        } else if (screen == SCREEN_FLAG_PROGRESS) {
            ui_draw_progress(lang_str(STR_STATUS_DOWNLOAD_FLAG));
            svcSleepThread(150000000ULL);
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

        } else if (screen == SCREEN_SSBU_MOD) {
            if (k & (HidNpadButton_B | HidNpadButton_Plus)) {
                screen = SCREEN_PICKER;
            } else if (k & HidNpadButton_A) {
                if (ssbuInstalled) {
                    nextendo_ssbu_remove();
                    ssbuInstalled = false;
                    rOk = true;
                    snprintf(rTitle, sizeof(rTitle), "Mod desinstalado");
                    snprintf(rMsg,   sizeof(rMsg),   "SSBU Online Deluxe eliminado de la SD.");
                } else {
                    bool ok = nextendo_ssbu_install();
                    ssbuInstalled = ok;
                    rOk = ok;
                    if (ok) {
                        snprintf(rTitle, sizeof(rTitle), "Mod instalado");
                        snprintf(rMsg,   sizeof(rMsg),   "SSBU Online Deluxe instalado. Reinicia en modo Nextendo para activarlo.");
                    } else {
                        snprintf(rTitle, sizeof(rTitle), "Error al instalar");
                        snprintf(rMsg,   sizeof(rMsg),   "No se pudo copiar el mod a la SD.");
                    }
                }
                screen = SCREEN_SSBU_RESULT;
            }
            if (screen == SCREEN_SSBU_MOD) ui_draw_ssbu_mod(ssbuInstalled);

        } else { // SCREEN_S2_RESULT / SCREEN_UPD_RESULT / SCREEN_FLAG_RESULT / SCREEN_SSBU_RESULT (fallback)
            if (k & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))
                screen = (screen == SCREEN_FLAG_RESULT) ? SCREEN_FLAG_MENU
                       : (screen == SCREEN_SSBU_RESULT) ? SCREEN_SSBU_MOD
                                                        : SCREEN_PICKER;
            ui_draw_result(rTitle, rMsg, rOk);
        }
    }

    ui_exit();
    writeExitLog(screen, rTitle, rMsg, rOk, boot, noEmummc, current, &upd);
    audio_exit();
    romfsExit();
    return 0;
}

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

// Point d'entree : ecran a deux colonnes (rail de sections + panneau), verif de MAJ au lancement.
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
    // Ne restent modaux que confirmation / progression / resultat, et la liste de 110 pays, trop longue pour un panneau.
};

// Log de sortie (sdmc:/prelude_exit.log) : contexte + dernier ecran + trace et log BCAT integraux. Point d'entree unique du debug.
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

// Appele sur le hilo principal, donc on dessine directement. On ne redessine qu'au changement de pourcentage : sinon 17 Mo = ~550 presentations calees sur le vsync.
static int s_updLastPct = -1;
static nextendo_update_phase s_updLastPhase = NUP_PHASE_DOWNLOAD;

static void updateProgress(nextendo_update_phase phase, long done, long total) {
    int pct = (total > 0) ? (int)((done * 100) / total) : 0;
    if (pct > 100) pct = 100;
    if (pct == s_updLastPct && phase == s_updLastPhase) return;
    s_updLastPct   = pct;
    s_updLastPhase = phase;

    // Dixiemes de Mio en entier : pas de flottant pour une ligne d'etat.
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

// Travail reseau du demarrage, hors du hilo principal : il durait plusieurs secondes et bloquait le rendu.
// `done` est le seul rendez-vous : le thread ecrit `upd` PUIS pose done=1, et la barriere garantit que main ne voit pas un `upd` a moitie ecrit.
// Le verrou de MAJ obligatoire en depend : tant que done vaut 0, on IGNORE s'il existe une version plus recente.
static struct {
    NextendoUpdate  upd;
    int             mode;
    volatile bool   done;
} s_boot;

static Thread s_bootThread;

static void bootWorker(void *arg) {
    (void)arg;
    NextendoUpdate u = nextendo_update_check();
    nextendo_trace(u.available ? "14 update_check: MAJ DISPO -> homebrew verrouille (A inactif)"
                               : "14 update_check: a jour -> A actif");

    // Diagnostic reseau : nncs2 + etat hosts (trace pour 2123-0011 / 2810-1224).
    socketInitializeDefault();
    nextendo_diag_network();
    // DNS-MITM d'Atmosphere est charge paresseusement : sans cette requete d'amorcage, l'association nnAccount echoue.
    if (s_boot.mode == CHOICE_NEXTENDO) {
        struct hostent *he = gethostbyname("accounts.nintendo.com");
        nextendo_trace(he ? "15a dns warmup: accounts.nintendo.com OK"
                          : "15a dns warmup: accounts.nintendo.com FAIL");
    }
    socketExit();

    s_boot.upd  = u;
    __asm__ __volatile__("dmb ish" ::: "memory");  // upd visible AVANT done
    s_boot.done = true;
}

int main(int argc, char **argv) {
    // argv[0] : l'updater doit remplacer CE fichier, sinon la MAJ se depose a cote et l'ancienne version se relance indefiniment.
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

    // La trace repart de zero a chaque lancement : elle documente LA session dont l'utilisateur nous parle, et ne grossit pas.
    remove(NEXTENDO_TRACE_PATH);
    nextendo_trace("10 main: ui_init ok");

    // Sans emuMMC, blank_prodinfo_emummc n'a aucun effet : on previent au lieu de laisser croire a une protection inexistante. Detecte UNE fois.
    nextendo_trace("11 avant detect_boot (splInitialize)");
    BootType boot = nextendo_detect_boot();
    nextendo_trace(boot == BOOT_SYSMMC  ? "12 detect_boot = SYSMMC (pas d emuMMC)"
                 : boot == BOOT_EMUMMC  ? "12 detect_boot = EMUMMC"
                                        : "12 detect_boot = INCONNU (spl a echoue)");
    bool noEmummc = (boot == BOOT_SYSMMC);

    int  current = nextendo_current_mode();
    // Sans ca, mettre Prelude a jour laissait sur la carte les correctifs de la version precedente. Rien n'est touche en mode Nintendo.
    if (current == CHOICE_NEXTENDO) nextendo_provision_all_public();

    NextendoS3Status s3;
    nextendo_s3_status(&s3);
    int  sel    = (current == CHOICE_NEXTENDO) ? CHOICE_NINTENDO : CHOICE_NEXTENDO;
    int  railSel = RAIL_MODE;   // section du rail
    int  paneSel = 0;           // ligne du panneau
    bool paneFocus = false;     // false = les fleches agissent sur le rail
    int  screen = SCREEN_PICKER;
    // Seule occasion de sauvegarder les hosts d'AVANT nous : des qu'un mode est applique, les fichiers d'origine sont ecrases.
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
    // Lu depuis la SD (presence de boot2.flag), pas suppose : le joueur a pu le couper a un lancement precedent.
    bool ssbuOcDisabled = nextendo_ssbu_oc_is_disabled();

    // Séquence ↑↓←→ pour basculer l'IP du serveur.
    enum { SEQ_IDLE, SEQ_UP, SEQ_UP_DOWN, SEQ_UP_DOWN_LEFT };
    int seqState = SEQ_IDLE;
    bool touchHeld = false;   // front montant : un doigt pose ne vaut qu'une fois

    // Dans un thread : le picker s'affiche immediatement et reste navigable pendant ce temps.
    s_boot.mode = current;
    nextendo_trace("13 demarrage du thread reseau (picker deja affiche)");
    bool bootThreadOn = false;
    if (R_SUCCEEDED(threadCreate(&s_bootThread, bootWorker, NULL, NULL, 0x20000, 0x2C, -2))
        && R_SUCCEEDED(threadStart(&s_bootThread))) {
        bootThreadOn = true;
    } else {
        // Repli synchrone plutot que de laisser upd non renseigne : le verrou de MAJ obligatoire en depend.
        nextendo_trace("13b threadCreate KO -> repli synchrone");
        bootWorker(NULL);
    }
    NextendoUpdate upd = (NextendoUpdate){0};
    bool bootPublished = false;   // upd publie une seule fois (le succes d'une MAJ remet available=0)
    nextendo_trace("15 entree dans la boucle principale");

    bool tracedLoop = false, tracedConfirm = false;
    while (appletMainLoop()) {
        consoleUpdate(NULL);
        // One-shot : une MAJ reussie remet upd.available a 0, la copie ne doit pas ressusciter le bandeau a la frame suivante.
        if (!bootPublished && s_boot.done) { upd = s_boot.upd; bootPublished = true; }
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);

        // Le tag vise la DERNIERE frame dessinee, c'est-a-dire exactement ce que l'utilisateur voit.
        int tap = UI_TAP_NONE;
        HidTouchScreenState ts = {0};
        if (hidGetTouchScreenStates(&ts, 1) && ts.count > 0) {
            if (!touchHeld) tap = ui_tap_at((int)ts.touches[0].x, (int)ts.touches[0].y);
            touchHeld = true;
        } else {
            touchHeld = false;
        }
        // La barre de boutons declenche la touche qu'elle affiche, les modales leur moitie A / B.
        // Le reste (rail, lignes, bandeau) depend de l'ecran et se traite dans sa branche.
        if (UI_TAP_KIND(tap) == UI_TAP_BTN) {
            switch (UI_TAP_INDEX(tap)) {
            case UI_BTN_A:    k |= HidNpadButton_A;    break;
            case UI_BTN_B:    k |= HidNpadButton_B;    break;
            case UI_BTN_Y:    k |= HidNpadButton_Y;    break;
            case UI_BTN_PLUS: k |= HidNpadButton_Plus; break;
            }
        } else if (tap == UI_TAP_YES) k |= HidNpadButton_A;
        else if (tap == UI_TAP_NO)    k |= HidNpadButton_B;
        // Prouve que la boucle tourne ET que l'entree remonte : absente, c'est padUpdate/HID qui est mort, pas la logique.
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
                // B ne quitte que depuis le rail : dans le panneau il revient en arriere, et quitter sur un retour serait un piege.
                if (k & HidNpadButton_Plus) break;
                if ((k & HidNpadButton_B) && !paneFocus) break;

                // Seule une touche qui va CONSULTER upd attend le thread reseau : la navigation, elle, n'attend rien.
                if (k && !bootPublished) {
                    ui_draw_loading(lang_str(STR_CHECKING_UPDATE));
                    while (!s_boot.done) svcSleepThread(10000000ULL);  // 10 ms
                    upd = s_boot.upd;
                    bootPublished = true;
                }

                // Taper une section y va ; taper une ligne la choisit ET l'active, comme sur
                // l'ecran d'accueil de la console. Le verrou de MAJ ne laisse passer que le bandeau.
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
                    // MAJ OBLIGATOIRE : tout est verrouille sauf l'installation (Y) et la sortie (+/B).
                    if (k & HidNpadButton_Y) { screen = SCREEN_UPD_CONFIRM; }
                } else if (!paneFocus) {
                    // Colonne de gauche : on parcourt les sections.
                    if (k & HidNpadButton_AnyUp)   { railSel = (railSel + RAIL_N - 1) % RAIL_N; status[0] = 0; }
                    if (k & HidNpadButton_AnyDown) { railSel = (railSel + 1) % RAIL_N;          status[0] = 0; }
                    // Droite ET A entrent dans le panneau : la fleche est le geste attendu, A ne doit pas rester sans effet ici.
                    if (k & (HidNpadButton_AnyRight | HidNpadButton_A)) {
                        paneFocus = true;
                        paneSel = 0;
                        // On entre sur le mode NON courant : c'est celui vers lequel on peut basculer.
                        if (railSel == RAIL_MODE)
                            paneSel = (current == CHOICE_NEXTENDO) ? CHOICE_NINTENDO : CHOICE_NEXTENDO;
                    }
                } else {
                    // Colonne de droite : on parcourt les lignes de la section.
                    int rows = ui_pane_rows(railSel, ssbuInstalled);
                    if (k & HidNpadButton_AnyUp)   paneSel = (paneSel + rows - 1) % rows;
                    if (k & HidNpadButton_AnyDown) paneSel = (paneSel + 1) % rows;
                    if (k & (HidNpadButton_AnyLeft | HidNpadButton_B)) paneFocus = false;

                    if (k & HidNpadButton_A) {
                        switch (railSel) {
                        case RAIL_MODE:
                            // paneSel EST le mode choisi.
                            sel = paneSel;
                            nextendo_trace("17 A picker -> ecran de confirmation");
                            state = 1; status[0] = 0;
                            break;
                        case RAIL_S3:
                            // Reecrit les fichiers du romfs sans bascule de mode complete. Interdit en mode Nintendo, qui retire la pile de certificats expres.
                            if (current != CHOICE_NINTENDO) {
                                nextendo_provision_all_public();
                                nextendo_s3_status(&s3);   // relire : le panneau doit refleter la carte
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
                                // Le nombre de lignes depend de ssbuInstalled : sans ce reborne, paneSel pointe une ligne disparue.
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
                    // Tant que le thread reseau n'a pas publie, l'ecran DIT qu'il verifie, sans attendre un appui.
                    if (!bootPublished)
                        ui_draw_loading(lang_str(STR_CHECKING_UPDATE));
                    else
                        ui_draw_picker(railSel, paneSel, paneFocus, current,
                                       status[0] ? status : NULL,
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
                // Sans copie, la seconde question n'a pas d'objet.
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
            s_updLastPct = -1;   // une 2e tentative doit repartir de zero, pas du dernier %
            s_updLastPhase = NUP_PHASE_DOWNLOAD;
            nextendo_update_result res = nextendo_update_apply(upd.size, updateProgress);
            rOk = (res == NUP_OK);
            switch (res) {
                case NUP_OK: {
                    upd.available = false;   // faite : on retire le bandeau
                    snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_UPDATE_OK));
                    // Format issu de lang_str : controle par le developpeur, donc sur.
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
                // AnyUp/AnyDown, pas Up/Down : ces dernieres ignorent le stick. L et R sautent une page dans les 110 pays.
                int avant = flagSel;
                if (k & HidNpadButton_AnyUp)   flagSel--;
                if (k & HidNpadButton_AnyDown) flagSel++;
                if (k & HidNpadButton_L)       flagSel -= FLAG_ROWS;
                if (k & HidNpadButton_R)       flagSel += FLAG_ROWS;
                if (flagSel < 0)               flagSel = 0;
                if (flagSel > FLAG_COUNT - 1)  flagSel = FLAG_COUNT - 1;
                if (flagSel != avant) {
                    // Le defilement suit la selection, sans jamais sortir de la liste.
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
    // Le thread reseau doit etre fini avant qu'on parte : il ecrit s_boot et trace.
    if (bootThreadOn) { while (!s_boot.done) svcSleepThread(10000000ULL);
                        threadWaitForExit(&s_bootThread); threadClose(&s_bootThread); }
    if (!bootPublished && s_boot.done) upd = s_boot.upd;   // le log doit voir le vrai etat
    writeExitLog(screen, rTitle, rMsg, rOk, boot, noEmummc, current, &upd);
    audio_exit();
    romfsExit();
    return 0;
}

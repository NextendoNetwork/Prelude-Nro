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
#include <stdlib.h>
#include "nextendo_bcat.h"
#include "nextendo_update.h"
#include "nextendo_flag.h"
#include "ui_theme.h"
#include "lang.h"
#include "nextendo_time.h"

enum {
    SCREEN_PICKER, SCREEN_S2_INFO, SCREEN_S2_PROGRESS, SCREEN_S2_RESULT,
    SCREEN_UPD_CONFIRM, SCREEN_UPD_PROGRESS, SCREEN_UPD_RESULT,
    SCREEN_FLAG_MENU, SCREEN_FLAG_PROGRESS, SCREEN_FLAG_RESULT,
    SCREEN_BACKUP_ASK, SCREEN_USEBAK_ASK,
    // Smash et Langue ne sont plus des ecrans : leur contenu vit dans le
    // panneau du rail. Ne restent modaux que confirmation / progression /
    // resultat, et la liste de 110 pays, trop longue pour un panneau.
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

// --- Travail reseau du demarrage, hors du hilo principal ---------------------
// Verif de MAJ + diagnostic reseau + warmup DNS prenaient plusieurs secondes en
// bloquant le rendu : trois ecrans "Cargando..." avant de voir quoi que ce soit.
// On les deporte ici pour que le picker s'affiche tout de suite.
//
// SYNCHRONISATION : `done` est le seul point de rendez-vous. Le thread ecrit
// `upd` PUIS pose done=1 ; main ne lit `upd` qu'apres avoir vu done=1. La barriere
// est ce qui garantit que main ne voit pas un `upd` a moitie ecrit.
//
// Le verrou de MAJ OBLIGATOIRE en depend : tant que done vaut 0 on ne SAIT PAS
// s'il existe une version plus recente, donc toute action qui consulte `upd` doit
// attendre (cf. le rendez-vous sur bootPublished dans la boucle). Sans ca, un
// joueur assez rapide appliquerait un changement de mode pendant la fenetre ou
// upd.available est encore faux par IGNORANCE — le verrou qu on ne veut pas ouvrir.
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
    // DNS warmup : Atmosphere's DNS-MITM is lazy-loaded (reads hosts on first DNS query).
    // nnAccount linking fails if DNS-MITM hasn't loaded when it resolves accounts.nintendo.com.
    // Clover's workaround (BrowseNX from DBI title override) confirms any DNS query forces init;
    // we do it here so linking works without user workarounds.
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
    // hbmenu passe le chemin du .nro lance dans argv[0]. L'updater doit remplacer CE
    // fichier : sans ca il ecrivait a un emplacement fixe, la mise a jour se deposait a
    // cote, et l'utilisateur relancait indefiniment l'ancienne version.
    nextendo_update_set_self_path((argc > 0 && argv) ? argv[0] : NULL);

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
    int  railSel = RAIL_MODE;   // section du rail
    int  paneSel = 0;           // ligne du panneau
    bool paneFocus = false;     // false = les fleches agissent sur le rail
    int  screen = SCREEN_PICKER;
    // UNE SEULE FOIS, au tout premier lancement : on propose de sauvegarder les hosts
    // dns.mitm que l'utilisateur avait AVANT nous. C'est la SEULE occasion de le faire —
    // des qu'un mode est applique, les fichiers d'origine sont ecrases et il n'y a plus
    // rien a sauver. Une fois la question posee, elle ne revient jamais.
    if (nextendo_backup_prompt_needed()) screen = SCREEN_BACKUP_ASK;
    int  state  = 0;
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
    // Overclock embarque du mod : lu depuis la SD (presence de boot2.flag), pas
    // suppose — le joueur a pu le couper a un lancement precedent de Prelude.
    bool ssbuOcDisabled = nextendo_ssbu_oc_is_disabled();

    // Séquence ↑↓←→ pour basculer l'IP du serveur.
    enum { SEQ_IDLE, SEQ_UP, SEQ_UP_DOWN, SEQ_UP_DOWN_LEFT };
    int seqState = SEQ_IDLE;
    int toastFrames = 0;  // frames restantes d'affichage du toast

    // Travail reseau du demarrage, DANS UN THREAD : il durait plusieurs secondes et
    // bloquait le hilo principal, d'ou les trois ecrans "Cargando..." successifs.
    // Le picker s'affiche maintenant immediatement et reste navigable pendant ce temps.
    s_boot.mode = current;
    nextendo_trace("13 demarrage du thread reseau (picker deja affiche)");
    bool bootThreadOn = false;
    if (R_SUCCEEDED(threadCreate(&s_bootThread, bootWorker, NULL, NULL, 0x20000, 0x2C, -2))
        && R_SUCCEEDED(threadStart(&s_bootThread))) {
        bootThreadOn = true;
    } else {
        // Pas de thread : on retombe sur l'ancien comportement synchrone plutot que
        // de laisser upd non renseigne — le verrou de MAJ obligatoire en depend.
        nextendo_trace("13b threadCreate KO -> repli synchrone");
        bootWorker(NULL);
    }
    NextendoUpdate upd = (NextendoUpdate){0};
    bool bootPublished = false;   // upd publie une seule fois (le succes d'une MAJ remet available=0)
    nextendo_trace("15 entree dans la boucle principale");

    bool tracedLoop = false, tracedConfirm = false;
    while (appletMainLoop()) {
        consoleUpdate(NULL);
        // Publication passive : des que le thread reseau a fini, le bandeau de MAJ
        // apparait de lui-meme, sans que l'utilisateur ait a toucher quoi que ce soit.
        // One-shot (bootPublished) : une MAJ reussie remet upd.available a 0 et il ne
        // faut pas que la copie ressuscite le bandeau a la frame suivante.
        if (!bootPublished && s_boot.done) { upd = s_boot.upd; bootPublished = true; }
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
                // + quitte toujours. B ne quitte que depuis le rail : dans le
                // panneau il sert a revenir en arriere, et quitter l app sur un
                // retour serait le piege classique de ce modele.
                if (k & HidNpadButton_Plus) break;
                if ((k & HidNpadButton_B) && !paneFocus) break;

                // Rendez-vous avec le thread reseau. La navigation (haut/bas/gauche/
                // droite, R, L) n'attend RIEN — c'est ce qui fait disparaitre l'ecran
                // de chargement. Seule une touche qui va CONSULTER upd attend, parce
                // que tant que done vaut 0 on ignore s'il existe une MAJ obligatoire.
                // En pratique le thread a fini bien avant qu'on arrive ici : l'attente
                // ne se voit que si le reseau rame, et la elle est justifiee.
                if (k && !bootPublished) {
                    ui_draw_loading("Verificando actualizacion...");
                    while (!s_boot.done) svcSleepThread(10000000ULL);  // 10 ms
                    upd = s_boot.upd;
                    bootPublished = true;
                }

                if (upd.available) {
                    // MAJ OBLIGATOIRE : tant qu'une version plus recente existe, le homebrew
                    // est verrouille -> seules l'installation (Y) et la sortie (+/B) sont possibles.
                    if (k & HidNpadButton_Y) { screen = SCREEN_UPD_CONFIRM; }
                } else if ((k & HidNpadButton_X) && railSel == RAIL_MODE) {
                    screen = SCREEN_TIME_PROGRESS;
                } else if (!paneFocus) {
                    // --- Colonne de gauche : on parcourt les sections ---
                    if (k & HidNpadButton_AnyUp)   { railSel = (railSel + RAIL_N - 1) % RAIL_N; status[0] = 0; }
                    if (k & HidNpadButton_AnyDown) { railSel = (railSel + 1) % RAIL_N;          status[0] = 0; }
                    // Droite ET A entrent dans le panneau : la fleche parce que c'est
                    // le geste attendu entre deux colonnes, A parce que c'est le bouton
                    // de validation et qu'on ne veut pas qu'il ne fasse rien ici.
                    if (k & (HidNpadButton_AnyRight | HidNpadButton_A)) {
                        paneFocus = true;
                        paneSel = 0;
                        // Sur MODE, on entre sur le mode NON courant : c'est celui vers
                        // lequel on peut basculer, donc le choix par defaut utile.
                        if (railSel == RAIL_MODE)
                            paneSel = (current == CHOICE_NEXTENDO) ? CHOICE_NINTENDO : CHOICE_NEXTENDO;
                    }
                } else {
                    // --- Colonne de droite : on parcourt les lignes de la section ---
                    int rows = ui_pane_rows(railSel, ssbuInstalled);
                    if (k & HidNpadButton_AnyUp)   paneSel = (paneSel + rows - 1) % rows;
                    if (k & HidNpadButton_AnyDown) paneSel = (paneSel + 1) % rows;
                    if (k & (HidNpadButton_AnyLeft | HidNpadButton_B)) paneFocus = false;

                    if (k & HidNpadButton_A) {
                        switch (railSel) {
                        case RAIL_MODE:
                            // paneSel EST le mode choisi : avant, sel etait fige au
                            // demarrage et l'utilisateur ne choisissait rien.
                            sel = paneSel;
                            nextendo_trace("17 A picker -> ecran de confirmation");
                            state = 1; status[0] = 0;
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
                                // Le nombre de lignes depend de ssbuInstalled : sans ce
                                // reborne, paneSel peut pointer une ligne disparue.
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
                if (screen == SCREEN_PICKER && state == 0)
                    ui_draw_picker(railSel, paneSel, paneFocus, current,
                                   status[0] ? status : NULL,
                                   upd.available ? upd.maj : 0,
                                   upd.available ? upd.min : 0,
                                   upd.available ? upd.patch : 0,
                                   flagCurrent, ssbuInstalled, ssbuOcDisabled);

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
                        ui_draw_picker(railSel, paneSel, paneFocus, current, status,
                                       upd.available ? upd.maj : 0,
                                       upd.available ? upd.min : 0,
                                       upd.available ? upd.patch : 0,
                                       flagCurrent, ssbuInstalled, ssbuOcDisabled);
                        svcSleepThread(1200000000ULL);
                        audio_exit();
                        Result rebrc = nextendo_reboot();
                        if (R_SUCCEEDED(rebrc)) {
                            while (appletMainLoop()) {
                                svcSleepThread(100000000ULL);
                            }
                        } else {
                            snprintf(status, sizeof(status), "%s", lang_str(STR_STATUS_REBOOT_FAIL));
                            state = 0;
                        }
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
                // Sans copie, la seconde question n'a pas d'objet : on ne demande pas
                // a l'utilisateur quoi faire d'un fichier qui n'existe pas.
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

        } else if (screen == SCREEN_TIME_PROGRESS) {
            ui_draw_progress(lang_str(STR_STATUS_SYNC_TIME));
            svcSleepThread(150000000ULL);
            Result syncrc = nextendo_time_sync();
            rOk = R_SUCCEEDED(syncrc);
            if (rOk) {
                snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_TIME_OK));
                snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_TIME_OK_DESC));
            } else {
                snprintf(rTitle, sizeof(rTitle), "%s", lang_str(STR_STATUS_TIME_FAIL));
                snprintf(rMsg, sizeof(rMsg), "%s", lang_str(STR_STATUS_TIME_FAIL_DESC));
            }
            screen = SCREEN_TIME_RESULT;

        } else { // SCREEN_S2_RESULT / SCREEN_UPD_RESULT / SCREEN_FLAG_RESULT / SCREEN_TIME_RESULT (fallback)
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

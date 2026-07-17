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
#include <switch.h>

#include "ui.h"
#include "audio.h"
#include "nextendo_apply.h"
#include "nextendo_bcat.h"
#include "nextendo_update.h"
#include "ui_theme.h"

enum {
    SCREEN_PICKER, SCREEN_S2_INFO, SCREEN_S2_PROGRESS, SCREEN_S2_RESULT,
    SCREEN_UPD_PROGRESS, SCREEN_UPD_RESULT
};

int main(int argc, char **argv) {
    romfsInit();
    audio_init();

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
    int  focus  = FOCUS_MODE;
    int  screen = SCREEN_PICKER;
    int  state  = 0;
    char status[160] = {0};
    char rTitle[64] = {0}, rMsg[192] = {0};
    bool rOk = false;

    // Verif de mise a jour au demarrage (affiche d'abord le picker pour ne pas rester noir).
    ui_draw_picker(sel, current, focus, NULL, 0);
    nextendo_trace("13 picker dessine, avant update_check (reseau)");
    NextendoUpdate upd = nextendo_update_check();
    nextendo_trace(upd.available ? "14 update_check: MAJ DISPO -> homebrew verrouille (A inactif)"
                                 : "14 update_check: a jour -> A actif");
    nextendo_trace("15 entree dans la boucle principale");

    bool tracedLoop = false, tracedConfirm = false;
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 k = padGetButtonsDown(&pad);
        // Une seule fois : prouve que la boucle tourne ET que l'entree remonte (si A ne fait rien
        // alors que cette ligne est absente, c'est padUpdate/HID qui est mort, pas la logique).
        if (!tracedLoop && k) { nextendo_trace("16 premiere touche detectee dans la boucle"); tracedLoop = true; }

        if (screen == SCREEN_PICKER) {
            if (state == 0) {
                if (k & (HidNpadButton_B | HidNpadButton_Plus)) break;
                if (upd.available) {
                    // MAJ OBLIGATOIRE : tant qu'une version plus recente existe, le homebrew
                    // est verrouille -> seules l'installation (Y) et la sortie (+/B) sont possibles.
                    if (k & HidNpadButton_Y) screen = SCREEN_UPD_PROGRESS;
                } else {
                    if (k & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
                        focus = (focus == FOCUS_MODE) ? FOCUS_S2 : FOCUS_MODE;
                        status[0] = 0;
                    }
                    if (k & HidNpadButton_A) {
                        if (focus == FOCUS_MODE) { nextendo_trace("17 A picker -> ecran de confirmation"); state = 1; status[0] = 0; }
                        else { screen = SCREEN_S2_INFO; }
                    }
                }
                if (screen == SCREEN_PICKER && state == 0)
                    ui_draw_picker(sel, current, focus, status[0] ? status : NULL,
                                   upd.available ? upd.latest : 0);
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
                        snprintf(status, sizeof(status),
                                 sel == CHOICE_NEXTENDO
                                     ? "Mode NEXTENDO applique  -  redemarrage..."
                                     : "Mode NINTENDO applique  -  redemarrage...");
                        ui_draw_picker(sel, current, focus, status, upd.available ? upd.latest : 0);
                        svcSleepThread(1200000000ULL);
                        audio_exit();
                        nextendo_reboot();
                        snprintf(status, sizeof(status), "Echec du redemarrage");
                        state = 0;
                    } else {
                        snprintf(status, sizeof(status), "ERREUR : ecriture sur la SD impossible");
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

        } else if (screen == SCREEN_S2_PROGRESS) {
            ui_draw_progress("Telechargement et installation du planning...");
            svcSleepThread(150000000ULL);
            socketInitializeDefault();
            nextendo_bcat_result res = nextendo_bcat_install_s2();
            socketExit();
            rOk = (res == NB_OK);
            switch (res) {
                case NB_OK:
                    snprintf(rTitle, sizeof(rTitle), "Planning installe");
                    snprintf(rMsg, sizeof(rMsg),
                             "Ancien planning remplace. Relance Splatoon 2 pour l'appliquer.");
                    break;
                case NB_NO_SCHEDULE:
                    snprintf(rTitle, sizeof(rTitle), "Aucun planning");
                    snprintf(rMsg, sizeof(rMsg), "Rien de publie pour le moment.");
                    break;
                case NB_MOUNT_FAIL:
                    snprintf(rTitle, sizeof(rTitle), "Sauvegarde introuvable");
                    snprintf(rMsg, sizeof(rMsg), "Lance Splatoon 2 une fois, puis reessaie.");
                    break;
                case NB_NET_FAIL:
                    snprintf(rTitle, sizeof(rTitle), "Echec reseau");
                    snprintf(rMsg, sizeof(rMsg),
                             "Telechargement impossible (verifie la connexion / le mode).");
                    break;
                default:
                    snprintf(rTitle, sizeof(rTitle), "Echec ecriture");
                    snprintf(rMsg, sizeof(rMsg), "Ecriture dans la sauvegarde impossible.");
                    break;
            }
            screen = SCREEN_S2_RESULT;

        } else if (screen == SCREEN_UPD_PROGRESS) {
            ui_draw_progress("Telechargement de la mise a jour...");
            svcSleepThread(150000000ULL);
            nextendo_update_result res = nextendo_update_apply(upd.size);
            rOk = (res == NUP_OK);
            switch (res) {
                case NUP_OK:
                    upd.available = false;   // faite : on retire le bandeau
                    snprintf(rTitle, sizeof(rTitle), "Mise a jour installee");
                    snprintf(rMsg, sizeof(rMsg),
                             "FERME et relance le homebrew Prelude pour appliquer la v%d.", upd.latest);
                    break;
                case NUP_SIZE_FAIL:
                    snprintf(rTitle, sizeof(rTitle), "Telechargement corrompu");
                    snprintf(rMsg, sizeof(rMsg), "Taille inattendue. Reessaie.");
                    break;
                case NUP_WRITE_FAIL:
                    snprintf(rTitle, sizeof(rTitle), "Ecriture impossible");
                    snprintf(rMsg, sizeof(rMsg), "Impossible d'ecrire sur la carte SD.");
                    break;
                default:
                    snprintf(rTitle, sizeof(rTitle), "Echec reseau");
                    snprintf(rMsg, sizeof(rMsg), "Telechargement impossible.");
                    break;
            }
            screen = SCREEN_UPD_RESULT;

        } else { // SCREEN_S2_RESULT / SCREEN_UPD_RESULT
            if (k & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Plus))
                screen = SCREEN_PICKER;
            ui_draw_result(rTitle, rMsg, rOk);
        }
    }

    ui_exit();
    audio_exit();
    romfsExit();
    return 0;
}

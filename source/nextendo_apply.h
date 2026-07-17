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
//  Nextendo .nro — logique systeme (ecrire hosts / editer l'ini / reboot)
// ============================================================
#ifndef NEXTENDO_APPLY_H
#define NEXTENDO_APPLY_H
#include <switch.h>

typedef enum { BOOT_UNKNOWN = -1, BOOT_SYSMMC = 0, BOOT_EMUMMC = 1 } BootType;

// Ecrit une etape dans sdmc:/prelude_trace.txt et COMMIT immediatement. Le commit par ligne coute
// cher, mais c'est tout l'interet : sans lui les ecritures SD restent en cache et une coupure au
// POWER les perd — c'est precisement ce qui rendait un blocage invisible sur la carte. Ainsi la
// derniere ligne presente = la derniere etape franchie, meme apres un arret force.
void nextendo_trace(const char *step);
#define NEXTENDO_TRACE_PATH "sdmc:/prelude_trace.txt"

// Detecte le storage de boot (cosmetique : on ecrit les 2 fichiers de toute facon).
BootType nextendo_detect_boot(void);

// Mode ACTUELLEMENT charge : 0 = NEXTENDO, 1 = NINTENDO (memes valeurs que CHOICE_*).
// Lu depuis les fichiers hosts (redirection VPS presente ou non).
int nextendo_current_mode(void);

// Mode NEXTENDO : ecrit les hosts (sysmmc.txt + emummc.txt) + enable_dns_mitm=1.
bool nextendo_apply_nextendo(void);

// Mode NINTENDO : renomme les hosts Nextendo en .bak + enable_dns_mitm=0 -> tout redevient normal.
bool nextendo_apply_nintendo(void);

// Teste la connectivite reseau de base et loggue les resultats dans la trace
// (diagnostic pour 2123-0011 et 2810-1224). Necessite socketInitializeDefault().
void nextendo_diag_network(void);

// Redemarre la console (ne revient pas si succes ; bpcRebootSystem).
Result nextendo_reboot(void);

// --- Scan de configuration pour l'ecran de revue ---
#define REVIEW_MAX 6

typedef struct {
    const char *file;        // "exosphere.ini"
    const char *setting;     // "blank_prodinfo_emummc"
    char        old_val[28]; // valeur actuelle
    char        new_val[28]; // valeur cible
    const char *summary;     // explication courte (FR)
    bool        changed;     // true si la valeur va effectivement changer
} ReviewItem;

typedef struct ConfigReview {
    ReviewItem items[REVIEW_MAX];
    int        count;
    int        mode;         // CHOICE_NEXTENDO ou CHOICE_NINTENDO
} ConfigReview;

// Scanne les fichiers de configuration Atmosphere et remplit review.
// mode = CHOICE_NEXTENDO ou CHOICE_NINTENDO (la cible).
// Retourne false si lecture impossible (SD absente).
bool nextendo_scan_config(int mode, ConfigReview *review);

#endif // NEXTENDO_APPLY_H

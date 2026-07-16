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

// Redemarre la console (ne revient pas si succes ; bpcRebootSystem).
Result nextendo_reboot(void);

#endif // NEXTENDO_APPLY_H

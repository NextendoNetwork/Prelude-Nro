// ============================================================
//  Nextendo .nro — auto-mise a jour (via le serveur Nextendo).
//  Au lancement, on interroge /api/nro/latest ; si une version plus recente
//  existe, on affiche un bandeau. L'utilisateur peut telecharger le nouveau
//  .nro et remplacer /switch/nextendo.nro (le .nro courant tourne depuis la RAM).
// ============================================================
#ifndef NEXTENDO_UPDATE_H
#define NEXTENDO_UPDATE_H
#include <switch.h>

// Version de CE build. A INCREMENTER a chaque release (le serveur renvoie la derniere).
// build 4 : mode Nintendo coupe network_mitm (fix eShop 2137-7403) + auto-provisioning
//           du stack cert-trust complet au 1er passage en mode Nextendo (zero install manuelle).
// build 5 : release Prelude v1 (rebrand du homebrew Nextendo -> Prelude ; MAJ desormais OBLIGATOIRE).
#define NEXTENDO_BUILD 5

typedef struct {
    bool available;   // une version > NEXTENDO_BUILD est dispo
    int  latest;      // numero de version serveur
    long size;        // taille attendue du .nro (verif du telechargement)
} NextendoUpdate;

typedef enum {
    NUP_OK = 0,
    NUP_NET_FAIL,     // telechargement impossible
    NUP_SIZE_FAIL,    // taille recue != taille annoncee (download corrompu)
    NUP_WRITE_FAIL    // ecriture SD impossible
} nextendo_update_result;

// Interroge le serveur (rapide, timeout court). socketInitializeDefault géré en interne.
NextendoUpdate nextendo_update_check(void);

// Telecharge le nouveau .nro et remplace /switch/nextendo.nro (via un .new + rename).
nextendo_update_result nextendo_update_apply(long expectedSize);

#endif // NEXTENDO_UPDATE_H

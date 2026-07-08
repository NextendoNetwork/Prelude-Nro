// ============================================================
//  Nextendo .nro — musique de fond (BGM en boucle).
//  Charge romfs:/bgm.ogg et la joue en boucle infinie. Non-fatal :
//  si le fichier est absent ou l'audio indispo, l'app continue en silence.
// ============================================================
#ifndef AUDIO_H
#define AUDIO_H
#include <switch.h>

bool audio_init(void);   // demarre la BGM en boucle (true si lancee)
void audio_exit(void);   // arrete + libere

#endif // AUDIO_H

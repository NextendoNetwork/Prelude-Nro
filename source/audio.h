// Prelude — Nintendo Switch homebrew for the Nextendo Network.
// Copyright (C) 2026 Nextendo Network
//
// Licensed under the PolyForm Shield License 1.0.0.
//
// You may use, modify and distribute this software for any purpose EXCEPT providing a product
// that competes with Nextendo Network, or with any product Nextendo Network provides using it.
//
// See LICENSE.md for the full terms, or <https://polyformproject.org/licenses/shield/1.0.0>.
//
// Required Notice: Copyright 2026 Nextendo Network

// ============================================================
//  Nextendo .nro — musique de fond (BGM en boucle).
//  Charge romfs:/bgm.mp3 et la joue en boucle infinie. Non-fatal :
//  si le fichier est absent ou l'audio indispo, l'app continue en silence.
// ============================================================
#ifndef AUDIO_H
#define AUDIO_H
#include <switch.h>

// BGM en boucle depuis romfs:/bgm.mp3 (mpg123 + audout). Non-fatal : renvoie
// false si l'audio ou le fichier manque, l'app continue en silence.
bool audio_init(void);
void audio_exit(void);   // arrete le thread de decodage, puis libere

#endif // AUDIO_H

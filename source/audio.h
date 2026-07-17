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

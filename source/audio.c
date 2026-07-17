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
//  Nextendo .nro — musique de fond en boucle (SDL2_mixer, audio-only).
//  Joue romfs:/bgm.mp3 en boucle infinie. Non-fatal : si l'audio ou le
//  fichier manque, l'app continue en silence.
//  SDL_MAIN_HANDLED : on garde NOTRE main() (dans main.c), SDL ne le detourne pas.
// ============================================================
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <switch.h>

#include "audio.h"

static Mix_Music *s_music     = NULL;
static bool       s_audioOpen = false;

bool audio_init(void) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_AUDIO) != 0) return false;            // audio seul (pas de video)
    if (Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 4096) != 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    s_audioOpen = true;
    Mix_Init(MIX_INIT_MP3);                                     // active le decodeur MP3
    s_music = Mix_LoadMUS("romfs:/bgm.mp3");
    if (!s_music) return false;                                 // pas de musique -> on continue
    Mix_VolumeMusic((MIX_MAX_VOLUME * 7) / 10);                 // ~70 %
    Mix_PlayMusic(s_music, -1);                                 // -1 = boucle infinie
    return true;
}

void audio_exit(void) {
    if (s_music) { Mix_HaltMusic(); Mix_FreeMusic(s_music); s_music = NULL; }
    if (s_audioOpen) { Mix_CloseAudio(); s_audioOpen = false; }
    Mix_Quit();
    if (SDL_WasInit(SDL_INIT_AUDIO)) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

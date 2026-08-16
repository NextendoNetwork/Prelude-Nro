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
//  Nextendo .nro — musique de fond en boucle (mpg123 + audout, audio-only).
//  Joue romfs:/bgm.mp3 en boucle infinie. Non-fatal : si l'audio ou le
//  fichier manque, l'app continue en silence.
//
//  POURQUOI PAS SDL2_mixer (l'implementation precedente) : la ligne de lien
//  `pkg-config --static --libs SDL2_mixer` tirait libSDL2 (4.4 Mo), et avec
//  elle Mesa — libEGL (5.1 Mo) + libglapi — parce que SDL2 declare son
//  sous-systeme video, plus libmodplug (0.9 Mo) pour un format tracker qu'on
//  n'utilise pas. Prelude dessine en framebuffer direct (framebufferBegin) et
//  ne trace jamais un triangle OpenGL : tout ca ne servait qu'a decoder un MP3.
//  Mesure : le .nro passait de 25.97 Mo a 19.63 Mo rien qu'en retirant SDL2, la
//  section code de 8.47 Mo a 2.13 Mo (-75 %). Ici on garde le seul morceau
//  reellement utile — libmpg123 (0.24 Mo) — et on ecrit a la main le pont vers
//  audout, qui est le service audio de libnx.
//
//  Ca compte au-dela de la taille : le .nro est charge INTEGRALEMENT en RAM, et
//  en mode applet (lancement depuis l'Album) la memoire disponible est etroite.
//  Moins de .nro = plus de marge pour les tampons socket/SSL de l'updater.
//
//  ARCHITECTURE : audout consomme des tampons PCM ; on ne peut pas juste "jouer
//  un fichier". Un thread decode le MP3 par blocs et alimente une file de
//  NUM_BUFFERS tampons en rotation (voir audioThread pour le detail du cadencage).
//
//  PIEGE DU TAUX D'ECHANTILLONNAGE : audout est fige a 48 kHz et mpg123 ne
//  reechantillonne PAS vers un taux arbitraire — il sort le taux natif du flux ou
//  le divise par 2/4. bgm.mp3 etait en 44,1 kHz : forcer 48 kHz en sortie donnait
//  un decodeur qui ne produisait jamais rien, en silence total. L'asset est
//  desormais encode en 48 kHz, et audio_init() VERIFIE le taux et trace un message
//  explicite s'il ne correspond pas, au lieu d'echouer sans bruit.
// ============================================================
#include <switch.h>
#include <mpg123.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>   // SEEK_SET, FILE (lecture romfs via stdio)

#include "nextendo_apply.h"  // nextendo_trace : un echec audio doit laisser une trace

#include "audio.h"

// audout : 48 kHz / stereo / PCM16. SAMPLE_RATE ne sert qu'a dimensionner les
// tampons ; le taux REEL est lu via audoutGetSampleRate() et confronte a celui du
// MP3 dans audio_init() (cf. le piege decrit en tete de fichier).
#define SAMPLE_RATE  48000
#define CHANNELS     2
#define NUM_BUFFERS  3

// Taille d'un tampon : ~1/8 s de son. Assez court pour que audio_exit() rende la
// main vite, assez long pour absorber une hesitation du decodeur.
// buffer_size DOIT etre aligne sur 0x1000 (contrainte audout).
#define FRAME_BYTES  (SAMPLE_RATE * CHANNELS * sizeof(s16) / 8)
#define BUF_SIZE     ((FRAME_BYTES + 0xFFF) & ~0xFFFUL)

// --- E/S via stdio : mpg123 lit le romfs par ces callbacks (cf. audio_init). ---
static FILE *s_fp = NULL;   // NULL des que mpg123 en a pris possession

static ssize_t io_read(void *h, void *buf, size_t sz) {
    return (ssize_t)fread(buf, 1, sz, (FILE *)h);
}
static off_t io_seek(void *h, off_t off, int whence) {
    if (fseek((FILE *)h, (long)off, whence) != 0) return -1;
    return (off_t)ftell((FILE *)h);
}
static void io_cleanup(void *h) {
    if (h) fclose((FILE *)h);
}

static mpg123_handle *s_mh        = NULL;
static bool           s_audioOpen = false;
static bool           s_mpgInit   = false;
static Thread         s_thread;
static bool           s_threadOn  = false;
static volatile bool  s_stop      = false;

static AudioOutBuffer s_bufs[NUM_BUFFERS];
static void          *s_mem[NUM_BUFFERS];

// Volume applique au PCM decode (~70 %, comme l'ancien Mix_VolumeMusic).
// audoutSetAudioOutVolume() existe mais agit sur la sortie GLOBALE de l'appli ;
// on prefere attenuer nos propres echantillons et ne rien imposer au systeme.
#define VOL_NUM 7
#define VOL_DEN 10

static void applyVolume(s16 *pcm, size_t bytes) {
    size_t n = bytes / sizeof(s16);
    for (size_t i = 0; i < n; i++) pcm[i] = (s16)((int)pcm[i] * VOL_NUM / VOL_DEN);
}

// Decode un bloc dans le tampon. false = flux fini ou casse (l'appelant s'arrete).
static bool fillBuffer(AudioOutBuffer *b) {
    size_t done = 0;
    int rc = mpg123_read(s_mh, (unsigned char *)b->buffer, BUF_SIZE, &done);

    // Fin du fichier : on reboucle. mpg123_seek(0) plutot que rouvrir le fichier
    // — pas de re-parsing d'en-tetes, donc pas de trou audible a la boucle.
    if (rc == MPG123_DONE || (rc == MPG123_OK && done == 0)) {
        mpg123_seek(s_mh, 0, SEEK_SET);
        rc = mpg123_read(s_mh, (unsigned char *)b->buffer, BUF_SIZE, &done);
    }
    // MPG123_NEW_FORMAT ne peut pas arriver (format fige par mpg123_format()),
    // mais toute autre erreur est fatale : on sort en silence plutot que de
    // boucler sur un decodeur casse.
    if ((rc != MPG123_OK && rc != MPG123_DONE) || done == 0) return false;

    // Le dernier bloc d'une boucle est plus court que BUF_SIZE : on met a zero la
    // queue du tampon (evite de rejouer l'ancien contenu) et on ne declare que ce
    // qui a ete decode via data_size.
    if (done < BUF_SIZE) memset((u8 *)b->buffer + done, 0, BUF_SIZE - done);
    applyVolume((s16 *)b->buffer, done);
    b->data_size = done;
    return true;
}

// NE PAS utiliser audoutPlayBuffer() ici : il empile PUIS attend la fin de lecture
// du tampon, ce qui ne laisse jamais qu'un seul tampon en vol — le decodage du
// suivant s'entend comme un trou entre chaque bloc. On amorce donc la file avec
// tous les tampons, puis on ne fait que remplacer ceux que le driver rend.
// audoutWaitPlayFinish() n'est pas utilise non plus : sa doc ("use UINT64_MAX to
// wait until all finished") laisse entendre qu'il attend que TOUS les tampons
// soient joues, ce qui reviendrait a vider la file a chaque tour. Un sondage a
// 5 ms est sans ambiguite et negligeable face aux ~375 ms de son deja en file.
static void audioThread(void *arg) {
    (void)arg;

    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (!fillBuffer(&s_bufs[i])) return;
        if (R_FAILED(audoutAppendAudioOutBuffer(&s_bufs[i]))) return;
    }

    while (!s_stop) {
        AudioOutBuffer *rel = NULL;
        u32 n = 0;
        if (R_FAILED(audoutGetReleasedAudioOutBuffer(&rel, &n))) break;
        if (n == 0 || !rel) { svcSleepThread(5000000ULL); continue; }  // 5 ms
        if (!fillBuffer(rel)) break;
        if (R_FAILED(audoutAppendAudioOutBuffer(rel))) break;
    }
}

bool audio_init(void) {
    if (R_FAILED(audoutInitialize())) { nextendo_trace("audio: audoutInitialize KO"); return false; }
    if (R_FAILED(audoutStartAudioOut())) {
        nextendo_trace("audio: audoutStartAudioOut KO"); audoutExit(); return false;
    }
    s_audioOpen = true;

    if (mpg123_init() != MPG123_OK) { nextendo_trace("audio: mpg123_init KO"); audio_exit(); return false; }
    s_mpgInit = true;

    int err = MPG123_OK;
    s_mh = mpg123_new(NULL, &err);
    if (!s_mh) { nextendo_trace("audio: mpg123_new KO"); audio_exit(); return false; }

    // Lecture via stdio plutot que mpg123_open() : celui-ci passe par open()/read()
    // POSIX, et on ne veut pas dependre de ce que le devoptab romfs expose. fopen()
    // sur romfs: est le chemin deja utilise partout ailleurs dans l'app.
    mpg123_replace_reader_handle(s_mh, io_read, io_seek, io_cleanup);
    s_fp = fopen("romfs:/bgm.mp3", "rb");
    if (!s_fp) { nextendo_trace("audio: fopen bgm.mp3 KO"); audio_exit(); return false; }
    if (mpg123_open_handle(s_mh, s_fp) != MPG123_OK) {
        nextendo_trace("audio: mpg123_open_handle KO"); audio_exit(); return false;
    }
    s_fp = NULL;   // possede par mpg123 desormais (io_cleanup le fermera)

    // Le taux de audout est FIXE (48 kHz) et mpg123 ne reechantillonne pas vers un
    // taux arbitraire : il ne sait que sortir le taux natif du flux, ou le diviser
    // par 2 ou 4. Un bgm.mp3 en 44,1 kHz ne peut donc PAS alimenter audout.
    // C'est exactement le bug qui a rendu la musique muette : on forcait 48 kHz en
    // sortie, mpg123 acceptait la declaration mais ne produisait jamais rien, et
    // l'echec etait TOTALEMENT silencieux. On compare donc explicitement, et on
    // trace — un asset au mauvais taux doit rater bruyamment, pas discretement.
    long  rate = 0;
    int   chans = 0, enc = 0;
    if (mpg123_getformat(s_mh, &rate, &chans, &enc) != MPG123_OK) {
        nextendo_trace("audio: mpg123_getformat KO"); audio_exit(); return false;
    }
    if (rate != (long)audoutGetSampleRate()) {
        char m[96];
        snprintf(m, sizeof(m), "audio: bgm.mp3 a %ld Hz, audout exige %u Hz -> reencoder l'asset",
                 rate, audoutGetSampleRate());
        nextendo_trace(m);
        audio_exit(); return false;
    }

    // Format fige au taux natif : sans ca mpg123 peut changer de format en cours de
    // flux et renvoyer MPG123_NEW_FORMAT, ce que audout ne saurait pas suivre.
    mpg123_format_none(s_mh);
    if (mpg123_format(s_mh, rate, MPG123_STEREO, MPG123_ENC_SIGNED_16) != MPG123_OK) {
        nextendo_trace("audio: mpg123_format KO"); audio_exit(); return false;
    }

    for (int i = 0; i < NUM_BUFFERS; i++) {
        s_mem[i] = memalign(0x1000, BUF_SIZE);   // audout exige l'alignement 0x1000
        if (!s_mem[i]) { audio_exit(); return false; }
        memset(s_mem[i], 0, BUF_SIZE);
        s_bufs[i].next        = NULL;
        s_bufs[i].buffer      = s_mem[i];
        s_bufs[i].buffer_size = BUF_SIZE;
        s_bufs[i].data_size   = BUF_SIZE;
        s_bufs[i].data_offset = 0;
    }

    // Priorite 0x2C : au-dessus du thread principal (0x2D) pour que le decodage
    // ne soit pas famine par le rendu, sans monter au niveau des threads systeme.
    s_stop = false;
    if (R_FAILED(threadCreate(&s_thread, audioThread, NULL, NULL, 0x8000, 0x2C, -2))) {
        audio_exit(); return false;
    }
    if (R_FAILED(threadStart(&s_thread))) { threadClose(&s_thread); audio_exit(); return false; }
    s_threadOn = true;
    return true;
}

void audio_exit(void) {
    // Ordre impose : arreter le thread AVANT de liberer ce qu'il lit. s_stop est
    // vu au prochain tour de boucle ; audoutStopAudioOut() debloque un
    // audoutPlayBuffer() en attente pour que ce tour arrive tout de suite.
    if (s_threadOn) {
        s_stop = true;
        if (s_audioOpen) audoutStopAudioOut();
        threadWaitForExit(&s_thread);
        threadClose(&s_thread);
        s_threadOn = false;
    }
    if (s_mh) { mpg123_close(s_mh); mpg123_delete(s_mh); s_mh = NULL; }  // io_cleanup ferme le FILE
    if (s_fp) { fclose(s_fp); s_fp = NULL; }   // echec avant que mpg123 ne l'adopte
    if (s_mpgInit) { mpg123_exit(); s_mpgInit = false; }
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (s_mem[i]) { free(s_mem[i]); s_mem[i] = NULL; }
    }
    if (s_audioOpen) { audoutExit(); s_audioOpen = false; }
}

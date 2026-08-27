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

// Looping BGM: a thread decodes romfs:/bgm.mp3 in blocks and feeds NUM_BUFFERS audout buffers in rotation.
// Non-fatal: with no audio and no file, the app runs on in silence.
#include <switch.h>
#include <mpg123.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>   // SEEK_SET, FILE (romfs is read through stdio)

#include "nextendo_apply.h"  // nextendo_trace: an audio failure must leave a trace

#include "audio.h"

// SAMPLE_RATE only sizes the buffers: the REAL rate comes from audoutGetSampleRate().
#define SAMPLE_RATE  48000
#define CHANNELS     2
#define NUM_BUFFERS  3

// ~1/8 s of sound: short enough that audio_exit() returns quickly, long enough to absorb decoder jitter.
#define FRAME_BYTES  (SAMPLE_RATE * CHANNELS * sizeof(s16) / 8)
#define BUF_SIZE     ((FRAME_BYTES + 0xFFF) & ~0xFFFUL)

// mpg123 reads the romfs through these stdio callbacks.
static FILE *s_fp = NULL;   // NULL as soon as mpg123 has taken ownership

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

// We attenuate our own samples: audoutSetAudioOutVolume() would act on the app's GLOBAL output.
#define VOL_NUM 7
#define VOL_DEN 10

static void applyVolume(s16 *pcm, size_t bytes) {
    size_t n = bytes / sizeof(s16);
    for (size_t i = 0; i < n; i++) pcm[i] = (s16)((int)pcm[i] * VOL_NUM / VOL_DEN);
}

// Decodes one block into the buffer. false = stream finished or broken (the caller stops).
static bool fillBuffer(AudioOutBuffer *b) {
    size_t done = 0;
    int rc = mpg123_read(s_mh, (unsigned char *)b->buffer, BUF_SIZE, &done);

    // mpg123_seek(0) rather than reopening: no header re-parsing, so no audible gap at the loop.
    if (rc == MPG123_DONE || (rc == MPG123_OK && done == 0)) {
        mpg123_seek(s_mh, 0, SEEK_SET);
        rc = mpg123_read(s_mh, (unsigned char *)b->buffer, BUF_SIZE, &done);
    }
    // Any error is fatal: we bail out silently rather than spin on a broken decoder.
    if ((rc != MPG123_OK && rc != MPG123_DONE) || done == 0) return false;

    // Last block is shorter than BUF_SIZE: zeroing the tail avoids replaying the previous contents.
    if (done < BUF_SIZE) memset((u8 *)b->buffer + done, 0, BUF_SIZE - done);
    applyVolume((s16 *)b->buffer, done);
    b->data_size = done;
    return true;
}

// Do NOT use audoutPlayBuffer(): it waits for the queued buffer to finish, leaving one in flight and a gap between blocks.
// We prime the queue with every buffer then refill the ones the driver releases; a 5 ms poll is nothing against ~375 ms queued.
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

    // stdio rather than mpg123_open(): that one goes through POSIX open()/read(), which we do not want to require of the romfs devoptab.
    mpg123_replace_reader_handle(s_mh, io_read, io_seek, io_cleanup);
    s_fp = fopen("romfs:/bgm.mp3", "rb");
    if (!s_fp) { nextendo_trace("audio: fopen bgm.mp3 KO"); audio_exit(); return false; }
    if (mpg123_open_handle(s_mh, s_fp) != MPG123_OK) {
        nextendo_trace("audio: mpg123_open_handle KO"); audio_exit(); return false;
    }
    s_fp = NULL;   // owned by mpg123 from here on (io_cleanup will close it)

    // audout is fixed at 48 kHz and mpg123 does not resample: an asset at the wrong rate decodes into nothing, silently.
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

    // Format pinned: otherwise mpg123 can return MPG123_NEW_FORMAT mid-stream, which audout could not follow.
    mpg123_format_none(s_mh);
    if (mpg123_format(s_mh, rate, MPG123_STEREO, MPG123_ENC_SIGNED_16) != MPG123_OK) {
        nextendo_trace("audio: mpg123_format KO"); audio_exit(); return false;
    }

    for (int i = 0; i < NUM_BUFFERS; i++) {
        s_mem[i] = memalign(0x1000, BUF_SIZE);   // audout requires 0x1000 alignment
        if (!s_mem[i]) { audio_exit(); return false; }
        memset(s_mem[i], 0, BUF_SIZE);
        s_bufs[i].next        = NULL;
        s_bufs[i].buffer      = s_mem[i];
        s_bufs[i].buffer_size = BUF_SIZE;
        s_bufs[i].data_size   = BUF_SIZE;
        s_bufs[i].data_offset = 0;
    }

    // Priority 0x2C, above the main thread (0x2D): decoding must not be starved by rendering.
    s_stop = false;
    if (R_FAILED(threadCreate(&s_thread, audioThread, NULL, NULL, 0x8000, 0x2C, -2))) {
        audio_exit(); return false;
    }
    if (R_FAILED(threadStart(&s_thread))) { threadClose(&s_thread); audio_exit(); return false; }
    s_threadOn = true;
    return true;
}

void audio_exit(void) {
    // Stop the thread BEFORE freeing what it reads; audoutStopAudioOut() unblocks the wait so that happens at once.
    if (s_threadOn) {
        s_stop = true;
        if (s_audioOpen) audoutStopAudioOut();
        threadWaitForExit(&s_thread);
        threadClose(&s_thread);
        s_threadOn = false;
    }
    if (s_mh) { mpg123_close(s_mh); mpg123_delete(s_mh); s_mh = NULL; }  // io_cleanup closes the FILE
    if (s_fp) { fclose(s_fp); s_fp = NULL; }   // failure before mpg123 adopted it
    if (s_mpgInit) { mpg123_exit(); s_mpgInit = false; }
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (s_mem[i]) { free(s_mem[i]); s_mem[i] = NULL; }
    }
    if (s_audioOpen) { audoutExit(); s_audioOpen = false; }
}

#include "mi_decode.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_ONLY_MP3
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_CRC /* skip CRC checks: cheaper, and the SD is not a noisy channel */
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "dr_wav.h"

typedef enum { K_MP3, K_FLAC, K_WAV } kind_t;

struct mi_dec {
    kind_t   kind;
    FILE    *f;
    uint32_t hz;
    int      ch;
    int16_t *tmp; /* native-channel scratch */
    union {
        drmp3   mp3;
        drflac *flac;
        drwav   wav;
    } u;
};

static size_t cb_read(void *ud, void *out, size_t n) { return fread(out, 1, n, ((mi_dec_t *)ud)->f); }

/* The three libraries share the same shape for these callbacks but use different enums. */
static int do_seek(void *ud, int off, int whence) { return fseek(((mi_dec_t *)ud)->f, off, whence) == 0; }
#define SEEK_FN(fn, T, E, SET, CUR) \
    static T fn(void *ud, int o, E w) { return do_seek(ud, o, w == SET ? SEEK_SET : w == CUR ? SEEK_CUR : SEEK_END); }
SEEK_FN(mp3_seek, drmp3_bool32, drmp3_seek_origin, DRMP3_SEEK_SET, DRMP3_SEEK_CUR)
SEEK_FN(flac_seek, drflac_bool32, drflac_seek_origin, DRFLAC_SEEK_SET, DRFLAC_SEEK_CUR)
SEEK_FN(wav_seek, drwav_bool32, drwav_seek_origin, DRWAV_SEEK_SET, DRWAV_SEEK_CUR)
static drmp3_bool32 mp3_tell(void *ud, drmp3_int64 *c) { *c = ftell(((mi_dec_t *)ud)->f); return 1; }
static drflac_bool32 flac_tell(void *ud, drflac_int64 *c) { *c = ftell(((mi_dec_t *)ud)->f); return 1; }
static drwav_bool32 wav_tell(void *ud, drwav_int64 *c) { *c = ftell(((mi_dec_t *)ud)->f); return 1; }

mi_dec_t *mi_dec_open(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return NULL;
    mi_dec_t *d = calloc(1, sizeof *d);
    if (!d) return NULL;

    bool ok = false;
    d->f = fopen(path, "rb");
    if (!d->f) goto out;
    setvbuf(d->f, NULL, _IOFBF, 8192); /* fewer, larger SD reads */

    if (!strcasecmp(ext, ".mp3")) {
        d->kind = K_MP3;
        if (!drmp3_init(&d->u.mp3, cb_read, mp3_seek, mp3_tell, NULL, d, NULL)) goto out;
        d->hz = d->u.mp3.sampleRate;
        d->ch = d->u.mp3.channels;
    } else if (!strcasecmp(ext, ".flac")) {
        d->kind = K_FLAC;
        d->u.flac = drflac_open(cb_read, flac_seek, flac_tell, d, NULL);
        if (!d->u.flac) goto out;
        d->hz = d->u.flac->sampleRate;
        d->ch = d->u.flac->channels;
    } else if (!strcasecmp(ext, ".wav")) {
        d->kind = K_WAV;
        if (!drwav_init(&d->u.wav, cb_read, wav_seek, wav_tell, d, NULL)) goto out;
        d->hz = d->u.wav.sampleRate;
        d->ch = d->u.wav.channels;
    } else {
        goto out;
    }
    ok = d->hz > 0 && d->ch > 0;
    if (ok) {
        d->tmp = malloc(sizeof(int16_t) * MI_READ_FRAMES * d->ch);
        ok = d->tmp != NULL;
    }
out:
    if (!ok) { mi_dec_close(d); return NULL; }
    return d;
}

uint32_t mi_dec_hz(const mi_dec_t *d) { return d->hz; }

size_t mi_dec_read(mi_dec_t *d, int16_t *out, size_t max_frames)
{
    if (max_frames > MI_READ_FRAMES) max_frames = MI_READ_FRAMES;
    size_t n = 0;
    switch (d->kind) {
    case K_MP3:  n = (size_t)drmp3_read_pcm_frames_s16(&d->u.mp3, max_frames, d->tmp); break;
    case K_FLAC: n = (size_t)drflac_read_pcm_frames_s16(d->u.flac, max_frames, d->tmp); break;
    case K_WAV:  n = (size_t)drwav_read_pcm_frames_s16(&d->u.wav, max_frames, d->tmp); break;
    }
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = d->tmp[i * d->ch];
        out[2 * i + 1] = d->tmp[i * d->ch + (d->ch > 1)];
    }
    return n;
}

void mi_dec_close(mi_dec_t *d)
{
    if (!d) return;
    if (d->f) {
        switch (d->kind) {
        case K_MP3:  if (d->hz) drmp3_uninit(&d->u.mp3); break;
        case K_FLAC: if (d->u.flac) drflac_close(d->u.flac); break;
        case K_WAV:  if (d->hz) drwav_uninit(&d->u.wav); break;
        }
        fclose(d->f);
    }
    free(d->tmp);
    free(d);
}

#include "mi_decode.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

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

struct mi_dec {
    mi_fmt_t kind;
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
    mi_fmt_t kind = mi_dec_format_of(path);
    if (kind == MI_FMT_NONE) return NULL;
    mi_dec_t *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->kind = kind;

    bool ok = false;
    d->f = fopen(path, "rb");
    if (!d->f) goto out;
    setvbuf(d->f, NULL, _IOFBF, 8192); /* fewer, larger SD reads */

    switch (d->kind) {
    case MI_FMT_MP3:
        if (!drmp3_init(&d->u.mp3, cb_read, mp3_seek, mp3_tell, NULL, d, NULL)) goto out;
        d->hz = d->u.mp3.sampleRate;
        d->ch = d->u.mp3.channels;
        break;
    case MI_FMT_FLAC:
        d->u.flac = drflac_open(cb_read, flac_seek, flac_tell, d, NULL);
        if (!d->u.flac) goto out;
        d->hz = d->u.flac->sampleRate;
        d->ch = d->u.flac->channels;
        break;
    case MI_FMT_WAV:
        if (!drwav_init(&d->u.wav, cb_read, wav_seek, wav_tell, d, NULL)) goto out;
        d->hz = d->u.wav.sampleRate;
        d->ch = d->u.wav.channels;
        break;
    default:
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

uint32_t mi_dec_duration_ms(const mi_dec_t *d)
{
    uint64_t frames;
    switch (d->kind) {
    case MI_FMT_MP3:  frames = (uint64_t)drmp3_get_pcm_frame_count((drmp3 *)&d->u.mp3); break;
    case MI_FMT_FLAC: frames = (uint64_t)d->u.flac->totalPCMFrameCount; break;
    case MI_FMT_WAV:  frames = (uint64_t)d->u.wav.totalPCMFrameCount; break;
    default: return 0;
    }
    /* "Unknown" has to be recognised rather than converted: dr_mp3 reports
     * DRMP3_UINT64_MAX for a file whose length it cannot determine, and a stream that
     * never announced one reports 0. Reporting either as a duration would put a
     * nonsense number on a progress bar. */
    if (!frames || frames >= (1ULL << 40) || !d->hz) return 0;
    return (uint32_t)((frames * 1000ULL) / d->hz);
}

size_t mi_dec_read(mi_dec_t *d, int16_t *out, size_t max_frames)
{
    if (max_frames > MI_READ_FRAMES) max_frames = MI_READ_FRAMES;
    size_t n = 0;
    switch (d->kind) {
    case MI_FMT_MP3:  n = (size_t)drmp3_read_pcm_frames_s16(&d->u.mp3, max_frames, d->tmp); break;
    case MI_FMT_FLAC: n = (size_t)drflac_read_pcm_frames_s16(d->u.flac, max_frames, d->tmp); break;
    case MI_FMT_WAV:  n = (size_t)drwav_read_pcm_frames_s16(&d->u.wav, max_frames, d->tmp); break;
    default: break; /* MI_FMT_NONE never reaches here: mi_dec_open() rejects it */
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
        case MI_FMT_MP3:  if (d->hz) drmp3_uninit(&d->u.mp3); break;
        case MI_FMT_FLAC: if (d->u.flac) drflac_close(d->u.flac); break;
        case MI_FMT_WAV:  if (d->hz) drwav_uninit(&d->u.wav); break;
        default: break;
        }
        fclose(d->f);
    }
    free(d->tmp);
    free(d);
}

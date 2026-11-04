#include "mi_decode.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_timer.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_ONLY_MP3
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_CRC /* skip CRC checks: cheaper, and the SD is not a noisy channel */
/* 16 KB, not dr_flac's 4 KB default: the card is on SPI and a 24-bit 192 kHz FLAC
 * needs ~700 KB/s, so a bigger L2 cache cuts the number of callbacks and card
 * transactions. It is copied into drflac_init_info on the caller's stack during
 * drflac_open(), so the opening task must leave room for it (the decoded source
 * task in mini_icecast.c does). Must be a multiple of 8. */
#define DR_FLAC_BUFFER_SIZE 16384
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "dr_wav.h"

struct mi_dec {
    mi_fmt_t kind;
    FILE    *f;
    uint32_t hz;
    int      ch;
    uint64_t io_us; /* wall time blocked in fread(), so decode CPU can be separated */
    /* An MP3 without a Xing/Info header: a length estimate from the first frame, so the
     * broadcast is never held while dr_mp3 reads the whole file to count frames. */
    uint32_t dur_hint_ms;
    int16_t *tmp; /* native-channel scratch */
    union {
        drmp3   mp3;
        drflac *flac;
        drwav   wav;
    } u;
};

static size_t cb_read(void *ud, void *out, size_t n)
{
    mi_dec_t *d = (mi_dec_t *)ud;
    int64_t t0 = esp_timer_get_time();
    size_t got = fread(out, 1, n, d->f);
    d->io_us += (uint64_t)(esp_timer_get_time() - t0);
    return got;
}

uint64_t mi_dec_io_us(const mi_dec_t *d) { return d->io_us; }

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

/* The bitrate a 4-byte MPEG audio frame header declares, in kbps, or 0 when the header
 * is not a valid one. ISO 11172-3 and 13818-3's bitrate index tables. */
static uint32_t mp3_header_kbps(const uint8_t *h)
{
    static const uint16_t v1_l1[] = { 0, 32, 64, 96, 128, 160, 192, 224,
                                      256, 288, 320, 352, 384, 416, 448, 0 };
    static const uint16_t v1_l2[] = { 0, 32, 48, 56, 64, 80, 96, 112,
                                      128, 160, 192, 224, 256, 320, 384, 0 };
    static const uint16_t v1_l3[] = { 0, 32, 40, 48, 56, 64, 80, 96,
                                      112, 128, 160, 192, 224, 256, 320, 0 };
    static const uint16_t v2_l1[] = { 0, 32, 48, 56, 64, 80, 96, 112,
                                      128, 144, 160, 176, 192, 224, 256, 0 };
    static const uint16_t v2_l23[] = { 0, 8, 16, 24, 32, 40, 48, 56,
                                       64, 80, 96, 112, 128, 144, 160, 0 };

    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return 0;
    int version = (h[1] >> 3) & 0x3; /* 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5 */
    int layer = (h[1] >> 1) & 0x3;   /* 1 = III, 2 = II, 3 = I */
    int index = (h[2] >> 4) & 0xF;
    if (version == 1 || layer == 0 || index == 0 || index == 15) return 0;
    if (version == 3) {
        if (layer == 1) return v1_l3[index];
        if (layer == 2) return v1_l2[index];
        return v1_l1[index];
    }
    return layer == 3 ? v2_l1[index] : v2_l23[index];
}

/* The sample rate a frame header declares, in Hz, or 0. */
static uint32_t mp3_header_hz(const uint8_t *h)
{
    static const uint16_t v1[] = { 44100, 48000, 32000, 0 };
    static const uint16_t v2[] = { 22050, 24000, 16000, 0 };
    static const uint16_t v25[] = { 11025, 12000, 8000, 0 };
    int version = (h[1] >> 3) & 0x3; /* 3 = MPEG1, 2 = MPEG2, 0 = MPEG2.5 */
    int index = (h[2] >> 2) & 0x3;
    if (version == 1) return 0;
    return version == 3 ? v1[index] : version == 2 ? v2[index] : v25[index];
}

/* The length a frame header declares, in bytes, or 0. */
static uint32_t mp3_frame_bytes(const uint8_t *h)
{
    uint32_t kbps = mp3_header_kbps(h);
    uint32_t hz = mp3_header_hz(h);
    if (!kbps || !hz) return 0;
    int version = (h[1] >> 3) & 0x3;
    int layer = (h[1] >> 1) & 0x3; /* 1 = III, 2 = II, 3 = I */
    uint32_t pad = (uint32_t)((h[2] >> 1) & 0x1);
    uint32_t bps = kbps * 1000u;
    if (layer == 3) return (12u * bps / hz + pad) * 4u;        /* Layer I */
    if (layer == 1 && version != 3) return 72u * bps / hz + pad; /* Layer III, MPEG2/2.5 */
    return 144u * bps / hz + pad;                               /* the rest */
}

/* A Track's length when its MP3 carries no Xing/Info header. dr_mp3's exact count reads
 * the whole file in linear time, and doing that before the first sample leaves the
 * broadcast silent for as long as the card takes — a 2 s ring does not survive it.
 *
 * The estimate is the stream's byte length over the first frame's bitrate, which is the
 * real length for a CBR file. A VBR file without a length header cannot be estimated
 * from one frame (the first frame's bitrate says nothing about the average), so it gets
 * 0 - an unknown length, which the player shows as such, rather than a number that is
 * wrong by a factor. Two frame headers are what tells the two apart, and they cost two
 * sectors against a scan's ten thousand. */
static uint32_t mp3_estimate_ms(mi_dec_t *d)
{
    drmp3 *p = &d->u.mp3;
    if (p->streamStartOffset > 0x7FFFFFFF) return 0;
    long at = ftell(d->f);
    uint8_t h[4];
    uint32_t kbps = 0;
    uint32_t bytes = 0;
    if (fseek(d->f, (long)p->streamStartOffset, SEEK_SET) == 0 &&
        fread(h, 1, sizeof h, d->f) == sizeof h) {
        kbps = mp3_header_kbps(h);
        bytes = mp3_frame_bytes(h);
        if (kbps && bytes && p->streamStartOffset + bytes + 4 <= p->streamLength) {
            uint8_t next[4];
            if (fseek(d->f, (long)(p->streamStartOffset + bytes), SEEK_SET) == 0 &&
                fread(next, 1, sizeof next, d->f) == sizeof next &&
                mp3_header_kbps(next) != kbps) {
                kbps = 0; /* a VBR stream with no length header: not ours to guess */
            }
        }
    }
    if (at >= 0) fseek(d->f, at, SEEK_SET); /* the decoder's cursor is where it was */
    if (kbps == 0) return 0;
    uint64_t audio = p->streamLength > p->streamStartOffset
                         ? p->streamLength - p->streamStartOffset
                         : 0;
    uint64_t ms = audio * 8 / kbps;
    return ms > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)ms;
}

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
    setvbuf(d->f, NULL, _IOFBF, 32768); /* fewer, larger card reads */

    switch (d->kind) {
    case MI_FMT_MP3:
        if (!drmp3_init(&d->u.mp3, cb_read, mp3_seek, mp3_tell, NULL, d, NULL)) goto out;
        d->hz = d->u.mp3.sampleRate;
        d->ch = d->u.mp3.channels;
        /* The length is known only when the file carries a Xing/Info header. When it
         * does not, estimate it now rather than let a later call scan the card. */
        if (d->u.mp3.totalPCMFrameCount == DRMP3_UINT64_MAX) {
            d->dur_hint_ms = mp3_estimate_ms(d);
        }
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
    if (ok && d->ch != 2) {
        /* Stereo decodes straight into the caller's buffer; only mono and
         * more-than-stereo need the native-channel scratch. */
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
    case MI_FMT_MP3: {
        /* Never drmp3_get_pcm_frame_count(): for a file with no length header it reads
         * the whole file, and this is called at the moment the broadcast has nothing
         * else to send. The count dr_mp3 already has is exact; the estimate is not. */
        const drmp3 *p = &d->u.mp3;
        if (p->totalPCMFrameCount == DRMP3_UINT64_MAX) return d->dur_hint_ms;
        frames = (uint64_t)p->totalPCMFrameCount;
        /* The header's count includes the encoder's delay and padding. */
        frames = frames > p->delayInPCMFrames ? frames - p->delayInPCMFrames : 0;
        frames = frames > p->paddingInPCMFrames ? frames - p->paddingInPCMFrames : 0;
        break;
    }
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
    if (d->ch == 2) {
        /* The stereo corpus is everything this appliance plays in practice; decode
         * straight into the caller's interleaved buffer instead of writing the
         * decoder's scratch and copying it. */
        switch (d->kind) {
        case MI_FMT_MP3:  n = (size_t)drmp3_read_pcm_frames_s16(&d->u.mp3, max_frames, out); break;
        case MI_FMT_FLAC: n = (size_t)drflac_read_pcm_frames_s16(d->u.flac, max_frames, out); break;
        case MI_FMT_WAV:  n = (size_t)drwav_read_pcm_frames_s16(&d->u.wav, max_frames, out); break;
        default: break;
        }
        return n;
    }
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

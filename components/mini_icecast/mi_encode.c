#include "mi_encode.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "layer3.h"
/* The encoder's own struct, read only for its size: the startup log exists to
 * make the memory budget observable rather than arithmetic, and a hard-coded
 * number would be 12 bytes wrong on the 32-bit target. Vendored and pinned, so
 * this cannot drift silently - and the host test pins the absolute value too. */
#include "types.h"

#define TAG "encoder"

/* MP3 frame geometry, needed to re-frame the encoder's byte output. */
static const uint16_t k_bitrates[2][16] = {
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}, /* MPEG-1 */
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},     /* MPEG-2 / 2.5 */
};
static const uint16_t k_samplerates[3][3] = {
    {44100, 48000, 32000},
    {22050, 24000, 16000},
    {11025, 12000, 8000},
};

struct mi_enc {
    shine_t  shine;
    uint32_t out_hz, bitrate;
    size_t   spf; /* samples per channel in one encoder frame: 1152, or 576 below 32 kHz */
    bool     external;
    int16_t  acc[MI_ENC_MAX_FRAME_SAMPLES * 2];
    size_t   acc_n;
    uint8_t  pending[4 * MI_ENC_MAX_FRAME_BYTES]; /* < one frame + one encoder call */
    size_t   pending_n;
    uint64_t frames_encoded;
    uint64_t encode_us;
    size_t   desync_bytes;
};

static void set_err(char *err, size_t n, const char *fmt, ...)
{
    if (!err || !n) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, n, fmt, ap);
    va_end(ap);
}

/* Length of the MP3 frame starting at h, or 0 if this is not a frame header. */
static size_t frame_bytes(const uint8_t *h)
{
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return 0;
    unsigned version = (h[1] >> 3) & 3; /* 0 = MPEG-2.5, 2 = MPEG-2, 3 = MPEG-1 */
    unsigned layer = (h[1] >> 1) & 3;
    if (version == 1 || layer != 1) return 0; /* reserved version; this encoder only makes Layer III */
    unsigned bri = (h[2] >> 4) & 15;
    unsigned sri = (h[2] >> 2) & 3;
    unsigned pad = (h[2] >> 1) & 1;
    if (bri == 0 || bri == 15 || sri == 3) return 0;
    unsigned vrow = (version == 3) ? 0 : 1;
    unsigned srow = (version == 3) ? 0 : (version == 2) ? 1 : 2;
    unsigned br = k_bitrates[vrow][bri], sr = k_samplerates[srow][sri];
    if (!br || !sr) return 0;
    return ((version == 3) ? 144u : 72u) * br * 1000u / sr + pad;
}

int mi_enc_check_format(uint32_t out_hz, uint32_t bitrate, char *err, size_t err_size)
{
    if (shine_find_samplerate_index((int)out_hz) < 0) {
        set_err(err, err_size,
                "station rate %u Hz is not one the encoder accepts; use one of "
                "44100 48000 32000 22050 24000 16000 11025 12000 8000",
                (unsigned)out_hz);
        return -1;
    }
    int mpeg = shine_check_config((int)out_hz, (int)bitrate);
    if (mpeg < 0) { /* NOTE: 0 is a valid answer (MPEG-2.5); only < 0 is an error */
        set_err(err, err_size, "bit rate %u kbps is not one the encoder can use at %u Hz",
                (unsigned)bitrate, (unsigned)out_hz);
        return -1;
    }
    return mpeg;
}

const char *mi_enc_mpeg_name(int mpeg_version)
{
    switch (mpeg_version) {
    case MPEG_I: return "MPEG-1";
    case MPEG_II: return "MPEG-2";
    case MPEG_25: return "MPEG-2.5";
    default: return "invalid";
    }
}

mi_enc_t *mi_enc_new(uint32_t out_hz, uint32_t bitrate, char *err, size_t err_size)
{
    int mpeg = mi_enc_check_format(out_hz, bitrate, err, err_size);
    if (mpeg < 0) return NULL;

    /* The staging buffers are ours, so they take the PSRAM-preferred path with
     * an internal fallback (requirement P3). The encoder's own 95 KB of state
     * is allocated inside the vendored library and cannot be redirected. */
    mi_enc_t *e = heap_caps_malloc(sizeof *e, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    bool external = e != NULL;
    if (!e) e = calloc(1, sizeof *e);
    if (!e) {
        set_err(err, err_size, "out of memory for the encoder stage");
        return NULL;
    }
    memset(e, 0, sizeof *e);
    e->external = external;
    e->out_hz = out_hz;
    e->bitrate = bitrate;

    shine_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.wave.channels = PCM_STEREO;
    cfg.wave.samplerate = (int)out_hz;
    shine_set_config_mpeg_defaults(&cfg.mpeg);
    cfg.mpeg.mode = JOINT_STEREO;
    cfg.mpeg.bitr = (int)bitrate;

    e->shine = shine_initialise(&cfg);
    if (!e->shine) {
        set_err(err, err_size, "the encoder refused %u Hz / %u kbps", (unsigned)out_hz, (unsigned)bitrate);
        heap_caps_free(e);
        return NULL;
    }
    e->spf = (size_t)shine_samples_per_pass(e->shine);
    if (e->spf == 0 || e->spf > MI_ENC_MAX_FRAME_SAMPLES) {
        set_err(err, err_size, "the encoder reports an unusable frame size (%u samples)", (unsigned)e->spf);
        shine_close(e->shine);
        heap_caps_free(e);
        return NULL;
    }
    return e;
}

void mi_enc_free(mi_enc_t *e)
{
    if (!e) return;
    if (e->shine) shine_close(e->shine);
    heap_caps_free(e);
}

size_t mi_enc_samples_per_frame(const mi_enc_t *e) { return e->spf; }
size_t mi_enc_pending_bytes(const mi_enc_t *e) { return e->pending_n; }
size_t mi_enc_buffered_frames(const mi_enc_t *e) { return e->acc_n; }
size_t mi_enc_state_bytes(void) { return sizeof(shine_global_config); }
bool mi_enc_uses_external_memory(const mi_enc_t *e) { return e->external; }
uint64_t mi_enc_frames_encoded(const mi_enc_t *e) { return e->frames_encoded; }
uint64_t mi_enc_encode_us(const mi_enc_t *e) { return e->encode_us; }
size_t mi_enc_desync_bytes(const mi_enc_t *e) { return e->desync_bytes; }

/* Hand on every whole frame currently sitting in `pending`. */
static void drain(mi_enc_t *e, mi_enc_out_t out, void *ctx)
{
    size_t off = 0;
    while (e->pending_n - off >= 4) {
        size_t len = frame_bytes(e->pending + off);
        if (len == 0) { /* cannot happen with our own encoder; resync rather than spin */
            e->desync_bytes++;
            off++;
            continue;
        }
        if (e->pending_n - off < len) break; /* wait for the rest of this frame */
        out(e->pending + off, len, ctx);
        off += len;
    }
    if (off) {
        memmove(e->pending, e->pending + off, e->pending_n - off);
        e->pending_n -= off;
    }
}

static void push_bytes(mi_enc_t *e, const uint8_t *b, size_t n, mi_enc_out_t out, void *ctx)
{
    while (n) {
        size_t room = sizeof e->pending - e->pending_n;
        if (room == 0) {
            drain(e, out, ctx);
            room = sizeof e->pending - e->pending_n;
            if (room == 0) { /* a single frame larger than the buffer: drop and count */
                e->desync_bytes += n;
                return;
            }
        }
        size_t k = (n < room) ? n : room;
        memcpy(e->pending + e->pending_n, b, k);
        e->pending_n += k;
        b += k;
        n -= k;
        drain(e, out, ctx);
    }
}

/* One whole encoder frame from the accumulator. */
static void encode_block(mi_enc_t *e, mi_enc_out_t out, void *ctx)
{
    int written = 0;
    int64_t t0 = esp_timer_get_time();
    unsigned char *b = shine_encode_buffer_interleaved(e->shine, e->acc, &written);
    e->encode_us += (uint64_t)(esp_timer_get_time() - t0);
    e->frames_encoded++;
    if (b && written > 0) push_bytes(e, b, (size_t)written, out, ctx);
}

void mi_enc_feed(mi_enc_t *e, const int16_t *stereo, size_t frames,
                 mi_enc_out_t out, void *ctx)
{
    while (frames) {
        size_t room = e->spf - e->acc_n;
        size_t k = (frames < room) ? frames : room;
        memcpy(e->acc + 2 * e->acc_n, stereo, k * 2 * sizeof(int16_t));
        e->acc_n += k;
        stereo += 2 * k;
        frames -= k;
        if (e->acc_n == e->spf) {
            encode_block(e, out, ctx);
            e->acc_n = 0;
        }
    }
}

void mi_enc_flush(mi_enc_t *e, mi_enc_out_t out, void *ctx)
{
    if (e->acc_n) { /* complete the block with silence: <= 26 ms at 44.1 kHz */
        memset(e->acc + 2 * e->acc_n, 0, (e->spf - e->acc_n) * 2 * sizeof(int16_t));
        encode_block(e, out, ctx);
        e->acc_n = 0;
    }
    /* shine_flush() is not a flush: measured, it reports 0 bytes pending after
     * full frames, on a fresh encoder and repeatedly. Call it anyway so that a
     * future version which does drain the reservoir is not silently ignored,
     * and forward anything it hands back. */
    int written = 0;
    unsigned char *b = shine_flush(e->shine, &written);
    if (b && written > 0) {
        ESP_LOGW(TAG, "the encoder's flush produced %d bytes (it normally produces none)", written);
        push_bytes(e, b, (size_t)written, out, ctx);
    }
    drain(e, out, ctx);
}

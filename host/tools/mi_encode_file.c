/*
 * The deterministic one-shot encoding pipeline: decode a track, resample it to
 * the station rate, encode it to MP3 and flush. This is the documented procedure
 * behind T2b (the end-of-track tail) and behind the host/board digest comparison
 * in T11: it uses the same production files the firmware does, with no HTTP, no
 * pacing and no sockets in the way.
 *
 *   mi_encode_file <in.{mp3,flac,wav}> <out.mp3> <out_hz> [bitrate_kbps]
 *
 * Note for the digest comparison: with out_hz equal to the source rate the
 * resampler is a pass-through, so the only arithmetic involved is fixed-point and
 * the output must be byte-identical on host and board. With a rate conversion the
 * resampler's floating-point kernel is in the path, and only sample-rate
 * correctness and an SNR bound may be claimed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mi_decode.h"
#include "mi_encode.h"
#include "mi_resample.h"

typedef struct {
    FILE    *out;
    uint64_t frames;  /* whole encoded frames written */
    uint64_t bytes;
} sink_t;

static sink_t  g_sink;
static mi_enc_t *g_enc;

static void on_frame(const uint8_t *frame, size_t bytes, void *ctx)
{
    (void)ctx;
    fwrite(frame, 1, bytes, g_sink.out);
    g_sink.frames++;
    g_sink.bytes += bytes;
}

static void on_pcm(const int16_t *stereo, size_t frames, void *ctx)
{
    mi_enc_feed((mi_enc_t *)ctx, stereo, frames, on_frame, NULL);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <in.{mp3,flac,wav}> <out.mp3> <out_hz> [bitrate_kbps]\n", argv[0]);
        return 2;
    }
    uint32_t out_hz = (uint32_t)strtoul(argv[3], NULL, 10);
    uint32_t bitrate = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 10) : 128;

    char err[200];
    mi_dec_t *d = mi_dec_open(argv[1]);
    if (!d) {
        fprintf(stderr, "cannot decode %s\n", argv[1]);
        return 1;
    }
    g_enc = mi_enc_new(out_hz, bitrate, err, sizeof err);
    if (!g_enc) {
        fprintf(stderr, "encoder refused: %s\n", err);
        mi_dec_close(d);
        return 1;
    }
    mi_rs_t *rs = mi_rs_new(mi_dec_hz(d), out_hz, err, sizeof err);
    if (!rs) {
        fprintf(stderr, "resampler refused: %s\n", err);
        mi_enc_free(g_enc);
        mi_dec_close(d);
        return 1;
    }

    memset(&g_sink, 0, sizeof g_sink);
    g_sink.out = fopen(argv[2], "wb");
    if (!g_sink.out) {
        perror(argv[2]);
        return 1;
    }

    static int16_t pcm[MI_READ_FRAMES * 2];
    size_t n;
    uint64_t in_frames = 0;
    while ((n = mi_dec_read(d, pcm, MI_READ_FRAMES)) > 0) {
        in_frames += n;
        mi_rs_process(rs, pcm, n, on_pcm, g_enc);
    }
    mi_enc_flush(g_enc, on_frame, NULL);
    fclose(g_sink.out);

    printf("in=%s in_hz=%u in_frames=%llu out_hz=%u bitrate=%u out_bytes=%llu "
           "out_frames=%llu encoder_calls=%llu withheld_bytes=%u\n",
           argv[1], (unsigned)mi_dec_hz(d), (unsigned long long)in_frames, (unsigned)out_hz,
           (unsigned)bitrate, (unsigned long long)g_sink.bytes,
           (unsigned long long)g_sink.frames,
           (unsigned long long)mi_enc_frames_encoded(g_enc), (unsigned)mi_enc_pending_bytes(g_enc));

    mi_rs_free(rs);
    mi_enc_free(g_enc);
    mi_dec_close(d);
    return 0;
}

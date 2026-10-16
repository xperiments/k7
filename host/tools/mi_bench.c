/*
 * T11h: the host encoder-cost baseline.
 *
 * Times the encode loop with a monotonic clock over at least a million frames and
 * reports the cost per frame. This is scaling data only - never a pass or fail.
 * Host MHz says nothing about 240 MHz Xtensa with Wi-Fi active, and the board's
 * own measurement (T11) is the one that decides the station format.
 *
 *   mi_bench [frames]      (default 1000000)
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "mi_encode.h"
#include "test_util.h" /* pcm_gen_*: the same deterministic input the tests use */

static uint64_t g_bytes;
static uint64_t g_frames_out;

static void count_frame(const uint8_t *frame, size_t bytes, void *ctx)
{
    (void)frame;
    (void)ctx;
    g_bytes += bytes;
    g_frames_out++;
}

int main(int argc, char **argv)
{
    long long frames = argc > 1 ? atoll(argv[1]) : 1000000;
    if (frames < 1000) frames = 1000;

    char err[200];
    mi_enc_t *e = mi_enc_new(44100, 128, err, sizeof err);
    if (!e) {
        fprintf(stderr, "encoder refused: %s\n", err);
        return 1;
    }

    static int16_t pcm[MI_ENC_MAX_FRAME_SAMPLES * 2];
    pcm_gen_t gen;
    pcm_gen_init(&gen, 99u);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (long long done = 0; done < frames; done += MI_ENC_MAX_FRAME_SAMPLES) {
        size_t n = (size_t)(frames - done);
        if (n > MI_ENC_MAX_FRAME_SAMPLES) n = MI_ENC_MAX_FRAME_SAMPLES;
        pcm_gen_fill(&gen, pcm, n);
        mi_enc_feed(e, pcm, n, count_frame, NULL);
    }
    mi_enc_flush(e, count_frame, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double wall_us = (double)(t1.tv_sec - t0.tv_sec) * 1e6 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e3;
    uint64_t inner_us = mi_enc_encode_us(e);
    uint64_t calls = mi_enc_frames_encoded(e);
    double frame_us = 1e6 * MI_ENC_MAX_FRAME_SAMPLES / 44100.0; /* real-time budget per frame */

    printf("encode baseline (host): %" PRIu64 " encoder calls over %lld input frames\n", calls, frames);
    printf("  wall:    %10.0f us total, %8.2f us/frame, %6.2f%% of one core equivalent\n", wall_us,
           calls ? wall_us / (double)calls : 0.0, calls ? 100.0 * (wall_us / (double)calls) / frame_us : 0.0);
    printf("  inside:  %10" PRIu64 " us total, %8.2f us/frame, %6.2f%% of one core equivalent\n",
           inner_us, calls ? (double)inner_us / (double)calls : 0.0,
           calls ? 100.0 * ((double)inner_us / (double)calls) / frame_us : 0.0);
    printf("  output:  %" PRIu64 " bytes in %" PRIu64 " whole frames\n", g_bytes, g_frames_out);
    printf("  note:    host scaling data only; T11 on the board is what decides the format\n");

    mi_enc_free(e);
    return 0;
}

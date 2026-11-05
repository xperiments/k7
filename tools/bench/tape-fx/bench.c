/*
 * Host cost bench for tape_fx, shaped like the station's audio path:
 * 1024-frame stereo chunks at 44.1 kHz, exactly how mini_icecast feeds the
 * encoder (MINI_ICECAST_CHUNK_FRAMES = 1024, out_hz = 44100).
 *
 * Only tape_fx_process_s16 is timed: the PCM is generated once into a pool of
 * distinct chunks and replayed, so the source synthesis never enters the number.
 *
 *   ./bench [seconds] [channels] [silent]
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tape_fx.h"

#define CHUNK 1024
#define POOL  32

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int16_t pool[POOL][CHUNK * 2];

/* Deterministic stereo source: a few tones plus broadband noise, about -12 dBFS. */
static void build_pool(int ch, int silent)
{
    unsigned seed = 12345u;
    for (int k = 0; k < POOL; k++) {
        for (size_t i = 0; i < CHUNK; i++) {
            for (int c = 0; c < ch; c++) {
                unsigned s = (seed = seed * 1664525u + 1013904223u);
                float n = (float)((int)(s >> 8) - 8388608) / 8388608.0f;
                float v;
                if (silent) {
                    v = 0.0f;
                } else {
                    double ph = 2.0 * M_PI * (440.0 * (c + 1)) * (double)(k * CHUNK + i) / 44100.0;
                    v = (float)(0.4 * sin(ph)) + 0.1f * n;
                }
                int32_t q = (int32_t)(v * 32767.0f);
                if (q > 32767) q = 32767;
                if (q < -32768) q = -32768;
                pool[k][i * ch + c] = (int16_t)q;
            }
        }
    }
}

int main(int argc, char **argv)
{
    double seconds = argc > 1 ? atof(argv[1]) : 20.0;
    int ch = argc > 2 ? atoi(argv[2]) : 2;
    int silent = argc > 3 ? atoi(argv[3]) : 0;
    if (ch < 1) ch = 1;
    if (ch > 2) ch = 2;

    static tape_fx_t fx;
    tape_fx_init(&fx, 44100.f);
    build_pool(ch, silent);

    long chunks = (long)(seconds * 44100.0 / CHUNK);
    if (chunks < POOL) chunks = POOL;

    /* Warm up (cache, coefficient settle), then time. */
    tape_fx_process_s16(&fx, pool[0], CHUNK, ch);

    double t0 = now_s();
    for (long k = 0; k < chunks; k++)
        tape_fx_process_s16(&fx, pool[k % POOL], CHUNK, ch);
    double t1 = now_s();

    long long acc = 0;
    for (int k = 0; k < POOL; k++)
        for (size_t i = 0; i < CHUNK * (size_t)ch; i++) acc += pool[k][i];

    double per_chunk_ns = (t1 - t0) * 1e9 / (double)chunks;
    double audio_s = (double)chunks * CHUNK / 44100.0;
    printf("tape_fx host bench: %ld chunks x %d frames, %d ch%s\n", chunks, CHUNK, ch,
           silent ? ", silent input" : "");
    printf("  %8.1f ns/chunk  %6.2f ns/frame  %6.2f%% of one host core at 44.1 kHz\n",
           per_chunk_ns, per_chunk_ns / CHUNK, 100.0 * (t1 - t0) / audio_s);
    printf("  (checksum %lld)\n", acc);
    return 0;
}

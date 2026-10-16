#pragma once
/*
 * Shared scaffolding for the host unit tests: assertions, a deterministic PCM
 * generator (integer-only, so it produces the same samples on any host) and an
 * MP3 frame walker used to check that what comes out is whole encoded frames.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_checks;
static int test_failures;

#define CHECK(cond, ...)                                                                     \
    do {                                                                                     \
        test_checks++;                                                                       \
        if (!(cond)) {                                                                        \
            test_failures++;                                                                  \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                        \
            printf(__VA_ARGS__);                                                              \
            printf("\n");                                                                     \
        }                                                                                     \
    } while (0)

#define CHECK_EQ(got, want, what)                                                            \
    CHECK((long long)(got) == (long long)(want), "%s: got %lld, want %lld", what,              \
          (long long)(got), (long long)(want))

#define TEST_END()                                                                            \
    do {                                                                                      \
        printf("%-28s %4d checks, %d failures\n", __FILE__, test_checks, test_failures);        \
        return test_failures ? 1 : 0;                                                          \
    } while (0)

/* ------------------------------------------------------------ MP3 frames */

static const uint16_t k_mp3_bitrates[2][16] = {
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
};
static const uint16_t k_mp3_rates[3][3] = {
    {44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000},
};

/* Length of the MP3 frame starting at h, or 0 when h is not a frame header. */
static size_t mp3_frame_len(const uint8_t *h)
{
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return 0;
    unsigned version = (h[1] >> 3) & 3, layer = (h[1] >> 1) & 3;
    if (version == 1 || layer != 1) return 0;
    unsigned bri = (h[2] >> 4) & 15, sri = (h[2] >> 2) & 3, pad = (h[2] >> 1) & 1;
    if (bri == 0 || bri == 15 || sri == 3) return 0;
    unsigned br = k_mp3_bitrates[version == 3 ? 0 : 1][bri];
    unsigned sr = k_mp3_rates[version == 3 ? 0 : version == 2 ? 1 : 2][sri];
    if (!br || !sr) return 0;
    return ((version == 3) ? 144u : 72u) * br * 1000u / sr + pad;
}

/* Walks `data` as a sequence of whole MP3 frames. Returns the number of frames,
 * or -1 if a frame boundary is not a frame header, and reports how many bytes
 * are left over after the last whole frame. */
static long mp3_count_frames(const uint8_t *data, size_t n, size_t *leftover)
{
    size_t off = 0;
    long frames = 0;
    while (off + 4 <= n) {
        size_t len = mp3_frame_len(data + off);
        if (len == 0) return -1;
        if (off + len > n) break;
        off += len;
        frames++;
    }
    if (leftover) *leftover = n - off;
    return frames;
}

/* ------------------------------------------------- deterministic PCM input */

typedef struct {
    uint32_t state;
    uint32_t phase;
} pcm_gen_t;

static void pcm_gen_init(pcm_gen_t *g, uint32_t seed)
{
    g->state = seed ? seed : 1;
    g->phase = 0;
}

static uint32_t pcm_next(pcm_gen_t *g)
{
    g->state = g->state * 1103515245u + 12345u; /* the usual LCG: identical everywhere */
    return (g->state >> 8) & 0xFFFFFF;
}

/* A mix of noise and two slowly varying tones, comfortably inside int16. */
static void pcm_gen_fill(pcm_gen_t *g, int16_t *stereo, size_t frames)
{
    for (size_t i = 0; i < frames; i++) {
        int32_t l = (int32_t)(pcm_next(g) % 7001) - 3500;
        int32_t r = (int32_t)(pcm_next(g) % 7001) - 3500;
        g->phase += 97;
        stereo[2 * i] = (int16_t)(l + ((g->phase >> 3) & 0x1FFF) - 4096);
        stereo[2 * i + 1] = (int16_t)(r + ((g->phase >> 5) & 0x1FFF) - 4096);
    }
}

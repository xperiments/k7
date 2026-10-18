#pragma once
/*
 * cassette - what the cassette screen should show, with no GUI toolkit.
 *
 * The screen's arithmetic used to live in main/ui_cassette.c, behind LVGL
 * objects, where the only way to observe it was to look at a panel or decode a
 * serial framebuffer dump. The rules are small and easy to get wrong: a reel
 * advances one art frame per loop_ms / frame_count, a long gap between calls must
 * not skip the whole loop, the supply and take-up packs trade size with the
 * track's position, the label drops parenthesised asides, and the progress bar
 * must not be rewritten for a sub-pixel change.
 *
 * This module owns all of that and returns a frame. The adapter in
 * main/ui_cassette.c owns the pixels: positions, fonts, diameters, LVGL objects.
 * There is no LVGL include here, so host/Makefile can compile it.
 */
#include <stdbool.h>
#include <stdint.h>

#include "mini_icecast.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CASSETTE_TITLE_MAX 128
#define CASSETTE_DEFAULT_BAR_MS 1000

typedef struct {
    char     title[CASSETTE_TITLE_MAX]; /* the label text, cleaned; empty = nothing to play */
    bool     title_changed;             /* the adapter only rewrites the label when true */
    uint32_t permille;                  /* position through the track, 0..1000 */
    bool     permille_changed;          /* the adapter only moves the bar when true */
    uint16_t pack_fill[2];              /* 0..1000: [0] supply, [1] take-up */
    uint8_t  hub_frame[2];              /* index into the reel art, one loop = frame_count */
    bool     playing;                   /* reels run only while this is true */
    float    level_l, level_r;          /* 0..1 RMS, passed through from the status */
} cassette_frame_t;

typedef struct cassette cassette_t;

/* `loop_ms` is the length of the reel art's loop, `frame_count` its frames, and
 * `bar_period_ms` the slowest the progress bar may update (0 = every step).
 * Returns NULL when the geometry cannot advance (zero loop or no frames). */
cassette_t *cassette_new(uint32_t loop_ms, uint16_t frame_count, uint32_t bar_period_ms);
void        cassette_free(cassette_t *c);

/* Advance to `now_ms` and return the frame to draw. `st` is the latest status
 * snapshot; NULL advances time only and keeps the last status, which is what the
 * reel timer calls. The frame is owned by `c` and valid until the next step. */
const cassette_frame_t *cassette_step(cassette_t *c, const mini_icecast_status_t *st, uint32_t now_ms);

/* Back to the empty screen: no track, reels at frame 0, nothing reported changed
 * except the title and bar (so the adapter redraws them once). */
void cassette_reset(cassette_t *c);

#ifdef __cplusplus
}
#endif

#pragma once
/*
 * cassette - what the cassette screen should show, with no GUI toolkit.
 *
 * The screen's arithmetic used to live in components/app/screen_display/cassette_view.c,
 * behind LVGL objects, where the only way to observe it was to look at a panel or decode a
 * serial framebuffer dump. The rules are small and easy to get wrong: a reel turns as
 * the tape geometry says - the tape moves at 47.625 mm/s, the hub is 22 mm across,
 * and a full pack is about 48 mm, so a reel takes 1.45 s per revolution when empty
 * and 3.1-3.2 s when full. The supply and take-up sit at opposite ends of that range.
 * The art's `frame_count` is the angular resolution: one frame per 360/frame_count
 * degrees. A stopped track holds the last frame, the packs trade size with the
 * track's position, the label drops parenthesised asides, and the progress bar must
 * not be rewritten for a sub-pixel change. A track change does not snap to the new
 * side: for CASSETTE_REWIND_MS the tape runs back to the start on screen and lands
 * exactly where normal play picks it up, so the reel change has no break in it.
 *
 * This module owns all of that and returns a frame. The adapter in
 * components/app/screen_display/cassette_view.c owns the pixels: positions, fonts,
 * diameters, LVGL objects. There is no LVGL include here, so host/Makefile can compile it.
 */
#include <stdbool.h>
#include <stdint.h>

#include "mini_icecast.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CASSETTE_TITLE_MAX 128
#define CASSETTE_DEFAULT_BAR_MS 1000

/* The standard compact cassette's geometry. The tape speed is the 1 7/8 ips
 * standard (47.625 mm/s); the hub is 22 mm across; a full C60/C90 pack is about
 * 48 mm. Each reel's phase integrates the speed at its own pack radius, so the
 * supply and take-up turn at different speeds. */
#define CASSETTE_HUB_RADIUS_MM   11.0f
#define CASSETTE_FULL_RADIUS_MM  24.0f
#define CASSETTE_TAPE_SPEED_MM_S 47.625f
/* The empty reel keeps a minimum tape load: with a bare hub the pack's edge sits
 * under the hub ring, hiding the tape arc and the VU meter. */
#define CASSETTE_MIN_FILL 80
/* The reel change. When the track changes, the outgoing side has left its tape on the
 * take-up reel; for the new song's first CASSETTE_REWIND_MS the screen shows that tape
 * running back to the start - both packs migrating and both hubs turning in reverse -
 * rather than jumping there. The window opens where the screen first sees the new
 * track (normally within one poll of its start) so the first frame cannot jump, and it
 * closes on exactly the pack fill and hub phase normal play shows at that moment.
 *
 * The reverse spin is a whole number of revolutions: the art is the angular resolution,
 * so only the fractional part is visible, and rounding it per reel keeps the frame on
 * screen from changing at the cut. REVS is a perceptual speed, not the tape's: a real
 * rewind of a full side runs at hundreds of revolutions in this window, which the
 * 36-frame art cannot show at the panel's 25 fps. 11 is about 2 art frames per
 * 40 ms update: fast enough to read as a rewind, slow enough to trace one frame at a
 * time, where one frame per update would read as slow motion.
 *
 * The whole transport decelerates over the last EASE_MS so the reels wind down into
 * the start of the song. The progress bar and the label are not part of the fiction:
 * the bar snaps to the new position and the label names the new song. The VU data still
 * follows the new audio from the first frame, but the view cross-fades the tape's red
 * edge to the tape's own colour and fades the meters out for the window, so nothing is
 * metered while the tape winds back. */
#define CASSETTE_REWIND_MS      8000
#define CASSETTE_REWIND_EASE_MS 1200
#define CASSETTE_REWIND_REVS    11.0f
/* A track whose status has no duration still needs a side for the reel fill. */
#define CASSETTE_NOMINAL_SIDE_MS 1800000u  /* 30 min */

typedef struct {
    char     title[CASSETTE_TITLE_MAX]; /* the label text, cleaned; empty = nothing to play */
    bool     title_changed;             /* the adapter only rewrites the label when true */
    uint32_t permille;                  /* position through the track, 0..1000 */
    bool     permille_changed;          /* the adapter only moves the bar when true */
    uint16_t pack_fill[2];              /* CASSETTE_MIN_FILL..1000: [0] supply, [1] take-up */
    uint8_t  hub_frame[2];              /* index into the reel art, one revolution = frame_count */
    bool     rewinding;                 /* a track change is playing the tape back to the start */
    uint16_t rewind_permille;           /* 0..1000 through that window; 0 when not rewinding */
    bool     playing;                   /* the reels follow the audio only while this is true */
    float    level_l, level_r;          /* 0..1 RMS, passed through from the status */
    float    vu_l, vu_r;                /* 0..1 peak-hold of the RMS, for the VU meter */
} cassette_frame_t;

typedef struct cassette cassette_t;

/* `frame_count` is the art's frames per revolution - the angular resolution - and
 * `bar_period_ms` the slowest the progress bar may update (0 = every step).
 * Returns NULL when there is no art to advance. */
cassette_t *cassette_new(uint16_t frame_count, uint32_t bar_period_ms);
void        cassette_free(cassette_t *c);

/* Advance to the status's tape position and return the frame to draw. `st` is the
 * latest status snapshot; NULL keeps the last status and is what the first draw and
 * a status-less refresh use. Each reel's phase integrates the tape speed at its own
 * pack radius, so the supply and take-up turn at different speeds. A stopped track
 * holds the frames. A track change - the status's elapsed going backwards while it
 * was playing - opens the rewind window described above, so the frames and pack fills
 * come from that window instead of jumping; the first status after boot is not a
 * change, because it has no side to rewind from. The frame is owned by `c` and valid
 * until the next step. */
const cassette_frame_t *cassette_step(cassette_t *c, const mini_icecast_status_t *st, uint32_t now_ms);

/* Back to the empty screen: no track, reels at frame 0, nothing reported changed
 * except the title and bar (so the adapter redraws them once). */
void cassette_reset(cassette_t *c);

#ifdef __cplusplus
}
#endif

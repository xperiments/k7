#pragma once
/*
 * The render bench: what one frame of each cassette area costs on the panel.
 *
 * A diagnostic build (CONFIG_APP_UI_RENDER_BENCH), not a feature. It freezes the view on
 * the cassette, dirties one area at a time - or runs the real animation step that dirties
 * it - and times the refresh cycle LVGL answers with. The numbers are split into the draw
 * (the render before the first flush) and the panel wait, because in direct mode every
 * flush blocks on the RGB frame buffer's completion (panel_rgb.c).
 *
 * Both poses the owner asked about are the entry pose (the two-finger pose is Initial plus
 * the Card at the entry grid), so the view side owns every rect: it is the composition
 * that knows where its parts are. render_bench.c owns the state machine and the clock.
 */
#include <stdbool.h>

#include "sdkconfig.h"
#include "lvgl.h"

#if CONFIG_APP_UI_RENDER_BENCH

typedef enum {
    CASSETTE_BENCH_HUB_TICK = 0,   /* the real animation step: both hubs advance one frame */
    CASSETTE_BENCH_TAPE_FILL,      /* the real animation step: both packs change diameter */
    CASSETTE_BENCH_TAPE_FILL_BORDER, /* the same step with the preview's body left out */
    CASSETTE_BENCH_HUB_RIM_LEFT,
    CASSETTE_BENCH_HUB_RIM_RIGHT,
    CASSETTE_BENCH_PACK_LEFT,
    CASSETTE_BENCH_PACK_RIGHT,
    CASSETTE_BENCH_PACK_BORDER,    /* the left pack's box with the preview's body left out */
    CASSETTE_BENCH_STRAND_LEFT,
    CASSETTE_BENCH_STRAND_RIGHT,
    CASSETTE_BENCH_LABEL,          /* the whole print, as a design change repaints it */
    CASSETTE_BENCH_LABEL_TEXT,     /* only the text objects, as a Track change repaints them */
    CASSETTE_BENCH_CARD,
    CASSETTE_BENCH_WINDOW,
    CASSETTE_BENCH_TAPE_BODY,
    CASSETTE_BENCH_SCREEN,
    CASSETTE_BENCH_STEP_COUNT
} cassette_bench_step_t;

typedef struct {
    int x1, y1, x2, y2;
} cassette_bench_rect_t;

/* What a live frame carried. The view sets INK and LABEL, the tape body HUB and FILL;
 * the live bench ORs them between renders and takes them once per rendered frame. */
#define CASSETTE_BENCH_CH_HUB   (1u << 0)
#define CASSETTE_BENCH_CH_FILL  (1u << 1)
#define CASSETTE_BENCH_CH_INK   (1u << 2)
#define CASSETTE_BENCH_CH_LABEL (1u << 3)
uint32_t cassette_bench_take_changes(void);

/* The bench's clock and state machine. Called once, after the view exists. */
void render_bench_init(lv_display_t *disp);

/* The view side, all under the LVGL lock. A frozen view still builds and draws; it stops
 * taking the model's frames, so the only invalidation in a cycle is the bench's. */
void cassette_bench_freeze(bool on);
bool cassette_bench_frozen(void);

/* Put the picture in pose 0 (the default entry screen) or 1 (the two-finger pose) through
 * the same intents a finger sends. */
void cassette_bench_pose(int pose);

/* The annulus A/B: dirty only the band a fill moves through (and re-place the strand only
 * every 8 px), instead of resizing the pack and the strand every bar. */
void cassette_bench_annulus(bool on);

/* Fire one step at the current pose. A mutator step invalidates by itself; a rect step
 * fills `rects` and returns their count for the caller to invalidate. Returns -1 when the
 * step's area is not on screen at this pose. */
int cassette_bench_step(cassette_bench_step_t step, cassette_bench_rect_t *rects, int max);

const char *cassette_bench_step_name(cassette_bench_step_t step);

#else

static inline void render_bench_init(lv_display_t *disp) { (void)disp; }
static inline bool cassette_bench_frozen(void) { return false; }

#endif

/*
 * render_bench.c - see cassette_bench.h.
 *
 * A diagnostic build's clock. It owns no geometry: it asks the view for a pose, an area or
 * a real animation step, and prices the refresh cycle LVGL answers with.
 *
 * The split is LVGL's own: LV_EVENT_REFR_START opens the cycle (layout, the direct-mode
 * buffer sync, then the draws), LV_EVENT_FLUSH_START/FINISH bracket each flush, and
 * LV_EVENT_RENDER_READY closes it. In the panel's direct mode the *last* flush of a cycle
 * ends in esp_lcd_panel_draw_bitmap() + a counting-semaphore wait for the RGB frame to
 * complete (esp_lvgl_port_disp.c, `lvgl_port_flush_callback`); the earlier ones return
 * after `lv_disp_flush_ready`. So the cycle is draw + one panel wait, and the two are
 * reported apart. A direct-mode cycle flushes once per invalidated rectangle, so `nflush`
 * is a result too: the count, not the pixels, is what a tick costs once a frame is bound
 * to the panel.
 *
 * The bench freezes the view (station updates stop), pauses the performance monitor if one
 * is built, and then dirties one area at a time. Every frame it measures is therefore the
 * frame it asked for.
 */
#include "sdkconfig.h"

#if CONFIG_APP_UI_RENDER_BENCH

#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "cassette_bench.h"

static const char *TAG = "bench";

#define BENCH_START_MS  40000   /* the screen is live and the first Track's label is built */
#define BENCH_SETTLE_MS   800   /* let the pose change's own frames finish */
#define BENCH_MAX_RECTS     4

#if CONFIG_APP_UI_RENDER_BENCH_LIVE
/* ------------------------------------------------------------------ live */

#include <stdio.h>
#include <string.h>

#define LIVE_SETTLE_MS   3000
#define LIVE_WINDOW_MS  30000   /* per variant: base, then the annulus */

typedef struct {
    uint32_t n;
    uint32_t draw_us;
    uint32_t flush_us;
    uint32_t rects;
} live_bucket_t;

static lv_display_t *s_disp;
static int      s_phase;         /* 0 wait, 1 settle A, 2 run A, 3 settle B, 4 run B, 5 done */
static uint32_t s_settle_until;
static uint32_t s_run_until;
static uint32_t s_frames;
static uint32_t s_draw_total;
static uint32_t s_flush_total;
static uint32_t s_rects_total;
static live_bucket_t s_bucket[16];

static int64_t  s_t_cycle;
static int64_t  s_t_flush;
static uint32_t s_flush_acc;
static uint16_t s_flush_n;
static bool     s_rendering;

static const char *live_mask_name(uint32_t mask, char *buf, size_t size)
{
    size_t at = 0;
    buf[0] = '\0';
    static const char *const k_bit[4] = { "hub", "fill", "ink", "label" };
    for (int i = 0; i < 4; i++) {
        if (!(mask & (1u << i))) continue;
        const int n = snprintf(buf + at, size - at, "%s%s", at ? "+" : "", k_bit[i]);
        if (n < 0 || (size_t)n >= size - at) break;
        at += (size_t)n;
    }
    if (!at) snprintf(buf, size, "none");
    return buf;
}

static void live_log(const char *variant)
{
    ESP_LOGI(TAG, "live variant=%s window=%u ms frames=%u fps_x10=%u draw_total_us=%u "
             "draw_mean_us=%u flush_total_us=%u rects_mean=%u",
             variant, (unsigned)LIVE_WINDOW_MS, (unsigned)s_frames,
             (unsigned)(s_frames ? (uint32_t)((uint64_t)s_frames * 10000u / LIVE_WINDOW_MS) : 0),
             (unsigned)s_draw_total,
             (unsigned)(s_frames ? s_draw_total / s_frames : 0),
             (unsigned)s_flush_total,
             (unsigned)(s_frames ? s_rects_total / s_frames : 0));

    for (int m = 0; m < 16; m++) {
        if (!s_bucket[m].n) continue;
        char name[48];
        ESP_LOGI(TAG, "live mask=%s n=%u draw_mean_us=%u draw_total_us=%u "
                 "share_permille=%u flush_mean_us=%u rects_mean=%u",
                 live_mask_name((uint32_t)m, name, sizeof name),
                 (unsigned)s_bucket[m].n,
                 (unsigned)(s_bucket[m].draw_us / s_bucket[m].n),
                 (unsigned)s_bucket[m].draw_us,
                 (unsigned)(s_draw_total ? (uint32_t)((uint64_t)s_bucket[m].draw_us * 1000u /
                                                      s_draw_total) : 0),
                 (unsigned)(s_bucket[m].flush_us / s_bucket[m].n),
                 (unsigned)(s_bucket[m].rects / s_bucket[m].n));
    }
}

static void live_on_display(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    const int64_t now = esp_timer_get_time();

    switch (code) {
    case LV_EVENT_REFR_START:
        s_t_cycle = now;
        s_flush_acc = 0;
        s_flush_n = 0;
        s_rendering = false;
        break;

    case LV_EVENT_RENDER_START:
        s_rendering = true;
        break;

    case LV_EVENT_FLUSH_START:
        s_t_flush = now;
        break;

    case LV_EVENT_FLUSH_FINISH:
        s_flush_acc += (uint32_t)(now - s_t_flush);
        s_flush_n++;
        break;

    case LV_EVENT_RENDER_READY: {
        if (!s_rendering) break;
        s_rendering = false;
        const uint32_t mask = cassette_bench_take_changes();
        const uint32_t total = (uint32_t)(now - s_t_cycle);
        const uint32_t draw = total > s_flush_acc ? total - s_flush_acc : 0;
        live_bucket_t *b = &s_bucket[mask & 0xF];
        b->n++;
        b->draw_us += draw;
        b->flush_us += s_flush_acc;
        b->rects += s_flush_n;
        s_frames++;
        s_draw_total += draw;
        s_flush_total += s_flush_acc;
        s_rects_total += s_flush_n;
        break;
    }

    default:
        break;
    }
}

static void live_tick(lv_timer_t *t)
{
    (void)t;
    const uint32_t tick = lv_tick_get();

    if (s_phase == 5) return;

    if (s_phase == 0) {
        if (tick < BENCH_START_MS) return;
        s_phase = 1;
        s_settle_until = tick + LIVE_SETTLE_MS;
        cassette_bench_pose(1);
        cassette_bench_annulus(false);   /* window A is the old full-box path */
        ESP_LOGI(TAG, "live: two-finger pose up at %u ms, settling", (unsigned)tick);
        return;
    }

    if (s_phase == 1 || s_phase == 3) {
        if ((int32_t)(tick - s_settle_until) < 0) return;
        (void)cassette_bench_take_changes();   /* the switch's own dirt is not a frame */
        memset(s_bucket, 0, sizeof s_bucket);
        s_frames = s_draw_total = s_flush_total = s_rects_total = 0;
        s_phase = (s_phase == 1) ? 2 : 4;
        s_run_until = tick + LIVE_WINDOW_MS;
        ESP_LOGI(TAG, "live: %s window open for %d ms",
                 s_phase == 2 ? "base" : "annulus", LIVE_WINDOW_MS);
        return;
    }

    if ((int32_t)(tick - s_run_until) < 0) return;

    if (s_phase == 2) {
        live_log("base");
        cassette_bench_annulus(true);
        s_phase = 3;
        s_settle_until = tick + 1000;
        return;
    }

    live_log("annulus");
    cassette_bench_pose(0);
    s_phase = 5;
    ESP_LOGI(TAG, "live: done, the view is live again");
}

void render_bench_init(lv_display_t *disp)
{
    if (!disp) return;
    s_disp = disp;
    lv_display_add_event_cb(disp, live_on_display, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, live_on_display, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, live_on_display, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(disp, live_on_display, LV_EVENT_FLUSH_FINISH, NULL);
    lv_display_add_event_cb(disp, live_on_display, LV_EVENT_RENDER_READY, NULL);
    lv_timer_create(live_tick, 10, NULL);
    ESP_LOGI(TAG, "live bench armed: pose at %d ms, %d ms window", BENCH_START_MS,
             LIVE_WINDOW_MS);
}

#else

typedef struct {
    uint32_t total_us;
    uint32_t flush_us;
    uint32_t draw_us;
    uint16_t nflush;
} sample_t;

typedef enum {
    BENCH_PHASE_DEFAULT = 0,
    BENCH_PHASE_SWITCH,
    BENCH_PHASE_FULL,
    BENCH_PHASE_DONE,
} bench_phase_t;

/* Which pose each area is measured in: bit 0 is the default entry screen, bit 1 the
 * two-finger pose. The label, the Card and its window exist only in the second. */
static const struct {
    cassette_bench_step_t step;
    unsigned poses;
} k_steps[] = {
    { CASSETTE_BENCH_HUB_TICK,      0x3 },
    { CASSETTE_BENCH_TAPE_FILL,     0x3 },
    { CASSETTE_BENCH_TAPE_FILL_BORDER, 0x2 },
    { CASSETTE_BENCH_HUB_RIM_LEFT,  0x3 },
    { CASSETTE_BENCH_HUB_RIM_RIGHT, 0x3 },
    { CASSETTE_BENCH_PACK_LEFT,     0x3 },
    { CASSETTE_BENCH_PACK_BORDER,   0x2 },
    { CASSETTE_BENCH_PACK_RIGHT,    0x3 },
    { CASSETTE_BENCH_STRAND_LEFT,   0x3 },
    { CASSETTE_BENCH_STRAND_RIGHT,  0x3 },
    { CASSETTE_BENCH_LABEL,         0x2 },
    { CASSETTE_BENCH_LABEL_TEXT,    0x2 },
    { CASSETTE_BENCH_CARD,          0x2 },
    { CASSETTE_BENCH_WINDOW,        0x2 },
    { CASSETTE_BENCH_TAPE_BODY,     0x3 },
    { CASSETTE_BENCH_SCREEN,        0x3 },
};
#define BENCH_STEP_COUNT ((int)(sizeof k_steps / sizeof k_steps[0]))

static lv_display_t *s_disp;

static bool     s_started;
static bool     s_done;
static int      s_pose;          /* 0 default, 1 two-finger */
static int      s_phase;
static int      s_step;
static int      s_sample;
static int      s_target;
static bool     s_switch_fired;
static uint32_t s_settle_until;
static bool     s_pending;       /* an invalidation is waiting for its frame */
static bool     s_sample_ready;
static bool     s_discard;       /* this frame only drains the previous area's buffer sync */
static bool     s_need_flush;    /* a sync-drain frame is owed before the next sample */
static uint32_t s_area_px;
static char     s_drain_for[32]; /* the area whose buffer sync the drain frame is paying */

static sample_t s_got;
static uint32_t s_sum_total, s_sum_flush, s_sum_draw, s_sum_flush_n;
static uint32_t s_min_draw, s_max_draw;

static int64_t  s_t_render;
static int64_t  s_t_flush;
static uint32_t s_flush_acc;
static uint16_t s_flush_n;

/* ------------------------------------------------------------------ clock */

static void bench_stats_reset(void)
{
    s_sum_total = s_sum_flush = s_sum_draw = s_sum_flush_n = 0;
    s_min_draw = UINT32_MAX;
    s_max_draw = 0;
    s_sample = 0;
}

static void bench_log(const char *area)
{
    const uint32_t n = s_sample > 0 ? (uint32_t)s_sample : 1;
    ESP_LOGI(TAG,
             "area=%s pose=%s n=%u draw_us=%u draw_min_us=%u draw_max_us=%u "
             "flush_us=%u nflush=%u total_us=%u px=%u",
             area, s_pose ? "full" : "default", (unsigned)n,
             (unsigned)(s_sum_draw / n),
             (unsigned)s_min_draw, (unsigned)s_max_draw,
             (unsigned)(s_sum_flush / n),
             (unsigned)((s_sum_flush_n + n - 1) / n),
             (unsigned)(s_sum_total / n),
             (unsigned)s_area_px);
}

/* The drain frame is the direct-mode buffer sync of the area just measured: LVGL copies
 * the previous frame's invalidated areas into the buffer it is about to draw into
 * (lv_refr.c, refr_sync_areas()). One 1x1 invalidation makes that copy the frame's work,
 * so it is priced here and reported beside the area it belongs to. */
static void bench_log_sync(void)
{
    ESP_LOGI(TAG, "area=sync:%s pose=%s n=1 draw_us=%u flush_us=%u total_us=%u px=1",
             s_drain_for, s_pose ? "full" : "default",
             (unsigned)s_got.draw_us, (unsigned)s_got.flush_us, (unsigned)s_got.total_us);
}

static void bench_account(void)
{
    s_sum_total += s_got.total_us;
    s_sum_flush += s_got.flush_us;
    s_sum_draw += s_got.draw_us;
    s_sum_flush_n += s_got.nflush;
    if (s_got.draw_us < s_min_draw) s_min_draw = s_got.draw_us;
    if (s_got.draw_us > s_max_draw) s_max_draw = s_got.draw_us;
    s_sample++;
}

static void bench_on_display(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    const int64_t now = esp_timer_get_time();

    switch (code) {
    case LV_EVENT_REFR_START:
        /* The cycle starts here, not at RENDER_START: before the first draw LVGL lays the
         * screen out and, in direct double buffering, copies the previous frame's
         * invalidated areas into the buffer it is about to draw into
         * (lv_refr.c: refr_sync_areas()). That copy is part of the area's cost. */
        s_t_render = now;
        s_flush_acc = 0;
        s_flush_n = 0;
        break;

    case LV_EVENT_FLUSH_START:
        s_t_flush = now;
        break;

    case LV_EVENT_FLUSH_FINISH:
        s_flush_acc += (uint32_t)(now - s_t_flush);
        s_flush_n++;
        break;

    case LV_EVENT_RENDER_READY:
        if (!s_pending) break;
        s_got.total_us = (uint32_t)(now - s_t_render);
        s_got.flush_us = s_flush_acc;
        s_got.draw_us = s_got.total_us > s_flush_acc ? s_got.total_us - s_flush_acc : 0;
        s_got.nflush = s_flush_n;
        s_pending = false;
        s_sample_ready = true;
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ steps */

/* Fire the current step. A step whose area is hidden at this pose is logged and skipped. */
static void bench_fire(void)
{
    for (;;) {
        if (s_step >= BENCH_STEP_COUNT) return;

        if (!(k_steps[s_step].poses & (1u << s_pose))) {
            s_step++;
            continue;
        }

        const cassette_bench_step_t step = k_steps[s_step].step;
        cassette_bench_rect_t rects[BENCH_MAX_RECTS];
        const int n = cassette_bench_step(step, rects, BENCH_MAX_RECTS);
        if (n < 0) {
            ESP_LOGI(TAG, "area=%s pose=%s hidden", cassette_bench_step_name(step),
                     s_pose ? "full" : "default");
            s_step++;
            continue;
        }

        uint32_t px = 0;
        for (int i = 0; i < n; i++) {
            lv_area_t a = { rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2 };
            px += (uint32_t)((a.x2 - a.x1 + 1) * (a.y2 - a.y1 + 1));
            lv_obj_invalidate_area(lv_screen_active(), &a);
        }
        s_area_px = px;

        /* A big area's frame is seconds long at the two-finger pose; three samples price
         * it and keep the run short. The small ones take a few: each sample is one panel
         * frame whatever its area. */
        s_target = px > 300000 ? 3 : (px > 120000 ? 4 : 6);
        s_pending = true;
        return;
    }
}

/* A direct-mode frame swaps the buffers, so the next frame first copies the area it drew
 * into the other one (lv_refr.c's refr_sync_areas()). Left alone, that copy would land in
 * the next area's sample. One 1x1 invalidation spends it on a frame the bench throws away. */
static void bench_sync_drain(void)
{
    const lv_area_t one = { 0, 0, 0, 0 };
    lv_obj_invalidate_area(lv_screen_active(), &one);
    s_discard = true;
    s_pending = true;
}

/* The two-finger pose, as one frame: the Card, the label, the page flip and the tape's
 * re-clamp all land together, which is the cost the owner asked about. */
static void bench_switch_fire(void)
{
    bench_stats_reset();
    cassette_bench_pose(1);
    s_area_px = 800u * 480u;
    s_target = 1;
    s_pending = true;
}

static void bench_finish(void)
{
    s_phase = BENCH_PHASE_DONE;
    s_done = true;
    cassette_bench_pose(0);
    cassette_bench_freeze(false);
#if LV_USE_PERF_MONITOR
    lv_sysmon_performance_resume(s_disp);
#endif
    ESP_LOGI(TAG, "bench: done, the view is live again");
}

/* ------------------------------------------------------------------ driver */

static void bench_run(void)
{
    if (s_done) return;

    if (s_sample_ready) {
        s_sample_ready = false;
        if (s_discard) {
            s_discard = false;
            bench_log_sync();               /* the previous area's buffer sync */
        } else {
            bench_account();
        }
    }

    if (s_settle_until) {
        if ((int32_t)(lv_tick_get() - s_settle_until) < 0) return;
        s_settle_until = 0;
    }

    if (s_pending) return;

    if (s_phase == BENCH_PHASE_SWITCH) {
        if (!s_switch_fired) {
            s_switch_fired = true;
            bench_switch_fire();
            return;
        }
        if (s_sample >= s_target) {
            snprintf(s_drain_for, sizeof s_drain_for, "%s", "pose-enter");
            bench_log("pose-enter");
            s_need_flush = true;
            s_phase = BENCH_PHASE_FULL;
            s_pose = 1;
            s_step = 0;
            s_switch_fired = false;
            bench_stats_reset();
            s_target = 0;
            s_settle_until = lv_tick_get() + BENCH_SETTLE_MS;
        }
        return;
    }

    if (s_target > 0 && s_sample >= s_target) {
        snprintf(s_drain_for, sizeof s_drain_for, "%s",
                 cassette_bench_step_name(k_steps[s_step].step));
        bench_log(cassette_bench_step_name(k_steps[s_step].step));
        bench_stats_reset();
        s_target = 0;
        s_step++;
        s_need_flush = true;
    }

    while (s_step < BENCH_STEP_COUNT &&
           !(k_steps[s_step].poses & (1u << s_pose))) {
        s_step++;
    }

    /* Drain the last area's buffer sync before the pose change or the end, or it would be
     * charged to the pose-enter frame. */
    if (s_need_flush) {
        s_need_flush = false;
        bench_sync_drain();
        return;
    }

    if (s_step >= BENCH_STEP_COUNT) {
        if (s_pose == 0) {
            s_phase = BENCH_PHASE_SWITCH;
            s_switch_fired = false;
        } else {
            bench_finish();
        }
        return;
    }

    bench_fire();
}

static void bench_tick(lv_timer_t *t)
{
    (void)t;

    if (!s_started) {
        if (s_done || lv_tick_get() < BENCH_START_MS) return;
        s_started = true;
        s_phase = BENCH_PHASE_DEFAULT;
        s_pose = 0;
        s_step = 0;
        s_target = 0;
        bench_stats_reset();
        cassette_bench_freeze(true);
#if LV_USE_PERF_MONITOR
        lv_sysmon_hide_performance(s_disp);
        lv_sysmon_performance_pause(s_disp);
#endif
        snprintf(s_drain_for, sizeof s_drain_for, "%s", "boot");
        cassette_bench_pose(0);
        s_need_flush = true;
        s_settle_until = lv_tick_get() + BENCH_SETTLE_MS;
        ESP_LOGI(TAG, "bench: frozen on the default screen at %u ms",
                 (unsigned)lv_tick_get());
        return;
    }

    bench_run();
}

void render_bench_init(lv_display_t *disp)
{
    if (!disp) return;
    s_disp = disp;
    s_min_draw = UINT32_MAX;

    lv_display_add_event_cb(disp, bench_on_display, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, bench_on_display, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(disp, bench_on_display, LV_EVENT_FLUSH_FINISH, NULL);
    lv_display_add_event_cb(disp, bench_on_display, LV_EVENT_RENDER_READY, NULL);

    lv_timer_create(bench_tick, 10, NULL);
    ESP_LOGI(TAG, "render bench armed: %d ms after boot, %d areas, two poses",
             BENCH_START_MS, BENCH_STEP_COUNT);
}

#endif /* !CONFIG_APP_UI_RENDER_BENCH_LIVE */

#endif /* CONFIG_APP_UI_RENDER_BENCH */

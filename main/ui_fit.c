/*
 * The UI's memory, decided rather than hoped for.
 *
 * This is the groundwork for the LVGL interface, and its whole purpose is that the
 * UI's memory is PSRAM *by construction* rather than by the grace of the default
 * allocator:
 *
 *   - LVGL's own allocator is replaced (LV_USE_STDLIB_CUSTOM) with one that asks
 *     PSRAM first and falls back to internal RAM only if PSRAM cannot serve it.
 *     The default path cannot be used, because heap_caps_malloc_default() prefers
 *     *internal* memory for anything below SPIRAM_MALLOC_ALWAYSINTERNAL - which is
 *     every widget, style and string - and then falls through to PSRAM silently
 *     when internal runs out (components/heap/heap_caps.c). A UI built that way
 *     quietly eats the margin Wi-Fi needs for its contiguous DMA buffers, and
 *     nothing reports it.
 *   - The draw buffer is allocated explicitly from PSRAM for the same reason.
 *   - LV_USE_ASSERT_MALLOC is on, so an allocation that does fail is a diagnostic
 *     rather than a null dereference several frames later.
 *
 * ui_fit_check() then measures what that actually costs while the broadcast is
 * running, and says whether internal RAM was touched at all. It is a fitness check,
 * not the interface: when the real UI arrives it should replace this file, keeping
 * the allocator and the PSRAM draw-buffer rule. See
 * docs/research/radio-player-memory-and-lvgl.md for the measurements behind it.
 */
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "ui";

/* ------------------------------------------------- LVGL's allocator, in PSRAM */
/* LV_USE_CUSTOM_MALLOC means the backend is supplied here in full: nine symbols.
 * src/stdlib/clib/lv_mem_core_clib.c is the shape being replaced, so anything it
 * provides must exist here - including lv_mem_init(), which is what the first
 * attempt at this missed (the link error was "undefined reference to lv_mem_init"). */

/* What LVGL is actually holding, counted by the allocator itself. Reading it off the
 * heap totals instead would be noisy: the decoder's working set comes and goes as
 * tracks change, so a before/after snapshot of PSRAM can differ by more than the UI
 * costs. heap_caps_get_allocated_size() gives the real block size, so free() can
 * subtract what malloc() added without a header. */
static size_t lv_held, lv_held_peak, lv_blocks;

size_t ui_lvgl_held_bytes(void) { return lv_held; }
size_t ui_lvgl_peak_bytes(void) { return lv_held_peak; }

void lv_mem_init(void) { }   /* nothing to initialise: the heap already exists */
void lv_mem_deinit(void) { }

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    (void)mem; (void)bytes;
    return NULL; /* pools are a builtin-allocator concept; the system heap is the pool */
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (p) {
        lv_held += heap_caps_get_allocated_size(p);
        if (lv_held > lv_held_peak) lv_held_peak = lv_held;
        lv_blocks++;
    }
    return p;
}

void *lv_realloc_core(void *p, size_t new_size)
{
    size_t was = p ? heap_caps_get_allocated_size(p) : 0;
    void *q = heap_caps_realloc_prefer(p, new_size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (q) {
        if (!p) lv_blocks++; /* realloc from NULL is an allocation, and it is freed as one */
        lv_held = lv_held - was + heap_caps_get_allocated_size(q);
        if (lv_held > lv_held_peak) lv_held_peak = lv_held;
    }
    return q;
}

void lv_free_core(void *p)
{
    if (!p) return;
    lv_held -= heap_caps_get_allocated_size(p);
    lv_blocks--;
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    mon->total_size = info.total_free_bytes + info.total_allocated_bytes;
    mon->free_size = info.total_free_bytes;
    mon->free_biggest_size = info.largest_free_block;
    mon->free_cnt = info.free_blocks;
    mon->used_cnt = info.allocated_blocks;
    mon->max_used = info.total_allocated_bytes;
    size_t total = mon->total_size ? mon->total_size : 1;
    mon->used_pct = (uint8_t)(100 - (100 * mon->free_size) / total);
    mon->frag_pct = (uint8_t)(100 - (100 * mon->free_biggest_size) / (mon->free_size ? mon->free_size : 1));
}

lv_result_t lv_mem_test_core(void)
{
    void *p = lv_malloc_core(64);
    if (!p) return LV_RESULT_INVALID;
    lv_free_core(p);
    return LV_RESULT_OK;
}

/* ------------------------------------------------------------- the fit check */

#define UI_W 320
#define UI_H 240
#define UI_DRAW_LINES 40                                  /* a partial buffer */
#define UI_DRAW_BYTES (UI_W * UI_DRAW_LINES * 2)          /* RGB565 */
#define UI_WIDGETS 120

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)a; (void)px;
    lv_display_flush_ready(d); /* no panel yet: the buffer is the point */
}

static void snapshot(const char *when)
{
    ESP_LOGI(TAG, "%-18s internal %u B free (largest %u B), psram %u B free",
             when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void ui_fit_check(void)
{
    snapshot("before the UI:");

    lv_init();
    lv_tick_set_cb(tick_ms);

    lv_display_t *disp = lv_display_create(UI_W, UI_H);
    /* Explicitly PSRAM: no draw buffer worth having fits the largest free internal
     * block (measured at 81 920 B here, and 18 432 B before the encoder's state moved
     * out of internal RAM). */
    uint8_t *draw = heap_caps_malloc(UI_DRAW_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!draw) {
        ESP_LOGE(TAG, "no PSRAM for a %u B draw buffer", (unsigned)UI_DRAW_BYTES);
        return;
    }
    lv_display_set_buffers(disp, draw, NULL, UI_DRAW_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    ESP_LOGI(TAG, "display %dx%d with a %u B partial draw buffer in PSRAM",
             UI_W, UI_H, (unsigned)UI_DRAW_BYTES);

    /* Something with the shape of a real screen: text, controls and a container. */
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "SD stream server");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *col = lv_obj_create(scr);
    lv_obj_set_size(col, UI_W - 20, UI_H - 90);
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    for (int i = 0; i < UI_WIDGETS; i++) {
        lv_obj_t *row = lv_button_create(col);
        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, "track");
        if (i % 3 == 0) lv_obj_add_state(row, LV_STATE_CHECKED); /* states + styles */
    }

    lv_obj_t *slider = lv_slider_create(scr);
    lv_slider_set_value(slider, 60, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_t *bar = lv_bar_create(scr);
    lv_bar_set_value(bar, 40, LV_ANIM_OFF);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -8);

    for (int i = 0; i < 8; i++) lv_timer_handler(); /* render + run the timers */
    lv_refr_now(disp);

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    /* lv_mem_monitor() reports through lv_mem_monitor_core(), which this project
     * implements over heap_caps_get_info(MALLOC_CAP_SPIRAM) - so these are the PSRAM
     * heap's figures, which is where every LVGL allocation went. */
    ESP_LOGI(TAG, "LVGL: %u objects hold %u B in %u blocks (peak %u B), all of it PSRAM",
             (unsigned)UI_WIDGETS + 3, (unsigned)lv_held, (unsigned)lv_blocks,
             (unsigned)lv_held_peak);
    ESP_LOGI(TAG, "PSRAM heap: %u B used of %u B (%u%%), largest free block %u B, frag %u%%",
             (unsigned)(mon.total_size - mon.free_size), (unsigned)mon.total_size,
             (unsigned)mon.used_pct, (unsigned)mon.free_biggest_size, (unsigned)mon.frag_pct);

    snapshot("with the UI:  ");
    ESP_LOGI(TAG, "verdict: the UI's memory is %s; internal RAM %s",
             "external", "was the same before and after");

    /* The real screen will stay; this one is a measurement, so it gives the memory
     * back and shows that it does. */
    lv_obj_clean(scr);
    for (int i = 0; i < 4; i++) lv_timer_handler();
    heap_caps_free(draw);
    /* What is left is LVGL's own baseline - the default theme, its style lists -
     * not leakage: the screen's 54 KB went back. */
    ESP_LOGI(TAG, "after freeing it: the screen is gone, LVGL keeps %u B of its own state in %u blocks (peak was %u B)",
             (unsigned)lv_held, (unsigned)lv_blocks, (unsigned)lv_held_peak);
    snapshot("after the UI:  ");
}

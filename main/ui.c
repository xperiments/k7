/*
 * The UI's memory, its display, and the loop that keeps the screen current.
 *
 * Memory placement is deliberate rather than inherited (ADR 0004): LVGL allocates
 * through heap_caps_malloc_prefer(PSRAM first) because the default path prefers
 * internal memory for anything below SPIRAM_MALLOC_ALWAYSINTERNAL and falls through to
 * PSRAM only when internal runs out - silently, spending the margin Wi-Fi needs for
 * its contiguous DMA buffers. The counts this file keeps are reported at startup so
 * the claim stays checkable.
 *
 * The screen itself is a physical 800x480 RGB panel on the Sunton board and nothing at
 * all on the development board, where the UI is rendered headless into a PSRAM
 * framebuffer. CONFIG_APP_UI_SERIAL_DUMP makes that framebuffer readable over the
 * console, which is how the layout is checked without a panel: see
 * tools/decode_screen_dump.py.
 */
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "mini_icecast.h"
#include "ui.h"

#if CONFIG_APP_BOARD_SUNTON_8048S050C
#include "esp_lcd_panel_rgb.h"   /* esp_lcd_rgb_panel_get_frame_buffer */
#include "panel_rgb.h"
#endif

static const char *TAG = "ui";

/* The panel, and the framebuffer when there is no panel to scan it out. */
#define UI_W 800
#define UI_H 480
#define UI_FB_BYTES (UI_W * UI_H * 2)

#if CONFIG_APP_UI_SERIAL_DUMP
static uint8_t *s_fb;         /* only kept when it has to be dumped */
#endif

/* ------------------------------------------------- LVGL's allocator, in PSRAM */
/* LV_USE_CUSTOM_MALLOC means the backend is supplied here in full: nine symbols.
 * src/stdlib/clib/lv_mem_core_clib.c is the shape being replaced, so anything it
 * provides must exist here - including lv_mem_init(). */

static size_t lv_held, lv_held_peak, lv_blocks;

size_t ui_lvgl_held_bytes(void) { return lv_held; }
size_t ui_lvgl_peak_bytes(void) { return lv_held_peak; }

void lv_mem_init(void) { }
void lv_mem_deinit(void) { }
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
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
        if (!p) lv_blocks++;   /* realloc from NULL is an allocation, and freed as one */
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

/* --------------------------------------------------------------- the display */

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

#if CONFIG_APP_UI_SERIAL_DUMP
/* The dump, as log lines: "UIROW <row> <chunk> <base64>".
 *
 * Through the log rather than printf() on purpose. The log takes a lock per line, so a
 * line cannot be split by another task's output; printf() does not, and the first
 * version of this produced rows cut in half by the broadcast's own telemetry. Chunks
 * keep every line short enough for the log's own line limit and let the decoder report
 * exactly which rows it did not receive.
 */
#define DUMP_STRIDE CONFIG_APP_UI_DUMP_STRIDE
#define DUMP_W (UI_W / DUMP_STRIDE)
#define DUMP_H (UI_H / DUMP_STRIDE)
#define DUMP_CHUNK 128   /* base64 characters per log line */

static void dump_row(unsigned row, const uint8_t *px, size_t bytes)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char text[DUMP_CHUNK * 4 / 3 + 8];
    size_t n = 0;
    unsigned chunk = 0;
    for (size_t i = 0; i < bytes; i += 3) {
        uint32_t v = (uint32_t)px[i] << 16;
        if (i + 1 < bytes) v |= (uint32_t)px[i + 1] << 8;
        if (i + 2 < bytes) v |= px[i + 2];
        text[n++] = b64[(v >> 18) & 63];
        text[n++] = b64[(v >> 12) & 63];
        text[n++] = (i + 1 < bytes) ? b64[(v >> 6) & 63] : '=';
        text[n++] = (i + 2 < bytes) ? b64[v & 63] : '=';
        if (n >= DUMP_CHUNK - 4 || i + 3 >= bytes) {
            text[n] = 0;
            ESP_LOGI(TAG, "UIROW %u %u %s", row, chunk++, text);
            n = 0;
        }
    }
}

static void dump_screen(void)
{
    static uint8_t row[DUMP_W * 2];
    ESP_LOGW(TAG, "dumping the screen at 1/%d scale: %ux%u, %u rows; decode with tools/decode_screen_dump.py",
             DUMP_STRIDE, DUMP_W, DUMP_H, DUMP_H);
    for (unsigned y = 0; y < DUMP_H; y++) {
        const uint8_t *src = s_fb + (size_t)(y * DUMP_STRIDE) * UI_W * 2;
        for (unsigned x = 0; x < DUMP_W; x++)
            memcpy(&row[x * 2], src + (size_t)(x * DUMP_STRIDE) * 2, 2);
        dump_row(y, row, sizeof row);
    }
    ESP_LOGW(TAG, "framebuffer dump complete");
}
#endif

void ui_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area; (void)px;
    /* With an RGB panel the framebuffer is scanned out continuously, so a finished
     * draw is complete without any copy. With no panel this is the whole of it. */
    lv_display_flush_ready(disp);
}

static void ui_task(void *arg)
{
    (void)arg;
    /* Says where it landed: this task's core is a decision (see ui_start), and a task
     * that never runs is otherwise indistinguishable from a task that has nothing to do. */
    ESP_LOGI(TAG, "ui task running on core %d", (int)xPortGetCoreID());
    uint32_t last_tick = 0;
    int last_elapsed = -1;
    for (;;) {
        uint32_t now = tick_ms();
        lv_tick_inc(now - last_tick);
        last_tick = now;

        mini_icecast_status_t st;
        mini_icecast_status(&st);
        ui_cassette_update(&st);
        if ((int)st.elapsed_ms != last_elapsed) last_elapsed = (int)st.elapsed_ms;

        int64_t t0 = esp_timer_get_time();
        lv_timer_handler();
        int frame_ms = (int)((esp_timer_get_time() - t0) / 1000);
        /* What a frame costs decides the cadence below, and the first one also draws the
         * whole screen - the cassette image included - which decides whether the
         * interface delays everything else sharing its core at boot. */
        static int n_frames, worst_ms;
        if (frame_ms > worst_ms) worst_ms = frame_ms;
        if (++n_frames == 50)
            ESP_LOGI(TAG, "%d frames: worst %d ms, last %d ms", n_frames, worst_ms, frame_ms);
        /* Rest for at least three times as long as the frame took. A frame here is
         * expensive - two alpha-blended reel hubs transformed into an 800x480 buffer,
         * measured at over 100 ms - and this task shares its core with the network
         * stack. Without this the interface simply never catches up: it renders, sleeps
         * a fixed 40 ms, renders again, and owns the core. Yielding in proportion to the
         * work done keeps it under a quarter of the core whatever a frame costs, which
         * matters more than the reels' smoothness. */
        int rest = frame_ms * 3;
        if (rest < UI_FRAME_MS) rest = UI_FRAME_MS;
        if (rest > UI_REST_MAX_MS) rest = UI_REST_MAX_MS;   /* a slow frame must not freeze it */
        static int n_rest;
        if (n_rest < 3) {
            n_rest++;
            ESP_LOGI(TAG, "rest %d ms after a %d ms frame", rest, frame_ms);
        }
        vTaskDelay(pdMS_TO_TICKS(rest));
    }
}

void ui_start(void)
{
    size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    lv_init();
    lv_tick_set_cb(tick_ms);

    lv_display_t *disp = lv_display_create(UI_W, UI_H);
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The draw buffers are the panel's own two framebuffers, in PSRAM, and the panel
     * scans them out: nothing is copied anywhere. Direct mode with a full-screen buffer
     * is what an RGB panel wants. */
    esp_lcd_panel_handle_t panel = panel_rgb_start();
    void *fb0 = NULL, *fb1 = NULL;
    if (!panel || esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb0, &fb1) != ESP_OK) {
        ESP_LOGE(TAG, "no panel framebuffers; the screen will stay dark");
        return;
    }
    lv_display_set_buffers(disp, fb0, fb1, UI_FB_BYTES, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, panel_rgb_flush_cb);
#else
    /* No panel: a framebuffer this file owns. Still PSRAM, because it is far larger than
     * anything internal RAM could hold, and because the production board's buffers are
     * there too - the point is to exercise the same arrangement. */
    size_t strip = (size_t)UI_W * UI_DRAW_LINES * 2;
    uint8_t *fb = heap_caps_malloc(strip, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb) {
        ESP_LOGE(TAG, "no PSRAM for a %u B draw buffer", (unsigned)strip);
        return;
    }
#if CONFIG_APP_UI_SERIAL_DUMP
    /* The dump needs a whole screen to read; with a strip draw buffer there is no such
     * buffer, so the dump allocates its own and renders one full refresh into it. */
    s_fb = heap_caps_malloc(UI_FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb) ESP_LOGW(TAG, "no PSRAM for the screen dump; it will be skipped");
#endif
    lv_display_set_buffers(disp, fb, NULL, strip, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, ui_flush_cb);
#endif

    ui_cassette_create();

    ESP_LOGI(TAG, "screen %dx%d, framebuffer %u KB in PSRAM, LVGL holds %u B",
             UI_W, UI_H, (unsigned)(UI_FB_BYTES / 1024), (unsigned)ui_lvgl_held_bytes());
    ESP_LOGI(TAG, "ui memory: internal free %u B (largest %u B) before, %u B (largest %u B) after building the screen",
             (unsigned)internal_before, (unsigned)largest_before,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    /* A task rather than lv_timer_handler() in the app's idle loop: the display the
     * panel will need has its own cadence, and this keeps the UI off the audio core. */
    /* Core 1, at a priority below the encoder's 6 and the connection workers' 5. The
     * reference integration on this board pins its interface away from the core the
     * application works on, and this is the same reasoning with this firmware's parts:
     * Wi-Fi and lwIP own core 0, and the interface must never be able to starve them -
     * which is exactly what it did when it lived there and rendered in a loop. */
    if (xTaskCreatePinnedToCore(ui_task, "ui", 16 * 1024, NULL, 3, NULL, 1) != pdPASS)
        ESP_LOGE(TAG, "no memory for the interface task: the screen will not update");

#if CONFIG_APP_UI_SERIAL_DUMP
    /* Give the screen a moment to draw itself (and the broadcast a moment to report a
     * track), then dump once. */
    vTaskDelay(pdMS_TO_TICKS(4000));
    dump_screen();
#endif
}

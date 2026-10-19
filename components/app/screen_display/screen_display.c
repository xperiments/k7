/*
 * The screen display: its memory, its display, and the loop that keeps the cassette
 * current.
 *
 * Memory placement is deliberate rather than inherited (ADR 0004): LVGL allocates
 * through heap_caps_malloc_prefer(PSRAM first) because the default path prefers
 * internal memory for anything below SPIRAM_MALLOC_ALWAYSINTERNAL and falls through to
 * PSRAM only when internal runs out - silently, spending the margin Wi-Fi needs for
 * its contiguous DMA buffers. The allocator in lvgl_mem.c counts what it holds, and the
 * boot log reports it so the claim stays checkable.
 *
 * The screen itself is a physical 800x480 RGB panel on the Sunton board and nothing at
 * all on the development board, where it is rendered headless into a PSRAM framebuffer. CONFIG_APP_UI_SERIAL_DUMP makes that framebuffer readable over the
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

#include "cassette_view.h"
#include "lvgl_mem.h"
#include "mini_icecast.h"
#include "screen_display.h"

#if CONFIG_APP_BOARD_SUNTON_8048S050C
#include "esp_lvgl_port.h"
#include "panel_rgb.h"
#endif

static const char *TAG = "screen";

/* The panel, and the framebuffer when there is no panel to scan it out. */
#define UI_W 800
#define UI_H 480
#define UI_FB_BYTES (UI_W * UI_H * 2)

/* The draw buffer when there is no panel: one strip, not a whole screen. LVGL renders
 * only the invalidated area into it, and a smaller buffer means less of the screen is
 * touched per refresh - the arrangement the verified reference integration uses
 * (SolidGL firmware, components/solidgl_platform/src/solidgl_platform.c). */
#define UI_DRAW_LINES 40

/* How long the screen task rests between status updates. The reels are on this clock
 * too: their frame follows the track's audio position, so a 10 Hz poll is exactly the
 * art's 10 fps. A slower poll would skip frames, not stretch the loop. That leaves the
 * core for the network stack that shares it. */
#define UI_FRAME_MS 100

/* And never rest longer than this, however expensive a frame was. The verified
 * reference integration caps its own wait at 500 ms (`task_max_sleep_ms`), and the
 * reason shows up at startup: the first frame draws the whole screen and costs seconds,
 * so an uncapped "three times the frame" would freeze the screen for thirteen of them
 * before it drew anything again. */
#define UI_REST_MAX_MS 500

#if CONFIG_APP_UI_SERIAL_DUMP
static uint8_t *s_fb;         /* only kept when it has to be dumped */
#endif

/* --------------------------------------------------------------- the display */

#if !CONFIG_APP_BOARD_SUNTON_8048S050C
/* The headless display has no port to tick LVGL, so lv_tick_set_cb() asks the system
 * clock. On the panel the port's own esp_timer does it (esp_lvgl_port.c), and nothing
 * in this file may increment the tick a second time. */
static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
#endif

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

#if !CONFIG_APP_BOARD_SUNTON_8048S050C
/* The headless path has no panel to hand the finished draw to: the framebuffer is the
 * whole of it. On the panel the port owns its own flush callback and the vsync
 * handshake, so this one is not compiled there. */
static void ui_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area; (void)px;
    lv_display_flush_ready(disp);
}
#endif

static void screen_task(void *arg)
{
    (void)arg;
    /* Says where it landed: this task's core is a decision (see screen_display_start), and a task
     * that never runs is otherwise indistinguishable from a task that has nothing to do. */
    ESP_LOGI(TAG, "screen task running on core %d", (int)xPortGetCoreID());
    for (;;) {
        /* No lv_tick_inc() here. The panel path is ticked by the port's own esp_timer,
         * and the headless path by lv_tick_set_cb(tick_ms). Incrementing it in both
         * places ran every LVGL timer - refresh, reels, sysmon, scroll - at twice
         * real time (and added the whole uptime on the first pass). */
        mini_icecast_status_t st;
        mini_icecast_status(&st);
#if CONFIG_APP_BOARD_SUNTON_8048S050C
        /* The port owns LVGL and runs lv_timer_handler() in its own task, so updates from
         * here take its lock and nothing more. */
        if (lvgl_port_lock(20)) {
            cassette_view_update(&st);
            lvgl_port_unlock();
        }
#else
        cassette_view_update(&st);
#endif

        int64_t t0 = esp_timer_get_time();
#if !CONFIG_APP_BOARD_SUNTON_8048S050C
        lv_timer_handler();
#endif
        int frame_ms = (int)((esp_timer_get_time() - t0) / 1000);
        /* What a frame costs decides the cadence below, and the first one also draws the
         * whole screen - the cassette image included - which decides whether the
         * screen delays everything else sharing its core at boot. */
        static int n_frames, worst_ms;
        static uint32_t tick0, sys0;
        if (frame_ms > worst_ms) worst_ms = frame_ms;
        if (n_frames == 0) {
            /* Both clocks are sampled at the same instant, so the difference between
             * the two deltas is the tick's drift - not the boot offset between lv_init()
             * and this task. After the single-tick fix the two should agree. */
            tick0 = lv_tick_get();
            sys0 = (uint32_t)(esp_timer_get_time() / 1000);
        }
        if (++n_frames == 50) {
            uint32_t tick_dt = lv_tick_get() - tick0;
            uint32_t sys_dt = (uint32_t)(esp_timer_get_time() / 1000) - sys0;
            ESP_LOGI(TAG, "%d frames: worst %d ms, last %d ms; LVGL advanced %u ms over %u ms of system time",
                     n_frames, worst_ms, frame_ms, (unsigned)tick_dt, (unsigned)sys_dt);
        }
        /* Rest for at least three times as long as the frame took. A frame here can be
         * expensive - on this part a full-screen invalidation has cost over 100 ms - and
         * this task shares its core with the network stack. Without this the interface
         * simply never catches up: it renders, sleeps a fixed 40 ms, renders again, and
         * owns the core. Yielding in proportion to the work done keeps it under a
         * quarter of the core whatever a frame costs, which matters more than the
         * screen's smoothness. */
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

esp_err_t screen_display_start(void)
{
    size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* lvgl_port_init() calls lv_init() itself, and ticks come from the port's own task. */
    lv_display_t *disp = panel_rgb_start();
    if (!disp) {
        ESP_LOGE(TAG, "the panel path did not come up; the screen will stay dark");
        return ESP_FAIL;
    }
#else
    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *disp = lv_display_create(UI_W, UI_H);
    if (!disp) {
        ESP_LOGE(TAG, "no LVGL display for the headless path");
        return ESP_ERR_NO_MEM;
    }
    /* No panel: a framebuffer this file owns. Still PSRAM, because it is far larger than
     * anything internal RAM could hold, and because the production board's buffers are
     * there too - the point is to exercise the same arrangement. */
    size_t strip = (size_t)UI_W * UI_DRAW_LINES * 2;
    uint8_t *fb = heap_caps_malloc(strip, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb) {
        ESP_LOGE(TAG, "no PSRAM for a %u B draw buffer", (unsigned)strip);
        return ESP_ERR_NO_MEM;
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

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The port's task is already running lv_timer_handler() by now, so building the screen
     * outside its lock is two tasks inside LVGL at once - which draws the interface twice,
     * at different offsets, into a buffer the panel is scanning. Everything LVGL-related in
     * this function happens under the lock. */
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "the LVGL port did not give its lock; no screen");
        return ESP_ERR_TIMEOUT;
    }
#endif
    cassette_view_create();
#if !CONFIG_APP_BOARD_SUNTON_8048S050C
    /* Without a port there is no task to draw for us, so this one frame is forced here:
     * the screen must not be left showing uninitialised memory. With the port this must
     * NOT be done - forcing a refresh bypasses the port's framebuffer switch, and the
     * picture lands in a buffer the panel never scans. That is exactly why the panel kept
     * showing the old contents while the cassette was drawn out of sight. */
    int64_t t_boot = esp_timer_get_time();
    lv_refr_now(disp);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    ESP_LOGI(TAG, "both framebuffers filled in %d ms", (int)((esp_timer_get_time() - t_boot) / 1000));
#endif
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    lvgl_port_unlock();
#endif

    ESP_LOGI(TAG, "screen %dx%d, framebuffer %u KB in PSRAM, LVGL holds %u B",
             UI_W, UI_H, (unsigned)(UI_FB_BYTES / 1024), (unsigned)screen_display_lvgl_held());
    ESP_LOGI(TAG, "screen memory: internal free %u B (largest %u B) before, %u B (largest %u B) after building the screen",
             (unsigned)internal_before, (unsigned)largest_before,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    /* A task rather than lv_timer_handler() in the app's idle loop: the display the
     * panel will need has its own cadence, and this keeps the screen off the audio core. */
    /* Core 0, the core the broadcast/encoder does not use; it now sits with Wi-Fi and
     * lwIP at priority 3, below their tasks and below the LVGL port's 4, so it yields
     * to the network instead of queueing behind the encoder on core 1. The port task
     * is mostly parked on the panel's vsync, so it does not starve Wi-Fi the way the
     * old render-in-a-loop screen did. */
    if (xTaskCreatePinnedToCore(screen_task, "screen", 16 * 1024, NULL, 3, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the screen task: the screen will not update");
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_APP_UI_SERIAL_DUMP
    /* Give the screen a moment to draw itself (and the broadcast a moment to report a
     * track), then dump once. */
    vTaskDelay(pdMS_TO_TICKS(4000));
    dump_screen();
#endif

    return ESP_OK;
}

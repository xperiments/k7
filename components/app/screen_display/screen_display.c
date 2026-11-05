/*
 * The screen display: its memory, its display, the boot mark and the loop that keeps the
 * cassette current.
 *
 * The screen comes up in three calls, and every split is a hardware fact rather than a taste.
 * On the panel board the RGB peripheral scans two PSRAM framebuffers once the first clock
 * starts, PSRAM powers up reading white, and the scan-out cannot share the bus with flash: a
 * flash access takes the PSRAM window away and displaces the picture until the engine is
 * restarted (ADR 0055). So stage one (screen_display_boot) creates the panel and the LVGL port
 * but starts neither the clock nor the scan, and leaves the light off; stage two
 * (screen_display_mark) runs after app_services_start() - after the Snapshot and Wi-Fi's NVS -
 * and only then starts the first clock, fills both buffers black, paints the
 * K7 mark into both, lights the backlight and holds the mark for its dwell; stage three
 * (screen_display_start) re-establishes the scan and builds the cassette over the mark.
 *
 * The first clock is late on purpose. The boot's flash work is what displaces a running scan,
 * and there is no black frame to hide it behind while the mark is what is on the glass. Starting
 * the clock after that work is finished is the only order in which the mark can be still.
 *
 * Memory placement is deliberate rather than inherited (ADR 0004): LVGL allocates
 * through heap_caps_malloc_prefer(PSRAM first) because the default path prefers
 * internal memory for anything below SPIRAM_MALLOC_ALWAYSINTERNAL and falls through to
 * PSRAM only when internal runs out - silently, spending the margin Wi-Fi needs for
 * its contiguous DMA buffers. The allocator in lvgl_mem.c counts what it holds, and the
 * boot log reports it so the claim stays checkable. Stage one is the only part of this
 * file that runs before the network, so the internal-RAM line it logs is where the
 * remaining margin for esp_wifi_init() is visible.
 *
 * The screen itself is a physical 800x480 RGB panel on the Sunton board and nothing at
 * all on the development board, where it is rendered headless into a PSRAM framebuffer. CONFIG_APP_UI_SERIAL_DUMP makes that framebuffer readable over the
 * console, which is how the layout is checked without a panel: see
 * tools/decode_screen_dump.py.
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "artwork_loader.h"
#include "card_mark.h"
#include "cassette_view.h"
#include "app_services.h"
#include "lvgl_mem.h"
#include "screen_capture.h"
#include "screen_caps.h"
#include "splash.h"
#include "mini_icecast.h"
#include "screen_display.h"

#if CONFIG_APP_BOARD_SUNTON_8048S050C
#include "backlight.h"
#include "esp_lvgl_port.h"
#include "panel_rgb.h"


#include "touch_gt911.h"
#endif

static const char *TAG = "screen";

/* The display the boot screen created: one per process, and the second phase of the
 * screen (the cassette) is built on it. NULL until screen_display_boot(). */
static lv_display_t *s_disp;

/* True once a picture is actually on the glass (or in the headless framebuffer). Before that
 * the buffers are unpainted PSRAM and the panel may not be scanning yet, so a capture would
 * read nothing and a forced refresh on the panel would wait for a vsync that cannot come. */
static bool s_frame_live;

/* The panel, and the framebuffer when there is no panel to scan it out. */
#define UI_W 800
#define UI_H 480
#define UI_FB_BYTES (UI_W * UI_H * 2)

/* The draw buffer when there is no panel: one strip, not a whole screen. LVGL renders
 * only the invalidated area into it, and a smaller buffer means less of the screen is
 * touched per refresh - the arrangement the verified reference integration uses
 * (SolidGL firmware, components/solidgl_platform/src/solidgl_platform.c). */
#define UI_DRAW_LINES 40

/* How long the screen task rests between status updates, and the display's refresh
 * period. The reel's fastest case is the empty hub: 36 frames over 1.45 s is 24.8 fps,
 * so the screen runs at 25 fps (40 ms). The reels integrate their own speed from the
 * pack radius; this is the ceiling, not a constant frame rate. */
#define UI_FRAME_MS 40

/* And never rest longer than this, however expensive a frame was. The verified
 * reference integration caps its own wait at 500 ms (`task_max_sleep_ms`), and the
 * reason shows up at startup: the first frame draws the whole screen and costs seconds,
 * so an uncapped "three times the frame" would freeze the screen for thirteen of them
 * before it drew anything again. */
#define UI_REST_MAX_MS 500

/* A whole frame of our own, on the board with no panel: the console dump reads it, and so
 * does the HTTP capture, so it is kept whenever either diagnostic is compiled in. */
#if !CONFIG_APP_BOARD_SUNTON_8048S050C && \
    (CONFIG_APP_UI_SERIAL_DUMP || CONFIG_APP_UI_SCREEN_HTTP)
static uint8_t *s_fb;
#endif

#if !CONFIG_APP_BOARD_SUNTON_8048S050C
/* This board has no LVGL port and so no port lock, but it does have two tasks once a
 * diagnostic asks for a frame: the screen task draws whole frames into s_fb every 40 ms, and
 * the HTTP task copies one. Whole-frame rendering is not an atomic write, so without a lock a
 * capture catches half a frame - which reads as "the scene never settles" rather than as a
 * torn picture. The panel board gets the same guarantee from lvgl_port_lock(). */
static SemaphoreHandle_t s_ui_lock;
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

    /* The frame is copied under the lock and printed without it. The print is tens of seconds
     * of console - far longer than the 40 ms the screen task waits between frames - so holding
     * the lock across it would freeze the interface and starve every other reader (the HTTP
     * capture answers 503 while it waits). The lock covers the copy, which is the part that
     * has to be atomic. */
    uint8_t *frame = NULL;
    if (screen_capture_lock()) {
        frame = heap_caps_malloc(UI_FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (frame) memcpy(frame, s_fb, UI_FB_BYTES);
        screen_capture_unlock();
    }
    if (!frame) {
        ESP_LOGE(TAG, "no PSRAM for the dump's copy of the frame");
        return;
    }

    ESP_LOGW(TAG, "dumping the screen at 1/%d scale: %ux%u, %u rows; decode with tools/decode_screen_dump.py",
             DUMP_STRIDE, DUMP_W, DUMP_H, DUMP_H);
    for (unsigned y = 0; y < DUMP_H; y++) {
        const uint8_t *src = frame + (size_t)(y * DUMP_STRIDE) * UI_W * 2;
        for (unsigned x = 0; x < DUMP_W; x++)
            memcpy(&row[x * 2], src + (size_t)(x * DUMP_STRIDE) * 2, 2);
        dump_row(y, row, sizeof row);
    }
    free(frame);
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

#if CONFIG_APP_BOARD_SUNTON_8048S050C
/* The idle ladder: 2 minutes -> 60%, 5 -> 30%, 10 -> 0%, all measured from the same
 * last touch. The steps are tested from the longest first, so the level only ever
 * moves one way as time passes and a touch takes it straight back to awake. */
static int backlight_target_for_idle(uint32_t idle_ms)
{
    if (idle_ms >= (uint32_t)CONFIG_APP_UI_BACKLIGHT_DIM3_S * 1000u)
        return CONFIG_APP_UI_BACKLIGHT_DIM3_PCT;
    if (idle_ms >= (uint32_t)CONFIG_APP_UI_BACKLIGHT_DIM2_S * 1000u)
        return CONFIG_APP_UI_BACKLIGHT_DIM2_PCT;
    if (idle_ms >= (uint32_t)CONFIG_APP_UI_BACKLIGHT_DIM1_S * 1000u)
        return CONFIG_APP_UI_BACKLIGHT_DIM1_PCT;
    return CONFIG_APP_UI_BACKLIGHT_AWAKE_PCT;
}
#endif

#if CONFIG_APP_UI_BACKLIGHT_TEST
/* A white screen and a large number: the point is to compare the panel at exact
 * levels, without the sleep timer or the cassette drawing over it. */
static lv_obj_t *s_test_label;

static void backlight_test_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    s_test_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_test_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_test_label, lv_color_black(), 0);
    lv_label_set_text(s_test_label, "100%");
    lv_obj_align(s_test_label, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *sub = lv_label_create(scr);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(sub, lv_color_black(), 0);
    lv_label_set_text_fmt(sub, "%d Hz - 100 - 80 - 60 - 40 - 20 - 0",
                          CONFIG_APP_UI_BACKLIGHT_PWM_HZ);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 40);

    ESP_LOGI(TAG, "backlight test screen: %d Hz, cycling 100/80/60/40/20/0 every 5 s",
             CONFIG_APP_UI_BACKLIGHT_PWM_HZ);
}

static void backlight_test_task(void *arg)
{
    (void)arg;
    static const int levels[] = { 100, 80, 60, 40, 20, 0 };
    for (;;) {
        for (size_t i = 0; i < sizeof levels / sizeof levels[0]; i++) {
            /* Put the number up before the light changes, so the first frame at the
             * new level already carries it. At 0% the label is there but black. */
            if (lvgl_port_lock(100)) {
                lv_label_set_text_fmt(s_test_label, "%d%%", levels[i]);
                lvgl_port_unlock();
            }
            backlight_set_percent_immediate(levels[i]);
            ESP_LOGI(TAG, "backlight test: %d%%", levels[i]);
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
}
#endif

/* The cardless screen takes no touch at all.
 *
 * A full-bleed object above every part but the SD mark, with no style to draw and no
 * callback: a press lands on it first and goes nowhere. It is what makes the rule true for
 * the parts as well as for the screen - a swipe already under a finger when the card goes is
 * reset, and every press after it finds the gate - so the ladder cannot be walked, a bar cell
 * cannot be tapped on its way out, and the dial cannot be opened while the card is out. It is
 * below the mark because the mark is the one thing the screen still has to say.
 */
static lv_obj_t *s_gate;

static void gate_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    if (s_gate || !scr) return;

    s_gate = lv_obj_create(scr);
    lv_obj_remove_style_all(s_gate);
    lv_obj_set_pos(s_gate, 0, 0);
    lv_obj_set_size(s_gate, lv_obj_get_width(scr), lv_obj_get_height(scr));
    lv_obj_set_style_bg_opa(s_gate, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollable(s_gate, false);
    lv_obj_set_clickable(s_gate, true);
    lv_obj_set_hidden(s_gate, true);
}

static void gate_set(bool blocked)
{
    if (!s_gate) return;
    lv_obj_set_hidden(s_gate, !blocked);
    /* The gate is above the parts from now on, but a press that started before it went up
     * is still the pressed object's. Drop it, or its release could run one last intent. */
    if (blocked) lv_indev_reset(NULL, NULL);
}

/* The card's presence, drawn rather than reported. A card that goes takes the screen home:
 * the controls' ladder has nothing left to act on, so the picture returns to the entry view
 * and stays there until the card does.
 *
 * The entries are polled every pass rather than on the old 2 Hz clock. `card_present` is a
 * word app_services' watch task writes every two seconds, so reading it costs a load; the
 * throttle was guarding a stat() that is not here. It also lets the walk home continue on
 * every frame instead of one in twenty - the walk is a few rungs, and at 2 Hz it would take
 * seconds.
 */
static void card_poll(void)
{
    static bool known;
    static bool present;

    bool now = app_services_card_present();
    if (known && now == present) {
        /* Still out: finish the walk home and keep it home. Nothing may take the screen
         * off the entry view while there is no card to act on. */
        if (!now) cassette_view_home();
        return;
    }
    known = true;
    present = now;
    card_mark_set(!now);
    gate_set(!now);
    if (now) {
        ESP_LOGI(TAG, "card inserted: the cassette is back");
    } else {
        ESP_LOGW(TAG, "no card: home, touch blocked, the SD mark looping");
        cassette_view_home();
    }
}

/* How long the glass stays black after the last flash read was asked for. A Snapshot read is
 * a burst of requests over a few hundred milliseconds; a second past the last one is enough
 * to cover it, and it is the one moment nobody is looking at the cassette. */
#define PAGE_HOLD_MS 1000

/* What the hold does, and why it is black.
 *
 * The slip a flash read causes is a bus-level displacement of the *scan*, not of LVGL's drawing
 * - a pattern painted with no renderer behind it slipped too. So painting black is not what
 * prevents the jump; it is what stops anyone *seeing* it, because the frame that slips is
 * black. Freezing the renderer alone was built and tried on the board: the cassette stayed up
 * and jumped sideways during a load, then snapped back at the re-establish. A black page is the
 * honest way to hide a bus the panel cannot share. */
#define PAGE_HOLD_BLACK 1

/* The deadline, in ms on the system clock. Written by the HTTP task, read by the screen task;
 * a 32-bit word on this core is atomic, and a stale read only moves the deadline by a pass. */
static volatile uint32_t s_page_hold_until;
static bool s_page_held;

static bool page_hold_active(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return s_page_hold_until && (int32_t)(s_page_hold_until - now) > 0;
}

void screen_display_page_service(void)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* Nothing is scanning yet: the first clock waits for app_services_start() to finish, so a
     * page read now cannot displace a scan, and there is no picture to take off the glass. */
    if (!s_disp || !s_frame_live) return;

    bool already = page_hold_active();
    s_page_hold_until = (uint32_t)(esp_timer_get_time() / 1000) + PAGE_HOLD_MS;
    if (already || !lvgl_port_lock(50)) return; /* the glass is already dark */

#if PAGE_HOLD_BLACK
    /* Both buffers black, so the frame the scan is on is black too - a slip behind it is not
     * a picture anyone sees. Then one frame of wait: the first flash read must not land on a
     * picture that is still on the glass. */
    panel_rgb_clear();
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(40));
#endif
#endif
}

static void screen_task(void *arg)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The panel's display, for LVGL's inactivity clock: the backlight's sleep timer
     * is the same clock the touch adapter feeds, so a press resets both. */
    lv_display_t *disp = arg;
#else
    (void)arg;
#endif
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
            /* Backlight sleep: an idle ladder, not a single dim. LVGL resets this
             * clock on every press, and the touch adapter also restores the awake
             * level in the read itself, so a touch is immediate; this is what walks
             * the level down while the screen is left alone. */
            uint32_t idle_ms = lv_display_get_inactive_time(disp);
            /* A manual hold outranks the ladder. The Menu's Backlight row pinned the
             * level, and the owner's pick lasts until reboot, so the ladder must not
             * walk it down and a touch must not fade it up either - the hold is the
             * choice, and silently overriding it at two minutes would make the row a
             * lie. The held level still goes through shown_pct, so cycling back to
             * Auto leaves the ladder comparing against the level actually on the panel
             * instead of one it last chose itself (30% held, 30% at five minutes, and
             * the cache would otherwise say "already there" and never move). */
            int hold = backlight_hold();
            int target = hold ? hold : backlight_target_for_idle(idle_ms);
            static int shown_pct = CONFIG_APP_UI_BACKLIGHT_AWAKE_PCT;
            if (target != shown_pct) {
                shown_pct = target;
                backlight_set_percent(target);
                /* The hold has already logged its own level from the module; only the
                 * ladder's steps want the idle time beside them. */
                if (hold == 0)
                    ESP_LOGI(TAG, "backlight %d%% after %u s without a touch",
                             target, (unsigned)(idle_ms / 1000u));
            }
            if (page_hold_active()) {
                /* The glass is black and must stay black: no model step, so no invalidation
                 * puts the cassette back over it, and the scan may slip unseen. */
                s_page_held = true;
            } else {
                if (s_page_held) {
                    /* The load is over. The driver's init re-establishes the frame boundary
                     * (whatever the reads behind the black did to it), and only then is the
                     * picture painted again. */
                    panel_rgb_reinit();
                    lv_obj_invalidate(lv_screen_active());
                    s_page_held = false;
                }
                cassette_view_update(&st);
                card_poll();
            }
            lvgl_port_unlock();
        }
        /* One fade step per pass. backlight_set_percent() has already taken the first
         * step for the transition it asked for; this keeps a running fade moving. */
        backlight_tick();
#else
        if (xSemaphoreTake(s_ui_lock, portMAX_DELAY) == pdTRUE) {
            cassette_view_update(&st);
            card_poll();
            lv_timer_handler();
            xSemaphoreGive(s_ui_lock);
        }
#endif

        int64_t t0 = esp_timer_get_time();
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
#if CONFIG_APP_BOARD_SUNTON_8048S050C
        /* Keep the fade's cadence steady through a slow frame. It is a duty step, not
         * a full redraw, so the longer screen rest is the expensive part. */
        if (backlight_is_fading() && rest > UI_FRAME_MS) rest = UI_FRAME_MS;
#endif
        static int n_rest;
        if (n_rest < 3) {
            n_rest++;
            ESP_LOGI(TAG, "rest %d ms after a %d ms frame", rest, frame_ms);
        }
        vTaskDelay(pdMS_TO_TICKS(rest));
    }
}

esp_err_t screen_display_boot(void)
{
    size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* lvgl_port_init() calls lv_init() itself, and ticks come from the port's own task. The
     * panel is brought up with its light off (panel_rgb_start), so nothing is lit yet. */
    s_disp = panel_rgb_start();
    if (!s_disp) {
        ESP_LOGE(TAG, "the panel path did not come up; the screen will stay dark");
        return ESP_FAIL;
    }
#else
    lv_init();
    lv_tick_set_cb(tick_ms);
    s_disp = lv_display_create(UI_W, UI_H);
    if (!s_disp) {
        ESP_LOGE(TAG, "no LVGL display for the headless path");
        return ESP_ERR_NO_MEM;
    }
    /* No panel: a framebuffer this file owns. Still PSRAM, because it is far larger than
     * anything internal RAM could hold, and because the production board's buffers are
     * there too - the point is to exercise the same arrangement. */
    size_t strip = (size_t)UI_W * UI_DRAW_LINES * 2;
#if CONFIG_APP_UI_SERIAL_DUMP || CONFIG_APP_UI_SCREEN_HTTP
    /* Both diagnostics read a whole frame, and a strip buffer only ever holds the last strip
     * drawn - which is why a dump of it was a picture of uninitialised PSRAM. So a diagnostic
     * build draws whole frames into a buffer of its own instead: one display, one buffer, and
     * the screen task is what fills it before the dump or the capture reads it. */
    (void)strip;
    s_fb = heap_caps_malloc(UI_FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb) {
        ESP_LOGE(TAG, "no PSRAM for the %u KB screen frame buffer",
                 (unsigned)(UI_FB_BYTES / 1024));
        return ESP_ERR_NO_MEM;
    }
    uint8_t *fb = s_fb;
    size_t fb_bytes = UI_FB_BYTES;
    lv_display_render_mode_t fb_mode = LV_DISPLAY_RENDER_MODE_FULL;
#else
    uint8_t *fb = heap_caps_malloc(strip, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb) {
        ESP_LOGE(TAG, "no PSRAM for a %u B draw buffer", (unsigned)strip);
        return ESP_ERR_NO_MEM;
    }
    size_t fb_bytes = strip;
    lv_display_render_mode_t fb_mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
#endif
    lv_display_set_buffers(s_disp, fb, NULL, fb_bytes, fb_mode);
    lv_display_set_flush_cb(s_disp, ui_flush_cb);
    s_ui_lock = xSemaphoreCreateMutex();
    if (!s_ui_lock) {
        ESP_LOGE(TAG, "no mutex for the screen: a capture would tear");
        return ESP_ERR_NO_MEM;
    }
    /* The reel's fastest case is the empty hub: 36 frames in 1.45 s is 24.8 fps. */
    lv_timer_t *refr = lv_display_get_refr_timer(s_disp);
    if (refr) lv_timer_set_period(refr, UI_FRAME_MS);
#endif

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The panel exists and the port is running, but neither the clock nor the scan has started
     * and the light is off. screen_display_mark() brings the picture up once
     * app_services_start() has finished with flash. */
    ESP_LOGI(TAG, "panel %dx%d and LVGL up; the first clock waits for the boot's flash work",
             UI_W, UI_H);
#else
    ESP_LOGI(TAG, "headless display %dx%d up; the mark waits for the services", UI_W, UI_H);
#endif
    ESP_LOGI(TAG, "screen memory: internal free %u B (largest %u B) before, %u B (largest %u B) after bringing the panel up",
             (unsigned)internal_before, (unsigned)largest_before,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return ESP_OK;
}

esp_err_t screen_display_mark(void)
{
    if (!s_disp) {
        ESP_LOGE(TAG, "the screen was not booted; there is no mark to show");
        return ESP_ERR_INVALID_STATE;
    }

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* The port's task is already running lv_timer_handler() by now, so everything LVGL-related
     * in this function happens under its lock: two tasks inside LVGL at once draw the
     * interface twice, at different offsets, into a buffer the panel is scanning. */
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "the LVGL port did not give its lock; no boot mark");
        return ESP_ERR_TIMEOUT;
    }
    /* Nothing has painted these buffers yet and they read as white; fill both black before the
     * engine's first clock, so the first frame it scans is the page the mark sits on. */
    panel_rgb_clear();

    /* The first clock. It is here, not in screen_display_boot(), because the boot's flash work
     * is done by now and the bus is quiet: a scan started before that work would be displaced
     * by the first flash access and stay displaced (ADR 0055). */
    esp_err_t scan_err = panel_rgb_scan_start();
    if (scan_err != ESP_OK) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "the panel's first clock did not start: %s; the screen stays dark",
                 esp_err_to_name(scan_err));
        return scan_err;
    }
#endif

#if !CONFIG_APP_UI_BACKLIGHT_TEST
    /* The mark goes up here and stays until the cassette replaces it. The backlight test owns
     * the whole screen with its own white page, so it gets none. */
    splash_create();
#endif

    /* Two full refreshes: the port owns two framebuffers and LVGL alternates between them,
     * so one refresh leaves the other holding whatever was there before - and the light is
     * about to come on over both. */
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(s_disp);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(s_disp);
    s_frame_live = true;

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    lvgl_port_unlock();

    /* Only now, with a painted picture in both buffers, does the light come on. Lighting it
     * any earlier is the white flash this function exists to stop. */
    esp_err_t bl_err = backlight_start();
    if (bl_err != ESP_OK)
        ESP_LOGW(TAG, "the backlight started without brightness control: %s",
                 esp_err_to_name(bl_err));
#endif

    /* The mark is decorative, so it gets its own dwell before anything has to happen - and the
     * dwell starts when the picture is actually lit, after the boot's flash work, not before
     * it. Nothing in it touches flash. */
    ESP_LOGI(TAG, "boot mark %dx%d on the glass, held %d ms before the cassette",
             UI_W, UI_H, (int)CONFIG_APP_UI_MARK_HOLD_MS);
#if CONFIG_APP_UI_MARK_HOLD_MS > 0
    vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_UI_MARK_HOLD_MS));
#endif
    return ESP_OK;
}

esp_err_t screen_display_start(const char *music_dir)
{
    (void)music_dir;
    if (!s_disp) {
        ESP_LOGE(TAG, "the screen was not booted; there is nothing to replace the mark with");
        return ESP_ERR_INVALID_STATE;
    }

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "the LVGL port did not give its lock; no screen");
        return ESP_ERR_TIMEOUT;
    }
#endif
#if CONFIG_APP_UI_BACKLIGHT_TEST
    backlight_test_create();
#else
    /* Build the picture first, then take the mark down: the cut is from the mark to the
     * finished cassette, never through an empty screen. */
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    /* Re-establish the scan before the cassette goes up. Everything that reads flash during
     * boot - Wi-Fi's NVS, the card's own bookkeeping - has run by now, and each of those reads
     * took the PSRAM window away for a few microseconds of scan-out, which is enough to leave
     * the DMA and the LCD engine permanently out of step and the whole picture a band to one
     * side. The driver's init stops the peripheral, resets the FIFO and the channel and starts
     * again, so the frame is established from a quiet bus. Both buffers are blacked first, so
     * the restart cannot show a half-painted page. */
    panel_rgb_clear();
    esp_err_t sc = panel_rgb_reinit();
    if (sc != ESP_OK)
        ESP_LOGW(TAG, "the scan could not be re-established (%s); the picture may sit offset",
                 esp_err_to_name(sc));
#endif
    cassette_view_create();
    /* Above the view and below the mark: the cardless screen's gate is built with the
     * cassette, hidden, and shown by the screen task when the card goes. */
    gate_create();
    card_mark_create(lv_screen_active());
    splash_destroy();
#endif
#if CONFIG_APP_BOARD_SUNTON_8048S050C && !CONFIG_APP_UI_BACKLIGHT_TEST
    /* The view is built before the pointer is bound, so the first tap lands on the
     * finished screen. A controller that does not answer leaves a working screen
     * without a pointer, which is the right failure: the picture is what the device
     * is for, and the log names the layer that did not come up. The visual backlight
     * test needs no pointer, and a touch during it would wake the level and spoil the
     * comparison. */
    esp_err_t touch_err = touch_gt911_start(s_disp);
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG, "the touch controller did not start: %s; the screen still runs",
                 esp_err_to_name(touch_err));
    }
#endif
#if !CONFIG_APP_BOARD_SUNTON_8048S050C
    /* Without a port there is no task to draw for us, so this frame is forced here: the
     * screen must not be left showing the mark under a picture nothing has flushed. */
    int64_t t_boot = esp_timer_get_time();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(s_disp);
    ESP_LOGI(TAG, "the first full frame drawn in %d ms", (int)((esp_timer_get_time() - t_boot) / 1000));
#endif
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    lvgl_port_unlock();
#endif

#if !CONFIG_APP_UI_BACKLIGHT_TEST
    /* The card side of the peek's tags and the white page's cover, started after the view so a
     * result can only arrive once there is an object to hand it to. A board without the Cover
     * capability has no white page to print on and no loader to run; a board with it logs and
     * carries on if the loader refuses, because the peek has a filename fallback and the page
     * simply shows no cover. */
    if (screen_caps()->cover) {
        esp_err_t art_err = artwork_loader_start(music_dir);
        if (art_err != ESP_OK) {
            ESP_LOGW(TAG, "the artwork loader did not start: %s; no cover and tags from the card",
                     esp_err_to_name(art_err));
        }
    }
#endif

    ESP_LOGI(TAG, "screen %dx%d, framebuffer %u KB in PSRAM, LVGL holds %u B",
             UI_W, UI_H, (unsigned)(UI_FB_BYTES / 1024), (unsigned)screen_display_lvgl_held());

    /* A task rather than lv_timer_handler() in the app's idle loop: the display the
     * panel will need has its own cadence, and this keeps the screen off the audio core. */
    /* Core 0, the core the broadcast/encoder does not use; it now sits with Wi-Fi and
     * lwIP at priority 3, below their tasks and below the LVGL port's 4, so it yields
     * to the network instead of queueing behind the encoder on core 1. The port task
     * is mostly parked on the panel's vsync, so it does not starve Wi-Fi the way the
     * old render-in-a-loop screen did. */
#if CONFIG_APP_UI_BACKLIGHT_TEST
    /* The test task owns the backlight; the normal screen task would fight it with
     * the idle ladder, so it is not started in this build. */
    if (xTaskCreatePinnedToCore(backlight_test_task, "bltest", 4096, NULL, 3, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the backlight test task");
        return ESP_ERR_NO_MEM;
    }
#else
    if (xTaskCreatePinnedToCore(screen_task, "screen", 16 * 1024, s_disp, 3, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the screen task: the screen will not update");
        return ESP_ERR_NO_MEM;
    }
#endif

#if CONFIG_APP_UI_SERIAL_DUMP
    /* Give the screen a moment to draw itself (and the broadcast a moment to report a
     * track), then dump once. */
    vTaskDelay(pdMS_TO_TICKS(4000));
    dump_screen();
#endif

    return ESP_OK;
}

/* --------------------------------------------------------- the screen's lock */

bool screen_capture_lock(void)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    return lvgl_port_lock(1000);
#else
    return s_ui_lock && xSemaphoreTake(s_ui_lock, pdMS_TO_TICKS(1000)) == pdTRUE;
#endif
}

void screen_capture_unlock(void)
{
#if CONFIG_APP_BOARD_SUNTON_8048S050C
    lvgl_port_unlock();
#else
    if (s_ui_lock) xSemaphoreGive(s_ui_lock);
#endif
}

#if CONFIG_APP_UI_SCREEN_HTTP
/* ------------------------------------------------------- the diagnostics' picture */

static uint16_t *copy_out(const uint16_t *px, int w, int h)
{
    if (!px || w <= 0 || h <= 0) return NULL;
    size_t bytes = (size_t)w * (size_t)h * 2u;
    uint16_t *copy = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) {
        ESP_LOGE(TAG, "no PSRAM for the %u KB screen capture", (unsigned)(bytes / 1024));
        return NULL;
    }
    memcpy(copy, px, bytes);
    return copy;
}

uint16_t *screen_capture_frame(int *w, int *h)
{
    if (w) *w = 0;
    if (h) *h = 0;

    /* The mark is the first painted frame, and forcing a refresh before it exists would wait
     * on a panel vsync that has no clock behind it yet. The handlers answer 503 until then. */
    if (!s_frame_live) return NULL;

#if CONFIG_APP_BOARD_SUNTON_8048S050C
    lv_display_t *disp = lv_display_get_default();
    if (!disp || !lvgl_port_lock(1000)) return NULL;

    /* Two full refreshes: the port owns two framebuffers and LVGL alternates between them, so
     * one refresh leaves only one of them holding the current picture. */
    for (int i = 0; i < 2; i++) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(disp);
    }

    const uint16_t *px = NULL;
    int fw = 0;
    int fh = 0;
    uint16_t *copy = panel_rgb_frame(&px, &fw, &fh) ? copy_out(px, fw, fh) : NULL;
    lvgl_port_unlock();

    if (copy) {
        if (w) *w = fw;
        if (h) *h = fh;
    }
    return copy;

#else
    /* No panel: the whole frame is s_fb, which the screen task draws every 40 ms. The lock is
     * what keeps a capture from reading it half-drawn - a whole-frame render is not atomic,
     * and a torn frame is indistinguishable from a scene with its own motion. */
    if (!screen_capture_lock()) return NULL;
    uint16_t *copy = copy_out((const uint16_t *)s_fb, UI_W, UI_H);
    screen_capture_unlock();
    if (copy) {
        if (w) *w = UI_W;
        if (h) *h = UI_H;
    }
    return copy;
#endif
}
#endif

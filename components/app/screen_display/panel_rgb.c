/*
 * The Sunton 8048S050C's panel: 800x480 over the ESP32-S3's RGB peripheral, brought up
 * through esp_lvgl_port rather than by hand.
 *
 * That is a deliberate change of approach. A hand-written version of this file did create
 * the panel and draw into it, but the picture came out doubled and banded: the panel
 * alternates between the driver's two framebuffers, and keeping LVGL's rendering, the
 * buffer the panel is scanning and the vsync handshake in agreement is exactly what the
 * port exists to do. The reference project at ~/Documents/SolidGL runs this same board
 * correctly through the port, with `avoid_tearing` and direct mode, and this now follows
 * it: same initialisation, same flags, same task parameters.
 *
 * All values below come from boards/sunton-8048s050c.json - the pin map, the panel
 * timings, the framebuffer arrangement and the backlight pin.
 */
#include <string.h>

#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "panel_rgb.h"
#include "backlight.h"

static const char *TAG = "panel";

#define PANEL_W 800
#define PANEL_H 480
/* The profile's clock, and the only value this board has measured: 39.02 fps, 25.6 ms a
 * frame. It was raised to 18 MHz in a working tree that also carried a reverted
 * bounce-buffer experiment, and the move into this component (da817b6) carried the 18 in
 * with it - "the pixel clock back to 16 MHz" (8c8d6e7) was never undone on purpose. The
 * extra 12.5 % was never run on its own, and it buys nothing the UI can use: LVGL's own
 * refresh period is 33 ms, so both clocks are above the rate the screen asks for. It did
 * cost something - the scan-out pulls 36 MB/s out of PSRAM at 18 MHz instead of 32 - and a
 * panel driven past its measured timing shows a constant sideways offset before it shows
 * anything else. Back to 16. */
/* 12.5 MHz, down from the profile's 16: the scan-out reads PSRAM continuously, and the
 * panel's FIFO is what runs dry when the rest of the board is using the same memory. A
 * quarter less scan-out is a quarter more slack for everything else; 12.5 MHz still scans
 * 31 frames a second, and LVGL asks for 30. Worth its own measurement if the picture still
 * slips - the number to watch is the frame rate in the boot log, not the clock itself. */
#define PCLK_HZ 12500000


/* boards/sunton-8048s050c.json -> wiring.dataPins */
static const gpio_num_t k_data_pins[16] = {
    8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14,
};

static esp_lcd_panel_handle_t s_panel;
/* The display the port created in panel_rgb_start(); the refresh timer it owns stays paused
 * until the first clock, and panel_rgb_scan_start()/panel_rgb_reinit() resume it. */
static lv_display_t *s_lvgl_disp;
/* True once the LCD engine has been started at least once. */
static bool s_scanning;

/* The port's own task: the reference project's parameters, adjusted for this firmware's
 * core split - the broadcast/encoder owns core 1, so the refresh lives on core 0 with
 * Wi-Fi and lwIP, below their priorities, and no longer queues behind the encoder. A
 * 16 KB stack after the reference measured 72 bytes of margin at half that, and a
 * 500 ms cap on its sleep. */
static const lvgl_port_cfg_t k_port = {
    .task_priority = 4,
    .task_stack = 16384,
    .task_affinity = 0,
    .task_max_sleep_ms = 500,
    .timer_period_ms = 5,
};

lv_display_t *panel_rgb_start(void)
{
    /* The light goes off before the panel exists. The peripheral will scan the two PSRAM
     * framebuffers, and they read as white until something paints them; a pin left floating
     * lights the panel over exactly that. The scan itself does not start until the first
     * clock (panel_rgb_scan_start()), but the light has to be off before then. */
    backlight_hold_off();

    /* Then the panel's supply gets its time before the peripheral exists - before any signal
     * reaches it. This board gives the panel no reset line - the RGB driver sends it no
     * commands, and the profile wires no display-enable pin - so its power-on reset is the only
     * one it gets. The first clock is deferred much further than this (until after the boot's
     * flash work), so this wait is about the GPIO routing and the first data lines, not about
     * the clock. It is behind the light, where it costs boot latency and nothing visible. */
    if (CONFIG_APP_UI_PANEL_SETTLE_MS > 0) {
        ESP_LOGI(TAG, "waiting %d ms for the panel's supply before the peripheral exists",
                 CONFIG_APP_UI_PANEL_SETTLE_MS);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_APP_UI_PANEL_SETTLE_MS));
    }

    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = PCLK_HZ,
            .h_res = PANEL_W,
            .v_res = PANEL_H,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = { .pclk_active_neg = true },
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,               /* the tearing-free path needs both */
        .bounce_buffer_size_px = 0, /* the S3's DMA reaches PSRAM directly (profile) */
        .hsync_gpio_num = 39,
        .vsync_gpio_num = 41,
        .de_gpio_num = 40,
        .pclk_gpio_num = 42,
        .disp_gpio_num = -1,        /* the profile has no display-enable pin */
        .flags = { .fb_in_psram = true },
    };
    memcpy(cfg.data_gpio_nums, k_data_pins, sizeof k_data_pins);

    if (esp_lcd_new_rgb_panel(&cfg, &s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "the RGB panel did not come up (pins or timings refused)");
        s_panel = NULL;
        return NULL;
    }
    /* Optional controls are reported, never fatal: disp_on_off answers ESP_ERR_NOT_SUPPORTED
     * when the profile wires no display-enable pin, and treating that as fatal once turned a
     * working panel into a boot loop and a black screen. */
    esp_err_t err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) ESP_LOGW(TAG, "panel reset: %s (continuing)", esp_err_to_name(err));
    /* esp_lcd_panel_init() is deliberately NOT called here. It is the call that applies the
     * timings, starts the pixel clock and starts the scan, and the panel must stay dark
     * through the boot's flash work - the Snapshot, the Shell's table and Wi-Fi's NVS. A
     * flash access takes the PSRAM window from the scan-out for a few microseconds, which
     * displaces the picture permanently (ADR 0055). The first clock is therefore deferred to
     * panel_rgb_scan_start(), after app_services_start() has returned. */
    err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err == ESP_ERR_NOT_SUPPORTED)
        ESP_LOGI(TAG, "no display-enable pin in the profile, so disp_on_off does nothing");
    else if (err != ESP_OK)
        ESP_LOGW(TAG, "panel display on: %s (continuing)", esp_err_to_name(err));

    if (lvgl_port_init(&k_port) != ESP_OK) {
        ESP_LOGE(TAG, "the LVGL port did not start");
        return NULL;
    }

    const lvgl_port_display_cfg_t dcfg = {
        .panel_handle = s_panel,
        .buffer_size = PANEL_W * PANEL_H,
        .double_buffer = true,
        .hres = PANEL_W,
        .vres = PANEL_H,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_spiram = true,
            /* The port takes the panel's own two framebuffers, LVGL draws straight
             * into them, and each frame closes with the panel's vsync: nothing is
             * copied into the buffer being scanned. */
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rcfg = { .flags = { .avoid_tearing = true } };

    /* The port's lock is taken *before* the display exists and held across the first refresh
     * being scheduled and paused. lv_display_create() invalidates the screen and marks the
     * refresh timer ready, so without this the port task could flush into an engine that is
     * not scanning yet - and an RGB flush in direct mode waits for the panel's vsync, which
     * would never come. The lock is recursive, so lvgl_port_add_disp_rgb() takes it again
     * for itself. */
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "the LVGL port did not give its lock; the panel stays dark");
        return NULL;
    }
    lv_display_t *disp = lvgl_port_add_disp_rgb(&dcfg, &rcfg);
    if (!disp) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "the port refused the RGB display");
        return NULL;
    }
    s_lvgl_disp = disp;
    lv_timer_t *refr = lv_display_get_refr_timer(disp);
    if (refr) lv_timer_pause(refr); /* resumed by panel_rgb_scan_start() */
    lvgl_port_unlock();

    ESP_LOGI(TAG, "panel up through esp_lvgl_port: %dx%d, two framebuffers in PSRAM, pclk %u Hz, "
                  "first clock deferred",
             PANEL_W, PANEL_H, (unsigned)PCLK_HZ);

    /* The light stays off on purpose. The panel is dark until whoever brought it up has
     * painted something worth lighting - the boot mark - and only then calls
     * backlight_start(). Lighting it here showed the uninitialised framebuffers. */
    return disp;
}

esp_err_t panel_rgb_scan_start(void)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    if (s_scanning) return ESP_OK;

    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "the LVGL port did not give its lock; the panel's clock never started");
        return ESP_ERR_TIMEOUT;
    }
    /* This is the first clock and the first scan. It is legal for the caller to hold the port
     * lock already (the boot mark path does): the mutex is recursive. */
    esp_err_t err = esp_lcd_panel_init(s_panel);
    if (err == ESP_OK) {
        s_scanning = true;
        if (s_lvgl_disp) {
            lv_timer_t *refr = lv_display_get_refr_timer(s_lvgl_disp);
            if (refr) lv_timer_resume(refr);
        }
    }
    lvgl_port_unlock();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "the panel's first clock did not start: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "the panel's first clock starts with the boot's flash work behind it");
    return ESP_OK;
}

esp_err_t panel_rgb_clear(void)
{
    return panel_rgb_fill(0x0000);
}

esp_err_t panel_rgb_fill(uint16_t color)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;

    void *fb[2] = { NULL, NULL };
    if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &fb[0], &fb[1]) != ESP_OK || !fb[0])
        return ESP_FAIL;

    /* PSRAM powers up reading white, and the peripheral starts scanning both buffers at
     * driver init, so a panel that is lit before LVGL has drawn anything shows exactly
     * that. Filling both with a known colour is what makes "the first frame is the boot
     * mark" true rather than likely. */
    for (int i = 0; i < 2; i++) {
        if (!fb[i]) continue;
        uint16_t *px = (uint16_t *)fb[i];
        for (size_t n = 0; n < (size_t)PANEL_W * PANEL_H; n++) px[n] = color;
    }
    return ESP_OK;
}

esp_err_t panel_rgb_reinit(void)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    /* rgb_panel_init stops the LCD engine, resets the FIFO and the GDMA channel and starts the
     * transmission again, which is the only way from this side to re-establish the frame
     * boundary after something took the PSRAM window away. */
    esp_err_t err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) return err;

    /* It also starts the first clock if panel_rgb_scan_start() never managed to, so the
     * refresh timer is resumed here too: the cassette that follows must not wait on a timer
     * that was paused for a clock this call has just established. */
    s_scanning = true;
    if (s_lvgl_disp) {
        lv_timer_t *refr = lv_display_get_refr_timer(s_lvgl_disp);
        if (refr) lv_timer_resume(refr);
    }
    return ESP_OK;
}

bool panel_rgb_frame(const uint16_t **px, int *w, int *h)
{
    if (!s_panel || !px || !w || !h) return false;

    /* The port configured two framebuffers in PSRAM and LVGL draws into them in direct
     * mode, so they are ordinary readable memory; the peripheral scans them, it does not
     * own them. A caller that wants a whole picture refreshes the screen twice first, and
     * then neither index can be the stale one. */
    void *fb0 = NULL;
    void *fb1 = NULL;
    if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1) != ESP_OK || !fb0)
        return false;

    *px = (const uint16_t *)fb0;
    *w = PANEL_W;
    *h = PANEL_H;
    return true;
}

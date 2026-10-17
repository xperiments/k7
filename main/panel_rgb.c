/*
 * The Sunton 8048S050C's panel: 800x480 over the ESP32-S3's RGB peripheral.
 *
 * Every value here comes from boards/sunton-8048s050c.json, which is the board profile:
 * the pin map, the panel timings, the framebuffer arrangement and the backlight pin. The
 * profile says which of its rows are measured and which were declared by the reference
 * project; the timing values are the ones this board is known to run at.
 *
 * The two framebuffers live in PSRAM and the panel scans them out continuously, so LVGL
 * draws straight into one and there is no copy anywhere - which is why the draw buffer
 * on this board is a whole screen (800*480*2 = 750 KB) rather than a strip.
 */
#include <string.h>

#include "driver/gpio.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "panel_rgb.h"

static const char *TAG = "panel";

#define PANEL_W 800
#define PANEL_H 480
#define PCLK_HZ 16000000

/* boards/sunton-8048s050c.json -> wiring.dataPins, in the order the peripheral wants
 * them (B0..B4, G0..G5, R0..R4). */
static const gpio_num_t k_data_pins[16] = {
    8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14,
};

static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_vsync;
static int s_vsync_misses;

/* Signalled from the RGB peripheral's own interrupt when a frame has been sent. This is
 * what bounds how fast the interface can redraw: one frame at a time, no tearing. */
static bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *e, void *ctx)
{
    (void)panel; (void)e; (void)ctx;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(s_vsync, &wake);
    return wake == pdTRUE;
}

esp_lcd_panel_handle_t panel_rgb_start(void)
{
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
            .flags = {
                /* The profile records pclkActiveNeg true and the three idle levels low. */
                .pclk_active_neg = true,
                .hsync_idle_low = false,
                .vsync_idle_low = false,
                .de_idle_high = false,
            },
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,                 /* one being scanned out, one being drawn */
        .bounce_buffer_size_px = 0,   /* the S3's DMA reaches PSRAM directly (profile) */
        .hsync_gpio_num = 39,
        .vsync_gpio_num = 41,
        .de_gpio_num = 40,
        .pclk_gpio_num = 42,
        .disp_gpio_num = -1,          /* the profile has no panel reset pin */
        .flags = {
            .fb_in_psram = true,
        },
    };
    memcpy(cfg.data_gpio_nums, k_data_pins, sizeof k_data_pins);

    if (esp_lcd_new_rgb_panel(&cfg, &s_panel) != ESP_OK) {
        ESP_LOGE(TAG, "the RGB panel did not come up (pins or timings refused)");
        s_panel = NULL;
        return NULL;
    }
    s_vsync = xSemaphoreCreateCounting(2, 0);
    esp_lcd_rgb_panel_event_callbacks_t cbs = { .on_vsync = on_vsync };
    /* Checked, not assumed: IDF rejects an on_vsync callback it cannot place in IRAM, and
     * without the callback the flush handshake below has nothing to wait for. */
    esp_err_t cb_err = esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, NULL);
    if (cb_err != ESP_OK)
        ESP_LOGE(TAG, "the panel refused the vsync callback (%s): frames will not wait for it",
                 esp_err_to_name(cb_err));
    /* None of these reboots the firmware. The panel's optional controls answer
     * ESP_ERR_NOT_SUPPORTED when the profile does not wire them - this board has no
     * display-enable pin, so disp_on_off says exactly that - and treating an optional
     * feature's absence as a fatal error is what turned a working panel into a boot loop
     * and a black screen. They are reported and stepped over. */
    esp_err_t perr = esp_lcd_panel_reset(s_panel);
    if (perr != ESP_OK) ESP_LOGW(TAG, "panel reset: %s (continuing)", esp_err_to_name(perr));
    perr = esp_lcd_panel_init(s_panel);
    if (perr != ESP_OK) ESP_LOGW(TAG, "panel init: %s (continuing)", esp_err_to_name(perr));
    perr = esp_lcd_panel_disp_on_off(s_panel, true);
    if (perr == ESP_ERR_NOT_SUPPORTED)
        ESP_LOGI(TAG, "no display-enable pin in the profile, so disp_on_off does nothing");
    else if (perr != ESP_OK)
        ESP_LOGW(TAG, "panel display on: %s (continuing)", esp_err_to_name(perr));

    /* The backlight is a plain GPIO on this board (profile: backlight.gpio 2, active
     * high), not one of the I/O expander's pins. */
    gpio_config_t bl = { .pin_bit_mask = 1ULL << 2, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    /* Blinked on purpose. A framebuffer that is filled but never seen looks exactly like
     * a panel that never came up, and the difference is the backlight: if the screen's
     * brightness changes here, this pin drives it, and anything still invisible is above
     * this layer. First-light diagnostic; remove it once the panel is known good. */
    gpio_set_level(2, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    gpio_set_level(2, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    gpio_set_level(2, 1);
    ESP_LOGW(TAG, "backlight blinked on GPIO %d (on-off-on): if the screen did not change brightness, this is not its pin", 2);

    /* Something visible before anything above this layer is trusted to work.
     *
     * The panel scans its own framebuffer, so filling one with a test pattern proves the
     * whole hardware path - clock, timings, pins, PSRAM framebuffer, backlight - without
     * LVGL being involved at all. If these bars never appear, nothing further up can be
     * the reason. If they appear and the interface never does, the fault is above here and
     * the log will say so. */
    void *fb0 = NULL;
    if (esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb0) == ESP_OK && fb0) {
        uint16_t *px = fb0;
        for (int y = 0; y < PANEL_H; y++) {
            for (int x = 0; x < PANEL_W; x++) {
                uint16_t c;
                if (y < 24)                     c = 0xFFFF;                     /* white */
                else if (y < 96)                c = 0xF800;                     /* red */
                else if (y < 168)               c = 0x07E0;                     /* green */
                else if (y < 240)               c = 0x001F;                     /* blue */
                else                            c = (uint16_t)((x >> 3) & 0x1F) << 11; /* ramp */
                px[y * PANEL_W + x] = c;
            }
        }
        ESP_LOGW(TAG, "test pattern written to framebuffer 0 at %p: white, red, green, blue, then a ramp",
                 fb0);
    } else {
        ESP_LOGE(TAG, "no framebuffer to write a test pattern into");
    }

    ESP_LOGI(TAG, "panel up: %dx%d, %u framebuffers in PSRAM, pclk %u Hz, backlight on GPIO 2",
             PANEL_W, PANEL_H, 2u, (unsigned)PCLK_HZ);
    return s_panel;
}

void panel_rgb_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area; (void)px;
    if (lv_display_flush_is_last(disp)) {
        /* The RGB driver scans a whole framebuffer out; it does not copy an area. So what
         * it needs is the buffer LVGL is drawing into, not the pointer it hands over:
         * LVGL's own documentation says `px_map` is the rendered image *of `area`*, which
         * is a pointer into the middle of the buffer. Handing that over as if it were the
         * start of an 800x480 framebuffer is a shifted, wrapped, doubled picture - exactly
         * what the panel showed the first time it lit. */
        lv_draw_buf_t *active = lv_display_get_buf_active(disp);
        if (!active) { lv_display_flush_ready(disp); return; }
        px = active->data;
        /* Pointing the panel at one of its own framebuffers switches to it rather than
         * copying. Waiting for the next vsync afterwards is what keeps LVGL from drawing
         * into a buffer the panel is still reading - but the wait is bounded, because the
         * alternative is a first flush that never returns and a screen that stays black
         * with nothing in the log to say why. Three misses are logged; after that the
         * interface simply draws without the handshake, which shows a torn frame rather
         * than no frame at all. */
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, PANEL_W, PANEL_H, px);
        xSemaphoreTake(s_vsync, 0);                       /* drop a stale signal, if any */
        if (xSemaphoreTake(s_vsync, pdMS_TO_TICKS(100)) != pdTRUE) {
            if (s_vsync_misses < 3) {
                s_vsync_misses++;
                ESP_LOGW(TAG, "no vsync within 100 ms (miss %d): drawing without the handshake",
                         s_vsync_misses);
            }
        }
    }
    lv_display_flush_ready(disp);
}

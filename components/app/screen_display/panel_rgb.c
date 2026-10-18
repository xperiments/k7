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

static const char *TAG = "panel";

#define PANEL_W 800
#define PANEL_H 480
#define PCLK_HZ 18000000
#define BACKLIGHT_GPIO 2

/* boards/sunton-8048s050c.json -> wiring.dataPins */
static const gpio_num_t k_data_pins[16] = {
    8, 3, 46, 9, 1, 5, 6, 7, 15, 16, 4, 45, 48, 47, 21, 14,
};

static esp_lcd_panel_handle_t s_panel;

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
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) ESP_LOGW(TAG, "panel init: %s (continuing)", esp_err_to_name(err));
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
            /* The port takes the panel's own two framebuffers, LVGL draws straight into
             * them, and each frame closes with the panel's vsync: nothing is copied into
             * the buffer being scanned. */
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rcfg = { .flags = { .avoid_tearing = true } };

    lv_display_t *disp = lvgl_port_add_disp_rgb(&dcfg, &rcfg);
    if (!disp) {
        ESP_LOGE(TAG, "the port refused the RGB display");
        return NULL;
    }
    ESP_LOGI(TAG, "panel up through esp_lvgl_port: %dx%d, two framebuffers in PSRAM, pclk %u Hz",
             PANEL_W, PANEL_H, (unsigned)PCLK_HZ);

    /* Something visible before anything above this layer is trusted to work. */
    gpio_config_t bl = { .pin_bit_mask = 1ULL << BACKLIGHT_GPIO, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);
    gpio_set_level(BACKLIGHT_GPIO, 1);
    ESP_LOGI(TAG, "backlight on GPIO %d", BACKLIGHT_GPIO);
    return disp;
}

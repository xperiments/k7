/*
 * The strip, on the board; see strip.h.
 *
 * Eight WS2812B pixels on GPIO 17, refreshed from the RGB group's state at 30 Hz. The frame is
 * `rgb_frame()`'s, the level is the broadcast's, and the only thing this file decides is when
 * they run and how they reach the wire.
 *
 * Why its own clock: the screen task's cadence is adaptive - it rests at least three times its
 * own frame cost, which on this part has been over 100 ms for a full-screen invalidation
 * (`screen_display.c:443`) - so a strip that rode it would run the chase at whatever pace the
 * screen left behind. 30 Hz is above what the eye resolves in a chase of eight pixels and far
 * below what the RMT costs: 8 px x 24 bits is 192 bits at 800 kHz, about 240 us plus the part's
 * latch, under 2% of the tick.
 *
 * The state is copied, not shared: the panel writes it from the LVGL task and this task reads it,
 * five ints under a spinlock, 30 times a second. The publish hook is installed after the screen
 * has built the cassette - `cassette_rgb_create()` clears the hook, so an install before it would
 * be wiped - which makes that one late-install call the only read of the knobs outside the LVGL
 * lock: five aligned ints at boot, before the first touch.
 *
 * `level` is the louder channel, not the average: a mono strip has no stereo image to preserve,
 * and averaging would halve a hard-panned track. The LEVEL effect clamps what it is given, but
 * the sample is normalised here so a NaN from an idle encoder cannot reach the colour arithmetic.
 */
#include "strip.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/rmt_types.h"
#include "led_strip.h"

#include "mini_icecast.h"
#include "rgb.h"
#include "screen_display.h"

/* GPIO 17 is one of the four pins the Sunton 8048S050C leaves free - 0, 17, 43, 44 - and the
 * other three are the BOOT strap and the console's UART pair
 * (docs/research/server/s15-i2s-dac-output.md:629-637). The bare N16R8 devkit's recommended I2S
 * pinout takes 17 for BCK, so a strip and that DAC cannot share a board. */
#define STRIP_GPIO      17
#define STRIP_PIXELS    8
#define STRIP_PERIOD_MS 33

#define STRIP_TASK_STACK 3072
#define STRIP_TASK_PRIO  2 /* below the screen's 3 and the port's 4, which may preempt a tick */
#define STRIP_TASK_CORE  0 /* beside the screen, not the broadcast */

static const char *TAG = "strip";

static led_strip_handle_t s_strip;
static rgb_state_t s_state;                 /* the panel's last publish */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_started;

/* Where the panel's state goes: a copy, and the next tick shows it. Called from the LVGL task
 * on a knob move, and once from the appliance task when the hook is installed. */
static void strip_publish(const rgb_state_t *state, void *ctx)
{
    (void)ctx;
    portENTER_CRITICAL(&s_lock);
    s_state = *state;
    portEXIT_CRITICAL(&s_lock);
}

/* The broadcast's level, 0..1, as one number for a mono strip. `mini_icecast_levels()` is the
 * two fields without the status's lock and title copies: this runs 30 times a second and has no
 * use for a Track's name. A NaN (no level yet, or a divisor that never came) is taken out by the
 * first comparison, which is false for it. */
static float strip_level(void)
{
    float left, right;
    mini_icecast_levels(&left, &right);
    float level = left > right ? left : right;
    if (!(level >= 0.0f)) return 0.0f;
    if (level > 1.0f) return 1.0f;
    return level;
}

static void strip_task(void *arg)
{
    (void)arg;
    rgb_pixel_t px[STRIP_PIXELS];

    for (;;) {
        rgb_state_t state;
        portENTER_CRITICAL(&s_lock);
        state = s_state;
        portEXIT_CRITICAL(&s_lock);

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        int n = rgb_frame(&state, now_ms, strip_level(), px, STRIP_PIXELS);
        for (int i = 0; i < n; i++)
            led_strip_set_pixel(s_strip, (uint32_t)i, px[i].r, px[i].g, px[i].b);
        led_strip_refresh(s_strip);

        vTaskDelay(pdMS_TO_TICKS(STRIP_PERIOD_MS));
    }
}

bool strip_start(void)
{
    if (s_started) return true;

    const led_strip_config_t config = {
        .strip_gpio_num = STRIP_GPIO,
        .max_leds = STRIP_PIXELS,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, /* 100 ns a tick: the bit timings land exactly */
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&config, &rmt, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no RMT channel for the strip on GPIO %d: %s", STRIP_GPIO,
                 esp_err_to_name(err));
        return false;
    }

    /* The panel's own starting point, until its hook hands over the real one: full white at full
     * brightness, OFF, which is black. */
    rgb_init(&s_state);
    led_strip_clear(s_strip);

    if (xTaskCreatePinnedToCore(strip_task, "strip", STRIP_TASK_STACK, NULL, STRIP_TASK_PRIO,
                                NULL, STRIP_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "no task for the strip; the light stays dark");
        led_strip_del(s_strip);
        s_strip = NULL;
        return false;
    }

    screen_display_rgb_publish(strip_publish, NULL);
    s_started = true;
    ESP_LOGI(TAG, "%d WS2812B pixels on GPIO %d, %d Hz", STRIP_PIXELS, STRIP_GPIO,
             1000 / STRIP_PERIOD_MS);
    return true;
}

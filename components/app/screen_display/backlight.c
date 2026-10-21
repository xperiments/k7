/*
 * The backlight on the Sunton board: GPIO 2, driven by LEDC so the screen can dim in
 * steps and return to full brightness on a touch.
 *
 * The PWM is the reference firmware's 400 Hz, 8-bit configuration, not an inaudible
 * 20 kHz: higher frequencies made this panel's backlight cut out part-way down the
 * range. The duty is the percentage the Kconfig names, not a gamma-corrected one:
 * "60%" here means 60% of full duty, which is the number printed in the log and the
 * one to tune against the panel.
 *
 * The fade is software, stepped by backlight_tick() from the screen task. That is
 * deliberate: the LEDC hardware fade service needs its own interrupt, and on this
 * board its first fade was not dependable. A 25 Hz duty step is smooth over the
 * multi-second dim and costs no extra task or ISR.
 */
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "backlight.h"

static const char *TAG = "backlight";

#define BACKLIGHT_GPIO          2
#define BACKLIGHT_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_TIMER    LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BACKLIGHT_PWM_HZ        CONFIG_APP_UI_BACKLIGHT_PWM_HZ
#define BACKLIGHT_PWM_RES       LEDC_TIMER_8_BIT
#define BACKLIGHT_MAX_DUTY      ((1u << 8) - 1)

static bool s_pwm;
/* The duty actually written, and the level the current fade is heading to. */
static int  s_current_pct = -1;
static int  s_target_pct = -1;
static int  s_fade_from_pct;
static int  s_fade_ms;
static int64_t s_fade_start_us;

/* The touch read and the screen task both drive the level, and a fade is a
 * read-modify-write of two integers. Serialise them. Static storage: no heap. */
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;

/* The old adapter drove the pin as a plain output. That is still the right failure
 * path: if LEDC refuses the timer or the channel, hold the pin high so the panel is
 * lit, and let only the dimming go missing. */
static void backlight_fallback(void)
{
    gpio_config_t bl = { .pin_bit_mask = 1ULL << BACKLIGHT_GPIO, .mode = GPIO_MODE_OUTPUT };
    esp_err_t err = gpio_config(&bl);
    if (err == ESP_OK) err = gpio_set_level(BACKLIGHT_GPIO, 1);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "the backlight has no PWM and no plain output: %s", esp_err_to_name(err));
    else
        ESP_LOGW(TAG, "backlight on GPIO %d held high; brightness control is off", BACKLIGHT_GPIO);
}

/* The caller holds s_lock. */
static esp_err_t backlight_write_locked(int percent)
{
    uint32_t duty = (uint32_t)percent * BACKLIGHT_MAX_DUTY / 100;
    esp_err_t err = ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    if (err == ESP_OK) err = ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
    return err;
}

/* One fade step. The caller holds s_lock. The level is interpolated from the time
 * elapsed, not from the number of calls, so a slow screen frame cannot stretch or
 * compress the fade: it just gives the fade fewer, larger steps. */
static void backlight_step_locked(void)
{
    if (s_current_pct == s_target_pct) return;

    int elapsed_ms = (int)((esp_timer_get_time() - s_fade_start_us) / 1000);
    int pct;
    if (s_fade_ms <= 0 || elapsed_ms >= s_fade_ms) {
        pct = s_target_pct;
    } else {
        pct = s_fade_from_pct +
              (int)(((int64_t)(s_target_pct - s_fade_from_pct) * elapsed_ms) / s_fade_ms);
        if (pct == s_current_pct) return;   /* too early for a whole 1% step */
    }
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;

    if (backlight_write_locked(pct) == ESP_OK)
        s_current_pct = pct;
}

esp_err_t backlight_start(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_PWM_RES,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer refused: %s", esp_err_to_name(err));
        backlight_fallback();
        return err;
    }

    const ledc_channel_config_t channel = {
        .gpio_num = BACKLIGHT_GPIO,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        /* A light-sleep entry should not leave the backlight glowing; the screen is
         * not being refreshed then anyway. */
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel refused GPIO %d: %s", BACKLIGHT_GPIO, esp_err_to_name(err));
        backlight_fallback();
        return err;
    }

    s_pwm = true;
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);

    /* Boot at the awake level without a fade: the panel is about to show its first
     * frame, and a visible ramp from uninitialised content is not wanted. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_current_pct = CONFIG_APP_UI_BACKLIGHT_AWAKE_PCT;
    s_target_pct = CONFIG_APP_UI_BACKLIGHT_AWAKE_PCT;
    s_fade_from_pct = s_current_pct;
    s_fade_ms = 0;
    if (backlight_write_locked(s_current_pct) != ESP_OK)
        ESP_LOGE(TAG, "the awake backlight level was refused");
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "backlight on GPIO %d: %d%% awake, then %d%% at %d s, %d%% at %d s, "
             "%d%% at %d s; %d ms dim fade, %d ms wake fade",
             BACKLIGHT_GPIO, CONFIG_APP_UI_BACKLIGHT_AWAKE_PCT,
             CONFIG_APP_UI_BACKLIGHT_DIM1_PCT, CONFIG_APP_UI_BACKLIGHT_DIM1_S,
             CONFIG_APP_UI_BACKLIGHT_DIM2_PCT, CONFIG_APP_UI_BACKLIGHT_DIM2_S,
             CONFIG_APP_UI_BACKLIGHT_DIM3_PCT, CONFIG_APP_UI_BACKLIGHT_DIM3_S,
             CONFIG_APP_UI_BACKLIGHT_DIM_FADE_MS, CONFIG_APP_UI_BACKLIGHT_WAKE_FADE_MS);
    return ESP_OK;
}

bool backlight_is_dimmed(void)
{
    return s_pwm && s_target_pct >= 0 &&
           s_target_pct < CONFIG_APP_UI_BACKLIGHT_AWAKE_PCT;
}

bool backlight_is_fading(void)
{
    return s_pwm && s_current_pct != s_target_pct;
}

void backlight_tick(void)
{
    if (!s_pwm) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    backlight_step_locked();
    xSemaphoreGive(s_lock);
}

void backlight_set_percent(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (!s_pwm) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (percent == s_target_pct) {
        /* A second request for the level already in flight must not restart the
         * fade; a touch read asks every 10 ms while the finger is down. */
        xSemaphoreGive(s_lock);
        return;
    }

    s_fade_from_pct = (s_current_pct >= 0) ? s_current_pct : percent;
    s_target_pct = percent;
    s_fade_start_us = esp_timer_get_time();
    /* A step up is the wake; a step down is the idle ladder. They get different
     * times: the dim should be seen, the wake should feel immediate. */
    s_fade_ms = (percent > s_fade_from_pct)
                    ? CONFIG_APP_UI_BACKLIGHT_WAKE_FADE_MS
                    : CONFIG_APP_UI_BACKLIGHT_DIM_FADE_MS;

    /* First step now, so a wake begins on the frame of the touch and a dim begins
     * before the screen task next rests. */
    backlight_step_locked();
    xSemaphoreGive(s_lock);
}

void backlight_set_percent_immediate(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (!s_pwm) return;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (backlight_write_locked(percent) == ESP_OK) {
        s_current_pct = percent;
        s_target_pct = percent;
        s_fade_from_pct = percent;
        s_fade_ms = 0;
    } else {
        ESP_LOGE(TAG, "backlight duty %d%% refused", percent);
    }
    xSemaphoreGive(s_lock);
}

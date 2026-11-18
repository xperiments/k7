/*
 * The K7Player's light: one LED, on the devkit's own GPIO 2, driven by PWM so that "dim" is a
 * real state and not a second colour.
 *
 * The pattern is the light module's (1 Hz blink, dim, bright, 4 Hz blink); this only turns a
 * level into a duty. A board whose LED is elsewhere changes this one line and nothing above it.
 */
#include "player_board.h"

#include "driver/ledc.h"
#include "esp_log.h"

#define LIGHT_GPIO     2
#define LIGHT_CHANNEL  LEDC_CHANNEL_0
#define LIGHT_TIMER    LEDC_TIMER_0
#define LIGHT_MODE     LEDC_LOW_SPEED_MODE

static const char *TAG = "led";
static bool s_ready;

static bool bring_up(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LIGHT_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LIGHT_TIMER,
        .freq_hz = 5000, /* well above anything an eye sees, well below the LED's limit */
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if (ledc_timer_config(&timer) != ESP_OK) return false;

    const ledc_channel_config_t channel = {
        .gpio_num = LIGHT_GPIO,
        .speed_mode = LIGHT_MODE,
        .channel = LIGHT_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LIGHT_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    if (ledc_channel_config(&channel) != ESP_OK) return false;

    s_ready = true;
    ESP_LOGI(TAG, "light on GPIO %d", LIGHT_GPIO);
    return true;
}

void player_board_light(uint8_t level)
{
    if (!s_ready && !bring_up()) return;
    if (ledc_set_duty(LIGHT_MODE, LIGHT_CHANNEL, level) != ESP_OK) return;
    ledc_update_duty(LIGHT_MODE, LIGHT_CHANNEL);
}

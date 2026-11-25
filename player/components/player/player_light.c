/*
 * player_light - one level, four patterns.
 *
 * It touches three machines' locks in a fixed order (network, stream, playout) and never the
 * other way round, so there is no inversion: none of them calls the light.
 */
#include "player_light.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "player_board.h"
#include "player_net.h"
#include "player_ota.h"
#include "player_playout.h"
#include "player_stream.h"

static const char *TAG = "light";

static player_status_t s_status;
static player_light_t s_state;
static uint8_t s_level;
static player_light_t s_logged;
static uint32_t s_last_log_ms;

static player_light_t decide(void)
{
    player_net_status_t net;
    player_stream_status_t stream;
    player_playout_status_t out;
    player_net_status(&net);
    player_stream_status(&stream);
    player_playout_status(&out);

    /* A network it can use: associated, with the K7 found. While it sweeps, connects or
     * retries, it has none - that is Searching (R31). */
    player_status_set_network(&s_status, net.state == PLAYER_NET_CONNECTED);
    /* A stream is established when the body is being read. */
    player_status_set_stream(&s_status, stream.state == PLAYER_STREAM_BODY);
    /* Audio is going out only when the last output tick wrote decoded frames, not silence. */
    player_status_set_audio(&s_status, out.open && out.audio_now);
    /* A body that is not audio, or an output that will not open (R31). */
    player_status_set_fault(&s_status, stream.fault || out.failed);
    /* An image is being written: the one thing the K7 is doing to this board (R38). */
    player_status_set_updating(&s_status, player_ota_updating());

    return player_status_light(&s_status);
}

player_light_t player_light_tick(uint32_t now)
{
    player_light_t state = decide();
    uint8_t level;

    switch (state) {
    case PLAYER_LIGHT_SEARCHING:
        level = (now % 1000) < 500 ? PLAYER_LIGHT_BRIGHT : 0;
        break;
    case PLAYER_LIGHT_READY:
        level = PLAYER_LIGHT_DIM;
        break;
    case PLAYER_LIGHT_PLAYING:
        level = PLAYER_LIGHT_BRIGHT;
        break;
    case PLAYER_LIGHT_FAULT:
        level = (now % 250) < 125 ? PLAYER_LIGHT_BRIGHT : 0;
        break;
    case PLAYER_LIGHT_UPDATING: {
        /* Two short flashes a second: distinct from Searching's one and Fault's four. */
        uint32_t phase = now % 1000;
        level = (phase < 100 || (phase >= 200 && phase < 300)) ? PLAYER_LIGHT_BRIGHT : 0;
        break;
    }
    default:
        level = 0;
        break;
    }

    if (level != s_level) {
        s_level = level;
        player_board_light(level);
    }
    s_state = state;

    if (state != s_logged) {
        s_logged = state;
        ESP_LOGI(TAG, "%s", player_light_name(state));
    } else if (now - s_last_log_ms >= 5000) {
        /* Every five seconds: a heartbeat that also says what the light was told. The state
         * changes are the interesting lines; this is the one that shows a starved task. */
        s_last_log_ms = now;
        player_net_status_t net;
        player_stream_status_t stream;
        player_playout_status_t out;
        player_net_status(&net);
        player_stream_status(&stream);
        player_playout_status(&out);
        ESP_LOGI(TAG, "%s (net %s, stream %s, audio %d, failed %d) level %u; heap %u free, "
                      "largest %u",
                 player_light_name(state), player_net_state_name(net.state),
                 player_stream_state_name(stream.state), (int)out.audio_now, (int)out.failed,
                 (unsigned)level, (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    }
    return state;
}

void player_light_init(void)
{
    memset(&s_status, 0, sizeof s_status);
    player_status_init(&s_status);
    s_state = PLAYER_LIGHT_SEARCHING;
    s_level = 0;
    s_logged = (player_light_t)-1;
}

player_light_t player_light_state(void) { return s_state; }
uint8_t player_light_level(void) { return s_level; }

static void light_task(void *arg)
{
    (void)arg;
    for (;;) {
        player_light_tick((uint32_t)(esp_timer_get_time() / 1000));
        vTaskDelay(pdMS_TO_TICKS(PLAYER_LIGHT_TICK_MS));
    }
}

void player_light_start_task(void)
{
    if (xTaskCreatePinnedToCore(light_task, "light", 3072, NULL, 2, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "the light task did not start");
}

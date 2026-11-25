/*
 * The K7Player's entry point: bring the board up, start the machine that finds a K7, and get
 * out of the way. The stream client (PL-05), the outputs (PL-06, PL-08) and the light (PL-07)
 * are later tickets, each behind the same seams.
 */
#include "esp_log.h"

#include "player_board.h"
#include "player_net.h"
#include "sdkconfig.h"

#include "player_light.h"
#include "player_ota.h"
#include "player_playout.h"
#include "player_stream.h"
#include "player_tone.h"

static const char *TAG = "player";

void app_main(void)
{
    if (!player_board_init()) {
        ESP_LOGE(TAG, "the board did not come up: nothing to play with");
        return;
    }

    player_net_init();
    player_net_start_task();

    /* The tone's targets and its radio dispatch, before the stream task starts the radio:
     * a 0x06 that arrives early has somewhere to go. */
    player_tone_init();
    player_tone_start();

    /* The stream waits for the network machine to have an address; it starts now so its
     * clock is running when the K7 appears. */
    player_stream_init();
    player_stream_start_task();

    /* The last stage: the ring's frames decoded and sent to the line output. */
    player_playout_init();
    player_playout_start_task();

    /* The image a K7 hands over: its marker and rollback clock first, so a boot after an update
     * is the one that has to prove itself, then the task that writes the spare slot. */
    player_ota_init();
    player_ota_start_task();
    /* The update's frames reach player_ota from the radio's callback: without this the player
     * would hear a probe and drop it, which looks exactly like a K7 that found nobody. */
    player_board_espnow_set_update(player_ota_packet);

    /* The whole surface: one LED, four states, decided from the three machines above. */
    player_light_init();
    player_light_start_task();

    player_net_status_t st;
    player_net_status(&st);
    ESP_LOGI(TAG, "k7player on %s: %s", player_board_name(), player_net_state_name(st.state));
}

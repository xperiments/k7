#include "player_tone.h"

#include <string.h>

#include "espnow_stream.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "player_board.h"
#include "tone_eq.h"

/* The four targets are the only shared state; the filter belongs to the playout task. The
 * lock is a mutex rather than a critical section because the callback is the radio's task and
 * the reader is the playout task: each hold is four floats long. */
static tone_eq_t s_eq;
static uint32_t s_rate;
static SemaphoreHandle_t s_lock;
static float s_band_db[TONE_EQ_BANDS];
static float s_gain_db;
static bool s_have_targets;

static bool lock(void)
{
    if (!s_lock) return true;
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

static void unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

void player_tone_init(void)
{
    memset(&s_eq, 0, sizeof s_eq);
    /* The stream's rate is not known until the first frame header; 44.1 kHz is the station's
     * default and the filter is rebuilt by set_rate() if the stream says otherwise. */
    s_rate = 44100;
    tone_eq_init(&s_eq, s_rate);
    memset(s_band_db, 0, sizeof s_band_db);
    s_gain_db = 0.0f;
    s_have_targets = false;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
}

void player_tone_start(void)
{
    player_board_espnow_set_tone(player_tone_packet);
}

void player_tone_packet(const uint8_t *pkt, size_t len)
{
    es_tone_t t;
    if (!es_tone_parse(pkt, len, &t)) return;

    float band[TONE_EQ_BANDS] = {
        (float)t.low_cdb / 100.0f,
        (float)t.mid_cdb / 100.0f,
        (float)t.high_cdb / 100.0f,
    };
    float gain = (float)t.gain_cdb / 100.0f;

    if (!lock()) return;
    for (int b = 0; b < TONE_EQ_BANDS; b++) s_band_db[b] = band[b];
    s_gain_db = gain;
    s_have_targets = true;
    unlock();
}

void player_tone_set_rate(uint32_t rate)
{
    if (!rate || rate == s_rate) return;
    s_rate = rate;
    tone_eq_init(&s_eq, rate);

    float band[TONE_EQ_BANDS];
    float gain;
    bool have;
    if (!lock()) return;
    for (int b = 0; b < TONE_EQ_BANDS; b++) band[b] = s_band_db[b];
    gain = s_gain_db;
    have = s_have_targets;
    unlock();

    if (!have) return;
    for (int b = 0; b < TONE_EQ_BANDS; b++) tone_eq_set_band(&s_eq, b, band[b]);
    tone_eq_set_gain(&s_eq, gain);
}

void player_tone_process(int16_t *pcm, size_t frames)
{
    if (!pcm || frames == 0) return;

    float band[TONE_EQ_BANDS];
    float gain;
    bool have;
    if (!lock()) return;
    for (int b = 0; b < TONE_EQ_BANDS; b++) band[b] = s_band_db[b];
    gain = s_gain_db;
    have = s_have_targets;
    unlock();

    /* The setters only record targets; process() owns the slew and the coefficients. Setting
     * them every call is what makes a packet that landed between two chunks take effect on the
     * next one, and it is a few stores. */
    if (have) {
        for (int b = 0; b < TONE_EQ_BANDS; b++) tone_eq_set_band(&s_eq, b, band[b]);
        tone_eq_set_gain(&s_eq, gain);
    }
    tone_eq_process(&s_eq, pcm, frames);
}

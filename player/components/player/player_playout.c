/*
 * player_playout - decode, write, and keep writing when there is nothing to write.
 *
 * The decoder is dr_mp3's frame decoder, the same vendored header the K7 decodes its card with:
 * one whole MP3 frame in, up to 1152 stereo frames out. The ring already guarantees whole frames,
 * so this stage is a loop and not a parser.
 */
#include "player_playout.h"


#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"

#include "player_output.h"
#include "player_ring.h"
#include "player_stream.h"

static const char *TAG = "playout";

typedef struct {
    bool open;
    uint32_t rate;
    uint32_t decoded;
    uint32_t refused;
    uint32_t frames;
    uint32_t silent;
    uint32_t underruns;
    uint32_t primes;
    bool playing;
    bool priming;
    bool played_once;
    bool audio_now;
    bool failed;
    drmp3dec dec;
    int16_t pcm[DRMP3_MAX_SAMPLES_PER_FRAME];
} machine_t;

static machine_t M;
static SemaphoreHandle_t s_lock;
static int16_t s_silence[PLAYER_PLAYOUT_SILENCE_FRAMES * 2];

static bool lock(void)
{
    if (!s_lock) return true;
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE;
}

/* The ring's depth in time, not in frames: the station's bit rate is what says how many bytes
 * one second of it is. Capped by the room the ring actually has, less one frame: a station slow
 * enough for 4.5 s to be more than 96 KB primes the ring full. */
static uint32_t prime_bytes(uint32_t kbps, uint32_t ms)
{
    if (!kbps) return 0;
    uint32_t want = kbps * 125u * ms / 1000u;
    uint32_t room = player_ring_capacity() - PLAYER_RING_SLOT;
    return want > room ? room : want;
}

static void unlock(void)
{
    if (s_lock) xSemaphoreGive(s_lock);
}

/* The stream's rate decides the output's (R27), and a change reopens it. Called with the
 * lock held, and it is short: opening the line is a driver call, not a wait on audio. */
static void follow_rate(uint32_t rate)
{
    if (!rate) return;


    if (M.open && M.rate == rate) return;
    if (M.open) player_output_close();
    M.open = player_output_open(rate);
    M.rate = rate;
    M.failed = !M.open;
    if (M.open)
        ESP_LOGI(TAG, "output open at %u Hz on the line", (unsigned)rate);
    else
        ESP_LOGW(TAG, "no output at %u Hz", (unsigned)rate);
}

/* One MP3 frame decoded and written. No lock is held here: the write blocks until the I2S DMA
 * has room, which is most of a frame's duration, and a lock across it starves every reader -
 * a lower-priority task waiting on it can lose to this task forever, because this task
 * outranks it and re-takes the lock the moment it is free. */
static int decode_and_play(const uint8_t *frame, size_t len)
{
    drmp3dec_frame_info info;
    memset(&info, 0, sizeof info);
    int samples = drmp3dec_decode_frame(&M.dec, frame, (int)len, M.pcm, &info);
    if (samples <= 0 || info.channels < 1) return 0;

    if (info.channels == 1) {
        /* The line is stereo: a mono frame goes to both sides. */
        for (int i = samples - 1; i >= 0; i--) {
            M.pcm[i * 2] = M.pcm[i];
            M.pcm[i * 2 + 1] = M.pcm[i];
        }
    }
    player_output_write(M.pcm, (size_t)samples);
    return samples;
}

int player_playout_tick(void)
{
    player_stream_status_t st;
    player_stream_status(&st);

    lock();
    follow_rate(st.rate);
    bool open = M.open;
    unlock();
    if (!open) return 0;

    /* Priming: hold the line silent until the ring holds what an outage has to eat. Without
     * this the ring's depth is whatever the network's bursts left - measured at 27 KB of its
     * 64 - so a one-second outage empties it and the fix buys nothing. Only while the
     * connection is live: a body that has ended is played, not waited on. */
    if (!M.playing && st.state == PLAYER_STREAM_BODY &&
        st.ring_used < prime_bytes(st.bitrate, M.played_once ? PLAYER_PLAYOUT_RESUME_MS
                                                             : PLAYER_PLAYOUT_PRIME_MS)) {
        player_output_write(s_silence, PLAYER_PLAYOUT_SILENCE_FRAMES);
        lock();
        M.silent += PLAYER_PLAYOUT_SILENCE_FRAMES;
        if (!M.priming) {
            M.priming = true;
            M.primes++;
        }
        unlock();
        return PLAYER_PLAYOUT_SILENCE_FRAMES;
    }

    int written_now = 0;
    uint32_t decoded = 0, refused = 0, frames = 0;
    int played = 0;
    for (int i = 0; i < PLAYER_PLAYOUT_MAX_FRAMES; i++) {
        uint8_t frame[PLAYER_RING_SLOT];
        size_t len = 0;
        if (!player_stream_pop(frame, sizeof frame, &len)) break;
        int samples = decode_and_play(frame, len);
        if (samples <= 0) {
            refused++;
            continue;
        }
        decoded++;
        frames += (uint32_t)samples;
        written_now += samples;
        played++;
    }

    if (played == 0) {
        /* Nothing to play: silence rather than a stop. The DAC keeps clocking, and the gap is
         * the gap the network left. */
        player_output_write(s_silence, PLAYER_PLAYOUT_SILENCE_FRAMES);
        written_now = PLAYER_PLAYOUT_SILENCE_FRAMES;
    }

    lock();
    M.decoded += decoded;
    M.refused += refused;
    M.frames += frames;
    M.audio_now = played > 0;
    if (played > 0) {
        M.playing = true;
        M.priming = false;
        M.played_once = true;
    } else {
        /* Nothing to play: the output is about to wait for the buffer again, which is a
         * deliberate silence and not an underrun. */
        M.playing = false;
        M.silent += PLAYER_PLAYOUT_SILENCE_FRAMES;
        if (st.state == PLAYER_STREAM_BODY && !M.priming) M.underruns++;
    }
    unlock();
    return written_now;
}

void player_playout_init(void)
{
    memset(&M, 0, sizeof M);
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
}

void player_playout_status(player_playout_status_t *out)
{
    if (!out) return;
    lock();
    out->open = M.open;
    out->rate = M.rate;
    out->decoded = M.decoded;
    out->refused = M.refused;
    out->frames = M.frames;
    out->silent = M.silent;
    out->underruns = M.underruns;
    out->primes = M.primes;
    out->audio_now = M.audio_now;
    out->failed = M.failed;
    unlock();
}

static void playout_task(void *arg)
{
    (void)arg;
    uint32_t logged = 0;
    for (;;) {
        int wrote = player_playout_tick();
        /* One tick, not five milliseconds: FreeRTOS's tick is 10 ms here, so
         * pdMS_TO_TICKS(5) is 0 and vTaskDelay(0) only yields - a task that spins and starves
         * every other task on its core. When the output is open the writes pace this loop;
         * when it is not, this is the idle wait. */
        if (wrote == 0) vTaskDelay(1);

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (now - logged >= 1000) {
            logged = now;
            player_playout_status_t st;
            player_playout_status(&st);
            if (st.open)
                ESP_LOGI(TAG, "output %u Hz: %u frames, %u silent, %u underruns, %u refused, "
                              "%u primes",
                         (unsigned)st.rate, (unsigned)st.frames, (unsigned)st.silent,
                         (unsigned)st.underruns, (unsigned)st.refused, (unsigned)st.primes);
        }
    }
}

void player_playout_start_task(void)
{
    /* Above the stream's and the light's: this task's deadline is the DAC's, and it
     * spends almost all of its time blocked on the I2S write, so it does not starve them. */
    if (xTaskCreatePinnedToCore(playout_task, "playout", 6144, NULL, 5, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "the playout task did not start");
}

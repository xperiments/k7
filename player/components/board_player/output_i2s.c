/*
 * The K7Player's line output: I2S in standard Philips mode to an external DAC.
 *
 * The parts this targets (a PCM5102A breakout, a MAX98357A) have no control bus and no
 * hardware volume: three wires out of the ESP32 and an amplifier after them. That is why the
 * pin choice is the whole of the configuration, and why there is no volume call anywhere here
 * (k7player.md R11).
 *
 * The pins are the board's, named once:
 *
 *   GPIO 26  BCK     the bit clock
 *   GPIO 25  LRCK    the word clock
 *   GPIO 22  DIN     the data
 *
 * A PCM5102A needs no MCLK: its own PLL derives it, which is what makes a three-wire part
 * usable on a chip with no spare pins. On the breakout, SCK goes to GND (PLL mode) and FMT to
 * GND (I2S, not left-justified); XSMT unconnected is un-muted. Its LOUT/ROUT are line level and
 * full scale - the amplifier after them owns the volume (R11).
 */
#include "player_output.h"

#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define OUT_BCK  26
#define OUT_LRCK 25
#define OUT_DIN  22

static const char *TAG = "i2s";
static i2s_chan_handle_t s_tx;

/* The line's level, fixed by what this board feeds. The PCM5102A is a full-scale line output
 * with no control bus (R11), and these are active speakers fed straight from it, so a quarter
 * of full scale (-12 dB) is the board's level and not a control: no surface can change it.
 * A different load changes this one number. The scratch is one MP3 frame's worth of stereo. */
#define OUT_GAIN_Q15   3277 /* 0.1 = -20 dB: the bench speakers were still loud at a quarter */
#define OUT_MAX_FRAMES 1152
static int16_t s_scaled[OUT_MAX_FRAMES * 2];

void player_output_close(void); /* called from the open path's failure unwinding */
static uint32_t s_hz;

bool player_output_open(uint32_t hz)
{
    if (s_tx) player_output_close();
    if (!hz) return false;

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    /* The default is 6 x 240 frames - about 33 ms at 44.1 kHz - and that is a scheduling
     * budget, not an audio one: when the Wi-Fi or lwIP preempts the playout task for longer
     * than that, the DMA runs dry and auto_clear inserts silence in the middle of the music.
     * 8 x 512 is about 93 ms, which covers any preemption this device can produce, and costs
     * 16 KB of DMA-capable internal RAM. */
    chan.dma_desc_num = 8;
    chan.dma_frame_num = 512;
    chan.auto_clear = true; /* if it ever does run dry: silence, not the last buffer again */
    if (i2s_new_channel(&chan, &s_tx, NULL) != ESP_OK) {
        s_tx = NULL;
        return false;
    }

    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(hz),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = OUT_BCK,
            .ws = OUT_LRCK,
            .dout = OUT_DIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {false, false, false},
        },
    };

    if (i2s_channel_init_std_mode(s_tx, &cfg) != ESP_OK || i2s_channel_enable(s_tx) != ESP_OK) {
        i2s_del_channel(s_tx);
        s_tx = NULL;
        ESP_LOGE(TAG, "the I2S line did not start at %u Hz", (unsigned)hz);
        return false;
    }

    s_hz = hz;
    ESP_LOGI(TAG, "line output: %u Hz at %d dB (Q15 %d), BCK %d, LRCK %d, DIN %d; heap %u B free, "
                  "largest %u B",
             (unsigned)hz, -20, (int)OUT_GAIN_Q15, OUT_BCK, OUT_LRCK, OUT_DIN,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    return true;
}

void player_output_write(const int16_t *stereo, size_t frames)
{
    if (!s_tx || !stereo || frames == 0) return;
    size_t n = frames > OUT_MAX_FRAMES ? OUT_MAX_FRAMES : frames;
    /* The gain is applied here, where the line's level is a board fact: scaling in the playout
     * would make it a stream decision and the host tests would assert a level the board does
     * not have. */
    for (size_t i = 0; i < n * 2; i++)
        s_scaled[i] = (int16_t)(((int32_t)stereo[i] * OUT_GAIN_Q15) >> 15);

    /* Once, with numbers: the level is a board fact and the log should show it acting on the
     * samples that reach the DAC, not merely the constant it was compiled from. */
    static bool logged;
    if (!logged) {
        int32_t in_peak = 0, out_peak = 0;
        for (size_t i = 0; i < n * 2; i++) {
            int32_t a = stereo[i] < 0 ? -stereo[i] : stereo[i];
            int32_t b = s_scaled[i] < 0 ? -s_scaled[i] : s_scaled[i];
            if (a > in_peak) in_peak = a;
            if (b > out_peak) out_peak = b;
        }
        if (in_peak > 0) {
            logged = true;
            ESP_LOGI(TAG, "line gain: peak %d in, %d out (Q15 %d)", (int)in_peak, (int)out_peak,
                     (int)OUT_GAIN_Q15);
        }
    }
    size_t bytes = n * 2 * sizeof(int16_t);
    size_t written = 0;
    /* The timeout is the pacing, not an error path: the DMA takes the frame over the next
     * few milliseconds of audio. */
    i2s_channel_write(s_tx, s_scaled, bytes, &written, 1000);
}

void player_output_close(void)
{
    if (!s_tx) return;
    i2s_channel_disable(s_tx);
    i2s_del_channel(s_tx);
    s_tx = NULL;
    s_hz = 0;
}

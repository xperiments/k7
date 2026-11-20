/*
 * T07: the K7's tone, at the player's output (k7player.md R33, espnow-stream.md E12).
 *
 * The packets are the emitter's own bytes; the PCM is the playout's interleaved stereo. The
 * test pins the three things the module owes: neutral is a bypass (an old K7's tone is not
 * doubled), the gain and the low shelf reach the samples after the glide, and a malformed
 * packet changes nothing. A rate change rebuilds the filters and keeps the targets.
 */
#include "player_tone.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "espnow_stream.h"
#include "test_util.h"

#define RATE 44100
#define FRAMES 512

static void send_tone(float gain_db, float low_db, float mid_db, float high_db)
{
    es_tone_t t = {
        .gain_cdb = (int16_t)lroundf(gain_db * 100.0f),
        .low_cdb = (int16_t)lroundf(low_db * 100.0f),
        .mid_cdb = (int16_t)lroundf(mid_db * 100.0f),
        .high_cdb = (int16_t)lroundf(high_db * 100.0f),
    };
    uint8_t pkt[ES_TONE_SIZE];
    CHECK_EQ(es_tone_encode(&t, pkt, sizeof pkt), ES_TONE_SIZE, "the tone packet builds");
    player_tone_packet(pkt, sizeof pkt);
}

/* The tone's slew is 2 dB per call: 32 calls is more than a full-range move. */
static void settle(void)
{
    int16_t zeros[128 * 2] = {0};
    for (int i = 0; i < 32; i++) player_tone_process(zeros, 128);
}

static void fill_dc(int16_t *buf, size_t frames, int16_t value)
{
    for (size_t i = 0; i < frames; i++) {
        buf[2 * i] = value;
        buf[2 * i + 1] = value;
    }
}

static int16_t last(const int16_t *buf, size_t frames) { return buf[2 * (frames - 1)]; }

/* No packet: the samples are the decoder's, untouched. */
static void test_neutral_is_a_bypass(void)
{
    send_tone(0, 0, 0, 0);
    settle();

    int16_t buf[FRAMES * 2];
    fill_dc(buf, FRAMES, 1000);
    player_tone_process(buf, FRAMES);
    CHECK_EQ(last(buf, FRAMES), 1000, "a neutral tone does not touch the samples");
}

/* GAIN +6 reaches the samples after the glide, on both channels. */
static void test_gain(void)
{
    send_tone(6, 0, 0, 0);
    settle();

    int16_t buf[FRAMES * 2];
    fill_dc(buf, FRAMES, 1000);
    player_tone_process(buf, FRAMES);
    CHECK(fabs((double)last(buf, FRAMES) - 1995.0) <= 6.0, "gain +6: 1000 -> %d",
          (int)last(buf, FRAMES));
    CHECK_EQ(buf[2 * (FRAMES - 1) + 1], buf[2 * (FRAMES - 1)],
             "both channels take the same gain");
}

/* The low shelf is the DJ cut/boost: DC is below its 200 Hz corner, so it moves the whole
 * signal by the shelf's asymptotic gain. */
static void test_low_shelf(void)
{
    send_tone(0, 6, 0, 0);
    settle();

    int16_t buf[FRAMES * 2];
    fill_dc(buf, FRAMES, 1000);
    player_tone_process(buf, FRAMES);
    CHECK(fabs((double)last(buf, FRAMES) - 1995.0) <= 6.0, "low +6 at DC: 1000 -> %d",
          (int)last(buf, FRAMES));

    send_tone(0, -26, 0, 0);
    settle();
    fill_dc(buf, FRAMES, 1000);
    player_tone_process(buf, FRAMES);
    CHECK(fabs((double)last(buf, FRAMES) - 50.0) <= 4.0, "low -26 at DC: 1000 -> %d",
          (int)last(buf, FRAMES));
}

/* A packet that is not the K7's tone is ignored: the previous targets stay. */
static void test_malformed(void)
{
    send_tone(0, 0, 0, 0);
    settle();

    uint8_t bad[ES_TONE_SIZE];
    memset(bad, 0, sizeof bad);
    bad[0] = 2; /* a version from the future */
    bad[1] = ES_TYPE_TONE;
    player_tone_packet(bad, sizeof bad);

    uint8_t short_pkt[ES_TONE_SIZE - 1];
    memset(short_pkt, 0, sizeof short_pkt);
    short_pkt[0] = ES_VERSION;
    short_pkt[1] = ES_TYPE_TONE;
    player_tone_packet(short_pkt, sizeof short_pkt);

    int16_t buf[FRAMES * 2];
    fill_dc(buf, FRAMES, 1000);
    player_tone_process(buf, FRAMES);
    CHECK_EQ(last(buf, FRAMES), 1000, "neither malformed packet moved the tone");
}

/* The output's rate changes: the filters are rebuilt for it and the targets survive. */
static void test_rate_change(void)
{
    send_tone(6, 0, 0, 0);
    player_tone_set_rate(32000);
    settle();

    int16_t buf[FRAMES * 2];
    fill_dc(buf, FRAMES, 1000);
    player_tone_process(buf, FRAMES);
    CHECK(fabs((double)last(buf, FRAMES) - 1995.0) <= 6.0,
          "the gain survives a rate change: 1000 -> %d", (int)last(buf, FRAMES));
}

int main(void)
{
    player_tone_init();
    player_tone_set_rate(RATE);

    test_neutral_is_a_bypass();
    test_gain();
    test_low_shelf();
    test_malformed();
    test_rate_change();

    TEST_END();
}

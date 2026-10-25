/*
 * T17: the broadcast's three-band EQ and trim, without a board.
 *
 * The module is pure arithmetic: a low shelf, a mid bell, a high shelf and a gain,
 * all smoothed per chunk. The tests pin the unity case, the trim, the three bands'
 * gains (DC for the shelves, 1 kHz for the bell) and the clamp. The ranges are the
 * DJ ones the screen's knobs use: bands -26..+6 dB, trim -12..+12 dB.
 */
#include "test_util.h"

#include <math.h>

#include "tone_eq.h"

#define RATE 44100
#define BLOCK 2048

/* The slew is per chunk; processing zeros for long enough lands every target exactly
 * and clears the biquad state. */
static void settle(tone_eq_t *eq)
{
    int16_t z[128 * 2] = {0};
    for (int i = 0; i < 32; i++) tone_eq_process(eq, z, 128);
}

static void fill_dc(int16_t *buf, size_t frames, int16_t v)
{
    for (size_t i = 0; i < frames; i++) {
        buf[2 * i] = v;
        buf[2 * i + 1] = v;
    }
}

static double rms(const int16_t *buf, size_t frames, size_t from)
{
    double sum = 0;
    for (size_t i = from; i < frames; i++) {
        double l = buf[2 * i], r = buf[2 * i + 1];
        sum += l * l + r * r;
    }
    return sqrt(sum / (double)(2 * (frames - from)));
}

static void test_unity(void)
{
    tone_eq_t eq;
    tone_eq_init(&eq, RATE);
    tone_eq_set_gain(&eq, 0);
    for (int b = 0; b < TONE_EQ_BANDS; b++) tone_eq_set_band(&eq, b, 0);
    settle(&eq);

    int16_t in[BLOCK * 2], out[BLOCK * 2];
    for (size_t i = 0; i < BLOCK * 2; i++) in[i] = (int16_t)((i * 37u) % 4001u) - 2000;
    memcpy(out, in, sizeof out);
    tone_eq_process(&eq, out, BLOCK);

    int bad = 0;
    for (size_t i = 0; i < BLOCK * 2; i++)
        if (out[i] != in[i]) bad++;
    CHECK_EQ(bad, 0, "unity: every sample comes back unchanged");
}

static void test_trim(void)
{
    tone_eq_t eq;
    int16_t buf[256 * 2];

    tone_eq_init(&eq, RATE);
    tone_eq_set_gain(&eq, 6);
    settle(&eq);
    fill_dc(buf, 256, 1000);
    tone_eq_process(&eq, buf, 256);
    CHECK(fabs((double)buf[2 * 255] - 1995.0) <= 4.0, "gain +6: 1000 -> %.0f", (double)buf[2 * 255]);

    tone_eq_set_gain(&eq, -6);
    settle(&eq);
    fill_dc(buf, 256, 1000);
    tone_eq_process(&eq, buf, 256);
    CHECK(fabs((double)buf[2 * 255] - 501.0) <= 4.0, "gain -6: 1000 -> %.0f", (double)buf[2 * 255]);
}

static void test_low_shelf(void)
{
    tone_eq_t eq;
    int16_t buf[512 * 2];

    tone_eq_init(&eq, RATE);
    tone_eq_set_gain(&eq, 0);
    tone_eq_set_band(&eq, TONE_EQ_LOW, 6);
    settle(&eq);
    fill_dc(buf, 512, 1000);
    tone_eq_process(&eq, buf, 512);
    /* DC is below the 200 Hz corner: the shelf's asymptotic gain is 10^(+6/20). */
    CHECK(fabs((double)buf[2 * 511] - 1995.0) <= 6.0, "low +6 at DC: 1000 -> %.0f",
          (double)buf[2 * 511]);

    tone_eq_set_band(&eq, TONE_EQ_LOW, -26);
    settle(&eq);
    fill_dc(buf, 512, 1000);
    tone_eq_process(&eq, buf, 512);
    CHECK(fabs((double)buf[2 * 511] - 50.0) <= 4.0, "low -26 at DC: 1000 -> %.0f",
          (double)buf[2 * 511]);
}

static void test_high_shelf(void)
{
    tone_eq_t eq;
    int16_t buf[BLOCK * 2];

    tone_eq_init(&eq, RATE);
    tone_eq_set_gain(&eq, 0);
    tone_eq_set_band(&eq, TONE_EQ_HIGH, 6);
    settle(&eq);

    /* DC is below the 4 kHz corner: the high shelf leaves it alone. */
    fill_dc(buf, 512, 1000);
    tone_eq_process(&eq, buf, 512);
    CHECK(fabs((double)buf[2 * 511] - 1000.0) <= 4.0, "high +6 at DC: 1000 -> %.0f",
          (double)buf[2 * 511]);

    /* 10 kHz is above the corner: about +6 dB, so about twice the RMS. */
    for (size_t i = 0; i < BLOCK; i++) {
        int16_t v = (int16_t)lround(8000.0 * sin(2.0 * M_PI * 10000.0 * (double)i / RATE));
        buf[2 * i] = v;
        buf[2 * i + 1] = v;
    }
    int16_t in[BLOCK * 2];
    memcpy(in, buf, sizeof in);
    tone_eq_process(&eq, buf, BLOCK);
    double ratio = rms(buf, BLOCK, 1024) / rms(in, BLOCK, 1024);
    CHECK(fabs(ratio - 2.0) <= 0.1, "high +6 at 10 kHz: ratio %.3f", ratio);
}

static void test_mid_bell(void)
{
    tone_eq_t eq;
    int16_t buf[BLOCK * 2];

    tone_eq_init(&eq, RATE);
    tone_eq_set_gain(&eq, 0);
    tone_eq_set_band(&eq, TONE_EQ_MID, 6);
    settle(&eq);

    for (size_t i = 0; i < BLOCK; i++) {
        int16_t v = (int16_t)lround(8000.0 * sin(2.0 * M_PI * 1000.0 * (double)i / RATE));
        buf[2 * i] = v;
        buf[2 * i + 1] = v;
    }
    int16_t in[BLOCK * 2];
    memcpy(in, buf, sizeof in);
    tone_eq_process(&eq, buf, BLOCK);
    double ratio = rms(buf, BLOCK, 1024) / rms(in, BLOCK, 1024);
    CHECK(fabs(ratio - 2.0) <= 0.1, "mid +6 at 1 kHz: ratio %.3f", ratio);
}

static void test_clamp(void)
{
    tone_eq_t eq;
    int16_t buf[512 * 2];

    tone_eq_init(&eq, RATE);
    tone_eq_set_gain(&eq, 12);
    tone_eq_set_band(&eq, TONE_EQ_LOW, 6);
    settle(&eq);
    fill_dc(buf, 512, 30000);
    tone_eq_process(&eq, buf, 512);
    /* 30000 * 4 * 2 would wrap without the clamp. */
    CHECK(buf[2 * 511] > 32000, "clamp: a hot input stays positive (%d)", (int)buf[2 * 511]);
    CHECK(buf[2 * 511] <= 32767, "clamp: and inside int16 (%d)", (int)buf[2 * 511]);
}

int main(void)
{
    test_unity();
    test_trim();
    test_low_shelf();
    test_high_shelf();
    test_mid_bell();
    test_clamp();
    TEST_END();
}

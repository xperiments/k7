/*
 * The Card's ink, host tier.
 *
 * The colour a Track's cover contributes, the hue a colour reads back as, the complement a
 * drawn label is inked in, and the histogram over a decoded RGB565 cover. All of it is
 * arithmetic with no panel present, so it is pinned here the way the layout and the button
 * rule are.
 *
 * What is pinned:
 *   - the header's own examples - hue 0 is (188, 72, 72), hue 120 is (72, 188, 72) - and the
 *     six primaries they sit on, all at the module's fixed saturation and value;
 *   - a negative hue wraps, so from_hue(-60) is from_hue(300);
 *   - hue() reads every hue the module can emit back exactly, and a grey has none;
 *   - the complement is 180 degrees away at the colour's own saturation and value, and twice
 *     is the colour again;
 *   - the dominant hue is the fullest bucket, not the first one seen, and a tie goes to the
 *     lower hue; a bucket comes back as a multiple of 15, so a cover's ink is stable;
 *   - neither a grey nor a black cover contributes a pixel, so the table ink stays;
 *   - `step` samples every step-th pixel from 0, and `stride` is the row's, not w*2.
 * No display, no serial dump.
 */
#include "test_util.h"

#include <string.h>

#include "cassette_ink.h"

#define NO_HUE (-1)

static cassette_rgb_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    cassette_rgb_t c = { r, g, b };
    return c;
}

static void check_rgb(cassette_rgb_t got, cassette_rgb_t want, const char *what)
{
    CHECK(got.r == want.r && got.g == want.g && got.b == want.b,
          "%s: got (%d,%d,%d), want (%d,%d,%d)", what, (int)got.r, (int)got.g, (int)got.b,
          (int)want.r, (int)want.g, (int)want.b);
}

/* ----------------------------------------------------------- the emitted ink */

static void test_from_hue(void)
{
    /* The header names the first and the third; the rest are the same six sectors. */
    check_rgb(cassette_ink_from_hue(0), rgb(188, 72, 72), "hue 0 is red");
    check_rgb(cassette_ink_from_hue(60), rgb(188, 188, 72), "hue 60 is yellow");
    check_rgb(cassette_ink_from_hue(120), rgb(72, 188, 72), "hue 120 is green");
    check_rgb(cassette_ink_from_hue(180), rgb(72, 188, 188), "hue 180 is cyan");
    check_rgb(cassette_ink_from_hue(240), rgb(72, 72, 188), "hue 240 is blue");
    check_rgb(cassette_ink_from_hue(300), rgb(188, 72, 188), "hue 300 is magenta");
}

static void test_from_hue_between_the_sectors(void)
{
    /* Mid-sector the varying channel is half the chroma above the floor: hue 30 is
     * (188, 130, 72), not a lerp of the two neighbours' bytes. */
    check_rgb(cassette_ink_from_hue(30), rgb(188, 130, 72), "hue 30 is orange");
    check_rgb(cassette_ink_from_hue(210), rgb(72, 130, 188), "hue 210 is the far side");
}

static void test_from_hue_wraps(void)
{
    check_rgb(cassette_ink_from_hue(-60), cassette_ink_from_hue(300), "hue -60 is hue 300");
    check_rgb(cassette_ink_from_hue(360), cassette_ink_from_hue(0), "hue 360 is hue 0");
    check_rgb(cassette_ink_from_hue(-360), cassette_ink_from_hue(0), "hue -360 is hue 0");
    check_rgb(cassette_ink_from_hue(750), cassette_ink_from_hue(30), "hue 750 is hue 30");
    check_rgb(cassette_ink_from_hue(-1), cassette_ink_from_hue(359), "hue -1 is hue 359");
}

/* ------------------------------------------------------------- the reading */

static void test_hue(void)
{
    CHECK_EQ(cassette_ink_hue(rgb(255, 0, 0)), 0, "red reads as 0");
    CHECK_EQ(cassette_ink_hue(rgb(255, 255, 0)), 60, "yellow reads as 60");
    CHECK_EQ(cassette_ink_hue(rgb(0, 255, 0)), 120, "green reads as 120");
    CHECK_EQ(cassette_ink_hue(rgb(0, 255, 255)), 180, "cyan reads as 180");
    CHECK_EQ(cassette_ink_hue(rgb(0, 0, 255)), 240, "blue reads as 240");
    CHECK_EQ(cassette_ink_hue(rgb(255, 0, 255)), 300, "magenta reads as 300");

    CHECK_EQ(cassette_ink_hue(rgb(128, 128, 128)), NO_HUE, "grey has no hue");
    CHECK_EQ(cassette_ink_hue(rgb(0, 0, 0)), NO_HUE, "black has no hue");
    CHECK_EQ(cassette_ink_hue(rgb(255, 255, 255)), NO_HUE, "white has no hue");
}

/* Every hue the module can emit has to read back as itself, which is what makes a Track's
 * ink name the Track's hue and not a neighbour's. */
static void test_hue_round_trip(void)
{
    for (int hue = 0; hue < 360; hue++) {
        char ctx[64];
        snprintf(ctx, sizeof ctx, "from_hue(%d) reads back", hue);
        CHECK_EQ(cassette_ink_hue(cassette_ink_from_hue(hue)), hue, ctx);
    }
}

/* ------------------------------------------------------------- the label ink */

static void test_complement(void)
{
    check_rgb(cassette_ink_complement(rgb(255, 0, 0)), rgb(0, 255, 255),
              "red's complement is cyan");
    check_rgb(cassette_ink_complement(rgb(188, 72, 72)), rgb(72, 188, 188),
              "the Card ink's complement is the same ink at hue 180");
    check_rgb(cassette_ink_complement(rgb(128, 128, 128)), rgb(128, 128, 128),
              "a grey has no complement and stays itself");

    /* At the colour's own saturation and value, not the module's: max 200 and chroma 150
     * survive the turn. */
    check_rgb(cassette_ink_complement(rgb(200, 100, 50)), rgb(50, 150, 200),
              "the complement keeps its own saturation and value");
}

static void test_complement_round_trip(void)
{
    for (int hue = 0; hue < 360; hue++) {
        const cassette_rgb_t c = cassette_ink_from_hue(hue);
        char ctx[80];

        snprintf(ctx, sizeof ctx, "from_hue(%d) turned 180", hue);
        CHECK_EQ(cassette_ink_hue(cassette_ink_complement(c)), (hue + 180) % 360, ctx);

        snprintf(ctx, sizeof ctx, "from_hue(%d) turned 180 twice", hue);
        const cassette_rgb_t twice = cassette_ink_complement(cassette_ink_complement(c));
        CHECK_EQ(cassette_ink_hue(twice), hue, ctx);
        check_rgb(twice, c, ctx);
    }
}

/* ---------------------------------------------------------- the dominant hue */

/* The largest buffer any test needs. Rows are padded, because a descriptor's stride usually
 * is, and the padding is minted white so a wrong stride reads a different colour. */
#define BUF_W 8
#define BUF_H 8
#define BUF_PAD 4
#define BUF_STRIDE (BUF_W * 2 + BUF_PAD)

typedef struct {
    uint8_t px[BUF_STRIDE * BUF_H];
    int     w, h, stride;
} buf_t;

/* The test's own encoder, so the production decoder is checked against an independent one
 * rather than its own inverse. */
static uint16_t to565(cassette_rgb_t c)
{
    unsigned r = ((unsigned)c.r * 31u + 127u) / 255u;
    unsigned g = ((unsigned)c.g * 63u + 127u) / 255u;
    unsigned b = ((unsigned)c.b * 31u + 127u) / 255u;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void buf_init(buf_t *b, int w, int h, int stride)
{
    memset(b->px, 0xFF, sizeof b->px);
    b->w = w;
    b->h = h;
    b->stride = stride;
}

static void buf_put(buf_t *b, int x, int y, cassette_rgb_t c)
{
    uint16_t px = to565(c);
    int off = y * b->stride + x * 2;
    b->px[off] = (uint8_t)(px >> 8);
    b->px[off + 1] = (uint8_t)(px & 0xFF);
}

static void buf_fill(buf_t *b, cassette_rgb_t c)
{
    for (int y = 0; y < b->h; y++)
        for (int x = 0; x < b->w; x++) buf_put(b, x, y, c);
}

static void test_dominant_mostly_red(void)
{
    buf_t b;
    buf_init(&b, BUF_W, BUF_H, BUF_STRIDE);
    buf_fill(&b, rgb(255, 0, 0));
    for (int x = 0; x < 3; x++) buf_put(&b, x, 0, rgb(0, 0, 255));

    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), 0,
             "a cover that is mostly red is red");
}

static void test_dominant_fullest_wins(void)
{
    buf_t b;

    /* Green is seen first, in the top-left corner, but red owns the rest of the frame. */
    buf_init(&b, BUF_W, BUF_H, BUF_STRIDE);
    buf_fill(&b, rgb(255, 0, 0));
    for (int x = 0; x < 2; x++) buf_put(&b, x, 0, rgb(0, 255, 0));

    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), 0,
             "a fuller bucket beats the first one seen");

    /* Four of each: the tie is the lower hue, so the scan order cannot decide it. */
    buf_init(&b, 4, 2, BUF_STRIDE);
    buf_fill(&b, rgb(255, 0, 0));
    for (int x = 0; x < 4; x++) buf_put(&b, x, 1, rgb(0, 0, 255));

    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), 0,
             "a tie goes to the lower hue");
}

static void test_dominant_bucket_centres(void)
{
    buf_t b;

    /* (206, 65, 0) is hue 19, which is bucket 1: the answer is the bucket's first degree,
     * 15, and never 19. */
    buf_init(&b, BUF_W, BUF_H, BUF_STRIDE);
    buf_fill(&b, rgb(206, 65, 0));
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), 15,
             "hue 19 comes back as 15");

    /* Magenta is already a bucket centre and comes back unchanged. */
    buf_fill(&b, rgb(255, 0, 255));
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), 300,
             "hue 300 comes back as 300");
}

static void test_dominant_neutral(void)
{
    buf_t b;
    buf_init(&b, BUF_W, BUF_H, BUF_STRIDE);

    buf_fill(&b, rgb(128, 128, 128));
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), NO_HUE,
             "a grey cover keeps the table ink");

    buf_fill(&b, rgb(0, 0, 0));
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), NO_HUE,
             "a black cover keeps the table ink");

    buf_fill(&b, rgb(255, 255, 255));
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), NO_HUE,
             "a white cover keeps the table ink");
}

static void test_dominant_value_floor(void)
{
    buf_t b;
    buf_init(&b, BUF_W, BUF_H, BUF_STRIDE);

    /* Blue at 24 of 255 is under the 10 % floor; at 33 it is over it. */
    buf_fill(&b, rgb(0, 0, 24));
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), NO_HUE,
             "a cover too dark to name keeps the table ink");

    buf_fill(&b, rgb(0, 0, 33));
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), 240,
             "a blue just over the value floor counts");
}

static void test_dominant_step(void)
{
    buf_t b;
    buf_init(&b, BUF_W, BUF_H, BUF_STRIDE);
    buf_fill(&b, rgb(128, 128, 128));
    for (int y = 0; y < BUF_H; y++)
        for (int x = 1; x < BUF_W; x += 2) buf_put(&b, x, y, rgb(0, 0, 255));

    /* The sampled columns are 0, 2, 4, 6: every blue pixel is skipped, so the cover reads
     * as the grey it mostly is. */
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 2), NO_HUE,
             "step 2 samples the even columns and misses the odd blue");

    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), 240,
             "step 1 sees the same pixels");
}

static void test_dominant_stride(void)
{
    buf_t b;

    /* The pixels are blue; the bytes past w*2 on every row are red and must not be read as
     * another row. A decoder that used w*2 for the stride would see a red/blue tie and
     * answer red. */
    buf_init(&b, 4, 4, BUF_STRIDE);
    for (int y = 0; y < b.h; y++) {
        for (int x = 0; x < b.w; x++) buf_put(&b, x, y, rgb(0, 0, 255));
        for (int x = b.w; x < BUF_STRIDE / 2; x++) buf_put(&b, x, y, rgb(255, 0, 0));
    }

    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 1), 240,
             "a padded row is stepped over, not read as pixels");
}

static void test_dominant_guards(void)
{
    buf_t b;
    buf_init(&b, BUF_W, BUF_H, BUF_STRIDE);
    buf_fill(&b, rgb(255, 0, 0));

    CHECK_EQ(cassette_ink_dominant_hue(NULL, b.w, b.h, b.stride, 1), NO_HUE, "no buffer");
    CHECK_EQ(cassette_ink_dominant_hue(b.px, 0, b.h, b.stride, 1), NO_HUE, "no columns");
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, 0, b.stride, 1), NO_HUE, "no rows");
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, 0, 1), NO_HUE, "no stride");
    CHECK_EQ(cassette_ink_dominant_hue(b.px, b.w, b.h, b.stride, 0), NO_HUE, "no step");
}

int main(void)
{
    test_from_hue();
    test_from_hue_between_the_sectors();
    test_from_hue_wraps();
    test_hue();
    test_hue_round_trip();
    test_complement();
    test_complement_round_trip();
    test_dominant_mostly_red();
    test_dominant_fullest_wins();
    test_dominant_bucket_centres();
    test_dominant_neutral();
    test_dominant_value_floor();
    test_dominant_step();
    test_dominant_stride();
    test_dominant_guards();

    TEST_END();
}

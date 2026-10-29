/*
 * The Card's ink, as arithmetic; see cassette_ink.h.
 *
 * One rule lives in this file: the Track owns the hue, the UI owns the saturation and the
 * value. So a Cover contributes a hue and nothing else - a photograph's average is mud and
 * a near-white album would dissolve the Card into the white page - and every ink emitted
 * here is 62 % saturated at 74 % value because those are the page's numbers, not the
 * Track's. A drawn label is inked in the complement at the shell's own saturation and
 * value, so it contrasts with the Card instead of blending into it.
 *
 * The dominant hue is a histogram of 24 buckets rather than an average, and that is the
 * whole of its stability: a bucket's centre is a multiple of 15 degrees, so a cover whose
 * pixels drift by a degree or two contributes to the same bucket and the same ink comes
 * back on every Track change. An average would move with every pixel and shimmer.
 *
 * Everything is integer arithmetic - no float, no libm - so the triples pinned in the
 * header are exact on the host and on the S3 alike, and the histogram costs a pixel a
 * handful of adds on the one pass that has the whole Cover in hand.
 */
#include "cassette_ink.h"

#include <stddef.h>

#define DEGREES    360
#define SECTOR_DEG 60
#define SECTORS    6

/* HSV's six sectors, one row each: which channel carries V, which carries the mix and which
 * carries the floor. This is the textbook table, kept as data so the conversion has no
 * switch and no channel can be assigned twice. */
static const int k_channel[SECTORS][3] = {
    { 2, 1, 0 }, /*   0..59    red -> yellow */
    { 1, 2, 0 }, /*  60..119   yellow -> green */
    { 0, 2, 1 }, /* 120..179   green -> cyan */
    { 0, 1, 2 }, /* 180..239   cyan -> blue */
    { 1, 0, 2 }, /* 240..299   blue -> magenta */
    { 2, 0, 1 }, /* 300..359   magenta -> red */
};

static int wrap_hue(int hue)
{
    hue %= DEGREES;
    return hue < 0 ? hue + DEGREES : hue;
}

/* Divide rounding half away from zero, so the two signs of a chroma's term round alike and
 * a hue cannot drift by a degree on which channel happened to be the maximum. `den` is
 * positive. */
static int round_div(int num, int den)
{
    if (num >= 0) return (num + den / 2) / den;
    return -((-num + den / 2) / den);
}

/* HSV to RGB at an arbitrary chroma and floor. Keeping both as byte counts rather than as
 * percentages is what lets the complement land back on a colour's own saturation and value
 * instead of on the page's. */
static cassette_rgb_t hue_rgb(int hue_deg, int chroma, int base)
{
    const int hue = wrap_hue(hue_deg);
    const int sector = hue / SECTOR_DEG;
    const int frac = hue % SECTOR_DEG;

    /* The mix is the chroma's share of the sector: rising on the even sectors, falling on
     * the odd ones. */
    const int x = (sector & 1) ? round_div(chroma * (SECTOR_DEG - frac), SECTOR_DEG)
                               : round_div(chroma * frac, SECTOR_DEG);

    const int channel[3] = { base, base + x, base + chroma };
    const int *order = k_channel[sector];

    cassette_rgb_t c = {
        (uint8_t)channel[order[0]],
        (uint8_t)channel[order[1]],
        (uint8_t)channel[order[2]],
    };
    return c;
}

/* A colour's hue and its HSV shape in one pass: the chroma and the floor are what the
 * complement needs, and they are free once the maximum and the minimum are known. A grey
 * reports chroma 0 and hue -1 - it has no hue to turn. */
static int ink_hue(cassette_rgb_t c, int *out_chroma, int *out_base)
{
    const int r = c.r, g = c.g, b = c.b;
    int max = r > g ? r : g;
    int min = r < g ? r : g;
    if (b > max) max = b;
    if (b < min) min = b;

    const int chroma = max - min;
    if (out_chroma) *out_chroma = chroma;
    if (out_base) *out_base = min;
    if (chroma == 0) return -1;

    int hue;
    if (max == r) hue = round_div(SECTOR_DEG * (g - b), chroma);
    else if (max == g) hue = 2 * SECTOR_DEG + round_div(SECTOR_DEG * (b - r), chroma);
    else hue = 4 * SECTOR_DEG + round_div(SECTOR_DEG * (r - g), chroma);
    return wrap_hue(hue);
}

cassette_rgb_t cassette_ink_from_hue(int hue_deg)
{
    /* Both percentages truncate - 255 * 74 / 100 is 188 and 188 * 62 / 100 is 116 - which
     * is where the header's baked example, hue 0 at (188, 72, 72), puts them. */
    const int value = 255 * CASSETTE_INK_VAL / 100;
    const int chroma = value * CASSETTE_INK_SAT / 100;
    return hue_rgb(hue_deg, chroma, value - chroma);
}

int cassette_ink_hue(cassette_rgb_t c)
{
    return ink_hue(c, NULL, NULL);
}

cassette_rgb_t cassette_ink_complement(cassette_rgb_t c)
{
    int chroma, base;
    const int hue = ink_hue(c, &chroma, &base);

    /* A grey has no opposite, and inventing one would tint what the Cover did not. */
    if (hue < 0) return c;
    return hue_rgb(hue + DEGREES / 2, chroma, base);
}

/* ---------------------------------------------------------- the cover's reading */

/* RGB565's 5 and 6 bit channels expand by replication, so full scale stays full: 31 becomes
 * 255 and 63 becomes 255, not 248 and 252. */
static int expand5(unsigned v)
{
    return (int)((v << 3) | (v >> 2));
}

static int expand6(unsigned v)
{
    return (int)((v << 2) | (v >> 4));
}

int cassette_ink_dominant_hue(const uint8_t *rgb565, int w, int h, int stride, int step)
{
    /* A descriptor that does not fit, or a sampling grid with no step, is no reading at all;
     * the caller keeps the table ink, which is also the answer for a colourless Cover. */
    if (!rgb565 || w <= 0 || h <= 0 || step <= 0 || stride < w * 2) return -1;

    int counts[CASSETTE_INK_BUCKETS] = { 0 };

    for (int y = 0; y < h; y += step) {
        const uint8_t *row = rgb565 + y * stride;
        for (int x = 0; x < w; x += step) {
            const uint8_t *p = row + x * 2;
            const unsigned px = ((unsigned)p[0] << 8) | p[1];

            const cassette_rgb_t c = {
                (uint8_t)expand5((px >> 11) & 0x1F),
                (uint8_t)expand6((px >> 5) & 0x3F),
                (uint8_t)expand5(px & 0x1F),
            };

            int chroma, base;
            const int hue = ink_hue(c, &chroma, &base);
            if (hue < 0) continue;

            /* Too grey to name or too dark to see: neither can tell the Card anything. */
            const int max = base + chroma;
            if (chroma * 100 < max * 12) continue;
            if (max * 100 < 255 * 10) continue;

            counts[hue * CASSETTE_INK_BUCKETS / DEGREES]++;
        }
    }

    /* Strictly greater, so the fullest bucket wins and a tie keeps the lower hue. */
    int best = 0;
    for (int bucket = 1; bucket < CASSETTE_INK_BUCKETS; bucket++) {
        if (counts[bucket] > counts[best]) best = bucket;
    }
    if (counts[best] == 0) return -1;
    return best * DEGREES / CASSETTE_INK_BUCKETS;
}

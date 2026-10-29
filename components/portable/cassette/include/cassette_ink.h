#pragma once
/*
 * cassette_ink - the Card's colour, with no GUI toolkit.
 *
 * The white page's Card is one tinted rectangle (ADR 0039), and its ink is either the
 * Album's own or one of the table's five. That choice is arithmetic: a hue histogram over
 * the decoded cover, a complement for the drawn label, and one HSV-to-RGB conversion. All
 * of it is here, so the host tier can pin it without a panel - the same split
 * cassette_layout and cassette_button_rule already make.
 *
 * The one rule the numbers carry: the **hue** is the Track's, the **saturation and value**
 * are the UI's. A photograph's average is mud and a near-white album would dissolve the
 * Card into the white page, so only the hue is taken from the Cover; the vividness is
 * fixed here. `cassette_ink_from_hue()` is therefore the only way a Track's colour reaches
 * the screen, and `cassette_ink_complement()` is what a drawn label is inked in, so it
 * contrasts with the Card on purpose.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The saturation and the value every derived ink is emitted at, in percent of the HSV
 * cylinder: 62 % and 74 %. Strong enough to read as a colour on the white page, quiet
 * enough not to shout beside the print. */
#define CASSETTE_INK_SAT 62
#define CASSETTE_INK_VAL 74

/* The hue histogram's resolution: 24 buckets, 15 degrees each. A dominant hue comes back
 * as a multiple of 360 / 24, so it is stable when the cover's pixels shift by one. */
#define CASSETTE_INK_BUCKETS 24

/* A colour as eight-bit channels. The view is one COL_HEX() away from what LVGL wants. */
typedef struct {
    uint8_t r, g, b;
} cassette_rgb_t;

/* A hue in degrees (0..359, wrapped) at CASSETTE_INK_SAT and CASSETTE_INK_VAL.
 *
 * The conversion is the textbook HSV one, with V = 255 * VAL / 100 and the chroma
 * c = V * SAT / 100, both truncated - a value of 74 % is 188, not 189 - and the mixed
 * channel rounded half away from zero:
 *
 *     c = V * SAT / 100
 *     h' = hue / 60
 *     x = c * (1 - |h' mod 2 - 1|)
 *     m = V - c
 *
 * so hue 0 is (188, 72, 72) and hue 120 is (72, 188, 72). */
cassette_rgb_t cassette_ink_from_hue(int hue_deg);

/* The hue of an sRGB colour in degrees, rounded to the nearest, or -1 when it has none
 * (r == g == b). */
int cassette_ink_hue(cassette_rgb_t c);

/* The complement: the colour's hue turned 180 degrees at its own saturation and value. */
cassette_rgb_t cassette_ink_complement(cassette_rgb_t c);

/* The complement, or a plain light or dark ink when the complement has nothing to give.
 *
 * The drawn label is inked against the Card, and a complement is the rule: turn the hue and
 * the label reads. But a neutral Card - graphite, or any ink whose chroma is under
 * CASSETTE_INK_MIN_CHROMA - has no hue to turn, so its complement is itself and the label
 * would vanish into it. For those the label is near-white on a dark Card and near-black on a
 * light one, chosen by the Card's luminance. */
#define CASSETTE_INK_MIN_CHROMA 24
#define CASSETTE_INK_LIGHT 0xEE
#define CASSETTE_INK_DARK  0x11

cassette_rgb_t cassette_ink_label(cassette_rgb_t card);

/* The dominant hue of an RGB565 buffer, w x h pixels on `stride` bytes per row, sampled
 * every `step` pixels on both axes.
 *
 * Each sampled pixel with a hue contributes to one of CASSETTE_INK_BUCKETS; the fullest
 * bucket wins and the result is that bucket's first degree (a multiple of 360/24). Pixels
 * too grey (chroma under 12 % of the maximum channel) and too dark (value under 10 %) are
 * not counted, because neither can tell the Card anything. Ties go to the lower hue.
 *
 * Returns -1 when nothing qualified - a black-and-white cover - and the caller keeps the
 * table ink. */
int cassette_ink_dominant_hue(const uint8_t *rgb565, int w, int h, int stride, int step);

#ifdef __cplusplus
}
#endif

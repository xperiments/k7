#pragma once
/*
 * timeline - the arithmetic of the **Timeline bar**: the two times and the lit fill. Portable:
 * no LVGL, no ESP-IDF, no clock, no allocation.
 *
 * There is one Timeline bar drawn in two places - the **Peek**'s floating strip and the footer
 * band the **Controls** close the screen with. ADR 0032 put its geometry in `cassette_layout`
 * because "a copy that is allowed to hold its own copy of the numbers is a copy that drifts",
 * and left the arithmetic behind: a `format_time()` in each host, and two permille-to-width
 * calculations that had started from different inputs. This module is the rest of that
 * decision - the text, the fraction and the width - and nothing else.
 *
 * It does not draw and it owns no object. ADR 0032's other half still stands: the two hosts do
 * **not** share the bar. The Peek is a floating overlay with its own slide, two tags and their
 * tethers; the band is a fixed row that turns three faces every four seconds. They share the
 * idiom, the inks and these three rules, and each builds, places and hides its own labels and
 * its own fill.
 *
 * `t27_timeline` pins the lot. The rule is small, but it is the rule a viewer reads a Track's
 * position from, and both hosts would go on agreeing with each other while both were wrong.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "m:ss" - a Track's elapsed time or its total - in `out`, which is the caller's buffer.
 * `known` false prints "--:--": a length the decoder did not report, or a bar with nothing
 * playing, has no time to print. Sixteen bytes hold any minute count a day can produce.
 *
 * The format is the screen's own: minutes, a colon, and the seconds to two places, so a short
 * Track reads "0:36" rather than "00:36" and the two boxes stay the same width. */
void timeline_clock(char *out, size_t n, uint32_t ms, bool known);

/* The position through the Track as thousandths, clamped to 1000. A `duration_ms` of 0 - a
 * length the decoder did not report - is no position at all, so it yields 0 and the fill stays
 * empty rather than filling for a length nobody knows.
 *
 * The multiplication is widened: `elapsed_ms * 1000` overflows 32 bits after about 71 minutes,
 * and a long Track would otherwise show a fill that jumps backwards. */
uint32_t timeline_permille(uint32_t elapsed_ms, uint32_t duration_ms);

/* The lit fill's width in pixels for `permille` of a `track_w` px track, truncated. 1000 fills
 * it exactly, and a fraction past 1000 is clamped rather than allowed to overflow the track.
 *
 * Truncation is deliberate: the fill is a rectangle and a half pixel is not one. It means the
 * leftmost 1/288th of the track lights only at permille 4, which is the same step the Peek and
 * the band have always taken. */
int timeline_fill_w(int track_w, uint32_t permille);

#ifdef __cplusplus
}
#endif

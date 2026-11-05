#pragma once
/*
 * card_mark - the SD card, drawn where the boot mark was, while there is no card.
 *
 * The screen answers "nothing can play" with a picture rather than an empty cassette:
 * the icon at the boot mark's size, on the same black page, 80 px right of centre. It
 * rises from below the glass in a loop while the card is out - a rest in its place, then
 * another rise - so the cardless page reads as waiting rather than as a frozen screen. It
 * is created with the cassette and hidden until someone says the card is gone; the screen
 * task owns that decision, so no other part pushes state into it.
 *
 * Private to screen_display.
 */
#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the hidden mark on `screen` (which must already carry the cassette, so the mark
 * draws above it). */
void card_mark_create(lv_obj_t *screen);

/* Show or hide it. Showing it puts it under the screen's bottom edge, slides it to its
 * place and repeats from there for as long as it is up; hiding it is a cut and stops the
 * loop. A call for the state already held does nothing, so a repeat while the mark is
 * rising does not restart it. */
void card_mark_set(bool shown);

#ifdef __cplusplus
}
#endif

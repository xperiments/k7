#pragma once
/*
 * card_mark - the SD card, drawn where the boot mark was, while there is no card.
 *
 * The screen answers "nothing can play" with a picture rather than an empty cassette:
 * the icon at the boot mark's size, centred on the same black page. It is created with
 * the cassette and hidden until someone says the card is gone; the screen task owns
 * that decision, so no other part pushes state into it.
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

/* Show or hide it. A call for the state already held does nothing. */
void card_mark_set(bool shown);

#ifdef __cplusplus
}
#endif

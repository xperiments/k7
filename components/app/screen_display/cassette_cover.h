#pragma once
/*
 * cassette_cover - the Track's picture on the white controls page.
 *
 * The label is the top layer of the cassette: the stack is cinta < glow < label. The
 * picture is scaled to the outer frame's width and pinned to its top, clipped to that
 * frame's rounded corners plus 2 px. The label window is a real, transparent hole, not an
 * opaque fill: an A8 bitmap mask on the label cuts the inner frame's pill (radius = half
 * its shorter side), so the glow and the tape below show through. The frame[1] border takes
 * the same pill while the label is up (`cassette_tapes_set_cover_print()`), and the two
 * frames are raised over the label (`cassette_tapes_raise_frames()`).
 *
 * The label is created after the tape body and after the glow, so it covers the body inside
 * the outer frame; the tape exposed below the frame stays visible. The view owns when it is
 * up - the white controls page's down swipe turns it on, the up swipe turns it off - and the
 * panel board is the only build with the frames to mask against.
 *
 * Called under the LVGL lock, like the rest of the screen.
 */
#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the cover under `parent` (the cassette's container), hidden. It has no picture
 * until cassette_cover_set_image() hands it one. */
void cassette_cover_create(lv_obj_t *parent);

/* Re-place the clip box, the picture and the hole at the frames' current pixels. The
 * view calls this after the tape body has been placed, so the cover follows every step
 * of a pose change. */
void cassette_cover_place(void);

/* The Track's picture, or NULL when there is none. The descriptor must stay alive; the
 * loader's one cover buffer is reused, so NULL cuts the label at once (no fade over a buffer
 * being written) and the wish stays, so the new picture fades in when it lands. */
void cassette_cover_set_image(const lv_image_dsc_t *dsc);

/* The white page's wish: show the picture if there is one, hide otherwise, with a 1 s fade
 * in or out. The wish is remembered, so a picture that lands after the down appears
 * without a second swipe. */
void cassette_cover_set_shown(bool shown);

/* The white page's wish, whether or not a picture has landed. The down swipe sets it and
 * the up clears it, so the cover can be asked for while the decode is still running. */
bool cassette_cover_is_shown(void);


#ifdef __cplusplus
}
#endif

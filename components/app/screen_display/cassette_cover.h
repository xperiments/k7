#pragma once
/*
 * cassette_cover - the Track's picture printed on the white page's Card.
 *
 * The Cover is the Card's print (ADR 0039). Its container is the Card's own rectangle, 426x272
 * at (136,82), carrying the one A8 mask the Card and the print share: the picture is cut at the
 * 291x87 pill window at (203,157) and the tape below shows through it. Inside that container a
 * 402x188 box at +12,+20 - frame[0]'s rect at the controls pose, radius 10, `clip_corner` - is
 * the print proper: the width the picture is scaled to and the corner it is clipped by. The
 * window is at (55,55) in that box, which is where the mask's stadium falls once the print's
 * offset is taken off the Card's. The 1 s `opa_layered` fade is the container's, so the picture
 * and the mask go together and the Card does not fade with them.
 *
 * The mask is cassette_card.c's, not this module's, and the Card is built after this module -
 * the Cover is the cassette's bottom layer - so the descriptor is applied lazily, the first time
 * the print is looked at. Until it exists (or if its allocation failed) an opaque pill under the
 * picture stands in for the window. The Cover is raised over the Card when it shows, because the
 * Card is opaque outside the window and would otherwise hide the print.
 *
 * The picture is scaled to the print's width and pinned to its top; its height follows from the
 * source's aspect and the print clips whatever runs past its bottom.
 *
 * The Card's ink is not read from the picture: it is the table's plastic, and the label's ink is
 * the same row's (ADR 0040). The view hands both over when the Track changes.
 *
 * The view owns when the Cover is up - the white page's down swipe turns it on, the up swipe
 * turns it off - and the panel board is the only build with the pictures.
 *
 * Called under the LVGL lock, like the rest of the screen.
 */
#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the cover under `parent` (the cassette's container), hidden. Its geometry is the Card's
 * fixed rect, so it is built once: the masked container, the print inside it, and the fallback
 * pill. It has no picture until cassette_cover_set_image() hands it one. */
void cassette_cover_create(lv_obj_t *parent);

/* The Track's picture, or NULL when there is none. The descriptor must stay alive; the loader's
 * one cover buffer is reused, so NULL cuts the label at once (no fade over a buffer being
 * written) and the wish stays, so the new picture fades in when it lands.
 *
 * A picture with pixels to read also sets the Card's ink, from its dominant hue; with no
 * picture, or no hue to find, the Card keeps whatever ink the view gave it. */
void cassette_cover_set_image(const lv_image_dsc_t *dsc);

/* The white page's wish: show the picture if there is one, hide otherwise, with a 1 s fade in or
 * out. The wish is remembered, so a picture that lands after the down appears without a second
 * swipe. */
void cassette_cover_set_shown(bool shown);

/* The white page's wish, whether or not a picture has landed. The down swipe sets it and the up
 * clears it, so the cover can be asked for while the decode is still running. */
bool cassette_cover_is_shown(void);

/* A picture is on its way: the loader is about to overwrite its one buffer, so the print goes
 * empty and the drawn label stays down until the picture lands or the loader reports that this
 * Track has none. Without it the label would flash for the length of every decode. */
void cassette_cover_set_pending(void);

/* The Track's name, printed on the drawn label. The label is what the print shows when there is
 * no picture, so it is the one place a blitted Cover could never be: the Track's own words. */
void cassette_cover_set_title(const char *title);

/* The drawn label's ink: the row's label colour, from the table the plastic came from. The view
 * sets both together, so a cassette is one row and nothing is derived (ADR 0040). */
void cassette_cover_set_label_ink(lv_color_t ink);


#ifdef __cplusplus
}
#endif

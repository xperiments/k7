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
 * offset is taken off the Card's. The print is cut in and out, never faded: a page flip, a Track
 * change and a Track with no picture all land in one frame.
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
 * one cover buffer is reused, so NULL cuts the print at once and the wish stays, so the new
 * picture appears when it lands.
 *
 * A picture with pixels to read also sets the Card's ink, from its dominant hue; with no
 * picture, or no hue to find, the Card keeps whatever ink the view gave it. */
void cassette_cover_set_image(const lv_image_dsc_t *dsc);

/* The white page's wish: show the print if there is one, hide otherwise, in one frame. The wish
 * is remembered, so a picture that lands after the down appears without a second swipe. */
void cassette_cover_set_shown(bool shown);

/* Whether a picture has landed for the current Track: the print is the Cover, not the drawn
 * label. The view asks so the hubs can go white while the artwork is what the page shows. */
bool cassette_cover_has_picture(void);

/* The white page's wish, whether or not a picture has landed. The down swipe sets it and the up
 * clears it, so the cover can be asked for while the decode is still running. */
bool cassette_cover_is_shown(void);

/* The print's colours, from the row the plastic came from: the label's fill (the print when the
 * Track has no picture), the ink a word would use (the label is wordless), and the plastic the
 * window's rim is drawn in - the cassette is moulded around its window, so the frame and the
 * body are one material (ADR 0040). The view sets them together. */
void cassette_cover_set_inks(lv_color_t plastic, lv_color_t ink, lv_color_t label);

/* The window's slice, as a draw callback: the view hands the tapes' drawer
 * (cassette_tapes_draw_slice) here, so this module owns the porthole object and the tapes own
 * the paint. Set once, before the page can reach Real; a NULL drawer paints nothing. */
void cassette_cover_set_window_draw(void (*draw)(lv_layer_t *layer));

/* The tape moved inside the window: the slice is stale. The view calls this after
 * cassette_tapes_set() when the frame changed anything the window shows - the hub phase, the
 * fills or the ink - so a still Track costs nothing and a turning reel costs one capsule. */
void cassette_cover_window_invalidate(void);


#ifdef __cplusplus
}
#endif

#pragma once
/*
 * cassette_card - the white page's cassette.
 *
 * On the Real Level the cassette is one tinted rounded rectangle, 426x272 at (136,82),
 * with a 291x87 pill window at (203,157) through which the tape shows: the Card
 * (ADR 0039). The Track's picture, or the row's label fill, is printed on it at +12,+20 by
 * cassette_cover.c, which shares this module's mask.
 *
 * The Card is drawn at two layouts, both in screen pixels and neither scaling with the
 * pose: the Controls pose the screen shipped with, and the entry grid - the design box the
 * Controls numbers were derived from, -8,-23 814x520 - where a full-screen Real fills the
 * panel and bleeds off it. The pose's animation never resizes it: the layout changes in one
 * frame, with the flag that asks for it.
 *
 * The mask is the one thing here with a cost: an A8 bitmap of the Card's own size, one per
 * layout - 116,416 B at the Controls pose and 424,320 B at the entry grid. Two objects share
 * each - the Card and the print's container - because LVGL aligns a bitmap mask to the centre
 * of its object's area and clears everything outside the mask's rectangle, so a mask must
 * cover its object and two objects can share one only if their centres coincide. That is why
 * they have the same rect at both layouts.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The one number the Card's geometry left behind. Everything else about the box - its
 * corner, the print, the window, the rim, the plate - is in `k_card_layout`, the table that
 * actually draws it; this enum used to repeat all of it, and nothing read the copy. */
enum {
    CASSETTE_CARD_PLATE_DARKEN = LV_OPA_30,
};

/* One Card layout: the Card's box, and the window, print, rim and front plate measured from
 * its own corner. Two exist - see the table in cassette_card.c. */
typedef struct {
    int x, y, w, h, r;                 /* the Card */
    int wx, wy, ww, wh, wr;            /* the window's pill, from the Card's corner */
    int px, py, pw, ph, pr;            /* the print, from the Card's corner */
    int rim_h, rim_w;                  /* the rim's box (the window plus its bottom row) */
    int plx, ply, plw, plh, pl_inset;  /* the front plate */
} cassette_card_geometry_t;

/* Build the Card, its front plate and both masks under `parent`, hidden. Parent is the
 * cassette's container, and this must be called after the tape body so the Card draws over
 * it. Both masks are built here: 540,736 B in all, which the loader's own line says the heap
 * has, and building the full one lazily would put a 424 KB allocation on a gesture. */
void cassette_card_create(lv_obj_t *parent);

/* The layout in force. */
const cassette_card_geometry_t *cassette_card_geometry(void);

/* Draw the Card at the entry grid (true) or the Controls pose (false), in one frame. The
 * cover re-places the print, the rim and the window from the same geometry; the view calls
 * both under the LVGL lock. */
void cassette_card_set_full(bool full);

/* Whether the entry-grid layout is in force. */
bool cassette_card_is_full(void);

/* Show or hide it with the Page, in one frame. Hidden until the first call. */
void cassette_card_set_shown(bool shown);

/* The Card's ink: the Album's hue, or one of the table's five. It is a cut, never a
 * colour animation - a colour animation would repaint the panel on every step. */
void cassette_card_set_ink(lv_color_t ink);



/* The A8 descriptor for the layout in force, which the Card and the print's container
 * share, or NULL before create() has built it (or if that layout's allocation failed). */
const lv_image_dsc_t *cassette_card_mask(void);

/* The window's rect on the panel for the layout in force - (203,157) 291x87 at the Controls
 * pose, (120,120) 556x166 at the entry grid - which the packs clamp their boxes to while the
 * Card is up, so the reel tick never invalidates outside it. */
cassette_rect_t cassette_card_window(void);

#ifdef __cplusplus
}
#endif

#pragma once
/*
 * cassette_card - the white page's cassette.
 *
 * On the Real Level the cassette is one tinted rounded rectangle, 426x272 at (136,82),
 * with a 291x87 pill window at (203,157) through which the tape shows: the Card
 * (ADR 0039). The Track's picture, or the row's label fill, is printed on it at +12,+20 by
 * cassette_cover.c, which shares this module's mask.
 *
 * The Card places itself in screen pixels and never scales: it exists only at the
 * Controls pose, which is the only place the white page can be reached, so the pose's
 * animation never resizes it.
 *
 * The mask is the one thing here with a cost: an A8 bitmap of the Card's own size,
 * 116,416 B, built once. Two objects share it - the Card and the print's container -
 * because LVGL aligns a bitmap mask to the centre of its object's area and clears
 * everything outside the mask's rectangle, so a mask must cover its object and two
 * objects can share one only if their centres coincide. That is why they are both
 * 426x272 at (136,82).
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The Card's geometry, in panel pixels at the Controls pose. The window - the one opening
 * through which the tape shows - is expressed from the Card's corner; on the panel it is
 * (203,157). */
enum {
    CASSETTE_CARD_X  = 136,
    CASSETTE_CARD_Y  = 82,
    CASSETTE_CARD_W  = 426,
    CASSETTE_CARD_H  = 272,
    CASSETTE_CARD_R  = 12,

    /* Where the print - and the label fill - sits inside the Card. It is frame[0]'s
     * rect at the Controls pose: 402x188 at (148,102). */
    CASSETTE_CARD_PRINT_DX = 12,
    CASSETTE_CARD_PRINT_DY = 20,
    CASSETTE_CARD_PRINT_W  = 402,
    CASSETTE_CARD_PRINT_H  = 188,
    CASSETTE_CARD_PRINT_R  = 10,   /* the frame's 8 px corner, plus the owner's 2 */

    /* The window: frame[1]'s interior, the largest pill the print's height allows. */
    CASSETTE_CARD_WINDOW_DX = 67,
    CASSETTE_CARD_WINDOW_DY = 75,
    CASSETTE_CARD_WINDOW_W  = 291,
    CASSETTE_CARD_WINDOW_H  = 87,
    CASSETTE_CARD_WINDOW_R  = 43,  /* min(W, H) / 2: the largest pill the box allows */

    /* The rim around the window: 7 px of the Card's own plastic, the same 43 px radius, on a
     * 291x88 box - the window's 87 plus the row its bottom stroke lands on. It is drawn over
     * the print's cut edge, so it hides the mask's last row of antialiasing and reads as the
     * shell's moulding rather than as a line. */
    CASSETTE_CARD_WINDOW_RIM_W = 7,
    CASSETTE_CARD_WINDOW_RIM_H = 88,

    /* The shell's front plate: a trapezoid cut into the plastic below the print, filled 30 %
     * darker than it. Its bottom edge is the Card's own and its top edge is inset by
     * PLATE_INSET on each side. The numbers are the reference cassette's, measured against the
     * Card's box; there is no outline, only the fill. */
    CASSETTE_CARD_PLATE_DX  = 54,
    CASSETTE_CARD_PLATE_DY  = 216,
    CASSETTE_CARD_PLATE_W   = 318,
    CASSETTE_CARD_PLATE_H   = 56,
    CASSETTE_CARD_PLATE_INSET = 20,
    CASSETTE_CARD_PLATE_DARKEN = LV_OPA_30,
};

/* Build the Card and its mask under `parent`, hidden. Parent is the cassette's
 * container, and this must be called after the tape body so the Card draws over it. */
void cassette_card_create(lv_obj_t *parent);

/* Show or hide it with the Page, in one frame. Hidden until the first call. */
void cassette_card_set_shown(bool shown);

/* The Card's ink: the Album's hue, or one of the table's five. It is a cut, never a
 * colour animation - a colour animation would repaint the panel on every step. */
void cassette_card_set_ink(lv_color_t ink);



/* The one A8 descriptor the Card and the print's container share, or NULL before
 * create() has built it. */
const lv_image_dsc_t *cassette_card_mask(void);

/* The window's rect on the panel, (203,157) 291x87: what the packs clamp their boxes to
 * while the Card is up, so the reel tick never invalidates outside it. */
cassette_rect_t cassette_card_window(void);

#ifdef __cplusplus
}
#endif

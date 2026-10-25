#pragma once
/*
 * The LVGL object helpers every part of the Cassette screen builds with.
 *
 * They remove the widget's own style, put it at a position and size, and leave it
 * decorative: a press must reach the screen's gesture handler, not a painted box.
 * Making that true at construction is what lets the view stop walking the finished
 * tree in make_decorative() and stop requiring future controls to be created after
 * the walk.
 */
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A rounded box with a fill. */
lv_obj_t *cassette_box(lv_obj_t *parent, int x, int y, int w, int h, int r, lv_color_t bg);

/* A rounded box with a border. `bg_opa` makes the fill optional: LV_OPA_TRANSP
 * leaves only the border, so whatever is under the box shows through. */
lv_obj_t *cassette_border_box(lv_obj_t *parent, int x, int y, int w, int h, int r,
                              lv_color_t bg, lv_opa_t bg_opa,
                              lv_color_t border_col, int border_width);

/* A label. The caller sets its text and long mode. */
lv_obj_t *cassette_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour,
                        lv_align_t align, int x, int y);

/* An image object with no background, border or padding, so the picture sits at
 * the object's origin. `src` may be NULL when the caller sets it later. */
lv_obj_t *cassette_image(lv_obj_t *parent, const lv_image_dsc_t *src,
                         int x, int y, int w, int h);

/* Give a circle one of its two faces, for the parts the page flips - as a pair of objects,
 * because LVGL draws one shadow per object. `ambient` is a transparent circle exactly under
 * `dome` and carries the broad, soft shadow; `dome` is the disc itself and carries the tight
 * contact shadow. Together they read as one raised button in the bezel around it. On the
 * white page the disc is a flat light-grey fill (one colour, no gradient); on the dark page
 * both are line art again - no shadows, no fill, and whatever is behind shows through. The
 * caller owns the ring: its width and colour on `dome`, because the pressed states and M and
 * R differ there; on the white page it sets the width to 0. LVGL's shadows are outer-only, so
 * both are cast by their own object and fall on what is behind them; nothing here tries an
 * inset. Every number is sized so the pair stays inside a 96 px bezel around either the 44 px
 * knob or the 64 px button circle. */
void cassette_circle_face(lv_obj_t *ambient, lv_obj_t *dome, bool white);

#ifdef __cplusplus
}
#endif

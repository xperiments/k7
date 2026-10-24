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

#ifdef __cplusplus
}
#endif

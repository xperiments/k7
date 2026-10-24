/*
 * The Cassette screen's object helpers; see cassette_widgets.h.
 */
#include "cassette_widgets.h"

lv_obj_t *cassette_box(lv_obj_t *parent, int x, int y, int w, int h, int r, lv_color_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

lv_obj_t *cassette_border_box(lv_obj_t *parent, int x, int y, int w, int h, int r,
                              lv_color_t bg, lv_opa_t bg_opa,
                              lv_color_t border_col, int border_width)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, bg_opa, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_border_color(o, border_col, 0);
    lv_obj_set_style_border_width(o, border_width, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

lv_obj_t *cassette_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour,
                        lv_align_t align, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_obj_align(l, align, x, y);
    lv_obj_set_clickable(l, false);
    return l;
}

lv_obj_t *cassette_image(lv_obj_t *parent, const lv_image_dsc_t *src,
                         int x, int y, int w, int h)
{
    lv_obj_t *o = lv_image_create(parent);
    if (src) lv_image_set_src(o, src);
    /* No background or padding of the widget's own, so the picture sits at the
     * object's origin. */
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_clickable(o, false);
    return o;
}

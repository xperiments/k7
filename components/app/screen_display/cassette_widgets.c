/*
 * The Cassette screen's object helpers; see cassette_widgets.h.
 */
#include "cassette_widgets.h"

/* The dome's ink and its two shadows. LVGL draws one shadow per object, so the face is a
 * pair: the broad ambient shadow on the transparent wrapper, the tight contact shadow on the
 * disc itself. The broad one is the owner's tuned 24 px at a 2/5 px offset, 60 %; the contact
 * one is 8 px at a 1/2 px offset, which darkens the edge with its whole blur inside the 96 px
 * bezel (it reaches x=85, y=86; the broad one reaches x=94, its faint tail a couple of px past
 * the bezel's bottom). Each has its own colour so a tinted second shadow is a one-line
 * change. */
#define DOME_HEX        0x929292
#define AMBIENT_HEX     0x000000
#define AMBIENT_W       24
#define AMBIENT_DX      2
#define AMBIENT_DY      5
#define AMBIENT_OPA     60
#define CONTACT_HEX     0xFFFFFF
#define CONTACT_W       16
#define CONTACT_DX      -2
#define CONTACT_DY      -2
#define CONTACT_OPA     80

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

void cassette_circle_face(lv_obj_t *ambient, lv_obj_t *dome, bool white)
{
    if (white) {
        if (ambient) {
            lv_obj_set_style_shadow_color(ambient, lv_color_hex(AMBIENT_HEX), 0);
            lv_obj_set_style_shadow_opa(ambient, AMBIENT_OPA, 0);
            lv_obj_set_style_shadow_width(ambient, AMBIENT_W, 0);
            lv_obj_set_style_shadow_spread(ambient, 0, 0);
            lv_obj_set_style_shadow_offset_x(ambient, AMBIENT_DX, 0);
            lv_obj_set_style_shadow_offset_y(ambient, AMBIENT_DY, 0);
        }
        if (dome) {
            lv_obj_set_style_bg_opa(dome, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dome, lv_color_hex(DOME_HEX), 0);
            lv_obj_set_style_shadow_color(dome, lv_color_hex(CONTACT_HEX), 0);
            lv_obj_set_style_shadow_opa(dome, CONTACT_OPA, 0);
            lv_obj_set_style_shadow_width(dome, CONTACT_W, 0);
            lv_obj_set_style_shadow_spread(dome, 0, 0);
            lv_obj_set_style_shadow_offset_x(dome, CONTACT_DX, 0);
            lv_obj_set_style_shadow_offset_y(dome, CONTACT_DY, 0);
        }
    } else {
        /* The line art: no shadows at all, and the disc lets what is behind show through. */
        if (ambient) {
            lv_obj_set_style_shadow_opa(ambient, LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_width(ambient, 0, 0);
        }
        if (dome) {
            lv_obj_set_style_bg_opa(dome, LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_opa(dome, LV_OPA_TRANSP, 0);
            lv_obj_set_style_shadow_width(dome, 0, 0);
        }
    }
}

/*
 * The boot mark. See splash.h for why the screen has one.
 *
 * It owns no geometry: the mark is a label the active screen centres, and the page under it
 * is the same k_col_bg the cassette's page will paint a moment later, so the cut from the
 * mark to the cassette does not flash a different black.
 */
#include "splash.h"

#include "esp_log.h"

#include "cassette_icons.h"
#include "cassette_style.h"

static const char *TAG = "screen";

/* The mark at 128 px tall: the font is generated at a 314 px em because the emblem's ink is
 * 42 % of its em box, which lands the glyph on 238x128 (cassette_icons.h has the arithmetic).
 * Its cut carries this mark and nothing else, so the glyph check below is its whole contract. */
#define SPLASH_FONT (&xtape_logo_128)

static lv_obj_t *s_mark;

void splash_create(void)
{
    if (s_mark) return;

    lv_obj_t *scr = lv_screen_active();

    /* Opaque, not transparent: LVGL's default page is not black, and whatever the panel
     * powered up with is still underneath until a full frame is drawn. */
    lv_obj_set_style_bg_color(scr, k_col_bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    /* A mark the font cannot draw would be .notdef - a box - over the whole boot. The page
     * is already black, so leave it there and say why. */
    lv_font_glyph_dsc_t dsc;
    if (!lv_font_get_glyph_dsc(SPLASH_FONT, &dsc, CASSETTE_ICON_K7LOGO_CP, 0)) {
        ESP_LOGW(TAG, "the boot mark's font has no K7 glyph; the boot screen stays black");
        return;
    }

    s_mark = lv_label_create(scr);
    lv_obj_set_style_text_font(s_mark, SPLASH_FONT, 0);
    lv_obj_set_style_text_color(s_mark, k_col_primary, 0);
    lv_label_set_text(s_mark, CASSETTE_ICON_K7LOGO);
    lv_obj_align(s_mark, LV_ALIGN_CENTER, 0, -50);
}

void splash_destroy(void)
{
    if (!s_mark) return;
    lv_obj_delete(s_mark);
    s_mark = NULL;
}

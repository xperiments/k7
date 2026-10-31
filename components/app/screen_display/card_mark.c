/*
 * The cardless mark. See card_mark.h.
 *
 * It is a label, not an image: the SD glyph is in the icon font, which keeps the mark a
 * few hundred bytes instead of a bitmap, and it centres the same way the boot mark does.
 */
#include "card_mark.h"

#include "esp_log.h"

#include "cassette_icons.h"
#include "cassette_style.h"

static const char *TAG = "screen";

#define CARD_MARK_FONT (&xtape_sd_128)

static lv_obj_t *s_mark;
static bool      s_shown;

void card_mark_create(lv_obj_t *screen)
{
    if (s_mark || !screen) return;

    /* A glyph the cut does not carry would be .notdef over the whole screen, so the same
     * check the boot mark makes: no glyph, no mark, and say why once. */
    lv_font_glyph_dsc_t dsc;
    if (!lv_font_get_glyph_dsc(CARD_MARK_FONT, &dsc, CASSETTE_ICON_SD_CP, 0)) {
        ESP_LOGW(TAG, "the SD mark's font has no SD glyph; the cardless screen stays empty");
        return;
    }

    s_mark = lv_label_create(screen);
    lv_obj_set_style_text_font(s_mark, CARD_MARK_FONT, 0);
    lv_obj_set_style_text_color(s_mark, k_col_primary, 0);
    lv_label_set_text(s_mark, CASSETTE_ICON_SD);
    lv_obj_align(s_mark, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(s_mark, true);
    s_shown = false;
}

void card_mark_set(bool shown)
{
    if (!s_mark || shown == s_shown) return;
    s_shown = shown;
    lv_obj_set_hidden(s_mark, !shown);
}

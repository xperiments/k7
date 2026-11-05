/*
 * The cardless mark. See card_mark.h.
 *
 * It is a label, not an image: the SD glyph is in the icon font, which keeps the mark a
 * few hundred bytes instead of a bitmap, and it is placed the way the boot mark is - the
 * same centre line and height - then moved right by the one shift the cardless page wants.
 */
#include "card_mark.h"

#include "esp_log.h"

#include "cassette_icons.h"
#include "cassette_style.h"

static const char *TAG = "screen";

#define CARD_MARK_FONT (&xtape_sd_128)

/* The mark's place is the centre column moved 80 px right: the boot mark's centring is
 * the page's, and the cardless page is the mark beside nothing, so it sits off the
 * middle by hand. Its height is the glyph's, so the centre alignment is the y it keeps.
 */
#define CARD_MARK_X_SHIFT 80

/* The rise, in ms. Long enough to read as a movement and short enough that the mark is
 * there before anyone wonders why the screen changed: it is one whole glyph, 126 px tall,
 * crossing the lower third of the page. */
#define CARD_MARK_RISE_MS 600

/* How long the mark rests in its place between two rises. The loop is the cardless page's
 * pulse: without the pause the glyph is only ever seen moving and never standing still. */
#define CARD_MARK_HOLD_MS 500

static lv_obj_t *s_mark;
static bool      s_shown;

/* The place the rise ends at, in screen pixels, and the y it starts at - the screen's
 * bottom edge, so the first frame has the mark entirely below the glass. Computed once:
 * both depend on the glyph and the display, not on when the card goes. */
static int32_t s_target_x;
static int32_t s_target_y;
static int32_t s_start_y;

static void mark_rise_cb(void *var, int32_t y)
{
    lv_obj_set_pos((lv_obj_t *)var, s_target_x, y);
}

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
    /* The alignment and the label's own size are a layout pass away; ask for it here, or the
     * place below reads the coordinates the object had before it was placed. */
    lv_obj_update_layout(s_mark);
    s_target_x = lv_obj_get_x(s_mark) + CARD_MARK_X_SHIFT;
    s_target_y = lv_obj_get_y(s_mark);
    /* Those two are the place. The label is moved by coordinate from here - the rise sets a
     * y in screen pixels - so its align goes back to TOP_LEFT, which is what makes the
     * style's x and y absolute instead of offsets from the centre. */
    lv_obj_set_style_align(s_mark, LV_ALIGN_TOP_LEFT, 0);
    lv_obj_set_pos(s_mark, s_target_x, s_target_y);
    s_start_y = lv_obj_get_height(screen);
    lv_obj_set_hidden(s_mark, true);
    s_shown = false;
}

void card_mark_set(bool shown)
{
    if (!s_mark || shown == s_shown) return;
    s_shown = shown;

    /* A card that goes and comes back cancels the loop wherever it is, not only on the
     * way up: the animation is the object's, and hiding it is a cut. */
    lv_anim_delete(s_mark, mark_rise_cb);

    if (!shown) {
        lv_obj_set_hidden(s_mark, true);
        return;
    }

    /* Reveal it under the glass first, then let the animation bring it in. Placing it
     * before the reveal is what keeps the first visible frame below the screen. */
    lv_obj_set_pos(s_mark, s_target_x, s_start_y);
    lv_obj_set_hidden(s_mark, false);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_mark);
    lv_anim_set_exec_cb(&a, mark_rise_cb);
    lv_anim_set_values(&a, s_start_y, s_target_y);
    lv_anim_set_duration(&a, CARD_MARK_RISE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    /* The cardless page is a loop while the card is out: the mark rises from below the
     * glass, rests in its place, and starts again, so the screen is visibly waiting for a
     * card rather than showing a still picture that could be mistaken for a frozen one. */
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, CARD_MARK_HOLD_MS);
    lv_anim_start(&a);
}

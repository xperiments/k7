/*
 * fx_drawer - the effects panel that slides in from the right edge.
 *
 * Two controls, both acting on the running stream: the effect's on/off and which
 * tape flavour it is. They go through fx_ui_apply()/fx_ui_set_enabled(), the same
 * calls the web page makes, so the screen and the browser cannot disagree.
 *
 * Opened by a two-finger swipe to the left; any two-finger swipe, a tap on the
 * dimmed backdrop, or the close cross puts it away. It lives on the top layer
 * rather than the screen, and is hidden at an in-bounds position when closed, so
 * it is never part of the screen's layout.
 */
#include "fx_drawer.h"

#include <stdint.h>

#include "esp_log.h"
#include "fx_ui.h"
#include "lvgl.h"

LV_FONT_DECLARE(kalam_36);

static const char *TAG = "fx_drawer";

#define PANEL_W 380
#define ANIM_MS 220

#define COL_PANEL  0x241512
#define COL_CARD   0x3C2B2A
#define COL_TEXT   0xF3F2EE
#define COL_MUTED  0x9A8A80
#define COL_ACCENT 0xE54820
#define COL_EDGE   0x4A3430

static lv_obj_t *s_backdrop;
static lv_obj_t *s_panel;
static lv_obj_t *s_on;
static lv_obj_t *s_on_label;
static lv_obj_t *s_type[3];
static lv_obj_t *s_type_label[3];
static bool s_open;
static mini_icecast_status_t s_last;

/* ------------------------------------------------------------------ helpers */

static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h, uint32_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 8, 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, const lv_font_t *font, uint32_t colour)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    return l;
}

static void closed_ready(lv_anim_t *a);

static void anim_to(int x, bool closing)
{
    lv_anim_delete(s_panel, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_values(&a, lv_obj_get_x(s_panel), x);
    lv_anim_set_time(&a, ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    if (closing) lv_anim_set_ready_cb(&a, closed_ready);
    lv_anim_start(&a);
}

static void closed_ready(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(s_panel, lv_obj_get_width(lv_layer_top()) - PANEL_W);
}

static void show(bool open)
{
    if (!s_panel || s_open == open) return;
    s_open = open;
    const int w = lv_obj_get_width(lv_layer_top());
    if (open) {
        lv_obj_remove_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(s_panel, w);
        anim_to(w - PANEL_W, false);
    } else {
        lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);
        anim_to(w, true);
    }
}

/* ------------------------------------------------------------------- events */

static void backdrop_cb(lv_event_t *e)
{
    (void)e;
    show(false);
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    show(false);
}

static void on_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = fx_ui_set_enabled(!s_last.fx_on);
    if (err != ESP_OK) ESP_LOGW(TAG, "cannot switch the effect: %s", esp_err_to_name(err));
}

static void type_cb(lv_event_t *e)
{
    const int type = (int)(intptr_t)lv_event_get_user_data(e);
    mini_icecast_fx_t t = {
        .set = true,
        .type = (uint8_t)type,
        .dolby = s_last.fx_dolby,
        .tape_q = s_last.fx_tape_q,
        .head_q = s_last.fx_head_q,
        .motor_q = s_last.fx_motor_q,
    };
    esp_err_t err = fx_ui_apply(&t);
    if (err != ESP_OK) ESP_LOGW(TAG, "cannot set the tape type: %s", esp_err_to_name(err));
}

/* -------------------------------------------------------------------- build */

void fx_drawer_create(void)
{
    /* The top layer, not the screen: an overlay must not be part of the screen's
     * layout, or its off-screen position becomes scrollable content. */
    lv_obj_t *scr = lv_layer_top();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    const int w = lv_obj_get_width(scr);
    const int h = lv_obj_get_height(scr);

    s_backdrop = lv_obj_create(scr);
    lv_obj_remove_style_all(s_backdrop);
    lv_obj_set_pos(s_backdrop, 0, 0);
    lv_obj_set_size(s_backdrop, w, h);
    lv_obj_set_style_bg_color(s_backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_backdrop, LV_OPA_50, 0);
    lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_backdrop, backdrop_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_backdrop, LV_OBJ_FLAG_HIDDEN);

    s_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, PANEL_W, h);
    lv_obj_set_pos(s_panel, w - PANEL_W, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(COL_EDGE), 0);
    lv_obj_set_style_border_side(s_panel, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(s_panel, 2, 0);
    lv_obj_set_scrollable(s_panel, false);

    lv_obj_t *title = label(s_panel, 24, 16, &kalam_36, COL_TEXT);
    lv_label_set_text(title, "Efectos");

    lv_obj_t *x = card(s_panel, PANEL_W - 60, 20, 36, 36, COL_CARD);
    lv_obj_add_flag(x, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(x, close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *xl = label(x, 0, 0, NULL, COL_TEXT);
    lv_label_set_text(xl, LV_SYMBOL_CLOSE);
    lv_obj_center(xl);

    lv_obj_t *h1 = label(s_panel, 24, 86, NULL, COL_MUTED);
    lv_label_set_text(h1, "CINTA");

    s_on = card(s_panel, 24, 112, PANEL_W - 48, 64, COL_CARD);
    lv_obj_add_flag(s_on, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_on, on_cb, LV_EVENT_CLICKED, NULL);
    s_on_label = label(s_on, 0, 0, &kalam_36, COL_TEXT);
    lv_obj_align(s_on_label, LV_ALIGN_LEFT_MID, 18, 0);
    lv_label_set_text(s_on_label, "Apagada");

    lv_obj_t *h2 = label(s_panel, 24, 204, NULL, COL_MUTED);
    lv_label_set_text(h2, "TIPO");

    static const char *names[3] = {"Normal", "Chrome", "Metal"};
    const int bw = (PANEL_W - 48 - 2 * 10) / 3;
    for (int i = 0; i < 3; i++) {
        s_type[i] = card(s_panel, 24 + i * (bw + 10), 230, bw, 56, COL_CARD);
        lv_obj_add_flag(s_type[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_type[i], type_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_type_label[i] = label(s_type[i], 0, 0, NULL, COL_TEXT);
        lv_label_set_text(s_type_label[i], names[i]);
        lv_obj_center(s_type_label[i]);
    }

    lv_obj_t *hint = label(s_panel, 24, h - 46, NULL, COL_MUTED);
    lv_label_set_text(hint, "Dos dedos a la derecha para cerrar");

    s_open = false;
    lv_obj_set_x(s_panel, w - PANEL_W);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
}

/* ------------------------------------------------------------------- update */

void fx_drawer_update(const mini_icecast_status_t *st)
{
    if (!st || !s_panel) return;
    /* This runs every screen frame; touching an LVGL object invalidates it and
     * costs a redraw, so the controls only change when they actually change. */
    static bool have;
    if (have && st->fx_on == s_last.fx_on && st->fx_type == s_last.fx_type) return;
    have = true;
    s_last = *st;

    lv_label_set_text(s_on_label, st->fx_on ? "Encendida" : "Apagada");
    lv_obj_set_style_bg_color(s_on, lv_color_hex(st->fx_on ? COL_ACCENT : COL_CARD), 0);

    for (int i = 0; i < 3; i++) {
        const bool on = st->fx_on && st->fx_type == i;
        lv_obj_set_style_bg_color(s_type[i], lv_color_hex(on ? COL_ACCENT : COL_CARD), 0);
        lv_obj_set_style_text_color(s_type_label[i], lv_color_hex(on ? COL_TEXT : COL_MUTED), 0);
    }
}

/* ------------------------------------------------------------------ gesture */

bool fx_drawer_swipe(int dx, int dy)
{
    (void)dy;
    if (s_open) {          /* open: any two-finger swipe puts it away */
        show(false);
        return true;
    }
    if (dx < 0) {          /* closed: swipe left pulls it in from the right edge */
        show(true);
        return true;
    }
    return false;          /* a swipe to the right belongs to the cover */
}

bool fx_drawer_is_open(void) { return s_open; }

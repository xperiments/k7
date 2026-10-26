/*
 * The transport drawer, its three line-art buttons and their two states; see
 * cassette_buttons.h.
 *
 * The drawer is one panel object moved by lv_obj_set_x(). Its buttons are static
 * objects: each button is a clickable root with two prebuilt state children, one
 * unpressed and one pressed, and only the visible one is drawn. The press/release
 * handlers swap which child is not hidden; the click handler runs a short scale pulse
 * on the root. Nothing is allocated or restyled at touch time beyond that one style
 * property.
 *
 * The button's root bubbles gestures to the panel, and the panel bubbles them to the
 * screen, so the same swipe that opens or closes the drawer works when it starts on a
 * button. The drawer itself is clickable so a tap on its margin is absorbed rather than
 * read as a screen gesture; it has no click handler.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "cassette_buttons.h"

#include <stdint.h>

#include "esp_log.h"
#include "lvgl.h"

#include "cassette_style.h"
#include "cassette_widgets.h"
#include "mini_icecast.h"

LV_FONT_DECLARE(jura_36);

static const char *TAG = "screen";

/* The drawer: a 120 px strip at the left edge, hidden by sliding its own width out.
 * The three 96x96 buttons start 76 px from the screen's top and share their borders
 * (a -2 px gap); the middle button (<) is therefore on y=218. */
#define PANEL_W 120
#define PANEL_H 480
#define PANEL_HIDDEN_X (-PANEL_W)

#define BUTTON_W 96
#define BUTTON_H 96
#define BUTTON_R 8
#define BUTTON_GAP -2
#define BUTTON_X 16
#define BUTTON_TOP 64+12

/* The circle is centred in the button: 16 px of line art on every side at 64 px. */
#define CIRCLE_D 64
#define CIRCLE_R (CIRCLE_D / 2)
#define CIRCLE_INSET ((BUTTON_W - CIRCLE_D) / 2)

/* The rectangle and the disc keep the palette's inks on the dark page (k_col_button_rect and
 * k_col_button_circle); the pressed state keeps them with a heavier line. */
#define BUTTON_BORDER_W 2
#define BUTTON_BORDER_W_PRESSED 3

/* The click pulse: a linear 0..1024 animation whose scale is a triangle, 256 -> 232 ->
 * 256, so the button lands exactly where it started. */
#define BUTTON_SCALE_NONE 256
#define BUTTON_SCALE_PULSE 232
#define BUTTON_PULSE_MS 160

/* One state of a button: the 96x96 wrapper that is shown or hidden, and the three parts the
 * page paints - the rectangle's opaque fill, the disc and its letter, which the dark page
 * draws in the button's own inks. */
typedef struct {
    lv_obj_t *state;
    lv_obj_t *fill;
    lv_obj_t *ambient;   /* the transparent circle that carries the broad shadow */
    lv_obj_t *circle;    /* the disc: fill, ring, contact shadow, and the letter */
    lv_obj_t *letter;
    cassette_cap_t cap;   /* the white page's colours; the dark page is the palette's line art */
    int border_w;        /* the ring the dark page draws; the white page's dome has none */
} cassette_button_state_t;

typedef struct {
    lv_obj_t *root;
    cassette_button_state_t unpressed;
    cassette_button_state_t pressed;
    cassette_button_id_t id;   /* which button this is - the character, and the action it runs */
} cassette_button_t;

static lv_obj_t *s_panel;
static cassette_button_t s_buttons[CASSETTE_BUTTON_COUNT];
static bool s_shown;
static bool s_white;   /* the page the buttons are painted for, so a re-ink can repaint them */
static cassette_buttons_room_cb_t s_room;
static void *s_room_ctx;

/* --------------------------------------------------------------- the buttons */

/* Show one of the two prebuilt states and hide the other. */
static void button_state_show(cassette_button_t *b, bool pressed)
{
    if (!b || !b->unpressed.state || !b->pressed.state) return;
    lv_obj_set_hidden(b->unpressed.state, pressed);
    lv_obj_set_hidden(b->pressed.state, !pressed);
}

/* The scale pulse, as a triangle over the animation's 0..1024 step. The completion
 * step lands on BUTTON_SCALE_NONE, so no restoring callback is needed. */
static void button_pulse_cb(void *var, int32_t step)
{
    int32_t peak = step < 512 ? step : 1024 - step;
    int32_t scale = BUTTON_SCALE_NONE -
                    (BUTTON_SCALE_NONE - BUTTON_SCALE_PULSE) * peak / 512;
    lv_obj_set_style_transform_scale((lv_obj_t *)var, scale, 0);
}

static void button_pulse(lv_obj_t *root)
{
    if (!root) return;
    lv_anim_delete(root, button_pulse_cb);
    lv_obj_set_style_transform_scale(root, BUTTON_SCALE_NONE, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root);
    lv_anim_set_exec_cb(&a, button_pulse_cb);
    lv_anim_set_values(&a, 0, 1024);
    lv_anim_set_duration(&a, BUTTON_PULSE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void button_pressed_cb(lv_event_t *e)
{
    cassette_button_t *b = lv_event_get_user_data(e);
    if (!b) return;
    /* A new press replaces a pulse still in flight, at full size, before the pressed
     * state is swapped in. */
    lv_anim_delete(b->root, button_pulse_cb);
    lv_obj_set_style_transform_scale(b->root, BUTTON_SCALE_NONE, 0);
    button_state_show(b, true);
}

static void button_released_cb(lv_event_t *e)
{
    button_state_show(lv_event_get_user_data(e), false);
}

static void button_press_lost_cb(lv_event_t *e)
{
    button_state_show(lv_event_get_user_data(e), false);
}

/* A click runs the button's action (P is a placeholder; `<` and `>` are next/previous)
 * and the scale pulse. A horizontal swipe that started here is also a release and a
 * click, so a touch with a gesture direction - or one that moved past the press
 * threshold - is not a tap and does neither. */
static void button_clicked_cb(lv_event_t *e)
{
    cassette_button_t *b = lv_event_get_user_data(e);
    if (!b) return;
    button_state_show(b, false);

    lv_indev_t *indev = lv_indev_active();
    if (indev && (lv_indev_get_gesture_dir(indev) != LV_DIR_NONE ||
                  lv_indev_get_press_moved(indev))) return;
    if (b->id == CASSETTE_BUTTON_PREV) mini_icecast_prev();
    else if (b->id == CASSETTE_BUTTON_SKIP) mini_icecast_skip();
    button_pulse(b->root);
}

/* One state: the rounded rectangle, the disc at its centre and the letter inside the disc.
 * Both states of a button are built by this function, so their geometry cannot drift apart;
 * the line art is the palette's (built here, for the dark page it starts on), the cap is the
 * button's own for the white page, and only the border weight and the initial visibility
 * differ. */
static void button_state_create(lv_obj_t *parent, cassette_button_state_t *out,
                                const char *text, cassette_cap_t cap, int border_w, bool hidden)
{
    lv_obj_t *state = lv_obj_create(parent);
    lv_obj_remove_style_all(state);
    lv_obj_set_pos(state, 0, 0);
    lv_obj_set_size(state, BUTTON_W, BUTTON_H);
    lv_obj_set_style_bg_opa(state, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state, 0, 0);
    lv_obj_set_style_pad_all(state, 0, 0);
    lv_obj_set_scrollable(state, false);
    lv_obj_set_clickable(state, false);

    out->state = state;
    out->cap = cap;
    out->border_w = border_w;
    out->fill = cassette_border_box(state, 0, 0, BUTTON_W, BUTTON_H, BUTTON_R,
                                    k_col_bg, LV_OPA_COVER, k_col_button_rect, border_w);
    /* The dome is two circles: this transparent one carries the broad shadow, the one inside
     * it is the disc. On the dark page both are invisible and the disc is the line art. */
    out->ambient = cassette_border_box(state, CIRCLE_INSET, CIRCLE_INSET,
                                       CIRCLE_D, CIRCLE_D, CIRCLE_R,
                                       k_col_bg, LV_OPA_TRANSP, k_col_bg, 0);
    lv_obj_t *circle = cassette_border_box(out->ambient, 0, 0,
                                           CIRCLE_D, CIRCLE_D, CIRCLE_R,
                                           k_col_button_circle, LV_OPA_TRANSP,
                                           k_col_button_circle, border_w);
    lv_obj_t *letter = cassette_text(circle, &jura_36, k_col_button_letter,
                                     LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(letter, text);
    out->circle = circle;
    out->letter = letter;

    lv_obj_set_hidden(state, hidden);
}

static void button_create(lv_obj_t *parent, cassette_button_id_t id, const char *text, int y)
{
    cassette_button_t *b = &s_buttons[id];
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_pos(root, BUTTON_X, y);
    lv_obj_set_size(root, BUTTON_W, BUTTON_H);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_clickable(root, true);
    /* The scale pulse is centred on the button, not on its top-left corner. */
    lv_obj_set_style_transform_pivot_x(root, BUTTON_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(root, BUTTON_H / 2, 0);
    /* A swipe that starts on a button still reaches the screen's gesture handler, and
     * the press reaches screen_input so it can clear the two-finger latch. A click also
     * bubbles; the screen no longer gives a click a meaning. */
    lv_obj_set_gesture_bubble(root, true);
    lv_obj_set_event_bubble(root, true);

    /* The palette's default cap, given to both states; a caller may change a button's
     * afterwards with cassette_buttons_set_cap(). Built here rather than as a file-scope
     * constant because the k_col_* colours are objects, not constant expressions. */
    cassette_cap_t cap = { k_col_dome, k_col_bg };

    b->root = root;
    b->id = id;
    button_state_create(root, &b->unpressed, text, cap, BUTTON_BORDER_W, false);
    button_state_create(root, &b->pressed, text, cap, BUTTON_BORDER_W_PRESSED, true);

    lv_obj_add_event_cb(root, button_pressed_cb, LV_EVENT_PRESSED, b);
    lv_obj_add_event_cb(root, button_released_cb, LV_EVENT_RELEASED, b);
    lv_obj_add_event_cb(root, button_press_lost_cb, LV_EVENT_PRESS_LOST, b);
    lv_obj_add_event_cb(root, button_clicked_cb, LV_EVENT_CLICKED, b);
}

/* ----------------------------------------------------------------- the drawer */

static void panel_slide_cb(void *var, int32_t x)
{
    lv_obj_set_x((lv_obj_t *)var, x);
}

/* The slide out is over: hide the drawer so it cannot take a touch off screen. A slide
 * in never reaches here hidden; if a reverse deleted this animation, it does not run. */
static void panel_slide_done_cb(lv_anim_t *a)
{
    lv_obj_t *panel = lv_anim_get_user_data(a);
    if (panel && !s_shown) lv_obj_set_hidden(panel, true);
}

static void panel_slide(bool shown)
{
    if (!s_panel) return;
    if (shown) {
        lv_obj_set_hidden(s_panel, false);
        /* Chrome above every sheet; a vertical swipe re-raises it too. */
        lv_obj_move_foreground(s_panel);
    }

    lv_anim_delete(s_panel, panel_slide_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, panel_slide_cb);
    lv_anim_set_values(&a, lv_obj_get_x(s_panel), shown ? 0 : PANEL_HIDDEN_X);
    lv_anim_set_duration(&a, CASSETTE_BUTTONS_SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_user_data(&a, s_panel);
    if (!shown) lv_anim_set_completed_cb(&a, panel_slide_done_cb);
    lv_anim_start(&a);
}

void cassette_buttons_create(lv_obj_t *screen, cassette_buttons_room_cb_t room, void *ctx)
{
    if (!screen || s_panel) return;
    s_room = room;
    s_room_ctx = ctx;
    s_shown = false;
    s_white = false;

    s_panel = lv_obj_create(screen);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_pos(s_panel, PANEL_HIDDEN_X, 0);
    lv_obj_set_size(s_panel, PANEL_W, PANEL_H);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_set_scrollable(s_panel, false);
    /* Clickable so the margins absorb a tap instead of letting it fall through to the
     * screen's gesture handler; it has no click handler of its own. */
    lv_obj_set_clickable(s_panel, true);
    lv_obj_set_gesture_bubble(s_panel, true);
    /* The panel's press reaches screen_input's latch-clearing handler; its click bubbles
     * to the screen, which no longer gives a click a meaning. */
    lv_obj_set_event_bubble(s_panel, true);
    lv_obj_set_hidden(s_panel, true);

    static const char *const labels[CASSETTE_BUTTON_COUNT] = { "P", "<", ">" };
    for (int i = 0; i < CASSETTE_BUTTON_COUNT; i++)
        button_create(s_panel, (cassette_button_id_t)i, labels[i],
                      BUTTON_TOP + i * (BUTTON_H + BUTTON_GAP));
}

void cassette_buttons_set_shown(bool shown)
{
    if (!s_panel || shown == s_shown) return;
    s_shown = shown;
    if (s_room) s_room(shown, s_room_ctx);
    panel_slide(shown);
    ESP_LOGI(TAG, "buttons %s", shown ? "shown" : "hidden");
}

/* One state's page: the rectangle's fill, the disc's face and the letter's. The white page is
 * the button's cap - the disc's face in the cap's colour, the rim the original dome grey at
 * CASSETTE_CAP_RIM_W, the letter on it in the cap's letter colour. The dark page is the
 * palette's line art and nothing else: the playing red ring at the width this state was built
 * with and the playing red letter, the same for every button. */
static void button_state_page(cassette_button_state_t *s, bool white)
{
    if (s->fill) lv_obj_set_style_bg_color(s->fill, white ? k_col_white : k_col_bg, 0);
    if (s->letter)
        lv_obj_set_style_text_color(s->letter, white ? s->cap.letter : k_col_button_letter, 0);
    if (!s->circle) return;

    cassette_circle_face(s->ambient, s->circle, white, s->cap.face);
    if (white) {
        lv_obj_set_style_border_width(s->circle, CASSETTE_CAP_RIM_W, 0);
        lv_obj_set_style_border_color(s->circle, k_col_dome, 0);
    } else {
        lv_obj_set_style_border_width(s->circle, s->border_w, 0);
        lv_obj_set_style_border_color(s->circle, k_col_button_circle, 0);
    }
}

void cassette_buttons_set_page(bool white)
{
    s_white = white;
    for (int i = 0; i < CASSETTE_BUTTON_COUNT; i++) {
        button_state_page(&s_buttons[i].unpressed, white);
        button_state_page(&s_buttons[i].pressed, white);
    }
}

void cassette_buttons_set_cap(cassette_button_id_t id, cassette_cap_t cap)
{
    if ((unsigned)id >= CASSETTE_BUTTON_COUNT) return;
    cassette_button_t *b = &s_buttons[id];
    if (!b->root) return;
    b->unpressed.cap = cap;
    b->pressed.cap = cap;
    button_state_page(&b->unpressed, s_white);
    button_state_page(&b->pressed, s_white);
}

bool cassette_buttons_is_shown(void)
{
    return s_shown;
}

void cassette_buttons_raise(void)
{
    if (s_panel) lv_obj_move_foreground(s_panel);
}

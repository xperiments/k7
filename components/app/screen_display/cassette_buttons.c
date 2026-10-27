/*
 * The Transport panel and its three buttons; see cassette_buttons.h.
 *
 * The panel is a value handed to the left bar: one column of three cells, each built into a
 * carcase the bar made. This file is the buttons' table - the label, the cap and the action -
 * and the Menu's mode, which rewrites the label and the action in place. The disc, the
 * letter, the one pressed state, the pulse, the page ink and the tap guard are the shared
 * cassette_button's.
 *
 * The bar moves the cells, dispatches the page and raises them; a swipe that starts on a
 * button still reaches the screen because the bar bubbles the cells' gestures.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "cassette_buttons.h"

#include "cassette_bars.h"
#include "cassette_button.h"
#include "cassette_icons.h"
#include "cassette_style.h"
#include "cassette_widgets.h"
#include "mini_icecast.h"

LV_FONT_DECLARE(jura_36);

/* The three labels, in the id order: the Jura characters the panel is built with and the
 * Menu's icons, both swapped into the one label by the mode. */
static const char *const k_button_labels[CASSETTE_BUTTON_COUNT] = { "P", "<", ">" };
/* The controls' identities, independent of the mode's glyphs and actions. */
static const char *const k_button_names[CASSETTE_BUTTON_COUNT] = { "P", "PREV", "SKIP" };
static const char *const k_button_icons[CASSETTE_BUTTON_COUNT] = {
    CASSETTE_ICON_TAPE, CASSETTE_ICON_LEFT, CASSETTE_ICON_RIGHT
};

/* The transport outside the Menu: < is previous, > is next/previous's skip, and P is still a
 * placeholder whose click is the pulse alone. */
static void transport_prev(void *ctx)
{
    (void)ctx;
    /* A tap on a cell that is still arriving is not a transport command; the same gate the
     * EQ's cells apply to a moving knob. */
    if (cassette_bars_is_sliding()) return;
    mini_icecast_prev();
}

static void transport_skip(void *ctx)
{
    (void)ctx;
    if (cassette_bars_is_sliding()) return;
    mini_icecast_skip();
}

static cassette_button_action_t const k_transport_action[CASSETTE_BUTTON_COUNT] = {
    NULL, transport_prev, transport_skip
};

static cassette_button_t s_buttons[CASSETTE_BUTTON_COUNT];
static bool s_menu;    /* the Menu owns the three buttons: Close / Back / Enter */
static cassette_buttons_cb_t s_menu_back;
static cassette_buttons_cb_t s_menu_enter;
static cassette_buttons_cb_t s_menu_close;
static void *s_menu_ctx;

/* The Menu's action for a button, in the panel's order. */
static cassette_buttons_cb_t menu_action(int id)
{
    switch (id) {
    case CASSETTE_BUTTON_P: return s_menu_close;
    case CASSETTE_BUTTON_PREV: return s_menu_back;
    default: return s_menu_enter;
    }
}

/* Put the three buttons on the current mode: the Menu's glyphs, font and callbacks, or the
 * Jura characters and the transport. Only the label and the action move - the press look, the
 * pulse, the caps and the page ink are the button's. */
static void drawer_mode_apply(void)
{
    for (int id = 0; id < CASSETTE_BUTTON_COUNT; id++) {
        cassette_button_t *b = &s_buttons[id];
        if (!b->carcase) continue;
        if (s_menu) {
            cassette_button_set_label(b, k_button_icons[id], &xtape_36);
            cassette_button_set_action(b, menu_action(id), s_menu_ctx);
        } else {
            cassette_button_set_label(b, k_button_labels[id], &jura_36);
            cassette_button_set_action(b, k_transport_action[id], NULL);
        }
    }
}

/* One cell: the shared button inside the bar's carcase, on the current mode. The carcase's
 * ring thickens on a press (2 -> 3) and the disc's does too on the dark page; on the white
 * page the disc is the cap's fixed rim and the carcase's ring is the whole of the visible
 * press. The cap is the palette's neutral grey for all three; the mode swaps only the label
 * and the action, so a panel built while the Menu is up lands on its glyphs. */
static void transport_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)ctx;
    if (col != 0 || row < 0 || row >= CASSETTE_BUTTON_COUNT) return;
    int id = row;

    cassette_cap_t cap = { k_col_dome, k_col_bg };
    cassette_button_spec_t spec = {
        .name = k_button_names[id],
        .text = s_menu ? k_button_icons[id] : k_button_labels[id],
        .font = s_menu ? &xtape_36 : &jura_36,
        .cap = cap,
        .carcase_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED },
        .disc_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED },
        .on_click = s_menu ? menu_action(id) : k_transport_action[id],
        .ctx = s_menu ? s_menu_ctx : NULL,
    };
    cassette_button_create(&s_buttons[id], cell, &spec);
}

/* The panel's page ink: the bar has painted the carcases' default, this is the buttons' own
 * cap and line art. */
static void transport_set_page(bool white, void *ctx)
{
    (void)ctx;
    for (int id = 0; id < CASSETTE_BUTTON_COUNT; id++)
        cassette_button_set_page(&s_buttons[id], white);
}

/* The transport drawer: one column of three, the cells the bar's. It bubbles gestures, so a
 * swipe that starts on a button still opens or closes the drawer. It keeps no state when the
 * bar comes and goes. */
static const cassette_panel_t k_transport_panel = {
    .columns = 1,
    .rows = CASSETTE_BUTTON_COUNT,
    .cell_rect = NULL,
    .gesture_bubble = true,
    .build = transport_build,
    .set_page = transport_set_page,
    .shown = NULL,
    .ctx = NULL,
};

void cassette_buttons_create(void)
{
    cassette_bars_set_panel(CASSETTE_BARS_LEFT, &k_transport_panel);
}

void cassette_buttons_set_menu_mode(bool on, cassette_buttons_cb_t back,
                                    cassette_buttons_cb_t enter,
                                    cassette_buttons_cb_t close, void *ctx)
{
    s_menu = on;
    s_menu_back = back;
    s_menu_enter = enter;
    s_menu_close = close;
    s_menu_ctx = ctx;
    drawer_mode_apply();
}

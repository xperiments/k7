/*
 * The mode bar and its three buttons; see cassette_buttons.h.
 *
 * The panel is a value handed to the left bar: one column of three cells, each built into a
 * carcase the bar made. This file is the buttons' table - the glyph, the name and the callback -
 * and the two states the view drives: the callback and the latch. The disc, the letter, the one
 * pressed state, the pulse, the page ink and the tap guard are the shared cassette_button's.
 *
 * The bar moves the cells, dispatches the page and raises them; a swipe that starts on a button
 * still reaches the screen because the bar bubbles the cells' gestures.
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "cassette_buttons.h"

#include "cassette_bars.h"
#include "cassette_button.h"
#include "cassette_icons.h"
#include "cassette_style.h"
#include "cassette_widgets.h"

LV_FONT_DECLARE(xtape_36);

/* The three glyphs, in the id order: the cassette that opens the Menu, the sliders that select
 * the EQ group and the led that selects the RGB group. */
static const char *const k_mode_icon[CASSETTE_MODE_COUNT] = {
    CASSETTE_ICON_TAPE, CASSETTE_ICON_SLIDERS, CASSETTE_ICON_LED
};
/* The controls' identities, independent of the glyphs and the actions. */
static const char *const k_mode_name[CASSETTE_MODE_COUNT] = { "MENU", "EQ", "RGB" };

static cassette_button_t s_buttons[CASSETTE_MODE_COUNT];
static cassette_buttons_cb_t s_cb[CASSETTE_MODE_COUNT];
static void *s_ctx[CASSETTE_MODE_COUNT];
static bool s_on[CASSETTE_MODE_COUNT];   /* the latch, which may lead the cells */
static bool s_created;

static bool mode_valid(cassette_mode_t which)
{
    return (unsigned)which < CASSETTE_MODE_COUNT;
}

/* The one action every cell runs: the mode's callback, if the bar is out and settled. A tap on
 * a cell that is still arriving is not a command, the same gate the panels apply. */
static void mode_clicked(void *ctx)
{
    int id = (int)(intptr_t)ctx - 1;
    if (id < 0 || id >= CASSETTE_MODE_COUNT) return;
    if (cassette_bars_is_sliding()) return;
    if (s_cb[id]) s_cb[id](s_ctx[id]);
}

/* One cell: the shared button inside the bar's carcase, carrying the mode's glyph in the icon
 * font. The latch is applied on top, so a panel built while its mode is on lands lit. */
static void mode_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)ctx;
    if (col != 0 || row < 0 || row >= CASSETTE_MODE_COUNT) return;
    int id = row;

    /* The white page's idle disc: the palette's neutral cap, as it always was. A selected mode
     * takes k_col_button_cap on that page (cassette_button's latch ink, ADR 0038). */
    cassette_cap_t cap = { k_col_dome, k_col_bg };
    cassette_button_spec_t spec = {
        .name = k_mode_name[id],
        .text = k_mode_icon[id],
        .font = &xtape_36,
        .cap = cap,
        .carcase_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED },
        .disc_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED },
        .on_click = mode_clicked,
        .ctx = (void *)(intptr_t)(id + 1),
    };
    cassette_button_create(&s_buttons[id], cell, &spec);
    cassette_button_set_on(&s_buttons[id], s_on[id]);
}

/* The panel's page ink: the bar has painted the carcases' default, this is the buttons' own
 * cap and line art. The latch survives the page because the button keeps it. */
static void mode_set_page(bool white, void *ctx)
{
    (void)ctx;
    for (int id = 0; id < CASSETTE_MODE_COUNT; id++)
        cassette_button_set_page(&s_buttons[id], white);
}

/* The mode bar: one column of three, the cells the bar's. It bubbles gestures, so a swipe that
 * starts on a button still moves the ladder. */
static const cassette_panel_t k_mode_panel = {
    .columns = 1,
    .rows = CASSETTE_MODE_COUNT,
    .cell_rect = NULL,
    .gesture_bubble = true,
    .inert = false,
    .build = mode_build,
    .set_page = mode_set_page,
    .shown = NULL,
    .ctx = NULL,
};

void cassette_buttons_create(void)
{
    if (s_created) return;
    s_created = true;
    cassette_bars_set_panel(CASSETTE_BARS_LEFT, &k_mode_panel);
}

void cassette_buttons_set_cb(cassette_mode_t which, cassette_buttons_cb_t cb, void *ctx)
{
    if (!mode_valid(which)) return;
    s_cb[which] = cb;
    s_ctx[which] = ctx;
}

void cassette_buttons_set_on(cassette_mode_t which, bool on)
{
    if (!mode_valid(which) || s_on[which] == on) return;
    s_on[which] = on;
    cassette_button_set_on(&s_buttons[which], on);
}

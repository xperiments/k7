#pragma once
/*
 * cassette_bars - the two edge strips: their cells, their entrance and their page ink.
 *
 * The mode bar at the left edge and the group bar at the right are one mechanism. Each is
 * a column of 96x96 carcases on the layout's 94 px pitch that slides in from its edge over
 * 260 ms, paints for the Page, raises itself and is hidden at rest. This module owns the
 * carcases, the entrance, the page dispatch, the raise and the hit rule. A **panel** - a
 * static value, not a module that builds a strip - fills the cells it is handed; it creates
 * no carcase and registers no root.
 *
 * A bar is a value, not an LVGL object: its cells are screen children at the layout's rects
 * and the bar slides them by an offset. The left bar is the mode bar's one column of three -
 * the Menu's door and the two group selectors - and the right bar is whichever group panel is
 * up: the Empty grille, the EQ's six cells or the RGB's. Every cell belongs to the panel; there
 * is no chrome cell, because the Menu's door moved to the left bar and no panel can strand it.
 *
 * One panel per bar. set_panel() takes the value; a bar shows a different panel by taking
 * the old one's cells down and building the new one's, which is a cut rather than a slide.
 * The Empty group and the Menu's Pad are plates: 1x1, and their cell_rect() returns the
 * layout's Pad rect, one 186x284 surface instead of a lattice cell.
 *
 * The order the view uses: create() once, after the peek and before the bands; then each
 * panel installs its value (`cassette_buttons_create()`, `cassette_eq_create()`); then
 * set_shown() and set_page() follow the Controls model. Everything is called under the LVGL
 * lock, like the rest of the screen.
 *
 * The 260 ms is the chrome's: the bands and the window slide over the same time and the
 * cassette's pose runs with them. It is the one clock the chrome shares.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CASSETTE_BARS_SLIDE_MS 260

/* The two named bars. */
typedef enum {
    CASSETTE_BARS_LEFT = 0,     /* the transport bar, at the screen's left edge */
    CASSETTE_BARS_RIGHT,        /* the DJ bar, at the right edge */
    CASSETTE_BARS_COUNT
} cassette_bar_side_t;

/* What a bar hosts: one panel at a time, as a value. The bar builds one carcase per cell of
 * `columns` x `rows`, hands each to build(), and owns the rest of the strip. A panel that is
 * one surface rather than a grid - the Menu's Pad, one 186x284 plate - sets both to 1 and
 * gives cell_rect(), which returns that surface's rect instead of a lattice cell.
 *
 *   columns, rows  the grid the panel fills, no larger than its bar's (the bar clamps it).
 *   cell_rect      where cell (col,row) is, or NULL for the bar's lattice cell. A plate
 *                  panel returns one rect for (0,0).
 *   gesture_bubble whether a swipe that starts on a cell still reaches the screen's gesture
 *                  handler: true for the mode bar (a swipe that starts on a button still moves
 *                  the ladder) and for the Empty grille (a picture must not eat a swipe), false
 *                  for the EQ and RGB groups (a swipe on a knob is not one) and for the Pad.
 *   inert          the panel is a picture, not a control surface: its carcases stay out of the
 *                  hit test, so the touch falls through to the screen. The Empty grille sets it.
 *   build          one control inside one carcase, once per cell. The carcase is the bar's:
 *                  the panel sets no position, size, clickability or bubbling.
 *   set_page       the panel's page ink, after the bar has painted the cells' default: the
 *                  knob plate, the discs and M's and R's caps. NULL for a panel that does not
 *                  turn with the Page (the Pad).
 *   shown          the panel is visible (true) or being taken off screen (false): where it
 *                  ends an edit and cancels a pending open. NULL for a panel with no state.
 *   ctx            the panel's, passed to every hook.
 */
typedef struct {
    int columns;
    int rows;
    cassette_rect_t (*cell_rect)(int col, int row);
    bool gesture_bubble;
    bool inert;
    void (*build)(lv_obj_t *cell, int col, int row, void *ctx);
    void (*set_page)(bool white, void *ctx);
    void (*shown)(bool visible, void *ctx);
    void *ctx;
} cassette_panel_t;

/* Build both bars, hidden, at the layout's hidden offset. Their cells are built by
 * set_panel(). A second create() is a no-op. */
void cassette_bars_create(lv_obj_t *screen);

/* Make `panel` the bar's content: the old panel's cells are deleted, one carcase per cell of
 * the new panel's grid is built at the layout's rects (or at the panel's own, for a plate),
 * the current page is painted into them and each is built into by panel->build(). A late
 * panel lands where the bar already is, hidden if the bar is hidden. The panel's shown hook
 * is called with the bar's state once its cells are up, so a restored panel knows it is
 * visible. A NULL panel or a bar that was never created is a no-op. Swapping is a cut; the
 * caller decides which panel is up (the view, from the groups model). */
void cassette_bars_set_panel(cassette_bar_side_t side, const cassette_panel_t *panel);

/* Slide both bars in (true) or out (false), over one 260 ms ease-out. A call naming the state
 * already held does nothing, even mid-slide; a call naming the other reverses from the
 * current offset. The cells are hidden once a slide out completes. */
void cassette_bars_set_shown(bool shown);

/* True while either bar is between its hidden and resting offsets. A panel uses it to refuse
 * a click on a moving cell; the settling pose is the view's own test. */
bool cassette_bars_is_sliding(void);

/* Paint both bars for the Page: the cells' default fill and border, then each panel's own
 * ink. Also the page a panel built later lands on. */
void cassette_bars_set_page(bool white);

#ifdef __cplusplus
}
#endif

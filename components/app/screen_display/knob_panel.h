#pragma once
/*
 * knob_panel - the shell the right bar's two knob panels are made of, and the data a Group is.
 *
 * The EQ and the RGB Group are the same panel twice. Both are a 2x3 grid of 96 px bar cells
 * whose inside column's top two cells are cut into one 96x190 plate; both put a small dial in
 * each of the cells the plate leaves, and a dial is dragged up and down to move its value and
 * tapped to open the 3x editor; both are given the same editor bank, and both ink the dials'
 * cells the same way when the Page turns. What differs
 * is data - which knob sits in which cell, what the dials are called, what fills the plate, and
 * what a value publishes - so that data is a **Group** here and the shell is this module.
 *
 * The bank is the part worth being careful about, because a panel's cells do not outlive every
 * swap. The bar deletes them when another panel takes the bar, and the editor's own swap is one
 * of those: a tapped dial hands the bar to the Pad, so the panel's cells go while the values it
 * is editing stay. The shown hook therefore reports the bar's **wish**, and the bank follows it:
 *
 *   - shown(true) installs the bank, so a restored panel can be tapped again;
 *   - shown(false) *keeps* the bank while knob_editor_is_editing(), because the swap is the
 *     editor's and the values are this panel's. Only the cells are gone, which is why every hook
 *     that touches a cell tests the shown flag first;
 *   - open_allowed() is `shown && !cassette_bars_is_sliding()`, re-checked when the editor's
 *     delayed open lands: a tap armed while the bar is still arriving is not a command.
 *
 * A tap arms that delayed open, a double tap resets the knob, and the click a double tap leaves
 * behind is swallowed by the editor. None of that is a Group's business, which is why it is here
 * once instead of in both files.
 *
 * Private to screen_display. Every object is built under the LVGL lock.
 */
#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#include "cassette_bars.h"
#include "knob.h"
#include "knob_cell.h"
#include "knob_editor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The two cells of a Group's map that are not a knob's. A map entry of 0 or more is a knob id. */
enum {
    KNOB_PANEL_EMPTY = -1,   /* the plate above covers it: the bar builds no control */
    KNOB_PANEL_EXTRA = -2,   /* the Group's own build_extra() fills it - the EQ's VU meter */
};

/* What a Group supplies. Every array is in the Group's knob-id order, and its length is
 * `knob_count`; the Group owns the storage and this module only reads it.
 *
 *   columns, rows  the grid the panel fills. Both Groups are 2x3, which is the shape the plate
 *                  geometry below describes; a third with another shape would need it handed in.
 *   cell_kind      the control at (col,row), indexed `col * rows + row`: a knob id, or one of
 *                  the two sentinels. Declared `[columns][rows]` by the Group and passed as
 *                  `&k_cell_kind[0][0]`.
 *   names          the knob's own name, which is what the 3x dial's title pillow says.
 *   shorts         the badge on the 1x cell; a NULL or empty entry leaves it out.
 *   value_pill_y   the bottom pillow a stepped knob shows its selected slot in, or NULL for a
 *                  Group with no stepped knob. Per knob; a negative entry means "no pillow".
 *   build_extra    fills the KNOB_PANEL_EXTRA cell. NULL for a Group whose plate is a knob.
 *   page_extra     that cell's page ink, after the knobs'. NULL for a Group with no extra.
 *   publish        a knob's value moved or was reset: send it where it goes. Called for a valid
 *                  id only; a Group may ignore the id or the call as its own publishing needs.
 */
typedef struct {
    int columns;
    int rows;
    int knob_count;
    const int8_t *cell_kind;
    const char *const *names;
    const char *const *shorts;
    const int16_t *value_pill_y;
    void (*build_extra)(lv_obj_t *cell, void *ctx);
    void (*page_extra)(bool white, void *ctx);
    void (*publish)(int id, void *ctx);
    void *ctx;
} knob_panel_spec_t;

/* One Group's panel. The Group embeds this next to its own knobs and cells; it is a value, not a
 * singleton, because the two Groups are two panels and each has its own bank, its own shown flag
 * and its own cells. The bar's `ctx` is this struct, so the hooks find it without a global. */
typedef struct {
    const knob_panel_spec_t *spec;
    knob_t *knobs;
    knob_cell_t *cells;
    knob_editor_bank_t bank;
    cassette_panel_t value;   /* what the composition installs; its ctx is this struct */
    bool shown;               /* the bar's wish, not the settled picture */
} knob_panel_t;

/* Wire a Group's storage to the shell. `knobs` and `cells` are the Group's, one per knob id in
 * its own order; the knobs must already be initialised - their ranges, their steps, their marks
 * are the Group's data - because the names are the only part of a knob this module sets. Call
 * once, after cassette_bars_create() and knob_editor_create(), and before the panel can be
 * installed: the value this module hands the bar is filled in here. */
void knob_panel_init(knob_panel_t *panel, const knob_panel_spec_t *spec, knob_t *knobs,
                     knob_cell_t *cells);

/* The panel value for cassette_bars_set_panel(), the same address every time so a re-install of
 * the panel already up is the bar's no-op. */
const cassette_panel_t *knob_panel_value(knob_panel_t *panel);

#ifdef __cplusplus
}
#endif

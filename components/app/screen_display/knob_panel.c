/*
 * The knob panels' shell; see knob_panel.h.
 *
 * Two Groups are two tables, and this is everything around them. The table says which knob sits
 * in which cell and what fills the plate; the shell builds the dials, attaches the handlers that
 * make a dial draggable and tappable, keeps the shown gate and the editor's bank, paints the page
 * ink and hands the bar one panel value. Moving it here is what makes a third Group a table
 * rather than another two hundred lines.
 *
 * The Group is not named anywhere in this file: a value, a fill and a publish come through the
 * spec, so the EQ's meter and the RGB effect's dial-and-name plate are the same "extra cell" with
 * different owners.
 */
#include "knob_panel.h"

#include <stddef.h>

#include "cassette_button_rule.h"
#include "cassette_layout.h"

/* ------------------------------------------------------------------- the plate */

/* The inside column's top two cells as one surface, and nothing where the second of them would
 * have been. Two 96 px columns touch at x=688 and the rows sit at 76/170/264, so the merged rect
 * is 96x190: the EQ's meter plate and the RGB effect's dial-and-name plate are the same
 * rectangle, which is why the geometry is the shell's and not a Group's. A third Group with a
 * different shape would have to hand its own rect in. */
static cassette_rect_t panel_cell_rect(int col, int row)
{
    if (col == 0 && row == 0) {
        cassette_rect_t a = cassette_cell_rect(CASSETTE_BAR_RIGHT, 0, 0);
        cassette_rect_t b = cassette_cell_rect(CASSETTE_BAR_RIGHT, 0, 1);
        cassette_rect_t plate = { a.x, a.y, a.w, b.y + b.h - a.y };
        return plate;
    }
    if (col == 0 && row == 1) {
        cassette_rect_t none = { 0, 0, 0, 0 };
        return none;
    }
    return cassette_cell_rect(CASSETTE_BAR_RIGHT, col, row);
}

/* ---------------------------------------------------------------------- draw */

/* The small dial after its value moved. A stepped knob also says which of its slots is on, in the
 * bottom pillow the Group placed: that is a value rather than a range, and the only text a 96 px
 * cell has room for (ADR 0027). */
static void knob_apply(knob_panel_t *p, int id)
{
    knob_cell_apply(&p->cells[id], &p->knobs[id]);
    if (p->spec->value_pill_y && p->spec->value_pill_y[id] >= 0)
        knob_cell_set_value(&p->cells[id], knob_label(&p->knobs[id]));
}

/* ------------------------------------------------------------------- the bank */

/* Nothing to paint while the editor owns the bar: the Pad has taken these cells. */
static void bank_repaint(int id, void *ctx)
{
    knob_panel_t *p = ctx;
    if (!p || !p->shown) return;
    if (id >= 0 && id < p->spec->knob_count) knob_apply(p, id);
}

/* The value moved or was reset: the Group sends it where it goes - a band, a channel, the whole
 * strip. This is the only hook whose body is the Group's, and the only reason a Group knows that
 * a value moved at all. */
static void bank_publish(int id, void *ctx)
{
    knob_panel_t *p = ctx;
    if (!p || id < 0 || id >= p->spec->knob_count) return;
    if (p->spec->publish) p->spec->publish(id, p->spec->ctx);
}

/* The delayed open is re-checked here when it lands, so a panel that went away - or a bar that is
 * still arriving - refuses the dial instead of leaving an editor with no cells. */
static bool bank_open_allowed(void *ctx)
{
    knob_panel_t *p = ctx;
    return p && p->shown && !cassette_bars_is_sliding();
}

/* ------------------------------------------------------------------- the cells */

/* The cell a handler is on carries the panel that built it: the bar puts nothing in a carcase's
 * user data, so a press finds its Group without a second global beside the editor's own.
 *
 * It is the handler's own object - the current target - and not the one the press started on: a
 * dial's parts are decorative, so a press reaches the carcase today, but a clickable child
 * tomorrow must still find the panel rather than report none.
 */static knob_panel_t *panel_of(lv_event_t *e)
{
    return lv_obj_get_user_data(lv_event_get_current_target_obj(e));
}

/* The payload is the knob's id plus one, so a valid id 0 is not NULL. */
static int event_knob(lv_event_t *e)
{
    return (int)(intptr_t)lv_event_get_user_data(e) - 1;
}

/* A knob's own tap guard: the same rule the buttons apply, because a swipe that starts on a knob
 * is not the knob's click. The knob is not a button - it has no press look and no pulse - so it
 * reads the portable rule directly. */
static bool click_is_tap(void)
{
    lv_indev_t *indev = lv_indev_active();
    bool gesturing = indev && lv_indev_get_gesture_dir(indev) != LV_DIR_NONE;
    bool moved = indev && lv_indev_get_press_moved(indev);
    return cassette_button_is_tap(moved, gesturing);
}

/* The knob's value is the drag's, and the drag is the only way it moves (ADR 0027). The press at
 * 1x runs at 2 px a step; the same finger on the 3x dial would be four times as coarse, so that
 * one is the editor's. */
static void cell_pressed_cb(lv_event_t *e)
{
    knob_panel_t *p = panel_of(e);
    if (!p || !p->shown || cassette_bars_is_sliding()) return;
    int id = event_knob(e);
    if (id < 0 || id >= p->spec->knob_count) return;
    knob_editor_drag_begin(id);
}

static void cell_pressing_cb(lv_event_t *e)
{
    (void)e;
    knob_editor_drag_update();
}

static void cell_released_cb(lv_event_t *e)
{
    (void)e;
    knob_editor_drag_end();
}

/* A short tap arms the editor's delayed open; the editor drops it when a reset gesture still owes
 * one, or ignores it because an editor is open. A covered cell has no handler of its own: the
 * plate above it is the Group's and the shell adds nothing to it. */
static void cell_clicked_cb(lv_event_t *e)
{
    knob_panel_t *p = panel_of(e);
    if (!p || !p->shown || cassette_bars_is_sliding()) return;

    int id = event_knob(e);
    if (id < 0 || id >= p->spec->knob_count) return;
    if (!click_is_tap()) return;

    knob_editor_tap(id);
}

/* A double click on a small knob is the reset gesture. It runs before the second CLICKED, so the
 * editor disarms the delayed open, resets the knob and owes the click that follows; the 3x dial
 * never appears. */
static void cell_double_clicked_cb(lv_event_t *e)
{
    knob_panel_t *p = panel_of(e);
    if (!p || !p->shown || cassette_bars_is_sliding()) return;
    int id = event_knob(e);
    if (id < 0 || id >= p->spec->knob_count) return;
    knob_editor_double_tap(id);
}

/* A knob's five handlers: the drag's three and the editor's two. */
static void knob_handlers(lv_obj_t *cell, int id)
{
    lv_obj_add_event_cb(cell, cell_pressed_cb, LV_EVENT_PRESSED, (void *)(intptr_t)(id + 1));
    lv_obj_add_event_cb(cell, cell_pressing_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(cell, cell_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(cell, cell_clicked_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(id + 1));
    lv_obj_add_event_cb(cell, cell_double_clicked_cb, LV_EVENT_DOUBLE_CLICKED,
                        (void *)(intptr_t)(id + 1));
}

/* One knob in one carcase the bar made: the dial, the badge over it, the pillow under it if the
 * Group says this knob has one, and the handlers. The carcase remembers the panel, so an event on
 * it - or on the dial inside it - reaches this Group's shown flag and bank. */
static void knob_build(knob_panel_t *p, lv_obj_t *cell, int id)
{
    if (!knob_cell_create(&p->cells[id], cell)) return;

    /* The cell names its control in the pillow over the dial, so the 3x surface only has to say
     * where the value is (ADR 0035). */
    knob_cell_add_name_pill(&p->cells[id], p->spec->shorts[id]);

    if (p->spec->value_pill_y && p->spec->value_pill_y[id] >= 0)
        knob_cell_add_value_pill(&p->cells[id], CASSETTE_CELL_W / 2, p->spec->value_pill_y[id]);

    knob_apply(p, id);
    lv_obj_set_user_data(cell, p);
    knob_handlers(cell, id);
}

/* One cell of the panel. The bar made the carcase; the map says what goes in it. */
static void panel_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    knob_panel_t *p = ctx;
    if (!p || !cell) return;
    if (col < 0 || col >= p->spec->columns || row < 0 || row >= p->spec->rows) return;

    int kind = p->spec->cell_kind[col * p->spec->rows + row];
    if (kind == KNOB_PANEL_EMPTY) return;
    if (kind == KNOB_PANEL_EXTRA) {
        if (p->spec->build_extra) p->spec->build_extra(cell, p->spec->ctx);
        return;
    }
    if (kind < 0 || kind >= p->spec->knob_count) return;
    knob_build(p, cell, kind);
}

/* ---------------------------------------------------------------------- ink */

/* The white page paints a knob's cell the way it paints a button - the knob plate, with the cap
 * as a flat disc - and that is knob_cell's; this is only the walk over the cells, then the
 * Group's own plate, so the block reads as one surface rather than dials beside a plate. */
static void panel_set_page(bool white, void *ctx)
{
    knob_panel_t *p = ctx;
    if (!p) return;

    for (int id = 0; id < p->spec->knob_count; id++)
        if (p->cells[id].cell) knob_cell_set_page(&p->cells[id], white);

    if (p->spec->page_extra) p->spec->page_extra(white, p->spec->ctx);
}

/* ------------------------------------------------------------------- the gate */

/* The bar reports its wish: the panel is out, or it is being taken off the bar. The bank follows
 * it so a tap on a cell, and a tap after a swap, can only ever reach the knobs that are on
 * screen - and losing the panel ends the editor or disarms the pending open.
 *
 * The exception is the swap the editor itself makes. It puts the Pad where these cells were and
 * edits *these* values, so the bank has to survive it; only the cells are gone, which is why the
 * hooks above test the shown flag before touching one. */
static void panel_shown(bool visible, void *ctx)
{
    knob_panel_t *p = ctx;
    if (!p) return;

    p->shown = visible;
    if (!visible && knob_editor_is_editing()) return;
    knob_editor_set_bank(visible ? &p->bank : NULL);
}

/* -------------------------------------------------------------------- public */

void knob_panel_init(knob_panel_t *panel, const knob_panel_spec_t *spec, knob_t *knobs,
                     knob_cell_t *cells)
{
    if (!panel || !spec || !knobs || !cells) return;

    panel->spec = spec;
    panel->knobs = knobs;
    panel->cells = cells;
    panel->shown = false;

    /* A knob's name is the one part of it this module sets: it is panel text, not the model's
     * arithmetic, and both Groups would otherwise repeat the same loop. Ranges, steps and marks
     * stay the Group's, because those are what its publishing is about. */
    for (int id = 0; id < spec->knob_count; id++) knob_set_name(&knobs[id], spec->names[id]);

    panel->bank = (knob_editor_bank_t){
        .knobs = knobs,
        .count = spec->knob_count,
        .repaint = bank_repaint,
        .publish = bank_publish,
        .open_allowed = bank_open_allowed,
        .ctx = panel,
    };

    panel->value = (cassette_panel_t){
        .columns = spec->columns,
        .rows = spec->rows,
        .cell_rect = panel_cell_rect,
        .gesture_bubble = false,
        .inert = false,
        .build = panel_build,
        .set_page = panel_set_page,
        .shown = panel_shown,
        .ctx = panel,
    };
}

const cassette_panel_t *knob_panel_value(knob_panel_t *panel)
{
    return panel ? &panel->value : NULL;
}

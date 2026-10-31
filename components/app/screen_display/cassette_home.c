/*
 * The Home group's four keys; see cassette_home.h.
 *
 * The bar owns the carcases, the lattice, the hit rule and the pages; this module is the table
 * that puts four of the buttons in them and the merge that makes the top and bottom keys two
 * cells wide. Nothing here allocates: the handles are static, and the objects they point at are
 * the bar's, deleted and rebuilt every time the panel is installed.
 *
 * The handles are the build's and only the build's. A key the bar could not build is left zeroed,
 * so a page flip can never ink the handle a previous install left behind - and nothing clears the
 * handles on the way out, because the bar's shown(false) reports its *wish* and is also called
 * once for a panel that lands while the bars are still away, when the cells are alive and
 * clickable. Clearing them there is how the keys came out dead on the first boot and only worked
 * after another group and back.
 *
 * The cells are addressed the way the bar walks them - (col,row) over the panel's own grid, in
 * column-major order - and the merge is a table, not a special case in the walk:
 *
 *   (0,0) key 1, both columns   (1,0) covered
 *   (0,1) key 2                 (1,1) key 3
 *   (0,2) key 4, both columns   (1,2) covered
 *
 * The covered cells return an empty rect, which is the bar's own signal to build nothing there,
 * so the merge costs cassette_bars nothing at all. Called under the LVGL lock.
 */
#include "cassette_home.h"

#include <stdint.h>

#include "cassette_button.h"
#include "cassette_icons.h"
#include "cassette_layout.h"
#include "cassette_style.h"

enum { HOME_KEYS = 4 };

/* The panel's shape: the top and bottom keys take both of the bar's columns, the middle pair one
 * each. Indexed by the bar's row. A wide key is a plate with its glyph on the right - the owner's
 * call - so the two on this axis are also the two with no dome. */
static const bool k_wide[3] = { true, false, true };

/* The key at a cell, or -1 where a merge has covered it: indexed [col][row] over the panel's 2x3
 * grid, which is the order the bar walks it in. */
static const int8_t k_cell_key[2][3] = {
    { 0, 1, 3 },     /* the inside column: key 1, key 2, key 4 */
    { -1, 2, -1 },   /* the outer column: covered, key 3, covered */
};

/* The four keys in their own id order - which is how they read: top, the middle pair, bottom.
 * `name` is identity and is never drawn; `glyph` is what the dome draws, the owner's four from
 * the icon font. What each one does is still to come; the glyph is the name of the thing. */
static const struct {
    const char *name;
    const char *glyph;
} k_key[HOME_KEYS] = {
    { "PLAY", CASSETTE_ICON_PLAY },
    { "PREV", CASSETTE_ICON_PREV },
    { "NEXT", CASSETTE_ICON_NEXT },
    { "MIC", CASSETTE_ICON_MIC },
};

static cassette_button_t s_key[HOME_KEYS];
static bool s_created;

/* A cell of the panel: the bar's rect, or two of them merged into one 190x96 carcase, or nothing
 * where a merged key already covers the cell. */
static cassette_rect_t home_cell_rect(int col, int row)
{
    cassette_rect_t none = { 0, 0, 0, 0 };
    if (col < 0 || col > 1 || row < 0 || row > 2) return none;
    if (k_cell_key[col][row] < 0) return none;   /* a merged key already covers this cell */

    cassette_rect_t cell = cassette_cell_rect(CASSETTE_BAR_RIGHT, col, row);
    if (k_wide[row]) cell.w += CASSETTE_CELL_PITCH;
    return cell;
}

/* One cell: the shared button inside the bar's carcase, with the key's glyph in the icon font. No
 * action yet, so a click is the pulse alone; `disc_right` is what puts the ink in the outer
 * column of a merged cell, and `bare` is what takes the dome off it - a wide key is a plate and a
 * glyph, and a square one is a domed button. */
static void home_build(lv_obj_t *cell, int col, int row, void *ctx)
{
    (void)ctx;
    if (col < 0 || col > 1 || row < 0 || row > 2) return;

    int key = k_cell_key[col][row];
    if (key < 0) return;

    /* A carcase the bar could not build leaves no key: zeroed, so set_page() has nothing to ink
     * and the last install's pointer cannot come back. */
    if (!cell) {
        s_key[key] = (cassette_button_t){ 0 };
        return;
    }

    /* The white page's idle disc: the palette's neutral cap, as the mode bar's buttons have. */
    cassette_cap_t cap = { k_col_dome, k_col_bg };
    cassette_button_spec_t spec = {
        .name = k_key[key].name,
        .text = k_key[key].glyph,
        .font = &xtape_36,
        .cap = cap,
        .carcase_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED },
        .disc_ring = { CASSETTE_BUTTON_RING_W, CASSETTE_BUTTON_RING_W_PRESSED },
        .disc_right = k_wide[row],
        .bare = k_wide[row],
        .on_click = NULL,
        .ctx = NULL,
    };
    /* The page is not set here: the bar inks the cells it has just built, so the panel's
     * set_page() lands on every key before anything can be touched. */
    cassette_button_create(&s_key[key], cell, &spec);
}

/* The keys' page ink: the bar has painted the carcases' default, this is the buttons' own rings,
 * caps and glyphs. */
static void home_set_page(bool white, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < HOME_KEYS; i++) cassette_button_set_page(&s_key[i], white);
}

/* Two columns of three, of which the ends of the inside column take both columns: four carcases,
 * the border shared on the 94 px pitch like the EQ's cells. The keys are the whole surface's
 * controls, so the carcases are clickable and a swipe that starts on one is not the ladder's.
 * Two of them are domed buttons and two are bare plates; the difference is the key's, not the
 * panel's, so it is two flags in the spec rather than two kinds of cell. */
static const cassette_panel_t k_home_panel = {
    .columns = 2,
    .rows = 3,
    .cell_rect = home_cell_rect,
    .gesture_bubble = false,
    .build = home_build,
    .set_page = home_set_page,
    .shown = NULL,
    .ctx = NULL,
};

void cassette_home_create(void)
{
    /* Nothing to allocate and nothing to arm: the cells are the bar's, and the actions are not
     * wired yet. It exists because the composition builds every panel before installing one
     * (ADR 0042), and because that is where the four clicks will be wired. */
    if (s_created) return;
    s_created = true;
    for (int i = 0; i < HOME_KEYS; i++) s_key[i] = (cassette_button_t){ 0 };
}

const cassette_panel_t *cassette_home_panel(void)
{
    return &k_home_panel;
}

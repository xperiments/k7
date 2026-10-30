/*
 * The EQ group of the right bar; see cassette_eq.h.
 *
 * The panel fills all six of the bar's cells, two columns of three:
 *
 *   [VU  ] [HI]
 *   [VU  ] [MID]
 *   [Gain] [LOW]
 *
 * The inside column is the VU meter - one 96x190 plate over the top two cells - and the gain,
 * and the outer column is the three bands. There is no Back cell: the mode bar's sliders button
 * is the toggle, so a tap on it puts the group away. The four knobs are a 30% smaller 44 px cap
 * with a ring of marks outside it - no dots - and a line from the centre towards the rim; the
 * marks are the two ends and 0 dB's twelve o'clock, and the value marker is the brighter one
 * that points at the value. The VU plate is cassette_meter's, two 84 px rows.
 *
 * A knob's value moves by dragging it up and down - 2 px a step, with a magnet at 0 dB - and a
 * tap on it opens the 3x dial, which prints the two ends (-26 and +6, or -12 and +12), the
 * graduations, and the live value in its pillow.
 *
 * The shell around those dials - the grid, the five handlers that make a dial draggable and
 * tappable, the editor's bank and the shown gate - is knob_panel's, shared with the RGB group.
 * What is left here is this group's data: which knob sits in which cell, the two dB ranges with
 * their marks, the audio a moved value publishes, and the meter that fills the plate. That is the
 * whole of the difference between the two groups, which is why the panels were one file's worth
 * of code twice.
 */
#include "cassette_eq.h"

#include "lvgl.h"

#include "cassette_meter.h"
#include "knob.h"
#include "knob_panel.h"
#include "mini_icecast.h"

/* The knobs, in the bank's id order: the inside cell's gain, then the outer column's three
 * bands. The ids are contiguous because the editor indexes its bank by them. */
enum {
    KNOB_GAIN = 0,
    KNOB_HI,
    KNOB_MID,
    KNOB_LOW,
    KNOB_COUNT
};

/* The panel's control at one bar cell, in the bar's (col,row) order:
 *
 *   [VU  ] [HI ]
 *   [VU  ] [MID]
 *   [Gain] [LOW]
 *
 * The meter is the plate and the cell under it is covered, so that cell is never built.
 */
static const int8_t k_cell_kind[2][3] = {
    { KNOB_PANEL_EXTRA, KNOB_PANEL_EMPTY, KNOB_GAIN },
    { KNOB_HI, KNOB_MID, KNOB_LOW },
};

/* Which audio band a knob drives: HI=2, MID=1, LOW=0. The gain is not a band. */
static const int8_t k_knob_band[KNOB_COUNT] = { -1, 2, 1, 0 };

/* The controls' identities: what the 3x dial's pillow says, and what the mixer calls them. */
static const char *const k_knob_name[KNOB_COUNT] = { "GAIN", "HI", "MID", "LOW" };

/* What the cell's name pillow prints, one letter each: the pillow is a badge on a 96 px cell,
 * and the 3x dial is where the names are spelled out (ADR 0035). */
static const char *const k_knob_short[KNOB_COUNT] = { "G", "H", "M", "L" };

/* The two ends of each knob, printed on the 3x dial at their own angles: the bands cut to
 * -26 and boost to +6, the trim is symmetric. knob_label() reads these back when the knob
 * sits on one, so it is a marked position as well as a plaque. */
static const knob_mark_t k_band_marks[] = {
    { 0, "-26", true },
    { 100, "+6", true },
};
static const knob_mark_t k_gain_marks[] = {
    { 0, "-12", true },
    { 100, "+12", true },
};

static knob_t s_knob[KNOB_COUNT];     /* 0..100, 50 = 0 dB; the model owns the maths */
static knob_cell_t s_cell[KNOB_COUNT];
static knob_panel_t s_panel;
static bool s_created;

/* --------------------------------------------------------------------- audio */

static void knob_send(int id)
{
    float db = knob_db(&s_knob[id]);
    if (id == KNOB_GAIN) mini_icecast_gain_set(db);
    else mini_icecast_eq_set(k_knob_band[id], db);
}

/* --------------------------------------------------------------------- plate */

/* The meter is the panel's one cell that is not a knob. Its columns are fed by the cassette
 * frame and its inks turn with the page, both of which cassette_meter owns. */
static void meter_build(lv_obj_t *cell, void *ctx)
{
    (void)ctx;
    cassette_meter_create(cell);
}

static void meter_page(bool white, void *ctx)
{
    (void)ctx;
    cassette_meter_set_page(white);
}

/* ---------------------------------------------------------------------- data */

/* A value moved or was reset: the gain or one band, whichever the id names. The shell has
 * already refused an id its own bank does not have. */
static void publish(int id, void *ctx)
{
    (void)ctx;
    knob_send(id);
}

/* The panel: two columns of three, the inside column's top two cells the meter's plate. The
 * cells do not bubble gestures - a swipe that starts on a knob is not a ladder gesture - and the
 * page ink is the knobs' plate, which knob_panel paints. */
static const knob_panel_spec_t k_spec = {
    .columns = 2,
    .rows = 3,
    .knob_count = KNOB_COUNT,
    .cell_kind = &k_cell_kind[0][0],
    .names = k_knob_name,
    .shorts = k_knob_short,
    .value_pill_y = NULL,
    .build_extra = meter_build,
    .page_extra = meter_page,
    .publish = publish,
    .ctx = NULL,
};

/* -------------------------------------------------------------------- public */

void cassette_eq_create(void)
{
    if (s_created) return;
    s_created = true;

    knob_init(&s_knob[KNOB_GAIN], KNOB_RANGE_GAIN);
    knob_set_marks(&s_knob[KNOB_GAIN], k_gain_marks, 2);
    for (int id = 1; id < KNOB_COUNT; id++) {
        knob_init(&s_knob[id], KNOB_RANGE_BAND);
        knob_set_marks(&s_knob[id], k_band_marks, 2);
    }

    /* The shell names the knobs and holds the bank; the values and the audio stay here. */
    knob_panel_init(&s_panel, &k_spec, s_knob, s_cell);

    /* The audio starts from the same values; this makes a rebuilt screen agree with it. */
    for (int id = 0; id < KNOB_COUNT; id++) knob_send(id);
}

const cassette_panel_t *cassette_eq_panel(void)
{
    return knob_panel_value(&s_panel);
}

/*
 * The RGB group's panel; see cassette_rgb.h.
 *
 * The EQ panel's twin: the same shell, a different table. It fills all six of the bar's cells,
 * two columns of three:
 *
 *     [Effect] [Red  ]
 *     [Effect] [Green]
 *     [Bright] [Blue ]
 *
 * the inside column the effect - one 96x190 plate, its dial above and its name below - and the
 * brightness, the outer column the three channels. There is no Back cell: the mode bar's led
 * button is the toggle.
 *
 * Everything the panel shares with the EQ is knob_panel's: the grid, the dials, the five
 * handlers, the editor's bank and the shown gate. Everything a knob *is* - the dial itself, the
 * 3x surface, the tapped-open ordering - is knob_cell's, knob_editor's and knob_model's.
 * Everything the strip *is* - the channels, the brightness, the effects and their frames - is
 * rgb.h's. This file is the table between them: which knob is in which cell, the effect's six
 * marked slots, the state a move recomposes, and the publish seam.
 *
 * The effect selector is the one stepped knob: its six slots are marked positions carrying the
 * effect's own name, so the 3x dial's pillow says RAINBOW rather than EFFECT, and the 1x plate
 * shows the same name in a bottom pillow under the dial. The group's look is due a rework; this
 * module only carries the new knob vocabulary (ADR 0027).
 *
 * Called under the LVGL lock, like everything else on the screen.
 */
#include "cassette_rgb.h"

#include "knob.h"
#include "knob_panel.h"

/* The bottom pillow's y inside the effect's tall plate: below the dial, in the room the covered
 * cell would have taken. */
#define FX_PILL_Y 136

_Static_assert(RGB_EFFECT_COUNT <= KNOB_MARKS_MAX, "the marks array has a slot per effect");

/* The knobs, in the bank's id order: the three channels, the strip's brightness, the effect. */
enum {
    KNOB_RED = 0,
    KNOB_GREEN,
    KNOB_BLUE,
    KNOB_BRIGHT,
    KNOB_FX,
    KNOB_COUNT
};

/* The panel's control at one bar cell, in the bar's (col,row) order:
 *
 *   [Effect] [Red  ]
 *   [Effect] [Green]
 *   [Bright] [Blue ]
 *
 * The effect's plate is its dial and the cell under it is covered, so that cell is never built.
 */
static const int8_t k_cell_kind[2][3] = {
    { KNOB_FX, KNOB_PANEL_EMPTY, KNOB_BRIGHT },
    { KNOB_RED, KNOB_GREEN, KNOB_BLUE },
};

/* The controls' identities, which is what the 3x dial's pillow says. The effect's own name is
 * not a name but a marked position: it rides knob_label() instead. */
static const char *const k_knob_name[KNOB_COUNT] = { "RED", "GREEN", "BLUE", "BRIGHT", "EFFECT" };

/* What the cell's name pillow prints: the channels are their letters and the two long names
 * shorten, because the pillow is a badge on a 96 px cell (ADR 0035). */
static const char *const k_knob_short[KNOB_COUNT] = { "R", "G", "B", "BRI", "FX" };

/* Only the effect has a bottom pillow: it is this panel's one stepped knob, and the selected
 * slot's name is the only text it has to show (ADR 0027). */
static const int16_t k_value_pill_y[KNOB_COUNT] = { -1, -1, -1, -1, FX_PILL_Y };

static knob_t s_knob[KNOB_COUNT];
static knob_cell_t s_cell[KNOB_COUNT];
static knob_panel_t s_panel;
static rgb_state_t s_state;           /* what the knobs last published */
static knob_mark_t s_fx_marks[RGB_EFFECT_COUNT];   /* one per effect, at its slot */
static cassette_rgb_publish_t s_publish;
static void *s_publish_ctx;
static bool s_created;

/* --------------------------------------------------------------------- publish */

/* The knobs are the truth; the state is a reading of them. */
static void rgb_compose(void)
{
    s_state.red = knob_value(&s_knob[KNOB_RED]);
    s_state.green = knob_value(&s_knob[KNOB_GREEN]);
    s_state.blue = knob_value(&s_knob[KNOB_BLUE]);
    s_state.brightness = knob_value(&s_knob[KNOB_BRIGHT]);
    s_state.effect = rgb_effect_at(knob_step(&s_knob[KNOB_FX]));
}

static void rgb_publish(void)
{
    rgb_compose();
    if (s_publish) s_publish(&s_state, s_publish_ctx);
}

/* A value moved or was reset: the strip's state is a reading of all five knobs, so it is
 * recomposed rather than patched, and the whole of it goes out. The shell has already refused an
 * id its own bank does not have. */
static void publish(int id, void *ctx)
{
    (void)id;
    (void)ctx;
    rgb_publish();
}

/* ---------------------------------------------------------------------- data */

/* The panel: two columns of three, the inside column's top two cells the effect's plate. The
 * cells do not bubble gestures - a swipe that starts on a knob is not a ladder gesture - and the
 * page ink is the knobs' plate, which knob_panel paints. There is no extra cell and no extra
 * ink, so both hooks are NULL. */
static const knob_panel_spec_t k_spec = {
    .columns = 2,
    .rows = 3,
    .knob_count = KNOB_COUNT,
    .cell_kind = &k_cell_kind[0][0],
    .names = k_knob_name,
    .shorts = k_knob_short,
    .value_pill_y = k_value_pill_y,
    .build_extra = NULL,
    .page_extra = NULL,
    .publish = publish,
    .ctx = NULL,
};

/* -------------------------------------------------------------------- public */

void cassette_rgb_create(void)
{
    if (s_created) return;
    s_created = true;

    s_publish = NULL;
    s_publish_ctx = NULL;

    /* The channels and the brightness are level knobs, neutral at full; the effect is a stepped
     * knob with one slot per effect in rgb.h's list, neutral at OFF. */
    knob_init_unit(&s_knob[KNOB_RED]);
    knob_init_unit(&s_knob[KNOB_GREEN]);
    knob_init_unit(&s_knob[KNOB_BLUE]);
    knob_init_unit(&s_knob[KNOB_BRIGHT]);
    knob_init_steps(&s_knob[KNOB_FX], rgb_effect_count(), (int)RGB_EFFECT_OFF);

    /* Every slot of the selector is a marked position carrying the effect's own name, printed
     * as a plaque beside its position. That is the whole of the dial's text: a selector has no
     * title pillow, because the filled plaque already says which one is on (ADR 0029). */
    for (int slot = 0; slot < RGB_EFFECT_COUNT; slot++) {
        knob_set_step(&s_knob[KNOB_FX], slot);
        s_fx_marks[slot].value = knob_value(&s_knob[KNOB_FX]);
        s_fx_marks[slot].text = rgb_effect_name(rgb_effect_at(slot));
        s_fx_marks[slot].plaque = true;
    }
    knob_set_marks(&s_knob[KNOB_FX], s_fx_marks, RGB_EFFECT_COUNT);
    knob_reset(&s_knob[KNOB_FX]);   /* the slot walk left it on the last one */

    /* The shell names the knobs and holds the bank; the values and the strip stay here. */
    knob_panel_init(&s_panel, &k_spec, s_knob, s_cell);

    rgb_compose();
}

void cassette_rgb_set_publish(cassette_rgb_publish_t cb, void *ctx)
{
    s_publish = cb;
    s_publish_ctx = ctx;
    /* A hook installed after the panel is up still gets the values that are already there. */
    if (cb) {
        rgb_compose();
        cb(&s_state, ctx);
    }
}

const cassette_panel_t *cassette_rgb_panel(void)
{
    return knob_panel_value(&s_panel);
}

/*
 * The knobs' dial, without LVGL.
 *
 * The view used to keep the dB law, the angle, the crown's span and the reset as file
 * statics in cassette_eq.c, which the host tier may not compile. This pins the moved
 * arithmetic - the two dB curves and their anchors, the one 270 degree arc and its sign, the
 * labelled positions and their magnet, the drag, the clamps and the reset - and the edit
 * session's ordering: the delayed open, the reset gesture's swallowed click, and a close
 * that ends both a pending open and an open one.
 *
 * ADR 0027 is the arc these assert: no seam, cut at 7:30 and boost at 4:30, a drag for the
 * value and no angle to aim at.
 */
#include "test_util.h"

#include <math.h>
#include <string.h>

#include "knob.h"

/* ---------------------------------------------------------------------- helpers */

static float db_at(knob_range_t range, int value)
{
    knob_t k;
    knob_init(&k, range);
    knob_set(&k, value);
    return knob_db(&k);
}

static void db_near(knob_range_t range, int value, float want)
{
    float got = db_at(range, value);
    CHECK(fabsf(got - want) < 1e-5f, "dB at %d: got %g, want %g", value, (double)got,
          (double)want);
}

static int angle_at(int value)
{
    knob_t k;
    knob_init(&k, KNOB_RANGE_GAIN);
    knob_set(&k, value);
    return knob_angle_ddeg(&k);
}

/* A gain knob with its two ends labelled, the way cassette_eq builds one. */
static const knob_mark_t k_gain_marks[] = {
    { 0, "-12", true },
    { 100, "+12", true },
};

/* The RGB group's six effects, unplaqued: their names live in the dial's pillow. */
static const knob_mark_t k_fx_marks[] = {
    { 0, "OFF", false },     { 20, "SOLID", false }, { 40, "BREATHE", false },
    { 60, "RAINBOW", false }, { 80, "CHASE", false }, { 100, "LEVEL", false },
};

static void gain_knob(knob_t *k)
{
    knob_init(k, KNOB_RANGE_GAIN);
    knob_set_name(k, "GAIN");
    knob_set_marks(k, k_gain_marks, 2);
}

static bool effect_is_none(knob_edit_effect_t e)
{
    return !e.arm_timer && !e.cancel_timer && !e.reset_knob && !e.open_editor &&
           !e.close_editor && e.id == -1;
}

/* ------------------------------------------------------------------------- tests */

static void test_db_anchors(void)
{
    knob_t k;

    knob_init(&k, KNOB_RANGE_GAIN);
    CHECK_EQ(knob_value(&k), KNOB_DETENT, "a new knob is at the detent");
    CHECK(knob_db(&k) == 0.0f, "the detent is exactly 0 dB, got %g", (double)knob_db(&k));
    CHECK(!knob_set(&k, KNOB_DETENT), "setting the detent again does not move");

    /* Gain: symmetric -12..+12. */
    CHECK(db_at(KNOB_RANGE_GAIN, 0) == -12.0f, "gain at 0: got %g",
          (double)db_at(KNOB_RANGE_GAIN, 0));
    CHECK(db_at(KNOB_RANGE_GAIN, 25) == -6.0f, "gain at 25: got %g",
          (double)db_at(KNOB_RANGE_GAIN, 25));
    CHECK(db_at(KNOB_RANGE_GAIN, 50) == 0.0f, "gain at 50: got %g",
          (double)db_at(KNOB_RANGE_GAIN, 50));
    CHECK(db_at(KNOB_RANGE_GAIN, 75) == 6.0f, "gain at 75: got %g",
          (double)db_at(KNOB_RANGE_GAIN, 75));
    CHECK(db_at(KNOB_RANGE_GAIN, 100) == 12.0f, "gain at 100: got %g",
          (double)db_at(KNOB_RANGE_GAIN, 100));

    /* Bands: -26..+6, the cut side wider. The detent is 0 dB for all three. */
    CHECK(db_at(KNOB_RANGE_BAND, 0) == -26.0f, "band at 0: got %g",
          (double)db_at(KNOB_RANGE_BAND, 0));
    CHECK(db_at(KNOB_RANGE_BAND, 25) == -13.0f, "band at 25: got %g",
          (double)db_at(KNOB_RANGE_BAND, 25));
    CHECK(db_at(KNOB_RANGE_BAND, 50) == 0.0f, "band at 50: got %g",
          (double)db_at(KNOB_RANGE_BAND, 50));
    CHECK(db_at(KNOB_RANGE_BAND, 75) == 3.0f, "band at 75: got %g",
          (double)db_at(KNOB_RANGE_BAND, 75));
    CHECK(db_at(KNOB_RANGE_BAND, 100) == 6.0f, "band at 100: got %g",
          (double)db_at(KNOB_RANGE_BAND, 100));
}

static void test_db_law(void)
{
    /* One value step is max/50 on each side, and the spread from the detent is the whole
     * half-range; both fall out of the one expression, with no per-range branch for gain. */
    db_near(KNOB_RANGE_GAIN, 51, 12.0f / 50.0f);
    db_near(KNOB_RANGE_GAIN, 49, -12.0f / 50.0f);
    db_near(KNOB_RANGE_BAND, 51, 6.0f / 50.0f);
    db_near(KNOB_RANGE_BAND, 49, -26.0f / 50.0f);

    CHECK(db_at(KNOB_RANGE_GAIN, 100) - db_at(KNOB_RANGE_GAIN, 50) == 12.0f,
          "the gain's boost side spans +12");
    CHECK(db_at(KNOB_RANGE_GAIN, 50) - db_at(KNOB_RANGE_GAIN, 0) == 12.0f,
          "and its cut side spans -12");
    CHECK(db_at(KNOB_RANGE_BAND, 100) - db_at(KNOB_RANGE_BAND, 50) == 6.0f,
          "the band's boost side spans +6");
    CHECK(db_at(KNOB_RANGE_BAND, 50) - db_at(KNOB_RANGE_BAND, 0) == 26.0f,
          "and its cut side spans -26");

    /* Monotone over the whole dial, both ranges. */
    for (int range = KNOB_RANGE_GAIN; range <= KNOB_RANGE_BAND; range++) {
        for (int v = KNOB_MIN; v < KNOB_MAX; v++) {
            CHECK(db_at((knob_range_t)range, v + 1) > db_at((knob_range_t)range, v),
                  "range %d is not monotone at %d", range, v);
        }
    }
}

/* One arc for every sweep: 270 degrees, min at 7:30, max at 4:30, twelve o'clock at 50. */
static void test_angle(void)
{
    CHECK_EQ(angle_at(0), -1350, "0 is 7:30, and the cut side is on the left");
    CHECK_EQ(angle_at(25), -675, "25 is halfway down the cut side");
    CHECK_EQ(angle_at(50), 0, "the detent is twelve o'clock");
    CHECK_EQ(angle_at(75), 675, "75 is halfway up the boost side");
    CHECK_EQ(angle_at(100), 1350, "100 is 4:30");

    /* 2.7 degrees per unit, against the value. */
    CHECK_EQ(angle_at(51) - angle_at(50), 27, "one step is 27 tenths of a degree");
    CHECK_EQ(angle_at(50) - angle_at(49), 27, "and it is signed clockwise-positive");

    /* Both ends are places, not one seam: the sweep covers the 270 degree arc. */
    CHECK_EQ(angle_at(100) - angle_at(0), 2700, "the arc is 270 degrees end to end");

    CHECK_EQ(knob_angle_for_value(-5), -1350, "a value below the floor clamps to 7:30");
    CHECK_EQ(knob_angle_for_value(200), 1350, "a value above the ceiling clamps to 4:30");

    /* A level knob rides the same arc, so the two only differ in what the value means. */
    knob_t k;
    knob_init_unit(&k);
    CHECK_EQ(knob_angle_ddeg(&k), 1350, "a level knob starts at 4:30");
    knob_set(&k, 0);
    CHECK_EQ(knob_angle_ddeg(&k), -1350, "and off is 7:30");
}

/* The labelled positions: what the dial's pillow says, and which of them the dial prints. */
static void test_marks(void)
{
    knob_t k;
    gain_knob(&k);

    CHECK_EQ(knob_mark_count(&k), 2, "two labelled ends");
    CHECK(knob_mark_at(&k, 0) && knob_mark_at(&k, 0)->value == 0, "the first is the cut end");
    CHECK(strcmp(knob_mark_at(&k, 0)->text, "-12") == 0, "and it says -12");
    CHECK(knob_mark_at(&k, 0)->plaque, "which the dial prints");
    CHECK(knob_mark_at(&k, 2) == NULL, "past the end there is no mark");
    CHECK(knob_mark_at(&k, -1) == NULL, "and neither before it");

    CHECK(strcmp(knob_label(&k), "GAIN") == 0, "at rest the pillow says the knob's name");
    knob_set(&k, 0);
    CHECK(strcmp(knob_label(&k), "-12") == 0, "at the cut end it says that end's value");
    knob_set(&k, 100);
    CHECK(strcmp(knob_label(&k), "+12") == 0, "and at the boost end its value");
    knob_set(&k, 37);
    CHECK(strcmp(knob_label(&k), "GAIN") == 0, "anywhere between, the name again");

    /* Clearing and clamping. Too many marks is a bug, not a crash. */
    knob_set_marks(&k, NULL, 4);
    CHECK_EQ(knob_mark_count(&k), 0, "NULL clears the marks");
    knob_set_marks(&k, k_fx_marks, 6);
    CHECK_EQ(knob_mark_count(&k), 6, "six fit");
    knob_set_marks(&k, k_fx_marks, 1);
    CHECK_EQ(knob_mark_count(&k), 1, "so does a subset");

    /* A stepped knob's marked slots make the pillow the selected value. */
    knob_t fx;
    knob_init_steps(&fx, 6, 0);
    knob_set_name(&fx, "EFFECT");
    knob_set_marks(&fx, k_fx_marks, 6);
    CHECK(knob_set_step(&fx, 3), "slot 3 moves");
    CHECK(strcmp(knob_label(&fx), "RAINBOW") == 0, "and the pillow says RAINBOW");
    CHECK(!knob_mark_at(&fx, 0)->plaque, "whose names are not printed round the dial");

    /* A knob with no name and no marks has nothing to say, and says it without crashing. */
    knob_t bare;
    knob_init(&bare, KNOB_RANGE_GAIN);
    CHECK(knob_label(&bare) == NULL, "an unnamed, unmarked knob has no label");
}

/* The magnet: a detent holds on, takes a push to leave, and pulls a value that lands near. */
static void test_snap(void)
{
    knob_t k;
    gain_knob(&k);

    /* Sitting on the detent, anything within the release band is pulled back onto it. */
    CHECK_EQ(knob_snap(&k, 50), 50, "the detent is its own place");
    CHECK_EQ(knob_snap(&k, 48), 50, "two away is pulled in");
    CHECK_EQ(knob_snap(&k, 45), 50, "five away still is, while the knob is on it");
    CHECK_EQ(knob_snap(&k, 44), 44, "six away is not: that is the escape");

    /* Away from it, only the narrower stick band pulls. */
    knob_set(&k, 44);
    CHECK_EQ(knob_snap(&k, 46), 46, "four away does not pull, off the detent");
    CHECK_EQ(knob_snap(&k, 52), 50, "two away does");

    /* The knob's own positions are magnets too: the marks, and its neutral. */
    knob_set(&k, 90);
    CHECK_EQ(knob_snap(&k, 98), 100, "the boost end pulls a value two away");
    knob_set(&k, 10);
    CHECK_EQ(knob_snap(&k, 2), 0, "and so does the cut end");
    CHECK_EQ(knob_snap(&k, 6), 6, "but not one five away");

    /* A level knob's home is full, and that is its detent. */
    knob_t lvl;
    knob_init_unit(&lvl);
    CHECK_EQ(knob_snap(&lvl, 98), 100, "a level knob sticks at full");
    knob_set(&lvl, 60);
    CHECK_EQ(knob_snap(&lvl, 57), 57, "and off it, thirty away is nobody's magnet");

    /* Every slot of a stepped knob is a place it can sit. */
    knob_t fx;
    knob_init_steps(&fx, 6, 0);
    knob_set(&fx, 40);
    CHECK_EQ(knob_snap(&fx, 39), 40, "a stepped knob pulls to its slot");
    CHECK_EQ(knob_snap(&fx, 61), 60, "and to the next one up");

    /* A knob with no neutral of its own beyond the detent still has one magnet. */
    knob_t bare;
    knob_init(&bare, KNOB_RANGE_BAND);
    CHECK_EQ(knob_snap(&bare, 52), 50, "the neutral is a magnet with no marks at all");
}

/* The drag: pixels of travel up boost the value, and a drag cannot land next to a detent. */
static void test_drag(void)
{
    knob_t k;
    gain_knob(&k);

    /* 2 px per step at 1x: ten pixels is five steps, which is inside the detent's release
     * band, so the knob stays home. */
    CHECK_EQ(knob_drag_value(&k, 50, 10, 2), 50, "a five-step drag does not escape the detent");
    CHECK_EQ(knob_drag_value(&k, 50, 12, 2), 56, "seven steps does");
    CHECK_EQ(knob_drag_value(&k, 50, -12, 2), 44, "and down is the cut side");

    /* While the knob is off the detent, a drag that lands near it is pulled exactly onto it,
     * and one that lands four steps out stays where it landed. */
    knob_set(&k, 40);
    CHECK_EQ(knob_drag_value(&k, 40, 24, 2), 50, "a drag to two past the detent is pulled in");
    knob_set(&k, 40);
    CHECK_EQ(knob_drag_value(&k, 40, 28, 2), 54, "four past it is not");

    /* The rails are walls, whatever the travel. */
    knob_set(&k, 100);
    CHECK_EQ(knob_drag_value(&k, 100, 40, 2), 100, "a drag past the top stops at full");
    knob_set(&k, 0);
    CHECK_EQ(knob_drag_value(&k, 0, -40, 2), 0, "and past the bottom at the floor");

    /* 4 px per step at 3x is the same drag at half the speed. */
    knob_set(&k, 50);
    CHECK_EQ(knob_drag_value(&k, 50, 24, 4), 56, "twenty-four pixels is six steps at 4 px");
    knob_set(&k, 50);
    CHECK_EQ(knob_drag_value(&k, 50, 60, 4), 65, "and sixty is fifteen");

    /* A nonsensical resolution is one step per pixel, not a division by zero. */
    knob_set(&k, 50);
    CHECK_EQ(knob_drag_value(&k, 50, 0, 0), 50, "no travel is no move");
    CHECK_EQ(knob_drag_value(&k, 50, 20, 0), 70, "and a zero resolution is a pixel a step");

    /* A stepped knob quantises on the way: a drag lands on slots, never between them. */
    knob_t fx;
    knob_init_steps(&fx, 6, 0);
    knob_set(&fx, 0);
    CHECK_EQ(knob_drag_value(&fx, 0, 42, 2), 20, "the drag quantises onto the next slot");
    knob_set(&fx, 0);
    CHECK_EQ(knob_drag_value(&fx, 0, 44, 2), 20, "and the next value up is still that slot");

    /* The band knob's own ends are labelled: dragging to the bottom lands on -26. */
    knob_t band;
    knob_init(&band, KNOB_RANGE_BAND);
    static const knob_mark_t band_marks[] = { { 0, "-26", true }, { 100, "+6", true } };
    knob_set_marks(&band, band_marks, 2);
    knob_set(&band, 30);
    CHECK_EQ(knob_drag_value(&band, 30, -58, 2), 0, "a long drag down reaches the floor");
    knob_set(&band, 30);
    CHECK_EQ(knob_drag_value(&band, 30, -50, 2), 5, "a shorter one stops short of it");
}

/* The tap's cycle: the stops a knob means to visit - its neutral, its marks, and every slot of
 * a stepped knob - in value order, wrapping at the top. The Pad's tap is what calls this. */
static void test_cycle(void)
{
    knob_t k;
    gain_knob(&k);

    /* -12, 0 dB, +12, and round again. */
    knob_set(&k, 0);
    CHECK_EQ(knob_cycle(&k), 50, "from the cut end the cycle goes to the detent");
    knob_set(&k, 50);
    CHECK_EQ(knob_cycle(&k), 100, "from the detent to the boost end");
    knob_set(&k, 100);
    CHECK_EQ(knob_cycle(&k), 0, "and off the boost end wraps to the cut end");

    /* Anywhere else it goes to the next stop above, not the nearest. */
    knob_set(&k, 30);
    CHECK_EQ(knob_cycle(&k), 50, "a value below the detent goes to it");
    knob_set(&k, 70);
    CHECK_EQ(knob_cycle(&k), 100, "and one above it goes to the boost end");
    knob_set(&k, 1);
    CHECK_EQ(knob_cycle(&k), 50, "just above the floor, the next stop is the detent");

    /* The band's marks are its own: -26 and +6 with 0 dB between them. */
    knob_t band;
    knob_init(&band, KNOB_RANGE_BAND);
    static const knob_mark_t band_marks[] = { { 0, "-26", true }, { 100, "+6", true } };
    knob_set_marks(&band, band_marks, 2);
    knob_set(&band, 0);
    CHECK_EQ(knob_cycle(&band), 50, "the band's first stop is its detent");
    knob_set(&band, 50);
    CHECK_EQ(knob_cycle(&band), 100, "then +6");

    /* A stepped knob walks its slots, one tap each, and wraps. */
    knob_t fx;
    knob_init_steps(&fx, 6, 0);
    knob_set_marks(&fx, k_fx_marks, 6);
    for (int slot = 0; slot < 5; slot++) {
        knob_set_step(&fx, slot);
        CHECK(knob_value(&fx) == slot * 20, "slot %d", slot);
        CHECK(knob_cycle(&fx) == (slot + 1) * 20, "a tap walks from slot %d to the next", slot);
    }
    CHECK(knob_set_step(&fx, 5), "the last slot");
    CHECK_EQ(knob_cycle(&fx), 0, "wraps to OFF");

    /* A knob with one stop only jumps home. */
    knob_t lvl;
    knob_init_unit(&lvl);
    knob_set(&lvl, 40);
    CHECK_EQ(knob_cycle(&lvl), 100, "a level knob's only stop is full");
    CHECK_EQ(knob_cycle(&lvl), 100, "and it stays there");
}

static void test_reset_and_clamp(void)
{
    knob_t k;
    knob_init(&k, KNOB_RANGE_BAND);

    CHECK(knob_set(&k, -5), "a value below the floor moves");
    CHECK_EQ(knob_value(&k), KNOB_MIN, "and clamps to 0");
    CHECK(knob_set(&k, 1000), "a value above the ceiling moves");
    CHECK_EQ(knob_value(&k), KNOB_MAX, "and clamps to 100");
    CHECK(!knob_set(&k, 1000), "the same clamped value does not move again");

    CHECK(knob_set(&k, 73), "73 is a move");
    CHECK_EQ(knob_value(&k), 73, "and lands there");
    CHECK(knob_reset(&k), "a reset from 73 moves");
    CHECK_EQ(knob_value(&k), KNOB_DETENT, "to the detent");
    CHECK(knob_db(&k) == 0.0f, "which is exactly 0 dB, got %g", (double)knob_db(&k));
    CHECK(!knob_reset(&k), "a reset at the detent reports no move");
}

/* The level sweep: the same arc, min 0 at 7:30 and max 100 at 4:30, home at full. The RGB
 * channels and the strip's brightness ride this one. */
static void test_level_sweep(void)
{
    knob_t k;

    knob_init_unit(&k);
    CHECK_EQ(knob_value(&k), KNOB_MAX, "a level knob starts full");
    CHECK(knob_unit(&k) == 1.0f, "which is 1.0, got %g", (double)knob_unit(&k));
    CHECK_EQ(knob_angle_ddeg(&k), 1350, "full is 4:30");
    CHECK(knob_set(&k, 0), "off moves");
    CHECK(knob_unit(&k) == 0.0f, "off is 0.0, got %g", (double)knob_unit(&k));
    CHECK_EQ(knob_angle_ddeg(&k), -1350, "off is 7:30");

    knob_set(&k, 50);
    CHECK_EQ(knob_angle_ddeg(&k), 0, "half is twelve o'clock");

    CHECK_EQ(knob_step(&k), -1, "a level knob has no step");
    CHECK(!knob_set_step(&k, 2), "and cannot be stepped");
    CHECK(knob_reset(&k), "a reset moves");
    CHECK_EQ(knob_value(&k), KNOB_MAX, "to full, its home");
}

/* The stepped sweep: the same arc, cut into equal detents. The effect selector. */
static void test_steps(void)
{
    knob_t k;

    knob_init_steps(&k, 6, 0);
    CHECK_EQ(knob_value(&k), 0, "a stepped knob starts on its neutral's slot");
    CHECK_EQ(knob_step(&k), 0, "slot 0");
    CHECK_EQ(knob_angle_ddeg(&k), -1350, "at the min end");

    CHECK(knob_set(&k, 23), "23 moves");
    CHECK_EQ(knob_value(&k), 20, "and quantises to the nearest slot");
    CHECK_EQ(knob_step(&k), 1, "which is slot 1");
    CHECK(!knob_set(&k, 24), "another value in the same slot does not move");

    CHECK(knob_set_step(&k, 5), "the last slot moves");
    CHECK_EQ(knob_value(&k), 100, "and is full");
    CHECK_EQ(knob_angle_ddeg(&k), 1350, "at 4:30");
    CHECK(!knob_set_step(&k, 5), "the same slot does not move");

    CHECK(knob_reset(&k), "a reset from full moves");
    CHECK_EQ(knob_value(&k), 0, "to the neutral, OFF");
    CHECK(!knob_reset(&k), "and a reset at the neutral does not");

    /* A slot outside the range clamps to the ends rather than vanishing. */
    CHECK(knob_set_step(&k, 99), "a slot past the end moves");
    CHECK_EQ(knob_value(&k), 100, "to full");
}

static void test_name(void)
{
    knob_t k;
    knob_init(&k, KNOB_RANGE_GAIN);
    CHECK(knob_name(&k) == NULL, "a fresh knob has no name");
    knob_set_name(&k, "GAIN");
    CHECK(knob_name(&k) && strcmp(knob_name(&k), "GAIN") == 0, "the name is carried");
    knob_set_name(&k, NULL);
    CHECK(knob_name(&k) == NULL, "the name can be cleared");
}

static void test_edit_ordering(void)
{
    knob_edit_t e;
    knob_edit_effect_t fx;

    knob_edit_init(&e);
    CHECK(!knob_edit_is_editing(&e), "a new editor is idle");
    CHECK_EQ(knob_edit_open_id(&e), -1, "with no open knob");
    CHECK(effect_is_none(knob_edit_delay_expired(&e)), "nothing pending opens nothing");

    fx = knob_edit_tap(&e, 2);
    CHECK(fx.arm_timer, "a tap arms the delayed open");
    CHECK_EQ(fx.id, 2, "for that knob");
    CHECK(!fx.cancel_timer && !fx.reset_knob && !fx.open_editor && !fx.close_editor,
          "and only arms");
    CHECK(!knob_edit_is_editing(&e), "a tap does not open the editor by itself");

    fx = knob_edit_delay_expired(&e);
    CHECK(fx.open_editor, "the delayed open opens the editor");
    CHECK_EQ(fx.id, 2, "for the pending knob");
    CHECK(knob_edit_is_editing(&e), "which is now editing");
    CHECK_EQ(knob_edit_open_id(&e), 2, "and reports the open id");

    CHECK(effect_is_none(knob_edit_tap(&e, 2)), "a tap while open is ignored");
    CHECK(effect_is_none(knob_edit_tap(&e, 3)), "and so is a tap on another knob");

    fx = knob_edit_close(&e);
    CHECK(fx.close_editor, "a close closes the editor");
    CHECK_EQ(fx.id, 2, "for the open knob");
    CHECK(!knob_edit_is_editing(&e), "and the editor is idle again");
    CHECK_EQ(knob_edit_open_id(&e), -1, "with no open knob");
    CHECK(effect_is_none(knob_edit_close(&e)), "closing an idle editor does nothing");
}

static void test_edit_reset_swallow(void)
{
    knob_edit_t e;
    knob_edit_effect_t fx;

    knob_edit_init(&e);
    CHECK(knob_edit_tap(&e, 0).arm_timer, "the first tap arms the open");

    fx = knob_edit_double_tap(&e, 0);
    CHECK(fx.cancel_timer, "the reset gesture disarms the pending open");
    CHECK(fx.reset_knob, "and asks for the reset");
    CHECK_EQ(fx.id, 0, "of that knob");
    CHECK(!fx.open_editor && !fx.close_editor, "and does not open or close an editor");
    CHECK(!knob_edit_is_editing(&e), "the virtual knob never appeared");

    CHECK(effect_is_none(knob_edit_tap(&e, 0)),
          "the click after the reset gesture is swallowed once");
    fx = knob_edit_tap(&e, 0);
    CHECK(fx.arm_timer && fx.id == 0, "the tap after that arms again");

    fx = knob_edit_delay_expired(&e);
    CHECK(fx.open_editor && fx.id == 0, "and the editor opens after the delay");

    /* While open, the same gesture resets and closes; the overlay has consumed its
     * click, so no click is owed and the next small-knob tap arms normally. */
    fx = knob_edit_double_tap(&e, 0);
    CHECK(fx.reset_knob, "a double tap while open resets");
    CHECK(fx.close_editor, "and closes the editor");
    CHECK(fx.cancel_timer, "and disarms the timer");
    CHECK(!knob_edit_is_editing(&e), "leaving it idle");
    CHECK(knob_edit_tap(&e, 0).arm_timer, "with no stale swallowed click");

    /* A call for a knob that is not the open one does nothing. */
    knob_edit_init(&e);
    knob_edit_tap(&e, 1);
    knob_edit_delay_expired(&e);
    CHECK(effect_is_none(knob_edit_double_tap(&e, 2)),
          "a double tap on a closed knob while another is open does nothing");
    CHECK_EQ(knob_edit_open_id(&e), 1, "the open knob is unchanged");
}

static void test_edit_close_pending(void)
{
    knob_edit_t e;
    knob_edit_effect_t fx;

    knob_edit_init(&e);
    knob_edit_tap(&e, 3);

    /* The block hides or the Pad covers it while the delayed open is still armed: the
     * close must disarm the timer and forget the pending knob. */
    fx = knob_edit_close(&e);
    CHECK(fx.cancel_timer, "a close disarms a pending open");
    CHECK_EQ(fx.id, 3, "for that knob");
    CHECK(!fx.close_editor, "with nothing open to close");
    CHECK(effect_is_none(knob_edit_delay_expired(&e)),
          "a fire after the close cannot open the knob");

    CHECK(knob_edit_tap(&e, 3).arm_timer, "and a later tap arms a fresh open");
}

int main(void)
{
    test_db_anchors();
    test_db_law();
    test_angle();
    test_marks();
    test_snap();
    test_drag();
    test_cycle();
    test_reset_and_clamp();
    test_level_sweep();
    test_steps();
    test_name();
    test_edit_ordering();
    test_edit_reset_swallow();
    test_edit_close_pending();

    TEST_END();
}

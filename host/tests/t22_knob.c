/*
 * The DJ block's knob, without LVGL.
 *
 * The view used to keep the dB law, the angle, its inverse, the crown's span and the
 * reset as file statics in cassette_eq.c, which the host tier may not compile. This pins
 * the moved arithmetic - the two dB curves and their anchors, the angle in tenths of a
 * degree and its sign, the ring's inverse and the bottom seam, the crown's lit run, the
 * clamps and the reset - and the edit session's ordering: the delayed open, the reset
 * gesture's swallowed click, and a close that ends both a pending open and an open one.
 */
#include "test_util.h"

#include <math.h>
#include <string.h>

#include "knob.h"

#define TEST_PI 3.14159265358979323846f

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

static void point_at(int value, int r, int *dx, int *dy)
{
    float rad = (float)angle_at(value) / 10.0f * TEST_PI / 180.0f;
    *dx = (int)lroundf((float)r * sinf(rad));
    *dy = -(int)lroundf((float)r * cosf(rad));
}

static int lit_count(int value, int dot_count)
{
    knob_t k;
    knob_init(&k, KNOB_RANGE_BAND);
    knob_set(&k, value);

    int n = 0;
    for (int i = 0; i < dot_count; i++)
        if (knob_dot_lit(&k, i, dot_count)) n++;
    return n;
}

/* The first and last lit index, or -1/-1 when nothing is lit. */
static void lit_run(int value, int dot_count, int *first, int *last)
{
    knob_t k;
    knob_init(&k, KNOB_RANGE_BAND);
    knob_set(&k, value);

    *first = -1;
    *last = -1;
    for (int i = 0; i < dot_count; i++) {
        if (!knob_dot_lit(&k, i, dot_count)) continue;
        if (*first < 0) *first = i;
        *last = i;
    }
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

static void test_angle(void)
{
    CHECK_EQ(angle_at(0), 1800, "0 is half a turn clockwise (180.0 degrees)");
    CHECK_EQ(angle_at(25), 900, "25 is a quarter turn clockwise");
    CHECK_EQ(angle_at(50), 0, "the detent is twelve o'clock");
    CHECK_EQ(angle_at(75), -900, "75 is a quarter turn counter-clockwise");
    CHECK_EQ(angle_at(100), -1800, "100 is half a turn counter-clockwise");

    /* 3.6 degrees per unit, clockwise as the value drops. */
    CHECK_EQ(angle_at(49) - angle_at(50), 36, "one step is 36 tenths of a degree");
    CHECK_EQ(angle_at(50) - angle_at(51), 36, "and it is signed clockwise-positive");
}

static void test_click_round_trip(void)
{
    static const int radii[] = { 16, 66, 142 };

    for (size_t r = 0; r < sizeof(radii) / sizeof(radii[0]); r++) {
        for (int v = 1; v < 100; v++) {
            int dx, dy;
            point_at(v, radii[r], &dx, &dy);
            int got = knob_value_at(dx, dy);
            CHECK(got == v, "round trip at r=%d, value %d: point (%d,%d) gave %d", radii[r], v,
                  dx, dy, got);
        }
    }
}

static void test_click_seam(void)
{
    /* The dial is 180 degrees per half, so the bottom is a seam: exactly six o'clock is
     * the full cut, and one pixel either side is the full boost or the full cut. */
    CHECK_EQ(knob_value_at(0, 100), 0, "the bottom is the full cut");
    CHECK_EQ(knob_value_at(-1, 100), 100, "a hair counter-clockwise is the full boost");
    CHECK_EQ(knob_value_at(1, 100), 0, "a hair clockwise is the full cut");
}

static void test_compass(void)
{
    CHECK_EQ(knob_value_at(0, -100), 50, "twelve o'clock is the detent");
    CHECK_EQ(knob_value_at(100, 0), 25, "three o'clock is a quarter cut");
    CHECK_EQ(knob_value_at(-100, 0), 75, "nine o'clock is a quarter boost");
    CHECK_EQ(knob_value_at(0, 100), 0, "six o'clock is the full cut");

    /* The diagonals round to the nearest value with lroundf, which is what the view's
     * click always did: 37.5 -> 38, so 135 degrees is value 12, not 13. */
    CHECK_EQ(knob_value_at(100, 100), 12, "the lower-right diagonal");
    CHECK_EQ(knob_value_at(-100, 100), 88, "the lower-left diagonal");
    CHECK_EQ(knob_value_at(100, -100), 37, "the upper-right diagonal");
    CHECK_EQ(knob_value_at(-100, -100), 63, "the upper-left diagonal");
}

static void test_click_degenerate(void)
{
    CHECK_EQ(knob_value_at(0, 0), KNOB_DETENT, "a zero-length point reads as the detent");

    /* Any distance gives the same value: the inverse only uses the angle. */
    CHECK_EQ(knob_value_at(0, -1), knob_value_at(0, -100), "r does not change the answer");
    CHECK_EQ(knob_value_at(1, -1), knob_value_at(100, -100), "on a diagonal either");
}

static void test_dot_span(void)
{
    int first, last;
    knob_t k;

    CHECK_EQ(lit_count(50, 24), 0, "the detent lights nothing");
    lit_run(50, 24, &first, &last);
    CHECK(first == -1 && last == -1, "and no index is lit");

    CHECK_EQ(lit_count(0, 24), 12, "the full cut lights half the crown");
    lit_run(0, 24, &first, &last);
    CHECK_EQ(first, 1, "from the dot after the tick");
    CHECK_EQ(last, 12, "clockwise to six o'clock");

    CHECK_EQ(lit_count(25, 24), 6, "a quarter cut lights six dots");
    lit_run(25, 24, &first, &last);
    CHECK_EQ(first, 1, "on the clockwise side");
    CHECK_EQ(last, 6, "ending at a quarter turn");

    CHECK_EQ(lit_count(100, 24), 12, "the full boost lights the other half");
    lit_run(100, 24, &first, &last);
    CHECK_EQ(first, 12, "starting at six o'clock counter-clockwise");
    CHECK_EQ(last, 23, "ending before the tick");

    CHECK_EQ(lit_count(75, 24), 6, "a quarter boost lights six dots");
    lit_run(75, 24, &first, &last);
    CHECK_EQ(first, 18, "on the counter-clockwise side");
    CHECK_EQ(last, 23, "ending before the tick");

    /* Dot 0 is the 0 dB tick in the view's inks, never a lit value dot. */
    knob_init(&k, KNOB_RANGE_GAIN);
    for (int v = KNOB_MIN; v <= KNOB_MAX; v++) {
        knob_set(&k, v);
        CHECK(!knob_dot_lit(&k, 0, 24), "dot 0 is not lit at value %d", v);
    }

    /* The span scales with the crown: a 12-dot crown at the full cut lights 6. */
    CHECK_EQ(lit_count(0, 12), 6, "a 12-dot crown lights half at the full cut");
    CHECK_EQ(lit_count(25, 12), 3, "and a quarter at a quarter cut");

    knob_init(&k, KNOB_RANGE_GAIN);
    knob_set(&k, 0);
    CHECK(!knob_dot_lit(&k, -1, 24), "a negative index is not lit");
    CHECK(!knob_dot_lit(&k, 24, 24), "an index past the crown is not lit");
    CHECK(!knob_dot_lit(&k, 1, 0), "a crown with no dots has nothing lit");
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
    test_click_round_trip();
    test_click_seam();
    test_compass();
    test_click_degenerate();
    test_dot_span();
    test_reset_and_clamp();
    test_name();
    test_edit_ordering();
    test_edit_reset_swallow();
    test_edit_close_pending();

    TEST_END();
}

/*
 * The chrome's entrance rule, without LVGL: where a piece is, where it was asked to go, and
 * what a reversal starts from.
 *
 * Seven pieces of the controls screen move the same way, and before this rule each of them
 * kept its own "already shown" flag and its own idea of a reversal. Those are the edges
 * pinned here: a repeat is not a restart, a reversal begins at the pixels on screen, and a
 * piece is hidden only when it has finished leaving. No display, no serial dump.
 */
#include "test_util.h"

#include "entrance.h"

/* A piece starts away and unasked-for, so the controls' first arrival animates rather than
 * snapping: the build places the object at its away position and the entrance agrees. */
static void test_starts_away(void)
{
    entrance_t e;
    entrance_init(&e, -72, 6);

    CHECK_EQ(e.value, -72, "it starts at the away value");
    CHECK(!e.target, "and is not asked to be up");
    CHECK_EQ(e.away, -72, "the away value is remembered");
    CHECK_EQ(e.here, 6, "and the resting one");
}

/* Asking for the state it is already heading for is not a request: it must not return an
 * animation, because a caller that restarts a motion on every repeat is a caller whose piece
 * twitches whenever two events land in one frame. */
static void test_a_repeat_is_not_a_restart(void)
{
    entrance_t e;
    entrance_init(&e, 0, 100);

    int32_t from = -1;
    int32_t to = -1;
    CHECK(entrance_ask(&e, true, &from, &to), "the first ask is a motion");
    CHECK_EQ(from, 0, "from where it is");
    CHECK_EQ(to, 100, "to where it rests");
    CHECK(e.target, "and it is now asked to be up");

    from = -1;
    to = -1;
    CHECK(!entrance_ask(&e, true, &from, &to), "the same ask again is nothing");
    CHECK_EQ(from, -1, "and writes no ends");
    CHECK_EQ(to, -1, "at all");

    /* Even mid-slide: the value moved, but the target did not. */
    entrance_step(&e, 40);
    CHECK(!entrance_ask(&e, true, NULL, NULL), "a repeat halfway through is still nothing");
    CHECK_EQ(e.value, 40, "and leaves the piece where the animator put it");
}

/* A reversal starts from wherever the piece is, not from the end it last left: that is what
 * makes a second swipe halfway through the first turn the motion around. */
static void test_a_reversal_starts_from_where_it_is(void)
{
    entrance_t e;
    entrance_init(&e, -60, 0);

    int32_t from = 0;
    int32_t to = 0;
    CHECK(entrance_ask(&e, true, &from, &to), "coming in");
    entrance_step(&e, -21);
    CHECK(!entrance_landed(&e), "a piece on its way in is not landed");

    CHECK(entrance_ask(&e, false, &from, &to), "and going back out");
    CHECK_EQ(from, -21, "reverses from the pixel it had reached");
    CHECK_EQ(to, -60, "to the away value");

    entrance_step(&e, -60);
    CHECK(entrance_landed(&e), "a piece that has finished leaving is landed");
    CHECK(!entrance_ask(&e, false, NULL, NULL), "and asking it to leave again is nothing");

    CHECK(entrance_ask(&e, true, &from, &to), "coming back in is a motion again");
    CHECK_EQ(from, -60, "from the away value");
    CHECK_EQ(to, 0, "to the resting one");
    entrance_step(&e, 0);
    CHECK(!entrance_landed(&e), "a piece that arrived is not landed, so it is not hidden");
}

/* The window moves an opacity and a bar moves an offset: both are signed values and neither
 * has a special case. */
static void test_any_end_values(void)
{
    entrance_t e;
    entrance_init(&e, 255, 0);

    int32_t from = 0;
    int32_t to = 0;
    CHECK(entrance_ask(&e, true, &from, &to), "transparent to opaque is a motion");
    CHECK_EQ(from, 255, "from transparent");
    CHECK_EQ(to, 0, "to opaque");
    CHECK(!entrance_landed(&e), "and it is not hidden when it arrives");
}

/* The callers are C, and a module that is handed a NULL is a module that says so rather than
 * a module that faults. */
static void test_null_is_quiet(void)
{
    int32_t from = 7;
    int32_t to = 7;

    entrance_init(NULL, 0, 1);
    CHECK(!entrance_ask(NULL, true, &from, &to), "no rule, no motion");
    CHECK_EQ(from, 7, "and no ends written");
    entrance_step(NULL, 3);
    CHECK(!entrance_landed(NULL), "and nothing is landed");
}

int main(void)
{
    test_starts_away();
    test_a_repeat_is_not_a_restart();
    test_a_reversal_starts_from_where_it_is();
    test_any_end_values();
    test_null_is_quiet();

    TEST_END();
}

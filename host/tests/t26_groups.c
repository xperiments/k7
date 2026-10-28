/*
 * Which group the right bar is showing, without LVGL.
 *
 * The rules the owner asked for and the fact that bounds them: a group button is a toggle (one
 * group at a time, the same button again returns to the speaker - the only way back, now that
 * the panels have no Back cell), and the groups live with the chrome - hiding the bars drops
 * the active group, so the controls always open on the grille. The Menu's Pad is not a group
 * and the model is not stepped while it is open; the view reinstalls the group on close.
 */
#include "test_util.h"

#include <string.h>

#include "groups.h"

static groups_t fresh(void)
{
    groups_t g;
    groups_init(&g);
    return g;
}

static void test_starts_empty(void)
{
    groups_t g = fresh();
    CHECK_EQ(groups_active(&g), GROUPS_ID_EMPTY, "a new model has no group selected");
    CHECK_EQ(groups_handle(&g, GROUPS_INTENT_NONE, false), 0u,
             "a step with the chrome away and nothing selected changes nothing");
    CHECK_EQ(groups_handle(&g, GROUPS_INTENT_NONE, true), 0u,
             "and raising the chrome alone changes nothing");
}

static void test_toggle(void)
{
    groups_t g = fresh();

    CHECK_EQ(groups_handle(&g, GROUPS_INTENT_TAP_EQ, true), GROUPS_CHANGED_ACTIVE,
             "a tap on the sliders selects the EQ group");
    CHECK_EQ(groups_active(&g), GROUPS_ID_EQ, "which is the active one");

    CHECK_EQ(groups_handle(&g, GROUPS_INTENT_TAP_EQ, true), GROUPS_CHANGED_ACTIVE,
             "the same button again moves the group");
    CHECK_EQ(groups_active(&g), GROUPS_ID_EMPTY, "back to the speaker, not to a second group");

    CHECK_EQ(groups_handle(&g, GROUPS_INTENT_TAP_RGB, true), GROUPS_CHANGED_ACTIVE,
             "and the led selects the RGB group");
    CHECK_EQ(groups_active(&g), GROUPS_ID_RGB, "one group at a time");

    CHECK_EQ(groups_handle(&g, GROUPS_INTENT_TAP_EQ, true), GROUPS_CHANGED_ACTIVE,
             "the other group's button switches straight across");
    CHECK_EQ(groups_active(&g), GROUPS_ID_EQ, "without going through Empty first");
}

static void test_chrome_reset(void)
{
    groups_t g = fresh();

    groups_handle(&g, GROUPS_INTENT_TAP_RGB, true);
    CHECK_EQ(groups_active(&g), GROUPS_ID_RGB, "a group is up");

    CHECK_EQ(groups_handle(&g, GROUPS_INTENT_NONE, false), GROUPS_CHANGED_ACTIVE,
             "hiding the chrome drops it");
    CHECK_EQ(groups_active(&g), GROUPS_ID_EMPTY, "so the controls open on the speaker");

    /* A tap and a hide in the same step: the hide wins. */
    groups_handle(&g, GROUPS_INTENT_TAP_EQ, true);
    CHECK_EQ(groups_handle(&g, GROUPS_INTENT_TAP_RGB, false), GROUPS_CHANGED_ACTIVE,
             "a tap that arrives with the chrome gone still moves");
    CHECK_EQ(groups_active(&g), GROUPS_ID_EMPTY, "but cannot leave a group up");
}

static void test_stable(void)
{
    groups_t g = fresh();
    groups_handle(&g, GROUPS_INTENT_TAP_EQ, true);

    CHECK_EQ(groups_handle(&g, GROUPS_INTENT_NONE, true), 0u,
             "a step that repeats the state reports nothing");
    CHECK_EQ(groups_handle(&g, GROUPS_INTENT_NONE, true), 0u, "and again");
    CHECK_EQ(groups_active(&g), GROUPS_ID_EQ, "the group is still the EQ");

    /* NULL is not a state the screen has, but the model refuses it rather than crashing. */
    CHECK_EQ(groups_handle(NULL, GROUPS_INTENT_TAP_EQ, true), 0u, "a NULL model reports nothing");
    CHECK_EQ(groups_active(NULL), GROUPS_ID_EMPTY, "and reads as Empty");
}

int main(void)
{
    test_starts_empty();
    test_toggle();
    test_chrome_reset();
    test_stable();

    TEST_END();
}

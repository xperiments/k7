#pragma once
/*
 * groups - which of the right bar's panels is up, as a value. No LVGL, no board, no clock.
 *
 * The right bar hosts one **group** at a time and the Home group by default: the four keys that
 * belong to no mode. The EQ group when the left bar's sliders button is on, and the RGB group
 * when its led button is. The Menu's Pad is not a group: it takes the bar for the Menu's
 * lifetime and the group comes back afterwards, which is why this model neither knows about the
 * Menu nor is stepped while it is open.
 *
 * The rules, and nothing else:
 *   - a group button is a toggle: a tap selects its group, a tap on the one already up puts
 *     the Home group back (one group at a time, never two). That toggle is the only way back:
 *     the panels have no Back cell, because the bar's button already is one
 *   - the groups live with the chrome: when the bars go away the active group is dropped, so
 *     the controls always open on Home (the "shows by default" rule)
 *
 * The state is a few bytes and there is nothing to allocate. The view applies the mask: a
 * changed ACTIVE means reinstall the right bar's panel and relatch the left bar's discs.
 */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GROUPS_ID_HOME = 0,    /* the default panel: no mode group selected */
    GROUPS_ID_EQ,
    GROUPS_ID_RGB,
    GROUPS_ID_COUNT
} groups_id_t;

typedef enum {
    GROUPS_INTENT_NONE = 0,
    GROUPS_INTENT_TAP_EQ,    /* the left bar's sliders-v */
    GROUPS_INTENT_TAP_RGB,   /* the left bar's led */
} groups_intent_t;

/* The changed-mask groups_handle() returns. */
enum {
    GROUPS_CHANGED_ACTIVE = 1u << 0,   /* the active group moved: panel and latch */
};

typedef struct {
    groups_id_t active;
    bool chrome_shown;    /* what the last step saw, so the reset is an edge not a level */
} groups_t;

/* Start on the Home group, with the chrome away. */
void groups_init(groups_t *g);

groups_id_t groups_active(const groups_t *g);

/* One step: the intent, plus the fact the model cannot own. Returns the changed-mask; a step
 * that changes nothing returns 0, so a repeated call is free. The chrome rule runs last, so a
 * group cannot outlive the bars even if a tap and a hide arrive together. */
unsigned groups_handle(groups_t *g, groups_intent_t intent, bool chrome_shown);

#ifdef __cplusplus
}
#endif

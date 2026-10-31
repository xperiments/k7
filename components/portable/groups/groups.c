/*
 * Which group the right bar is showing; see groups.h.
 *
 * Two rules and one fact, no objects, no clock, no allocation. The model's whole job is to keep
 * "one group at a time, and its own button is the toggle" and "the groups live with the chrome"
 * in one place the host tier can pin.
 */
#include "groups.h"

#include <stddef.h>

void groups_init(groups_t *g)
{
    if (!g) return;
    g->active = GROUPS_ID_HOME;
    g->chrome_shown = false;
}

groups_id_t groups_active(const groups_t *g)
{
    return g ? g->active : GROUPS_ID_HOME;
}

/* A group button's toggle: its group, or Home when it is already the one up. */
static groups_id_t toggled(groups_id_t active, groups_id_t which)
{
    return active == which ? GROUPS_ID_HOME : which;
}

unsigned groups_handle(groups_t *g, groups_intent_t intent, bool chrome_shown)
{
    if (!g) return 0;

    groups_id_t was = g->active;
    g->chrome_shown = chrome_shown;

    switch (intent) {
    case GROUPS_INTENT_TAP_EQ: g->active = toggled(g->active, GROUPS_ID_EQ); break;
    case GROUPS_INTENT_TAP_RGB: g->active = toggled(g->active, GROUPS_ID_RGB); break;
    case GROUPS_INTENT_NONE:
    default: break;
    }

    /* The groups live with the chrome: hiding the bars drops whatever was selected, so the
     * controls open on Home every time. This runs last so it wins. */
    if (!chrome_shown) g->active = GROUPS_ID_HOME;

    return g->active != was ? GROUPS_CHANGED_ACTIVE : 0u;
}

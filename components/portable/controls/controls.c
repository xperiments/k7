/*
 * controls - the Cassette screen's Controls model, portable: the level ladder, the Page, the
 * Cover, the one-overlay rule and the Menu's adoption. No LVGL, no clock, no collaborators
 * (controls.h).
 *
 * One step is (state, intent, facts) -> the groups that changed. The rules are ADR 0025's
 * four-rung stack, rung for rung as cassette_view.c applies them; the comments below name
 * the rung each branch reproduces. The mask is the diff of the state before and after the
 * step, so the view can apply only what moved.
 */
#include "controls.h"

#include <stdlib.h>

struct controls {
    controls_level_t level; /* the enum is the stack, bottom first: down is +1 */
    bool             menu;  /* only while the level is Art or Real */
};

/* Whether the chrome - the drawer, the block, the bands, the window, the edge mark - is on
 * screen. It is out at Art and Real and nowhere else, because the enum's last two values are
 * exactly those two rungs. */
static bool chrome_out(controls_level_t level)
{
    return level >= CONTROLS_LEVEL_ART;
}

/* The room is a function of the level. Art and Real share the Controls' pose: moving between
 * them turns the paper and prints the picture, and moves no pixel of the cassette. */
static controls_room_t room_of(controls_level_t level)
{
    switch (level) {
        case CONTROLS_LEVEL_PEEK: return CONTROLS_ROOM_PEEK;
        case CONTROLS_LEVEL_ART:
        case CONTROLS_LEVEL_REAL: return CONTROLS_ROOM_CONTROLS;
        case CONTROLS_LEVEL_INITIAL:
        default: return CONTROLS_ROOM_ENTRY;
    }
}

/* One rung towards Real. Real's down is the end, and Art's down waits for the settled
 * controls pose: the Cover's mask is built at that size (ADR 0021), so a swipe during the
 * shrink is refused rather than printed at the wrong one. */
static controls_level_t lower(controls_level_t level, const controls_facts_t *facts)
{
    if (level >= CONTROLS_LEVEL_REAL) return level;
    if (level == CONTROLS_LEVEL_ART && !facts->at_controls_pose) return level;
    return (controls_level_t)(level + 1);
}

/* One rung towards the Peek. The Peek's up is the end, and Initial's up waits for the settled
 * entry pose: the Peek was always raised from the entry view, never from a slide (ADR 0020). */
static controls_level_t raise_level(controls_level_t level, const controls_facts_t *facts)
{
    if (level <= CONTROLS_LEVEL_PEEK) return level;
    if (level == CONTROLS_LEVEL_INITIAL && !facts->at_entry_pose) return level;
    return (controls_level_t)(level - 1);
}

/* One rung of the ladder, or nothing. Runs only when neither modality swallowed the intent.
 * There is no invariant to repair afterwards: the Page, the Cover, the Peek and the chrome
 * are all readings of the level, so moving the level moves them together. */
static void apply(controls_t *c, controls_intent_t intent, const controls_facts_t *facts)
{
    switch (intent) {
        case CONTROLS_INTENT_DOWN:
            c->level = lower(c->level, facts);
            break;

        case CONTROLS_INTENT_UP:
            c->level = raise_level(c->level, facts);
            break;

        case CONTROLS_INTENT_MENU_TAP:
            /* The M tap: the Menu opens from the settled controls pose, which is both Art
             * and Real. The edit session and an open Menu were refused before this ran. */
            if (chrome_out(c->level) && facts->at_controls_pose) c->menu = true;
            break;

        case CONTROLS_INTENT_NONE:
        default:
            break;
    }
}

controls_t *controls_new(void)
{
    controls_t *c = calloc(1, sizeof(controls_t));
    if (c) c->level = CONTROLS_LEVEL_INITIAL;
    return c;
}

void controls_free(controls_t *c)
{
    free(c);
}

controls_changed_t controls_handle(controls_t *c, controls_intent_t intent,
                                   const controls_facts_t *facts)
{
    if (!c) return CONTROLS_CHANGED_NONE;

    const controls_facts_t zero = { false, false, false, false };
    const controls_facts_t f = facts ? *facts : zero;

    /* Pull-and-adopt (ADR 0022): the Menu opens through this model and closes itself from
     * three places inside cassette_menu.c, so its bit is a fact read before the step, not
     * a rule's output. Adopting it before the diff is what keeps a self-close silent: the
     * Menu has already done what a MENU bit would ask the view to do. A Menu can only exist
     * over the chrome, so a fact that says otherwise is not adopted. */
    c->menu = f.menu_open && chrome_out(c->level);

    const controls_level_t was_level = c->level;
    const bool was_menu = c->menu;

    if (f.editing) {
        /* The edit overlay is absolute: every intent is dropped. */
    } else if (f.menu_open) {
        /* The Menu is modal, one rung above the ladder: up closes it, and so does another tap
         * on the cassette's door, which is the same button that opened it. Everything else
         * waits. */
        if (intent == CONTROLS_INTENT_UP || intent == CONTROLS_INTENT_MENU_TAP) c->menu = false;
    } else {
        apply(c, intent, &f);
    }

    uint32_t mask = 0;
    if ((c->level == CONTROLS_LEVEL_REAL) != (was_level == CONTROLS_LEVEL_REAL)) {
        mask |= CONTROLS_CHANGED_PAGE;
    }
    if ((c->level == CONTROLS_LEVEL_PEEK) != (was_level == CONTROLS_LEVEL_PEEK)) {
        mask |= CONTROLS_CHANGED_PEEK;
    }
    if (chrome_out(c->level) != chrome_out(was_level)) mask |= CONTROLS_CHANGED_SHOWN;
    if (c->menu != was_menu) mask |= CONTROLS_CHANGED_MENU;
    if (room_of(c->level) != room_of(was_level)) mask |= CONTROLS_CHANGED_ROOM;

    return (controls_changed_t)mask;
}

void controls_snapshot(const controls_t *c, controls_state_t *out)
{
    if (!out) return;

    /* Initial, not a zeroed struct: the Peek is the enum's zero value, so a NULL model is
     * named explicitly rather than left as whatever the bits happened to be. */
    controls_state_t s = { CONTROLS_LEVEL_INITIAL, CONTROLS_ROOM_ENTRY, false };
    if (c) {
        s.level = c->level;
        s.room = room_of(c->level);
        s.menu = c->menu;
    }
    *out = s;
}

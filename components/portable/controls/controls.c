/*
 * controls - the Cassette screen's Controls model, portable: the level ladder, the Page, the
 * Cover, the one-overlay rule and the Menu. No LVGL, no clock, no collaborators (controls.h).
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
    bool             full;  /* Real's picture at the entry pose: only at Initial */
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
 * controls pose: the Card and its print exist at that pose only (ADR 0039), so a swipe during
 * the shrink is refused rather than bringing up a page that is not laid out yet. */
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
 * The Page, the Cover, the Peek and the chrome are all readings of the level, so moving the
 * level moves them together and no rung has anything of its own to repair. The one repair the
 * step does make - a Menu cannot outlive the chrome - is in controls_handle, after the rungs. */
static void apply(controls_t *c, controls_intent_t intent, const controls_facts_t *facts)
{
    switch (intent) {
        case CONTROLS_INTENT_DOWN:
            c->full = false;   /* the picture is a reading of Initial: the step takes it */
            c->level = lower(c->level, facts);
            break;

        case CONTROLS_INTENT_UP:
            c->full = false;
            c->level = raise_level(c->level, facts);
            break;

        case CONTROLS_INTENT_MENU_TAP:
            /* The door: the Menu opens from the settled controls pose, which is both Art
             * and Real. The edit session and an open Menu were refused before this ran. */
            if (chrome_out(c->level) && facts->at_controls_pose) c->menu = true;
            break;

        case CONTROLS_INTENT_MENU_CLOSE:
            /* Nothing to close: a close is only meaningful while the Menu is up, and the
             * modality above is the one place it can land. */
            break;

        case CONTROLS_INTENT_FULL_ON:
            /* The entry pose and nothing over it: the picture takes the whole panel, so it
             * cannot be asked for during a slide or from behind the Menu or a dial. */
            if (c->level == CONTROLS_LEVEL_INITIAL && facts->at_entry_pose) c->full = true;
            break;

        case CONTROLS_INTENT_FULL_OFF:
            c->full = false;
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

    const controls_facts_t zero = { false, false, false };
    const controls_facts_t f = facts ? *facts : zero;

    const controls_level_t was_level = c->level;
    const bool was_menu = c->menu;
    const bool was_full = c->full;

    if (f.editing) {
        /* The edit overlay is absolute: every intent is dropped. */
    } else if (c->menu) {
        /* The Menu is modal, one rung above the ladder. It closes on an up swipe, on another
         * tap on the cassette's door - the same button that opened it - and on the close its
         * own input asks for: its Close row, the Pad's Back at the root. Everything else waits
         * (ADR 0022). */
        if (intent == CONTROLS_INTENT_UP || intent == CONTROLS_INTENT_MENU_TAP ||
            intent == CONTROLS_INTENT_MENU_CLOSE) {
            c->menu = false;
        }
    } else {
        apply(c, intent, &f);
    }

    /* A Menu is only ever over the chrome. Through the intents that is already true - the
     * modality above swallows every rung while the Menu is up - so this repairs an invariant
     * rather than expressing a rule a caller can reach. It is here so the invariant holds
     * wherever the level is written, instead of in the caller's discipline. */
    if (!chrome_out(c->level)) c->menu = false;

    /* The full-screen picture is a reading of Initial, exactly as the Menu is a reading of
     * the chrome: it cannot outlive the level it belongs to. Repaired here, like the Menu,
     * so every write of the level keeps the invariant. */
    if (c->level != CONTROLS_LEVEL_INITIAL) c->full = false;

    uint32_t mask = 0;
    /* The Page and the Cover are one reading: Real's rung, or the full-screen picture at
     * Initial. The view applies the geometry first (FULL) and then the page and the print. */
    const bool was_picture = (was_level == CONTROLS_LEVEL_REAL) || was_full;
    const bool is_picture = (c->level == CONTROLS_LEVEL_REAL) || c->full;
    if (is_picture != was_picture) mask |= CONTROLS_CHANGED_PAGE;
    if (c->full != was_full) mask |= CONTROLS_CHANGED_FULL;
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
    controls_state_t s = { CONTROLS_LEVEL_INITIAL, CONTROLS_ROOM_ENTRY, false, false };
    if (c) {
        s.level = c->level;
        s.room = room_of(c->level);
        s.menu = c->menu;
        s.full = c->full;
    }
    *out = s;
}

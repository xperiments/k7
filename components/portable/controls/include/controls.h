#pragma once
/*
 * controls - the Cassette screen's Controls as a value: the Level the screen is showing,
 * and whether the Menu is open.
 *
 * The model owns the ladder (ADR 0025), the Page rule, the one-overlay rule, the Cover's
 * wish (ADR 0021) and the control modalities (ADR 0018's edit, ADR 0022's Menu). It owns no
 * object, no animation, no timer and no collaborator: one step is a function of the state,
 * the intent and the facts. The state is a few bytes, and controls_new() is the only call
 * that allocates.
 *
 * The state it owns:
 *   level       one of the four screens, never two: Initial, Peek, Art or Real
 *   menu        the Menu's open state; adopted from the facts on every step
 *
 * Everything else is derived, because it is one thing on screen:
 *   the Page    white exactly at Real, dark at the other three
 *   the Cover   printed exactly at Real (ADR 0021's picture, now the Page's own)
 *   the chrome  out at Art and Real: the drawer, the DJ block, the header and footer bands,
 *               the window and the white Page's edge mark
 *   the Peek    up exactly at Peek
 *
 * The facts it cannot own, handed in on every step:
 *   editing            a knob's 3x overlay owns the touch (knob_editor_is_editing())
 *   menu_open          the Menu is up (cassette_menu_is_open()); it also closes itself
 *                      from three places inside cassette_menu.c, so the model pulls and
 *                      adopts it rather than being pushed to
 *   at_entry_pose      the pose on screen is cassette_pose_entry()
 *   at_controls_pose   and cassette_pose_controls()
 *
 * The pose is a fact, not state. The model names the room it wants - a derived value and a
 * mask bit - and the view animates to it; the 260 ms, the easing and the reversal are the
 * view's. The view builds the two settled facts by comparing the pose on screen with the
 * two named poses (cassette_layout.h), which is the ladder's "settled" test; a swipe during
 * a slide finds the fact false.
 *
 * The input is the screen's intents: screen_input.h reports the vertical direction and
 * cassette_view.c maps its enum to this one. A tap on the mode bar's cassette is the third
 * intent, and it toggles: it opens the Menu, and a second tap closes it.
 * Two modalities swallow intents before any rule runs. While a knob is being edited, every
 * intent is ignored (ADR 0018): the edit overlay owns the touch. While the Menu is up, UP
 * closes it and every other intent is ignored (ADR 0022).
 *
 * The stack, from the top of the screen to the bottom:
 *
 *   Real      the controls on the white Page, with the Track's picture on the cassette
 *   Art       the controls on the dark Page, orange line art only
 *   Initial   the full cassette, nothing over it
 *   Peek      the timeline bar and the two floating tags
 *
 * A down swipe is one rung towards Real, an up swipe one rung towards the Peek, and the two
 * ends stop. The enum is that stack bottom-first, so down is `level + 1` and up is
 * `level - 1`; the two guards are the only exceptions:
 *
 *   DOWN  Initial -> Art    no guard: the chrome cuts in while the cassette is still moving
 *   DOWN  Art -> Real       at the settled controls pose: the Cover's mask is built there
 *   DOWN  Peek -> Initial   no guard
 *   UP    Initial -> Peek   from the settled entry pose (ADR 0020's rule, kept)
 *   UP    Art -> Initial    no guard
 *   UP    Real -> Art       no guard
 *
 * And the two ends: a down at Real and an up at the Peek change nothing. A swipe that is
 * refused by a guard instead of moving - DOWN at Art before the pose lands - changes
 * nothing too, and the next swipe, once settled, takes the rung.
 *
 * The two horizontal swipes are gone (ADR 0025): the chrome is the ladder's, and the only
 * left swipe in the build is the Menu's Back on the Pad. The VU meter's two-finger gesture
 * is gone as well; the meter is parked, hidden, until it has a new home.
 *
 * The state satisfies, after every step:
 *   - the level is exactly one of the four values;
 *   - the menu is out only at Art or Real;
 *   - the room is a function of the level alone: the Peek's 200/0/-60, Initial's 256/0/0,
 *     and the Controls' 134/-51/-20 for Art and Real (they share it, so moving between them
 *     moves no pose);
 *   - a step that changes nothing reports CONTROLS_CHANGED_NONE and leaves the state as it
 *     was, with the one exception below;
 *   - the same (state, intent, facts) always produces the same mask and the same state.
 *
 * The exception is the Menu. A Menu that closed itself (P, `<` at the root, a row's Close)
 * is a fact, not an effect of the intent that follows it, so the model adopts menu_open at
 * the top of every step and reports no bit for it: the Menu has already done what the bit
 * would ask the view to do. A self-close noticed on a later step therefore moves the
 * model's `menu` with CONTROLS_CHANGED_NONE.
 *
 * The adapter applies the changed groups in this order, which keeps today's before/after
 * relationships: PAGE, PEEK, SHOWN, MENU, ROOM. PAGE before the rest because the Page flip
 * and the Cover's fade are one step and the picture belongs with the paper; PEEK before
 * SHOWN because opening the chrome closes the Peek first; SHOWN before MENU because the Menu
 * wants the chrome in place; ROOM last because it starts an animation and nothing it paints
 * depends on the others.
 *
 * It has one adapter, components/app/screen_display/cassette_view.c, and no second one is
 * planned: this is not a seam with two implementations. The host tier cannot compile
 * components/app, so the ladder and the Page rule could otherwise only be checked with a
 * finger; the model exists so host/tests/t24_controls.c can pin them.
 *
 * No LVGL, no ESP-IDF: this header includes only the C standard library.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The touch's meaning, as screen_input.h reports it, plus the M tap. A direction, not a
 * screen: the model knows which rung the direction takes. The two horizontal swipes that
 * used to be here are gone with the drawer's sideways toggle (ADR 0025). */
typedef enum {
    CONTROLS_INTENT_NONE = 0,     /* the zero value: adopts the facts and runs no rule */
    CONTROLS_INTENT_DOWN,         /* one rung towards Real */
    CONTROLS_INTENT_UP,           /* and one towards the Peek */
    CONTROLS_INTENT_MENU_TAP,     /* the DJ block's M: the Menu's way in (ADR 0022) */
} controls_intent_t;

/* The four screens, in the stack's order from the bottom: each value's successor is the
 * rung a down swipe takes. The order is the ladder's, not the enum's convenience - down is
 * +1 and up is -1 (controls.c), with the two settled-pose guards as the exceptions. */
typedef enum {
    CONTROLS_LEVEL_PEEK = 0,      /* the timeline bar and the two floating tags */
    CONTROLS_LEVEL_INITIAL,       /* the full cassette, nothing over it */
    CONTROLS_LEVEL_ART,           /* the chrome on the dark Page, orange line art */
    CONTROLS_LEVEL_REAL,          /* the chrome on the white Page, the picture printed */
} controls_level_t;

/* The room the cassette asks for: the pose the view animates to. Derived from the level,
 * never a wish of its own - Initial is the entry view, the Peek its 200/256 with the -60 px
 * lift, Art and Real the Controls' 134/256 - so there is nothing left to arbitrate between
 * two overlays. */
typedef enum {
    CONTROLS_ROOM_ENTRY = 0,   /* cassette_pose_entry(): 256/0/0 */
    CONTROLS_ROOM_PEEK,        /* cassette_pose_peek(): 200/0/-60 */
    CONTROLS_ROOM_CONTROLS,    /* cassette_pose_controls(): 134/-51/-20 */
} controls_room_t;

/* What the view knows and the model cannot: a knob's edit session, the Menu's own mode and
 * the poses that have landed. A zero struct is "nothing modal, neither named pose on
 * screen", which is what every guarded rung refuses. */
typedef struct {
    bool editing;          /* a knob's 3x overlay owns the touch */
    bool menu_open;        /* the Menu is up */
    bool at_entry_pose;    /* the pose on screen equals cassette_pose_entry() */
    bool at_controls_pose; /* and cassette_pose_controls() */
} controls_facts_t;

/* The model's truth after the last step. The view reads it; it never writes it. `room` is
 * derived from `level` and is here so the applier needs one call: it is the pose to
 * animate to, not the pose on screen. */
typedef struct {
    controls_level_t level;
    controls_room_t  room;
    bool             menu;
} controls_state_t;

/* What the step changed, one bit per group the adapter applies. CONTROLS_CHANGED_NONE means
 * the intent did nothing - the common case for a guarded rung. Bits combine with |.
 *
 *   PAGE    the Page's ink and the Cover together: the level entered or left Real. Apply the
 *           page flip first, then the picture, on this one bit
 *   PEEK    the Peek raised or put away: the level entered or left the Peek
 *   SHOWN   the chrome shown or hidden: the strips, the drawer, the block, the bands, the
 *           window and the white Page's edge mark
 *   MENU    the Menu opened or closed
 *   ROOM    the room changed: animate the cassette to the pose `room` names. Art and Real
 *           share the Controls' room, so moving between them is not a ROOM change
 */
typedef enum {
    CONTROLS_CHANGED_NONE   = 0,
    CONTROLS_CHANGED_PAGE   = 1u << 0,
    CONTROLS_CHANGED_PEEK   = 1u << 1,
    CONTROLS_CHANGED_SHOWN  = 1u << 2,
    CONTROLS_CHANGED_MENU   = 1u << 3,
    CONTROLS_CHANGED_ROOM   = 1u << 4,
} controls_changed_t;

typedef struct controls controls_t;

/* The one instance, on the heap, at Initial - which is exactly the screen at boot: the full
 * cassette on the dark Page, the Peek down, no chrome, no Menu. The Peek is the enum's zero
 * value, so this sets the level rather than relying on calloc. NULL on no memory; the view
 * then keeps the screen inert, as it already does for the cassette model. */
controls_t *controls_new(void);
void        controls_free(controls_t *c);

/* One intent, one step. `facts` may be NULL, which means the zero facts. Returns the groups
 * that changed; the caller applies only those, in the order the header gives. A NULL model,
 * or an intent value the model does not know, returns CONTROLS_CHANGED_NONE. */
controls_changed_t controls_handle(controls_t *c, controls_intent_t intent,
                                   const controls_facts_t *facts);

/* Copy the model's truth out. `out` is caller-owned; there is no pointer into the model and
 * no lifetime rule. A NULL model fills Initial, at the entry room, with no Menu. */
void controls_snapshot(const controls_t *c, controls_state_t *out);

#ifdef __cplusplus
}
#endif

/*
 * T24: the Controls model, host tier.
 *
 * The model is driven through controls.h only: one instance, four intents, a facts struct
 * the test owns. No LVGL, no fixtures, no mocks - the model has no collaborators, which is
 * the point of extracting it.
 *
 * What is pinned here:
 *   - the whole ladder: every intent from every reachable state, with the exact mask;
 *   - the stack's order: down is one rung towards Real, up one towards the Peek, and the
 *     ends stop - the enum's own ordering, so the walk is a +1/-1 walk;
 *   - the two modalities: a knob's edit swallows everything and the Menu swallows
 *     everything but UP;
 *   - the two settled gates, each rung reading the pose it is supposed to read;
 *   - the Page and the Cover as one rung: the bit that flips the paper prints the picture,
 *     and there is no state with one and not the other;
 *   - Art and Real share the Controls' room, so moving between them is never a ROOM change;
 *   - the Menu's pull-and-adopt close, and that a self-close is silent;
 *   - the repeat that changes nothing: the same intent twice, NONE the second time;
 *   - the invariants after every step, over a sweep of states x intents x facts.
 *
 * ADR 0025, ADR 0021 and ADR 0022 are the source of truth.
 */
#include "test_util.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "controls.h"

/* Short names for the mask bits, so a case table reads as what the view would apply. */
enum {
    M_NONE  = CONTROLS_CHANGED_NONE,
    M_PAGE  = CONTROLS_CHANGED_PAGE,
    M_PEEK  = CONTROLS_CHANGED_PEEK,
    M_SHOWN = CONTROLS_CHANGED_SHOWN,
    M_MENU  = CONTROLS_CHANGED_MENU,
    M_ROOM  = CONTROLS_CHANGED_ROOM,
    M_ALL   = M_PAGE | M_PEEK | M_SHOWN | M_MENU | M_ROOM,
};

/* ------------------------------------------------------------- scaffolding */

/* A state the test wants the model in. Every one is reachable: model_in() replays the
 * intents that get there from the zero state, and test_seeding() checks the replay. */
typedef struct {
    const char      *name;
    controls_level_t level;
    bool             menu;
} expect_t;

static const expect_t k_initial = { "Initial", CONTROLS_LEVEL_INITIAL, false };
static const expect_t k_peek    = { "the Peek", CONTROLS_LEVEL_PEEK, false };
static const expect_t k_art     = { "Art", CONTROLS_LEVEL_ART, false };
static const expect_t k_real    = { "Real", CONTROLS_LEVEL_REAL, false };
static const expect_t k_menu_art  = { "the Menu on Art", CONTROLS_LEVEL_ART, true };
static const expect_t k_menu_real = { "the Menu on Real", CONTROLS_LEVEL_REAL, true };

#define COUNT(a) (sizeof (a) / sizeof (a)[0])

static const expect_t *const k_states[] = {
    &k_initial, &k_peek, &k_art, &k_real, &k_menu_art, &k_menu_real,
};

static controls_facts_t facts(bool editing, bool menu_open, bool at_entry_pose,
                              bool at_controls_pose)
{
    controls_facts_t f = {
        .editing = editing,
        .menu_open = menu_open,
        .at_entry_pose = at_entry_pose,
        .at_controls_pose = at_controls_pose,
    };
    return f;
}

/* The facts the view would hand in for a model in this state: the pose on screen is the one
 * the state's own room names, so the settled fact that matters is true. */
static controls_facts_t facts_for(const expect_t *w)
{
    return facts(false, w->menu, w->level == CONTROLS_LEVEL_INITIAL,
                 w->level == CONTROLS_LEVEL_ART || w->level == CONTROLS_LEVEL_REAL);
}

/* The oracle for the room mapping: an independent copy, not the model's function. Art and
 * Real share the Controls' room. */
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

/* Build the wanted state by replaying the intents, which is also the only way the test can
 * reach a state: the interface has no setter. */
static controls_t *model_in(const expect_t *w)
{
    controls_t *c = controls_new();
    if (!c) {
        printf("FATAL: controls_new returned NULL\n");
        exit(1);
    }

    const controls_facts_t rest = facts(false, false, true, false);   /* at the entry pose */
    const controls_facts_t controls = facts(false, false, false, true);
    const controls_facts_t anywhere = facts(false, false, false, false);

    switch (w->level) {
        case CONTROLS_LEVEL_PEEK:
            controls_handle(c, CONTROLS_INTENT_UP, &rest);
            break;
        case CONTROLS_LEVEL_ART:
            controls_handle(c, CONTROLS_INTENT_DOWN, &anywhere);
            break;
        case CONTROLS_LEVEL_REAL:
            controls_handle(c, CONTROLS_INTENT_DOWN, &anywhere);
            controls_handle(c, CONTROLS_INTENT_DOWN, &controls);
            break;
        case CONTROLS_LEVEL_INITIAL:
        default:
            break;
    }
    if (w->menu) controls_handle(c, CONTROLS_INTENT_MENU_TAP, &controls);

    return c;
}

static const char *intent_name(controls_intent_t intent)
{
    switch (intent) {
        case CONTROLS_INTENT_NONE: return "NONE";
        case CONTROLS_INTENT_DOWN: return "DOWN";
        case CONTROLS_INTENT_UP: return "UP";
        case CONTROLS_INTENT_MENU_TAP: return "MENU_TAP";
        default: return "?";
    }
}

static const char *level_name(controls_level_t level)
{
    switch (level) {
        case CONTROLS_LEVEL_PEEK: return "Peek";
        case CONTROLS_LEVEL_INITIAL: return "Initial";
        case CONTROLS_LEVEL_ART: return "Art";
        case CONTROLS_LEVEL_REAL: return "Real";
        default: return "?";
    }
}

static void check_mask(controls_changed_t got, unsigned want, const char *ctx)
{
    CHECK((unsigned)got == want, "%s: mask 0x%02x, want 0x%02x", ctx, (unsigned)got, want);
    CHECK(((unsigned)got & ~(unsigned)M_ALL) == 0, "%s: unknown mask bits 0x%02x", ctx,
          (unsigned)got & ~(unsigned)M_ALL);
}

static void check_fields(const controls_t *c, controls_level_t level, bool menu,
                         const char *ctx)
{
    controls_state_t s;
    controls_snapshot(c, &s);

    CHECK(s.level == level, "%s: level %s, want %s", ctx, level_name(s.level),
          level_name(level));
    CHECK(s.menu == menu, "%s: menu %d, want %d", ctx, (int)s.menu, (int)menu);
    CHECK(s.room == room_of(s.level), "%s: room %d does not belong to level %s", ctx,
          (int)s.room, level_name(s.level));
    CHECK(!s.menu || s.room == CONTROLS_ROOM_CONTROLS, "%s: a Menu off the Controls' room",
          ctx);
}

static void check_state(const controls_t *c, const expect_t *w, const char *ctx)
{
    check_fields(c, w->level, w->menu, ctx);
}

static bool same_state(const controls_state_t *a, const controls_state_t *b)
{
    return a->level == b->level && a->room == b->room && a->menu == b->menu;
}

/* ---------------------------------------------------------------- the zero */

static void test_zero(void)
{
    controls_t *c = controls_new();
    if (!c) {
        printf("FATAL: controls_new returned NULL\n");
        exit(1);
    }

    controls_state_t s;
    controls_snapshot(c, &s);
    CHECK_EQ(s.level, CONTROLS_LEVEL_INITIAL, "a new model is at Initial");
    CHECK_EQ(s.room, CONTROLS_ROOM_ENTRY, "a new model asks for the entry room");
    CHECK(!s.menu, "a new model has no Menu");

    /* The Peek is the enum's zero value and the boot state is not the zero bytes: this is
     * the one line that catches a regression to calloc's bits. */
    CHECK(CONTROLS_LEVEL_PEEK == 0, "the stack's zero value is still the Peek");

    CHECK(controls_handle(NULL, CONTROLS_INTENT_DOWN, NULL) == CONTROLS_CHANGED_NONE,
          "a NULL model changes nothing");

    controls_snapshot(NULL, &s);
    CHECK(s.level == CONTROLS_LEVEL_INITIAL && s.room == CONTROLS_ROOM_ENTRY && !s.menu,
          "a NULL model snapshots as Initial");

    controls_facts_t anywhere = facts(false, false, false, false);
    CHECK(controls_handle(c, (controls_intent_t)42, &anywhere) == CONTROLS_CHANGED_NONE,
          "an intent the model does not know changes nothing");
    check_state(c, &k_initial, "an unknown intent");

    CHECK(controls_handle(c, CONTROLS_INTENT_NONE, NULL) == CONTROLS_CHANGED_NONE,
          "a NULL facts struct is the zero facts");
    check_state(c, &k_initial, "NULL facts");

    controls_free(c);
    controls_free(NULL);
}

/* The replay is what every other test starts from, so pin it once. */
static void test_seeding(void)
{
    for (size_t i = 0; i < COUNT(k_states); i++) {
        controls_t *c = model_in(k_states[i]);
        check_state(c, k_states[i], k_states[i]->name);
        controls_free(c);
    }
}

/* ----------------------------------------------------------------- the ladder */

typedef struct {
    controls_intent_t intent;
    const expect_t   *next;
    unsigned          mask;
} case_t;

/* Initial: the way in from nothing. UP raises the Peek from the settled entry pose; DOWN
 * shows the chrome on the dark Page with no gate, because the chrome cuts in while the
 * cassette is still shrinking. */
static const case_t k_from_initial[] = {
    {CONTROLS_INTENT_NONE,     &k_initial, M_NONE},
    {CONTROLS_INTENT_DOWN,     &k_art,     M_SHOWN | M_ROOM},
    {CONTROLS_INTENT_UP,       &k_peek,    M_PEEK | M_ROOM},
    {CONTROLS_INTENT_MENU_TAP, &k_initial, M_NONE},
};

/* The Peek: down puts it away, up does nothing, and M has no chrome to open a Menu over. */
static const case_t k_from_peek[] = {
    {CONTROLS_INTENT_NONE,     &k_peek,    M_NONE},
    {CONTROLS_INTENT_DOWN,     &k_initial, M_PEEK | M_ROOM},
    {CONTROLS_INTENT_UP,       &k_peek,    M_NONE},
    {CONTROLS_INTENT_MENU_TAP, &k_peek,    M_NONE},
};

/* Art: down flips the paper and prints the picture as one rung; up closes the chrome and
 * returns to Initial; M opens the Menu on the dark Page. */
static const case_t k_from_art[] = {
    {CONTROLS_INTENT_NONE,     &k_art,      M_NONE},
    {CONTROLS_INTENT_DOWN,     &k_real,     M_PAGE},
    {CONTROLS_INTENT_UP,       &k_initial,  M_SHOWN | M_ROOM},
    {CONTROLS_INTENT_MENU_TAP, &k_menu_art, M_MENU},
};

/* Real: the top of the stack, so down is nothing; up takes the Page and the picture back
 * with it; M opens the Menu on the white Page. */
static const case_t k_from_real[] = {
    {CONTROLS_INTENT_NONE,     &k_real,      M_NONE},
    {CONTROLS_INTENT_DOWN,     &k_real,      M_NONE},
    {CONTROLS_INTENT_UP,       &k_art,       M_PAGE},
    {CONTROLS_INTENT_MENU_TAP, &k_menu_real, M_MENU},
};

/* The Menu is modal: up closes it and leaves the level where it is; every other intent
 * waits (ADR 0022). */
static const case_t k_from_menu_art[] = {
    {CONTROLS_INTENT_NONE,     &k_menu_art, M_NONE},
    {CONTROLS_INTENT_DOWN,     &k_menu_art, M_NONE},
    {CONTROLS_INTENT_UP,       &k_art,      M_MENU},
    {CONTROLS_INTENT_MENU_TAP, &k_menu_art, M_NONE},
};

static const case_t k_from_menu_real[] = {
    {CONTROLS_INTENT_NONE,     &k_menu_real, M_NONE},
    {CONTROLS_INTENT_DOWN,     &k_menu_real, M_NONE},
    {CONTROLS_INTENT_UP,       &k_real,      M_MENU},
    {CONTROLS_INTENT_MENU_TAP, &k_menu_real, M_NONE},
};

static void ladder_from(const expect_t *from, const case_t *cases, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char ctx[192];
        snprintf(ctx, sizeof ctx, "%s, %s", from->name, intent_name(cases[i].intent));

        controls_t *c = model_in(from);
        controls_facts_t f = facts_for(from);
        check_mask(controls_handle(c, cases[i].intent, &f), cases[i].mask, ctx);
        check_state(c, cases[i].next, ctx);
        controls_free(c);
    }
}

static void test_ladder(void)
{
    ladder_from(&k_initial, k_from_initial, COUNT(k_from_initial));
    ladder_from(&k_peek, k_from_peek, COUNT(k_from_peek));
    ladder_from(&k_art, k_from_art, COUNT(k_from_art));
    ladder_from(&k_real, k_from_real, COUNT(k_from_real));
    ladder_from(&k_menu_art, k_from_menu_art, COUNT(k_from_menu_art));
    ladder_from(&k_menu_real, k_from_menu_real, COUNT(k_from_menu_real));
}

/* ---------------------------------------------------------------- the stack */

/* The enum is the stack, so the walk is arithmetic: three downs from the Peek reach Real
 * with no rung skipped, and three ups come back. Both settled facts are handed in, which the
 * view can never do at once but the walk needs to pass both gates. */
static void test_stack_order(void)
{
    const controls_facts_t settled = facts(false, false, true, true);
    controls_t *c = model_in(&k_peek);

    static const controls_level_t down[] = {
        CONTROLS_LEVEL_INITIAL, CONTROLS_LEVEL_ART, CONTROLS_LEVEL_REAL,
    };
    for (size_t i = 0; i < COUNT(down); i++) {
        char ctx[96];
        snprintf(ctx, sizeof ctx, "down rung %u", (unsigned)i + 1);
        controls_handle(c, CONTROLS_INTENT_DOWN, &settled);
        check_fields(c, down[i], false, ctx);
    }

    /* Real's down is the end. */
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &settled), M_NONE, "down at Real");
    check_fields(c, CONTROLS_LEVEL_REAL, false, "down at Real");

    static const controls_level_t up[] = {
        CONTROLS_LEVEL_ART, CONTROLS_LEVEL_INITIAL, CONTROLS_LEVEL_PEEK,
    };
    for (size_t i = 0; i < COUNT(up); i++) {
        char ctx[96];
        snprintf(ctx, sizeof ctx, "up rung %u", (unsigned)i + 1);
        controls_handle(c, CONTROLS_INTENT_UP, &settled);
        check_fields(c, up[i], false, ctx);
    }

    /* The Peek's up is the end. */
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &settled), M_NONE, "up at the Peek");
    check_fields(c, CONTROLS_LEVEL_PEEK, false, "up at the Peek");

    controls_free(c);
}

/* ---------------------------------------------------------------- the gates */

/* The edit overlay is absolute: every intent is dropped from every state. The Menu fact
 * stays as the state had it, so this also pins that editing is checked first: an up swipe
 * while editing does not close the Menu. */
static void test_editing(void)
{
    static const controls_intent_t intents[] = {
        CONTROLS_INTENT_NONE, CONTROLS_INTENT_DOWN,
        CONTROLS_INTENT_UP,   CONTROLS_INTENT_MENU_TAP,
    };

    for (size_t i = 0; i < COUNT(k_states); i++) {
        const expect_t *w = k_states[i];
        for (size_t j = 0; j < COUNT(intents); j++) {
            char ctx[192];
            snprintf(ctx, sizeof ctx, "%s, %s while editing", w->name,
                     intent_name(intents[j]));

            controls_t *c = model_in(w);
            controls_facts_t f = facts_for(w);
            f.editing = true;
            check_mask(controls_handle(c, intents[j], &f), M_NONE, ctx);
            check_state(c, w, ctx);
            controls_free(c);
        }
    }
}

/* Which pose each guarded rung reads, and that the settled test is a fact and not a proxy:
 * the facts the test hands in are the whole answer. */
static void test_settled_gates(void)
{
    controls_t *c;

    /* UP's peek rung reads the entry pose, not the controls pose. */
    c = model_in(&k_initial);
    controls_facts_t at_controls = facts(false, false, false, true);
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &at_controls), M_NONE,
               "UP from Initial, at the controls pose");
    check_state(c, &k_initial, "UP from Initial, at the controls pose");
    controls_free(c);

    /* DOWN's Real rung reads the controls pose, not the entry pose. */
    c = model_in(&k_art);
    controls_facts_t at_entry = facts(false, false, true, false);
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &at_entry), M_NONE,
               "DOWN from Art, at the entry pose");
    check_state(c, &k_art, "DOWN from Art, at the entry pose");
    controls_free(c);

    /* The M tap reads the controls pose. */
    c = model_in(&k_art);
    check_mask(controls_handle(c, CONTROLS_INTENT_MENU_TAP, &at_entry), M_NONE,
               "MENU_TAP at the entry pose");
    check_state(c, &k_art, "MENU_TAP at the entry pose");
    controls_free(c);

    /* The down into Art has no pose gate: the chrome and the shrink cut in together. */
    c = model_in(&k_initial);
    controls_facts_t nowhere = facts(false, false, false, false);
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &nowhere), M_SHOWN | M_ROOM,
               "DOWN from Initial, no pose settled");
    check_state(c, &k_art, "DOWN from Initial, no pose settled");

    /* A swipe during the shrink does nothing; the next one, after the pose lands, prints
     * Real. */
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &nowhere), M_NONE,
               "DOWN again, still mid-slide");
    check_state(c, &k_art, "DOWN again, still mid-slide");
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &at_controls), M_PAGE,
               "DOWN after the pose landed");
    check_state(c, &k_real, "DOWN after the pose landed");
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &at_controls), M_NONE, "DOWN at Real");
    controls_free(c);
}

/* --------------------------------------------------------------- the Menu */

/* The Menu closes itself from three places inside cassette_menu.c. The model pulls the fact
 * and adopts it, and the adoption is silent: the Menu has already done the work. */
static void test_menu_adoption(void)
{
    controls_facts_t closed = facts(false, false, false, true);

    /* A Menu that closed itself is noticed without any bit, and the level is untouched. */
    controls_t *c = model_in(&k_menu_real);
    check_mask(controls_handle(c, CONTROLS_INTENT_NONE, &closed), M_NONE,
               "the Menu closed itself");
    check_fields(c, CONTROLS_LEVEL_REAL, false, "the Menu closed itself");

    /* And the next UP is the ladder's again. */
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &closed), M_PAGE,
               "UP after the Menu closed itself");
    check_state(c, &k_art, "UP after the Menu closed itself");
    controls_free(c);

    /* The same call adopts and runs: the fact is read before the rule, so a DOWN that
     * arrives on the step the Menu closed is not swallowed. */
    c = model_in(&k_menu_art);
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &closed), M_PAGE,
               "DOWN on the step the Menu closed");
    check_fields(c, CONTROLS_LEVEL_REAL, false, "DOWN on the step the Menu closed");
    controls_free(c);

    /* While the fact says open, the Menu still swallows everything but UP. */
    controls_facts_t open = facts(false, true, false, true);
    c = model_in(&k_menu_real);
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &open), M_NONE,
               "DOWN with the Menu open");
    check_state(c, &k_menu_real, "DOWN with the Menu open");
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &open), M_MENU, "UP with the Menu open");
    check_state(c, &k_real, "UP with the Menu open");
    controls_free(c);

    /* A Menu reported open while the model is at Initial is not adopted - there is no
     * chrome for it to open over - and the intents wait on the fact all the same, so the UP
     * that would raise the Peek is refused. */
    c = model_in(&k_initial);
    controls_facts_t open_anywhere = facts(false, true, true, false);
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &open_anywhere), M_NONE,
               "DOWN while a Menu is reported open over Initial");
    check_state(c, &k_initial, "DOWN while a Menu is reported open over Initial");
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &open_anywhere), M_NONE,
               "UP while a Menu is reported open over Initial");
    check_state(c, &k_initial, "UP while a Menu is reported open over Initial");
    controls_free(c);
}

/* --------------------------------------------------------------- the repeats */

/* The same intent twice: the second one is NONE and the state does not move when the first
 * one already satisfied it. The ladder's rungs are the exception - the second swipe takes
 * the next rung once its pose has landed - and the guards are what make a quick double swipe
 * stop. */
static void test_repeats(void)
{
    controls_t *c;
    controls_changed_t mask;

    /* DOWN from Initial: the chrome once, then nothing while the pose has not landed. */
    c = model_in(&k_initial);
    controls_facts_t no_pose = facts(false, false, false, false);
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &no_pose), M_SHOWN | M_ROOM,
               "DOWN from Initial, twice (first)");
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &no_pose), M_NONE,
               "DOWN from Initial, twice (second)");
    check_state(c, &k_art, "DOWN from Initial, twice");
    controls_free(c);

    /* DOWN at Real is the end, twice. */
    c = model_in(&k_real);
    controls_facts_t at_controls = facts(false, false, false, true);
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &at_controls), M_NONE,
               "DOWN at Real, twice (first)");
    check_mask(controls_handle(c, CONTROLS_INTENT_DOWN, &at_controls), M_NONE,
               "DOWN at Real, twice (second)");
    check_state(c, &k_real, "DOWN at Real, twice");
    controls_free(c);

    /* UP from Initial at the entry pose: the Peek once, then nothing - UP does not lower
     * it. */
    c = model_in(&k_initial);
    controls_facts_t at_entry = facts(false, false, true, false);
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &at_entry), M_PEEK | M_ROOM,
               "UP from Initial, twice (first)");
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &at_entry), M_NONE,
               "UP from Initial, twice (second)");
    check_state(c, &k_peek, "UP from Initial, twice");
    controls_free(c);

    /* UP from Art is one rung: the chrome closes, and the next UP (from the settled entry
     * pose) raises the Peek. */
    c = model_in(&k_art);
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &at_controls), M_SHOWN | M_ROOM,
               "UP from Art (first)");
    check_state(c, &k_initial, "UP from Art (first)");
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &at_entry), M_PEEK | M_ROOM,
               "UP from Art (second)");
    check_state(c, &k_peek, "UP from Art (second)");
    controls_free(c);

    /* UP at Real takes the Page and the picture back; the next UP takes the chrome. */
    c = model_in(&k_real);
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &at_controls), M_PAGE,
               "UP at Real (first)");
    check_state(c, &k_art, "UP at Real (first)");
    check_mask(controls_handle(c, CONTROLS_INTENT_UP, &at_controls), M_SHOWN | M_ROOM,
               "UP at Real (second)");
    check_state(c, &k_initial, "UP at Real (second)");
    controls_free(c);

    /* The Menu twice: the second tap needs the fact the view would report after the first
     * one: an open Menu. */
    c = model_in(&k_art);
    check_mask(controls_handle(c, CONTROLS_INTENT_MENU_TAP, &at_controls), M_MENU,
               "MENU_TAP twice (first)");
    check_state(c, &k_menu_art, "MENU_TAP twice (first)");
    controls_facts_t open = facts(false, true, false, true);
    mask = controls_handle(c, CONTROLS_INTENT_MENU_TAP, &open);
    check_mask(mask, M_NONE, "MENU_TAP twice (second)");
    check_state(c, &k_menu_art, "MENU_TAP twice (second)");
    controls_free(c);
}

/* -------------------------------------------------------------- the invariants */

/* Every legal state x every intent x editing x the Menu fact x the three possible pose
 * facts, checked after every step: the mask is exactly the diff, the state keeps its
 * invariants, and the same step on a second instance gives the same answer. */
static void test_invariants(void)
{
    static const controls_intent_t intents[] = {
        CONTROLS_INTENT_NONE, CONTROLS_INTENT_DOWN,
        CONTROLS_INTENT_UP,   CONTROLS_INTENT_MENU_TAP,
    };
    /* A pose cannot be both poses at once, so the two "settled" facts are three cases. */
    static const bool settled[][2] = {{false, false}, {true, false}, {false, true}};

    for (size_t i = 0; i < COUNT(k_states); i++) {
        const expect_t *w = k_states[i];
        for (size_t j = 0; j < COUNT(intents); j++) {
            for (int editing = 0; editing <= 1; editing++) {
                for (int menu = 0; menu <= 1; menu++) {
                    for (size_t k = 0; k < COUNT(settled); k++) {
                        controls_facts_t f = facts(editing != 0, menu != 0, settled[k][0],
                                                   settled[k][1]);
                        char ctx[192];
                        snprintf(ctx, sizeof ctx, "%s, %s, editing %d, menu %d, poses %d/%d",
                                 w->name, intent_name(intents[j]), editing, menu,
                                 (int)settled[k][0], (int)settled[k][1]);

                        controls_t *a = model_in(w);
                        controls_t *b = model_in(w);

                        /* Adopt the facts first, so the step measured is the rule's and an
                         * empty mask means a byte-identical state. */
                        controls_handle(a, CONTROLS_INTENT_NONE, &f);
                        controls_handle(b, CONTROLS_INTENT_NONE, &f);

                        controls_state_t before, after_a, after_b;
                        controls_snapshot(a, &before);
                        controls_changed_t ma = controls_handle(a, intents[j], &f);
                        controls_changed_t mb = controls_handle(b, intents[j], &f);
                        controls_snapshot(a, &after_a);
                        controls_snapshot(b, &after_b);

                        /* The same step twice, on two instances, is the same answer. */
                        CHECK(ma == mb, "%s: the mask is not deterministic", ctx);
                        CHECK(same_state(&after_a, &after_b),
                              "%s: the state is not deterministic", ctx);

                        /* The mask is the diff: every bit is a reading that moved, and every
                         * reading that moved has its bit. The Page and the Cover are one
                         * reading - Real - so one bit. */
                        unsigned want = M_NONE;
                        if ((after_a.level == CONTROLS_LEVEL_REAL) !=
                            (before.level == CONTROLS_LEVEL_REAL)) {
                            want |= M_PAGE;
                        }
                        if ((after_a.level == CONTROLS_LEVEL_PEEK) !=
                            (before.level == CONTROLS_LEVEL_PEEK)) {
                            want |= M_PEEK;
                        }
                        const bool was_chrome = before.level == CONTROLS_LEVEL_ART ||
                                                before.level == CONTROLS_LEVEL_REAL;
                        const bool is_chrome = after_a.level == CONTROLS_LEVEL_ART ||
                                               after_a.level == CONTROLS_LEVEL_REAL;
                        if (was_chrome != is_chrome) want |= M_SHOWN;
                        if (after_a.menu != before.menu) want |= M_MENU;
                        if (after_a.room != before.room) want |= M_ROOM;
                        CHECK((unsigned)ma == want, "%s: mask 0x%02x, want 0x%02x", ctx,
                              (unsigned)ma, want);

                        /* The invariants, after every step. */
                        CHECK(after_a.level >= CONTROLS_LEVEL_PEEK &&
                                  after_a.level <= CONTROLS_LEVEL_REAL,
                              "%s: the level is not one of the four", ctx);
                        CHECK(after_a.room == room_of(after_a.level),
                              "%s: the room does not follow the level", ctx);
                        CHECK(!after_a.menu || is_chrome, "%s: a Menu without the chrome", ctx);

                        /* An empty mask leaves the state alone, and so does its repeat. */
                        if (ma == CONTROLS_CHANGED_NONE) {
                            CHECK(same_state(&before, &after_a),
                                  "%s: CONTROLS_CHANGED_NONE moved the state", ctx);
                            controls_changed_t again = controls_handle(a, intents[j], &f);
                            controls_state_t after_again;
                            controls_snapshot(a, &after_again);
                            CHECK(again == CONTROLS_CHANGED_NONE,
                                  "%s: a NONE step repeated is not NONE", ctx);
                            CHECK(same_state(&after_a, &after_again),
                                  "%s: a NONE step repeated moved the state", ctx);
                        }

                        controls_free(a);
                        controls_free(b);
                    }
                }
            }
        }
    }
}

int main(void)
{
    test_zero();
    test_seeding();
    test_ladder();
    test_stack_order();
    test_editing();
    test_settled_gates();
    test_menu_adoption();
    test_repeats();
    test_invariants();

    TEST_END();
}

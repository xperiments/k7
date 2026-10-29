#pragma once
/*
 * cassette_button - one button, built three times: the mode bar's cassette, sliders-v and led.
 *
 * A button is a 64 px circle at the centre of a 96x96 carcase, one character inside it, a
 * press look, a 160 ms click pulse and an action. The carcase itself is the bar's: it builds
 * the 96x96 cell and hands it here (the mode bar's three, the groups' six). This module
 * builds the two circles (the transparent ambient one that carries the broad shadow, and the
 * disc), the letter, and the press and the page ink inside that box; it creates no second
 * carcase, so the bar's cell and the disc inside it remain one rectangle each.
 *
 * One pressed state, not two, and one latch beside it. A press writes the ring widths - the
 * carcase's ring, which is the drawer's visible press, and the disc's ring, which is M's and
 * R's - and RELEASED or PRESS_LOST writes them back. The latch is not a press state: it is
 * what the button *is* while its mode is on, a filled k_col_primary disc with a k_col_bg
 * glyph, and set_on() repaints the whole disc's ink. The ring tables are spec data: see
 * cassette_button_ring_t and cassette_button_ring_w() in the portable cassette_button_rule.
 * Nothing is hidden or prebuilt, so a button is four objects rather than eleven.
 *
 * The page is the only ink the module owns. On the dark page the carcase is filled k_col_bg
 * and bordered k_col_button_rect, the disc is the palette's line art - a k_col_button_circle
 * ring and no face - and the letter is k_col_button_letter. On the white page the disc is the
 * button's cap: a cassette_circle_face() disc of cap.face when idle - the mode bar's is
 * k_col_dome - and of k_col_button_cap (0xBBBBBB) when the mode is on, with CASSETTE_CAP_RIM_W of
 * k_col_dome as its rim, the letter in cap.letter, and the carcase filled k_col_white and
 * bordered k_col_button_rect. The idle cap is spec data; the latch's ink is the module's, and the
 * dark page ignores the cap entirely.
 *
 * The tap guard is the module's and runs before the action and the pulse. A click whose touch
 * had a gesture direction or moved past LVGL's press limit is not a tap: no action and no
 * pulse. The rule is cassette_button_is_tap(moved, gesturing); one rule for all five
 * buttons.
 *
 * The action is a callback, not an id: a button knows nothing about the Menu, a group or the
 * audio. The mode bar maps its three to the Menu's toggle and the two group selectors. A NULL
 * action is a button whose click is the pulse alone.
 *
 * What is the bar's: the carcase object (its geometry, its clickability and its
 * gesture/event bubbling - the bar sets the bubbling per panel: the mode bar bubbles both,
 * the groups events only), the lattice, the visibility and the page. What is the module's: the
 * disc, the letter, the ring tables, the pulse and its pivot, and the page ink.
 *
 * Ordering: create once, then set_page() from the bar's page flip and set_label() /
 * set_action() from the host's own mode changes, in any order. The setters never
 * allocate and never change the child order; a setter on a handle that was never built is a
 * no-op, so a failed create cannot crash the screen. Under the LVGL lock.
 */
#include <stdbool.h>

#include "lvgl.h"

#include "cassette_button_rule.h"
#include "cassette_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* What a click means to the host, run after the guard and after the pulse has been started.
 * `ctx` is the value given with it, copied here so the button holds the pair. The callback
 * runs under the LVGL lock and may call back into set_label()/set_action() on this or
 * another button (the Menu closing does exactly that on < and >). */
typedef void (*cassette_button_action_t)(void *ctx);

/* What a button is told once. Everything here is data; the label and the action are the only
 * parts a host may change afterwards, and the cap is the only part it may re-ink. */
typedef struct {
    const char *name;                     /* identity, e.g. "PREV" or "MENU"; never drawn */
    const char *text;                     /* "P", "<", "M", or an icon glyph */
    const lv_font_t *font;                /* NULL: &jura_36. An icon glyph passes &xtape_36 */
    cassette_cap_t cap;                   /* the white page's face and letter; unused on the dark page */
    cassette_button_ring_t carcase_ring;  /* the host's rectangle: drawer {2,3}, M and R {2,2} */
    cassette_button_ring_t disc_ring;     /* the 64 px disc: {2,3} for all five */
    cassette_button_action_t on_click;    /* NULL: the pulse is the whole of the click */
    void *ctx;
} cassette_button_spec_t;

/* The handle. Host-owned, no allocation: the drawer keeps three, the block two. `carcase` is
 * the host's object and is NULL until a create succeeds; a host may test it to know whether
 * the button exists. */
typedef struct {
    const char *name;                     /* identity, copied from the spec */
    lv_obj_t *carcase;                    /* the host's 96x96 box: the press, pulse and page ink */
    lv_obj_t *ambient;                    /* the transparent circle carrying the broad shadow */
    lv_obj_t *disc;                       /* the 64 px disc: cap face, ring, and the letter */
    lv_obj_t *letter;
    cassette_cap_t cap;
    cassette_button_ring_t carcase_ring;
    cassette_button_ring_t disc_ring;
    cassette_button_action_t action;
    void *ctx;
    bool white;                           /* the page to repaint a press on */
    bool pressed;
    bool on;                              /* latched: a filled k_col_primary disc, k_col_bg glyph */
} cassette_button_t;

/* Build one button inside `carcase`, a host-made 96x96 opaque box. Creates the two circles
 * and the letter, inks the carcase (fill, border colour, released ring) and listens on it for
 * press, release, press-lost and click. Returns false and builds nothing if `out`, `carcase`
 * or `spec` is NULL; a zeroed handle is a button that does nothing. The carcase stays the
 * host's: this neither sets its position, its size, its clickability nor its bubbling. */
bool cassette_button_create(cassette_button_t *out, lv_obj_t *carcase,
                            const cassette_button_spec_t *spec);

/* Paint for the page. `white` is the white page's cap (disc face, CASSETTE_CAP_RIM_W of
 * k_col_dome, the cap's letter, k_col_white carcase) or the dark page's line art (the
 * palette's ring and letter, k_col_bg carcase), with the current press ring applied on top.
 * A button built while the page is dark looks dark until the view's flip calls this. */
void cassette_button_set_page(cassette_button_t *b, bool white);

/* Latch the button on or off, for a button that is a mode and not a moment: the left bar's
 * three group selectors, and the cassette's door while the Menu is up. A latched button is a
 * filled k_col_primary disc with a k_col_bg glyph on both pages - the idle look inverted -
 * keeping the white page's 4 px k_col_dome rim. The pressed ring still runs on top. A call for
 * the state already held does nothing; a handle that was never built is a no-op. */
void cassette_button_set_on(cassette_button_t *b, bool on);

/* The label and its font, changed together: the Menu's glyphs and &xtape_36 replace the
 * drawer's Jura characters. NULL text leaves the label alone. Never touches the ink. */
void cassette_button_set_label(cassette_button_t *b, const char *text, const lv_font_t *font);

/* The control's internal name, the spec's pointer: a static string the host owns, NULL for
 * none. It is identity, not the label; nothing here draws it. */
const char *cassette_button_name(const cassette_button_t *b);

/* The action a click runs, replaced as a pair so an old ctx cannot outlive its callback. A
 * NULL action clears it: the pulse is then the whole of the click. */
void cassette_button_set_action(cassette_button_t *b, cassette_button_action_t action, void *ctx);

#ifdef __cplusplus
}
#endif

# The bars are one module, and their panels are data

The Transport drawer and the DJ block are the same strip built twice: each slides in from its
edge over 260 ms, paints for the **Page**, raises itself and hosts a column of 96 px carcases.
The owner wants them as components - a left bar and a right bar - with a cell's control easy to
swap and the right bar able to show a different panel (the DJ block today, an RGB config later).
Three interfaces were designed and compared: a minimal one, a flexible four-level one, and the
common caller's. This record is the third.

## Implementation status

*Landed: the bars, their panels and the Menu's Pad.* The bars own the cells and the panel is a
value: `cassette_bars` builds one 96x96 carcase per cell of the installed panel's grid, makes it
clickable, bubbles its events and slides it, and it is handed a `cassette_panel_t` -
`columns`, `rows`, an optional `cell_rect()` for a plate, `build()` into each carcase,
`set_page()`, `shown()` and `gesture_bubble`. The panels own no carcase and register no root:
`cassette_buttons_create()` and `cassette_eq_create()` install their values with
`cassette_bars_set_panel()`, and the EQ panel's `build()` skips the bar's chrome cell; the
Menu's open and close swap the right bar's panel the same way. The left bar's 120 px panel
object is gone - the drawer's three cells are the drawer. The right bar's M
is the bar's chrome: the bar builds its carcase and its shared `cassette_button`, and its click
calls the `set_menu_cb()` hook the view installs, so M is present whatever panel is up and no
panel can strand the Menu. The Transport panel fills the left bar's three cells; the right
bar's other five are the EQ panel's when the Menu is down and the Pad's one plate when it is
up.

The decision text and the code agree on the cells, the panel value, the chrome, the two panels
and the Menu's swap. One consequence is now met and two landed differently from the text:

- **The Pad is the right bar's menu panel, and the covered state is gone.**
  `cassette_menu_open()` installs the Pad's panel value with
  `cassette_bars_set_panel(CASSETTE_BARS_RIGHT, ...)` and `cassette_menu_close()` restores
  `cassette_eq_panel()`; the value is 1x1 with `cell_rect()` returning
  `cassette_part_rect(CASSETTE_PART_PAD)` (596,76,186,284), no page ink, and its `shown` hook
  re-inks the plate white on both pages. `cassette_eq_set_covered()`,
  `cassette_bars_set_covered()` and the bar's covered field are deleted, and the EQ panel's
  knob values and edit layer outlive the swap; an open edit ends with the panel, as it does
  whenever the panel goes.
- **`t20_bars` was never created.** The grid and the hit rule are pinned in
  `host/tests/t16_layout.c` (`test_cells()`), not in a test of their own; the entrance, the
  page dispatch, the z-order and the feel stay board-only as the record says.
- **No 190x284 root was added for the right bar.** The block's rect is layout data
  (`CASSETTE_PART_BLOCK`) and its cells are screen children of the bar, like the left bar's, so
  no 190x284 root exists. The strips measured 187 objects before this work and 159 after
  (-28): the left lost its panel and the shared button dropped the two prebuilt states.

## Decision

- **One module owns both strips.** `cassette_bars` builds the left and right **Bars** as named
  parts. Each owns its rect, its 260 ms ease-out entrance and exit (reversed from wherever it is,
  hidden when the exit completes), the raise, the **Page** ink dispatch and the hit rule. It hosts
  one **Panel** at a time; showing another is a cut, not a slide.
- **A Panel is data.** A panel is a static table of **Cells** (the Transport drawer's 1x3, the DJ
  block's 2x3) or one plate (the **Pad**). A new panel is a table, not code; a new *kind of
  control* is code, and that is the line the module is drawn on.
- **A Cell owns the carcase; the control inks it.** The bar creates one root per cell at the
  lattice rect, with the 96x96 carcase, the shared 2 px border, the 8 px corner, the z-order and
  the hit rule. The control (a button, a knob, the Pad's plate) sets its page ink, its press look
  and builds its disc, crown and letter inside. Swapping a button for a knob never moves the
  grid.
- **M is chrome; R is EQ content.** M is the Menu's only door, so it lives in the right bar and
  is present whatever panel is up; a future RGB panel cannot strand the Menu. The four knobs and
  R stay the DJ block's cells.
- **The Pad is the right bar's menu panel.** The Menu opens by swapping the right bar's panel to
  the Pad and closes by swapping back, which deletes `cassette_eq_set_covered()` and the EQ
  panel's covered state. The Pad's own press classification and callbacks stay with it.
- **The grid lives in `cassette_layout`.** The two bars' rects, the cells, the pitch, the hit
  rule and the Pad's rect join the Frame and the poses in the portable module; `t16_layout`
  pins them on the host (the planned `t20_bars` was not needed). The bar's entrance, page dispatch and z-order are LVGL and stay board-only.
- **Buttons emit action ids; knobs keep their direct calls.** A button's press means an action
  (previous, next, Menu, nothing), not a parameter, so the bar's panels hand it to one sink. A
  knob's value *is* the parameter, so the DJ block keeps calling `mini_icecast_eq_set()` and
  `mini_icecast_gain_set()` itself. *Landed as a per-button host callback rather than an
  emitted id: the panel wires its own cells; the action is still opaque to the button.*
- **The bars land before the button.** The button now fills a cell, so the strip's cells must
  exist first. The bar's first step reparents the existing controls into transparent cells; the
  button step then moves the carcase into the cell.

## Considered options

- **The minimal bar** - three verbs, the bar owns the cells, the panel fills them, the Pad stays
  the Menu's overlay. The smallest surface, but it leaves `set_covered` and an M/R exception in
  place, and a future right panel has to remember the Menu door.
- **The four-level bar** - Bar, Panel, Cell, Control, with the modal editor owned by the bar.
  The most flexible, and the most machinery for two panels; the Pad's plate needs the Cell level
  to bend, and the editor belongs with the knob anyway.
- **The bar knows no cells** - a panel supplies its own cells and the bar only frames them. The
  smallest bar, but every panel re-implements the 96/94 grid, which is the duplication this work
  exists to remove.
- **A pager or scroll of panels.** Rejected: the horizontal swipe is the Controls' (ADR 0016 and
  0020), and the vertical pair is the ladder; a second panel is a mode, not a carousel.
- **M as an EQ cell.** Rejected: the Menu would be unreachable from any other right panel, and
  ADR 0022 makes M the Menu's door.

## Consequences

- `cassette_bars` absorbs `cassette_buttons.c` and the strip half of `cassette_eq.c`; their
  headers shrink to their panels' own verbs. `cassette_buttons_raise()`, both `set_cap()`s and
  `cassette_buttons_is_shown()` have no callers and are deleted rather than reproduced.
- The left bar loses its 120 px panel object (it becomes a value and a slide over its cells); the
  right bar keeps its six cells, with no 190x284 root. The strips measured 187 -> 159 objects
  (-28), a reduction rather than an addition.
- The cell owns the carcase, so the Transport drawer's pressed border is a style write on a
  shared object. The button keeps one pressed state, and ADR 0016's two-prebuilt-states note is
  amended with the button work.
- The Pad becoming a panel amends ADR 0022's consequence that the EQ panel owns the covered
  state; the Menu's rules do not change. **Met:** the Menu swaps the right bar's panel, and the
  covered state and its two verbs are gone (see the status note above).
- The button work depends on this record (a control fills a cell); the knob work follows the
  button. The grid's move to `cassette_layout` lands first, because the bar's cells and the
  button's geometry both read it.
- The host tier pins the grid and the cell rects in `t16_layout`; the planned `t20_bars` was
  never needed. The entrance, the page dispatch, the z-order and the feel stay board-only,
  like every other part of the screen.

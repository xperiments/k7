# The screen is built, then installed

**Amends [ADR 0024](./0024-the-bars-and-their-panels.md):** a **Panel**'s `create()` no longer
installs it. The bar, the panel value, the cells, the entrance and the Menu's swap are
unchanged; who calls `cassette_bars_set_panel()` is not.

Every module in the screen display is a file-static singleton, and its `create()` both built
its values and put it on the screen. That made `cassette_view_create()` a 23-call ritual whose
order was the only thing holding the picture together. The order was written down three times
- in `cassette_bars.h`, in `cassette_eq.h` and in the component's README - and checked
nowhere, because there is nothing to check it against: each module reports success by having
no object.

Its error mode was worse than a crash. `cassette_eq_create()`, `cassette_rgb_create()` and
`cassette_empty_create()` each installed their own value with the right **Bar**, so the last
one to run won. And `cassette_bars_set_panel()` refused a call that arrived before the bars
existed — but the panel had already marked *itself* built, so the retry was refused too, for
the life of the process. Boot the modules in the wrong order and the screen comes up with an
empty bar, no message, and no way back.

The same shape reached one part of the geometry. The **Peek**'s two tethers end on the left
hub's rim, the rim belongs to the tape body, so the view asked the tape body where the rim was
and pushed a pair of screen coordinates into the Peek on every pose step. `cassette_peek_create()`
reset those anchors to its own defaults, so the view had to push them twice — once before the
Peek existed and again after it reset them.

## Decision

- **Building and installing are separate calls, and the composition makes the second one.**
  A panel's `create()` builds values and creates no object. `cassette_view_create()` installs
  the left bar's mode panel with `cassette_buttons_panel()`, then the right bar's active Group
  through `groups_install()`, and nothing else calls `cassette_bars_set_panel()` except the
  Menu, which owns the right bar while it is up.
- **Two `create()`s stopped existing rather than losing a line.** `cassette_buttons_create()`
  was its install and nothing else; `cassette_empty_create()` only cleared a static that is
  already zero. They are deleted, not re-documented.
- **A late install is refused out loud.** `cassette_bars_set_panel()` before the bars exist is
  an `ESP_LOGE` naming the side, not a silent return. The order is explicit in one function
  now, so a late install is a bug in the caller; a queue would have turned a loud bug into a
  quiet delay.
- **A part that is given the pose works its own geometry out.** The hub's design coordinates,
  its 168 px rim and `cassette_hub_anchor(pose, index, top)` moved into `cassette_layout`,
  pinned by `t16_layout` at all three poses. The Peek is handed the pose; `cassette_tapes_hub_anchor()`
  and `cassette_levels_set_hubs()` are gone, the double call is gone, and the tape body draws
  its rim from the same numbers the tether lands on.
- **The rule is checked, not intended.** `host/tests/layering.sh` gains A1: `cassette_bars_set_panel`
  appears only in the implementation, the composition and the Menu, so a fourth module cannot
  quietly start installing itself the way the fourth panel would have.

## Considered options

- **Composition calls, modules obey (chosen).** No new API: the installers are the setters the
  bars already had. The interface tells the truth — "create builds; something else installs" —
  and the failure mode becomes a named error.
- **One installer function.** Moving the 23 calls into `cassette_screen_build()` would have
  relocated the sequence without changing what any module promises. It is still a sequence,
  and it is now readable where it is.
- **Handles.** `create()` returning an opaque handle the view owns, instead of 30 file-static
  singletons, is the larger answer to the same friction. Rejected for this change: it touches
  every module and it is an ownership decision, not an installation one. The singletons stay
  until something else needs them gone.
- **Deferred install.** A queue would make "install before the bars exist" order-independent.
  Rejected: nothing in the tree needs it, and two late panels would raise a question with no
  good answer — which one wins. The composition installs in order; a caller that does not is
  wrong.

## Consequences

- The screen's assembly reads as two blocks: every module builds, then three calls install.
  The order is still a sequence, but it is one a reader can follow and a reviewer can check.
- `screen_frame.c`'s HTTP diagnostics made this measurable rather than argued: all seven chrome
  scenes are pixel-identical outside the parts that move on their own (the reels, the VU columns
  and the footer's rotating faces), and the new scene 8 photographs the Peek's tethers meeting
  the rim at (251, 87) and (251, 217) — the literals the view used to push in.
- `cassette_eq_create()` lost its ignored `lv_obj_t *` parameter, so the three panel builders
  have one shape.
- A third bar-owning mode has to install its own panel or route it through the composition; the
  A1 rule says which files may, and a reviewer sees the list in the test rather than in a
  header's prose.
- The panel `shown()` hook is unchanged, including its timing: it is told the bar's *target*
  state when a slide starts, not the settled one. That is a separate seam and a separate
  decision.

**Status:** accepted and measured on the Sunton, 2026-10-30.

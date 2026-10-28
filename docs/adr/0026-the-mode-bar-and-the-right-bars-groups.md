# The mode bar, and the right bar's groups

The owner moved the left and right bars' contents in one go. The left bar stopped being the
transport drawer and became a **mode bar**: the cassette glyph opens and closes the Menu, the
sliders-v glyph selects the **EQ group**, the led glyph selects the **RGB group**. The right bar
stopped being one DJ block with a Menu cell ("M") holding its door, and became a bar that hosts
one **group** at a time - a speaker grille by default, the EQ, or the RGB panel - with every cell
belonging to the group.

Three things were driven by the owner's own wording and are worth recording because they replaced
older ADRs rather than extending them:

- **"Empty shows by default."** The group is not a setting that persists: it lives with the
  chrome. Hiding the bars drops the active group, so the controls open on the speaker every time.
- **"We can click on the active one to just hide both groups."** A group button is a toggle. One
  group at a time, and the same button again returns to Empty.
- **"On menu open/close does not close the right panel."** The Menu's Pad still takes the right bar
  for the mode - it is the Menu's only scroll surface - but the group is remembered across it and
  reinstalled on close, not reset.

## Implementation status

*Landed: the mode bar, the groups model, the Empty grille, the EQ's re-layout, the meter's move,
the RGB group, and the shared 3x editor.*

- `components/portable/groups` owns "which group is up" as three rules and one fact: the toggle,
  Back meaning Empty, and the reset when the chrome hides. `t26_groups` pins them.
- `cassette_buttons` is the mode bar; the three buttons carry the icon font's `cassette-tape`,
  `sliders-v` and `led` glyphs, and `cassette_button` grew its latched ink for them.
- `cassette_empty` is the Empty group: one 186x284 plate at the Pad's footprint with a staggered
  dot grid, painted by one draw callback, and an **inert** panel - the bar leaves its carcase out
  of the hit test and bubbles gestures, so the default panel does not eat a ladder swipe.
- The EQ group is `[VU][HI] / [VU][MED] / [Gain][LOW]`; `cassette_meter` moved from the
  cassette's design grid into the VU plate (one 96x190 surface over the inside column's top two
  cells: two 30 px dot columns lit from the bottom, an L/R pill under each) and lost its pose
  plumbing.
- The RGB group is `[Effect][Red] / [Effect][Green] / [Bright][Blue]`; `components/portable/rgb`
  owns the strip's state and `rgb_frame()`.
- `knob_editor` was extracted from `cassette_eq` - the overlay, the 3x dial, the timer, the blink,
  the dim and the click-to-value map - and takes a **bank**: the knobs, the cell at a point, the
  pillow's text, the repaint, the publish, the disc to blink and the shown gate. `knob_cell` is
  the 1x dial both panels build.
- `knob_model` grew a per-knob sweep: bipolar (the EQ's bands and trim, unchanged), unipolar 270
  degrees (the RGB channels and the brightness) and stepped (the effect selector), each with its
  own neutral - 50 for a dB knob, full for a level, OFF for the effect.

Deleted with the old chrome: the bar's chrome cell and `cassette_bars_set_menu_cb()`, the
drawer's Menu mode (`cassette_buttons_set_menu_mode()` and its Close/Back/Enter callbacks), the
transport actions (`mini_icecast_prev()`/`mini_icecast_skip()` from the chrome), the R
placeholder, and the parked meter on the cassette.

## Decision

- **The left bar is a mode bar, in one mode.** Three cells, top to bottom: cassette (the Menu's
  door), sliders-v (EQ), led (RGB). Each is a toggle; the group buttons switch straight across;
  the cassette's disc is lit while the Menu is open. No modal drawer mode: the left bar's buttons
  mean the same thing whatever is up, and the Menu is navigated by the Pad (Back swipe) and the
  rows (tap), which it already was.
- **The right bar hosts one group.** Empty (the default), EQ, RGB. The group is a panel value in
  `cassette_bars`'s sense, so a swap is a cut; every cell belongs to the group, and there is no
  chrome cell left. The Menu's Pad is not a group: it takes the bar for the mode and the group is
  reinstalled after.
- **The groups live with the chrome, and the group's own button is the only way back.** Hiding
  the bars resets the group. There is no Back cell in a panel - the mode bar's button toggles its
  group off - so no control inside a group can move the ladder either.
- **Empty is a picture, and it is the default.** One plate, one draw callback (~220 dots), no
  controls, gestures through. It is the surface the controls open on, so it must not block a
  swipe.
- **The EQ group keeps its audio and moves its cells.** Gain took the inside column's bottom cell,
  the meter took the two above it as one plate, and the three bands stayed in the outer column. The knobs' behaviour, ranges and the tone_eq path are unchanged.
- **The VU meter's home is the EQ group's `[V]` cell.** The parked instance is gone; the module
  builds into the cell, in cell coordinates, and is created and deleted with the panel's cells.
- **The RGB group publishes a portable state.** `components/portable/rgb` owns three channels, a
  brightness applied **last** and one effect from a fixed list; `rgb_frame()` fills a pixel buffer
  from `(state, now_ms, level, n)` and is pure, so every effect is host-tested. The firmware has
  no strip driver yet, so `cassette_rgb_set_publish()` is the seam a driver will hang from.
- **The effect selector is a stepped knob on a tall plate.** One slot per effect on the unipolar
  arc, neutral at OFF. Its cell spans the inside column's top two cells so the dial can sit above
  the effect's name, which is written under it at 1x and in the 3x pillow. The sector ring is the
  later refinement.
- **One 3x editor, one overlay, panels as data.** `knob_editor` owns the modal surface and takes
  a bank from whichever panel is up; `knob_cell` owns the 1x dial. A new knob panel is a table of
  cells, a bank and its publish, not a copy of the EQ module.

## Considered options

- **M stays as the right bar's chrome, and the left bar keeps three group cells.** Rejected by
  the owner: the Menu's door moved to the left bar, so keeping M would have left a dead cell in
  every group.
- **Back on Empty closes the chrome.** Rejected: the swipes own the ladder, and a Back that
  sometimes moves a rung and sometimes does nothing is worse than one that is simply absent.
- **The Menu stops taking the right bar** (the Pad moving into the window beside the rows).
  Rejected: the Pad is the Menu's only ratchet, and moving it would re-lay the five 486 px rows.
- **The group is sticky** (remembered across the chrome, like the Menu's list). Rejected by the
  owner's "shows by default": the controls open on the speaker every time.
- **Duplicate the 3x editor in the RGB module.** Rejected: two overlays, two timers and two dials
  that drift the moment either is tuned.
- **Keep the RGB knobs on the bipolar dial.** Rejected: a colour control whose "off" and "full"
  are the same pixel at six o'clock reads as a pan knob, and the neutral at 50 is nobody's home.
- **Keep a strip driver out of scope and say so.** This record does not add one: no pin, no
  chipset and no pixel count were decided, so the seam is the deliverable.

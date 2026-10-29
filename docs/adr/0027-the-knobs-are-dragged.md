# The knobs are dragged, and the dial says what its ends mean

**Amended by [ADR 0028](./0028-the-pad-drives-the-knob.md):** the 3x dial takes no touch. The drag,
the plaque tap and the centre-circle close are gone from it; the right bar shows the **Pad** while
a knob is open, and the Pad ratchets the value, cycles the knob's stops on a tap and closes on a
left swipe. The plaques stay as a reading and lose their target. The click that ends a drag is
dropped by the drag itself, so trimming a knob no longer opens it.

**Amends [ADR 0018](./0018-dj-eq-knobs-and-transport-arrows.md)** (the knob's shape, its ink and
its gesture) **and [ADR 0026](./0026-the-mode-bar-and-the-right-bars-groups.md)** (the shared
editor and the per-knob sweep). Read this one where they disagree.

The owner came with a photograph of a hardware knob and five premises, and the premises turned
out to be one decision: the dial is a place, the finger is how you move it.

- "Remove the circular dots in the knobs"; "don't use dots for the position indicator, better
  small lines that point to center".
- "Allow angle limiting in the knobs, by example 270 centered at 135 deg for the hi/mid/lo (like
  the first image) ... only adding somehow the pillows like HI / -26 +6, with our real values."
- "For the big knob view we need to carry with the pillows before correctly placed and remove the
  current bottom ones, so each knob can have different fixed positions, tapping the pillow moves
  to the position."
- "EQ knobs does not work by tap/click but vertically adjusting with touch scrolling, with snap
  points to the 3 edges."
- "For those that are selection and not range, a small bottom pillow with the selected value must
  show in the small knob when selection finishes."

The scope of this pass is the **EQ group** (four knobs, two labelled ends each). The LED/RGB
group keeps working but is not designed here: it inherits the vocabulary, and its own record
still owes it a look, the five-position selector included.

## Implementation status

*Landed. Nothing about the audio path, the ranges or the panels' cells changed.*

- `knob_model` has **one arc**: 270 degrees, -1350 at 7:30, +1350 at 4:30, `(value - 50) x 27`.
  `knob_angle_for_value()` is the one formula the view reads, so the printed marks and the value
  marker cannot drift apart.
- **Cut is on the left, boost on the right.** `-26` sits at 7:30 and `+6` at 4:30, which reverses
  ADR 0018's "the right side cuts and the left side boosts". This is the photograph's layout, and
  it makes the dB knobs agree with the level knobs that already start at 7:30.
- `knob_value_at()` - the click-to-angle inverse - is **deleted**, with the bipolar seam test and
  both `knob_dot_*` predicates.
- **`knob_mark_t`**: a labelled position, `{ value, text, plaque }`, set with `knob_set_marks()`
  and read back by `knob_label()` (the mark the knob is on, else its name). The bank's `label()`
  hook is gone; RGB's effect names are marks now, so the dial's pillow says RAINBOW with no hook
  at all.
- **The magnet**: `knob_snap()` pulls onto the knob's own positions - its neutral, its marks, and
  every slot of a stepped knob - within `KNOB_SNAP_STICK` (3), and within `KNOB_SNAP_RELEASE` (5)
  for the position it is already on. `knob_drag_value()` applies it to a drag.
- `knob_cell` is **one dial at two sizes** (the 44 px cell and the 256 px 3x), so there is one
  ring painter, one stick, and one cap for both. The printed marks are two ends plus the detent
  where a bipolar knob has one; the graduations are the 3x's alone, every 22.5 degrees.
- The ring is **one object with one draw callback**, not twenty-four dot objects: a value move is
  one invalidation and one rotation instead of twenty-four colour writes.
- `knob_editor` puts the **pillow above the dial** (the knob's label, or the live value while a
  drag is in progress, for a second after the finger lifts) and draws a **plaque per marked
  position**, filled when the knob is on it. A tap on a plaque jumps there; a tap on the centre
  circle closes; a tap anywhere else does nothing.
- The **drag**: 6 px dead zone, 2 px a step at 1x and 4 px at 3x, measured from the value the
  press began on, published live on every step.
- A **stepped** knob also shows its selected value in a bottom pillow at 1x, which RGB's effect
  plate now uses in place of its bare label.
- `t22_knob` is rewritten: the arc and its sign, the marks and their labels, the magnet's two
  bands, the drag's arithmetic, and the edit ordering unchanged.

## Decision

- **One arc, and it is the whole circle minus the bottom.** 270 degrees for every sweep, min at
  7:30 and max at 4:30, a dead 90 degree gap below. A value is a place on it; there is no seam, so
  no value shares a pixel with another.
- **Twelve o'clock is the neutral, and the two sides are equal in degrees, not in dB.** A band
  knob's cut side gives 26 dB over 135 degrees and its boost 6 dB over the same 135: the pointer's
  pace is uneven and the two plaques say so. The alternative - a linear dB arc - would put 0 dB at
  about four o'clock, so the value a double tap returns to, and the one a knob rests on, would sit
  off to one side of a dial whose whole point is that it rests at twelve.
- **Dots are gone; the ring is lines pointing at the centre.** Two ends and the detent are printed;
  one brighter, longer line marks the value. Graduations exist on the 3x only, because that is
  where they can be read, and they cost nothing at 1x.
- **A knob's labels are its own data.** Marked positions live in `knob_t`, each with its value, its
  text and whether the dial prints it. Their **angles come from the model**, never from a
  coordinate table, so a position can never drift from the value it names; "each knob different
  positions" is a consequence, not a config.
- **The drag is the only way a value moves.** A tap is how you *look* at a value (it opens the 3x
  dial after 260 ms), a double tap resets, a plaque jumps. Aiming at an angle is gone.
- **The magnet holds a detent.** Three values of stick and five of release, so a drag passes
  through 0 dB with a small push and a knob left alone sits exactly on it - which is also the only
  way to get a true bypass without hunting.
- **The dial says the value while you are moving it.** The pillow takes the number (a signed dB, a
  percentage, or a selector's marked slot) while the finger is down, and goes back to the knob's
  label a second after it lifts. The 1x cell has no room for text, so the number lives where the
  reading happens.
- **A selector's value shows at 1x.** A stepped knob's panel puts a bottom pillow under the cell
  and keeps it filled with the selected mark's text; that is the "selected value" the owner asked
  for, and it is why the pillow is the cell's, not the editor's.
- **Both panels share all of it.** No fork: the EQ and the RGB group get the same dial, drag and
  editor, and the RGB rework will change its own layout and label density, not this vocabulary.

## Considered options

- **Keep tap-to-angle for coarse moves and add the drag for fine ones.** Rejected by the owner:
  two gestures on one control, and a tap that sometimes sets a value and sometimes opens a dial.
- **Keep the dots and make them smaller.** Rejected: the crown is the thing the owner objected to,
  and at 1x a line is cheaper than a dot both to draw and to read.
- **A per-knob table of plaque x/y positions.** Rejected: it duplicates the arc's arithmetic in
  every panel and drifts the moment a range changes. Angles from values give the same "fixed
  position per knob" with nothing to maintain.
- **Linear dB across the arc.** Rejected above: it moves the neutral off twelve.
- **Snap on release instead of while dragging.** Rejected: you would only discover the detent
  after letting go, and a deliberate "just off zero" would be impossible.
- **Delete the 3x view, since the drag now sets the value.** Rejected: it is where the marks, the
  graduations, the plaques and the number live, and the owner wants the photograph's dial. A drag
  does not open it - only a tap does.
- **Delete the double tap and put reset on the detent mark.** Rejected: the mixer's reset is one
  gesture today, and the magnet makes 0 dB reachable but not instant.
- **Fork the dial for the two sizes.** Rejected: two ring painters that differ the first time one
  is tuned.

## Consequences

- `knob_model` now knows about label *text*, not just values. It already carried a name, so this
  is a widening of the same idea rather than a new one, and it is why the bank lost its label hook.
- The 3x surface can no longer set a value by aim. Only a drag and the plaques can, which makes
  the plaques load-bearing for a knob whose ends matter.
- The right bar's cells still swallow gestures, so a drag on a knob is never a ladder swipe; the
  ladder simply has one fewer surface, as it already did.
- The knob's 24 dot objects per dial are gone, so the EQ group builds twenty fewer objects and
  the 3x dial twenty-three fewer.
- A knob's marks are copied into `knob_t` (8 slots), so a panel's static table can live on the
  stack. The RGB group's six effect names are marks now, which removes the last panel-specific
  text path from the editor.
- Anything that read `knob_value_at()` must move to the drag; nothing in the build does.

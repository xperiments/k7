# A multi-value knob's labels sit in a box, not on the ring

**Amends [ADR 0029](./0029-the-dial-is-quiet.md)**, which put one plaque per position around the
dial, and leaves [ADR 0027](./0027-the-knobs-are-dragged.md)'s two-ended plaques alone.

The owner, looking at the effect selector's six plaques: *"has puesto los labels en forma
concéntrica. Vamos a otorgar un máximo de 7 values para este tipo y distribuimos los labels: 3
derecha vertical, 1 central arriba, 3 izquierda alineados. No tienen por qué estar todos, pero así
me entiendes. Haz caja con su container y un padding de 24 px."*

On the ring, a value list fights the dial: the labels sit at 45°-ish angles where words run
diagonally, the wide ones collide with the ring's ticks, and reading them means following a circle
the thumb is not touching. A list wants a column.

## Resolution

- **Two labels or fewer stay on the ring** - a range knob's two ends, at 7:30 and 4:30, which is
  the photograph this whole line of work came from. More than two is a **value list** and goes in
  the box.
- **Three or more go in the box**: at most **seven**, being three stacked on the left, one above
  the dial, and three stacked on the right. Fewer than seven fill the sides evenly and drop the
  one above - which is why six has no top label.
- **The order across the screen is the arc's order**, which means the left column reads
  **upwards**: its lowest value sits at 7:30 and its last at 10:30, the median goes above, and the
  right column reads **downwards** to 4:30. So OFF is the bottom-left label, LEVEL the
  bottom-right, and the eye climbs one side and falls the other exactly as the pointer does. (The
  first cut of this had both columns reading down, which put OFF where BREATHE belongs.)
- **A side with one label sits level with the dial's centre**, two straddle it (±38 px), three go
  top, middle and bottom (±76 px).
- **The columns hug the dial and share an edge.** The left column is left-aligned on the box's
  padding, the right column right-aligned on the far padding, so a narrow label (`OFF`) lines up
  with a wide one (`BREATHE`) instead of floating in the middle of its column.
- **The box is the dial's container**: 456 x 284 px - the right bar's own height - with a 24 px
  padding, transparent, and the dial inside it at (100, 14) so the dial's centre is the bar's
  centre. Everything a knob prints lives in one rect that the editor can move, show and hide as
  one thing. The padding is the gap between the labels and the box's own edge; the ring keeps its
  own 8 px from the columns.
- **A range knob's composition drops 18 px; a selector's does not.** A range carries a title pillow
  above the dial and two plaques under it, so its ink is not symmetric about the dial: centred on
  the bar, the pillow was glued to the top edge and the plaques floated 46 px off the bottom. The
  drop leaves about 21 px at the top and 28 at the bottom. A selector has no pillow, so its ink is
  the labels' columns and it stays centred.
- **The filled label is still the value.** Nothing else marks the selection.
- **A label is primary, not tick.** The dial's own marks and value line use the brighter tick
  orange; a plaque drawn in it read as a second, yellower ink. Unselected is the primary outline
  and text; selected is the primary fill with black on it, so the position is what is highlighted
  and not the word.

## Considered options

- **Keep the labels on the ring and just space them out.** Rejected: that is the "concéntrica"
  the owner rejected; at 45° a word is unreadable at 20 px.
- **One column, either side.** Rejected: the dial would sit off-centre, and seven labels would
  need a 260 px tall column with the dial beside it, not inside it.
- **Two columns without the one above.** Rejected by the request: seven values need the odd one
  somewhere, and above the dial is where the title pillow used to be - the only free slot.
- **More than seven, scrolling or shrinking.** Rejected: a selector with more values than that is
  a list, and the Menu is the screen's list.
- **Left-align both columns.** Rejected for the right one: it would leave a ragged gap against the
  box's edge and overlap the ring at its widest label.

## Consequences

- The view caps a multi-value knob at **six** labels (`SLOT_MAX`, and `SLOT_MIN` marks where the
  list begins) while the model still allows eight marks. A panel that asks for more than six gets
  the first six, in arc order.
- A column's x is fixed by the box, so the labels no longer drift with the dial's geometry: a
  change to `PLAQUE_R` moves only the two-ended ranges.
- The container is wider than it was (456 vs 360): the plaques no longer rely on the box being
  wide enough for a ring, they *define* how wide it has to be.
- The effect selector's dial is legible for the first time as a list of six words; whether six is
  still the right count is the RGB group's own rework.

# The cell names its knob, in a letter

**Amends [ADR 0027](./0027-the-knobs-are-dragged.md).** That record put a knob's *value* in the 3x
dial's pillow and the name there with it, and left the 96 px cell with no text at all: "the 1x cell
has no room for text, so the number lives where the reading happens." That was true of the number.
The name had no home at 1x, so a cell was a shape with two ends and no identity.

The owner, after using the knobs - and then, after seeing the cells named in full:

- "I will like to have a round rect pillow on top of the knobs with his name (same font size that
  vumenter l/r) at top-left of the cell."
- "Colors: artline: background primary, text black; white: background black, text white."
- "One typo of myself is not med is mid."
- "For this Cell naming use a shorter version. We can simply do H/M/L and G for gain."
- "Also pad at least 3px the pillow now will be shorter."

## Decision

- **The cell prints a short name, the dial the long one.** The panel hands `knob_cell` the label
  it wants - `G`, `H`, `M`, `L` for the four EQ knobs, `R`, `G`, `B`, `BRI`, `FX` for the RGB
  group's - and `knob_name()` keeps the identity the 3x pillow prints (GAIN, HI, MID, LOW). A
  96 px cell is a badge, not a place to spell anything out; the dial is where the names live.
- **The pillow is a badge on the cell's ink**: a 14 px round rect inset `KNOB_NAME_PILL_INSET`
  (10 px, the owner's) from the cell's top and left, `lv_font_montserrat_14` (the VU meter's
  L/R), sized to its text with 12 px of padding, and hidden while it has no text. It is placed by
  the panel, like the value pillow, and it is set once - it does not move when a value does.
- **The dial gives the pillow its row.** `KNOB_CELL_DIAL_Y` is 10, the same inset: the ring's
  top mark is 16 px inside the box (r=32 on a 96 px box), so the dial's ink begins at 26, just
  under the pillow's row (10..23). The effect selector's tall plate uses the same row, so the two
  panels agree without a second rule. The cap ends up 10 px below the cell's centre: the pillow
  and the ring read as one group, and the group is what is placed.
- **The pillow turns over with the page.** On the line art it is a `k_col_primary` fill with the
  page's near-black ink on it; on the white page it is a `k_col_bg` fill with
  `k_col_button_white`, the palette's "reads white" ink. A filled primary pill on paper would
  read as a button, and a black one on the line art would read as a hole; this pair keeps the
  badge a badge on both rungs.
- **The band is `MID`**, not `MED`. It was the owner's typo from the first sketch of the DJ block;
  the label, the knob's table, the ids, the comments and the specs all say `MID` now.

## Considered options

- **The full name in the pillow** (GAIN, HI, MID, LOW). Rejected by the owner: it makes a 96 px
  cell mostly text - a six-letter name is 66 px of pillow - and the dial is the surface with room
  to spell a control out.
- **A fixed-width pillow** so the corners line up down the bar. Rejected: a box wide enough for
  BRIGHT is nearly the whole cell, and one letter in it reads as a mistake.
- **Flush with the cell's corner** (the first cut). Rejected by the owner: the pillow's rounded
  corner merges with the cell's border and the badge reads as a tab hanging off the carcase. He
  asked for "at least 3 px" and then tuned the inset and the dial's row to 10 on the panel.
- **Put the name in the bottom pillow** the selector already uses for its value. Rejected: that
  pillow is the value's - it changes as the knob moves, and on a range knob there is no bottom
  pillow to put it in.
- **A second name in the model** (`knob_set_short_name()`). Rejected: the short label is the
  panel's presentation of its own control, an argument to the one call that builds a pillow, and
  the portable model has no business knowing that a cell is 96 px wide.
- **Set it in `jura_20`, the plaques' font.** Rejected: 20 px fits neither the pillow's row nor
  the cell, and the meter's badge is the precedent for a small label on this screen.

## Consequences

- Every knob says which control it is without being opened; the 3x dial's pillow repeats the long
  name, which is a title and not new information.
- The 96 px cell's dial sits 10 px lower than it did before the pillow. Its ring's top ink is 26 px
  from the cell's top, its ends stop 10 px above the bottom, and the pillow is what the eye takes
  as the top margin.
- The effect selector's plate prints two texts: FX at the top-left, the selected effect at the
  bottom. The second is ADR 0027's "small bottom pillow with the selected value"; the first is
  this record.
- Two constants are the pillow's geometry and the RGB plate shares one of them:
  `KNOB_NAME_PILL_INSET` and `KNOB_CELL_DIAL_Y`, both in `knob_cell.h`.
- No host test reaches it - the pillow is LVGL, and `t22_knob` pins the model, not the ink.
  `CONFIG_APP_UI_DUMP_SCENE=2`, `=3` and `=5` are how the two panels and both pages were
  photographed.

# The value marker is half its length

**Amends [ADR 0027](./0027-the-knobs-are-dragged.md)** (the line that marks the value) **and
[ADR 0028](./0028-the-pad-drives-the-knob.md)** (its sizes: 4 x 11 at 1x, 5 x 18 at 3x).

The owner, after the dials got their names:

- "Make the outside knob position line shorter, maybe half."

## Decision

- **`value_len` halves on both dials: 11 → 6 at 1x and 18 → 9 at 3x.** The marker is drawn from
  `ring_r - value_len` to `ring_r`, so its outer end stays on the rim and its inner end moves out
  onto the band the printed marks already occupy (6 px at 1x, 8 at 3x). At 1x it is now exactly
  as long as a printed mark; at 3x it is one pixel longer.
- **What makes it the value is its width, not its length.** 4 px against the marks' 2 at 1x, 5
  against 2 at 3x, with a round cap on both ends - so the dial reads as a rim of ticks with one
  heavier one, instead of a clock hand reaching in towards the cap.
- **Both sizes, not only the cell.** The two dials share one draw callback and one idiom; leaving
  the 3x marker at 18 would make the app's biggest dial the one place the old hand survived.
- **The ink is unchanged**: the same `k_col_eq_tick`, drawn last, over the marks.

## Considered options

- **Only the 96 px cell.** Rejected: "the outside knob position line" is the same line at both
  sizes, and the two dials are one idiom down to the draw callback.
- **Halve the visible part only.** The marker starts 1 px inside the cap's rim already, so the
  arithmetic lands on the same 6 px; there is nothing to separate.
- **A 3 px tick.** Rejected: at 1x it would vanish inside a 2 px mark at the same radius, and the
  value would be told only by ink that is already the marks'.
- **Keep a longer line at 3x to preserve the ratio** (18 is 21 % of the ring, 11 is 34 % of the
  cell's). Rejected: "maybe half" is the owner's number and the 3x ring has graduations to read
  against, so it does not need the length to be found.

## Consequences

- ADR 0028's "5 x 18 at 3x, 4 x 11 at 1x" is superseded by 5 x 9 and 4 x 6.
- The band between the cap and the marks is empty ink now (at 1x the marker begins at r=26, 4 px
  outside the 22 px cap), so the ring reads as a rim of ticks rather than as a hand.
- A knob resting on one of its marks prints the marker exactly over the mark - same length, twice
  the width, round ends - which is the heavier stroke ADR 0028 already described.
- Nothing else moves: the graduations' 6 px, the printed marks' 6 and 8, the detent and the ends
  are the same, and the pillow's row above the dial is untouched.

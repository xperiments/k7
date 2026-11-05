# Real's picture takes the whole panel, behind two fingers

The owner wants the thing Real shows — the Track's picture printed on the cassette, seen
through the pill window — at the **entry pose**, filling the panel, reached by a two-finger
swipe up from the default screen and left by a two-finger swipe down.

The four rungs (ADR 0025) are a vertical stack and the room is a function of the level
(`controls.c:29-38`), so this cannot be a fifth rung: a new value above Real would put a fifth
entry in the arithmetic `level + 1`, and it would renumber every exact-diff mask in
`host/tests/t24_controls.c`. Nor can it be a pose: the Card is authored in the Controls pose's
screen pixels and does not scale (`cassette_card.h`), so the entry pose has to be a second
*layout* of the same three objects, not a bigger one.

## Decision

- **A flag, not a rung.** `controls_state_t` gains `full`, valid only while the level is
  Initial: `CONTROLS_INTENT_FULL_ON` sets it, `_OFF` clears it, any step off Initial clears it,
  and the model repairs the invariant where it already repairs the Menu's (`controls.c`). It
  has its own changed bit, `CONTROLS_CHANGED_FULL`, and no ROOM bit: both sides are the entry
  pose, so the change is a cut.
- **The page and the cover are one reading.** `CONTROLS_CHANGED_PAGE` now means "Real's rung
  **or** the full-screen flag", so the view flips the page and cuts the print in for either.
  FULL is applied before PAGE: `page_set()` clamps the packs to `cassette_card_window()`, and
  that window is the layout's.
- **Two Card layouts, two masks.** `cassette_card.c` carries the Controls rect (426x272 at
  136,82) and the entry grid it was derived from (-8,-23 814x520), each with its own A8 mask —
  116,416 B and 424,320 B, both built at create. The full one is built up front rather than on
  the gesture, which would otherwise carry a 424 KB allocation. The entry grid bleeds off every
  edge of the 800x480 panel (8 px left, 6 right, 23 top, 17 bottom), which is how it was drawn
  to; the window becomes 556x166 at (120,120), exactly the band the two hubs sit in.
- **One tape, seen through a hole.** The Card and the print keep the mask design (ADR 0039):
  the window is an opening, and the tape under it is the picture. Nothing is drawn twice.
- **The two-finger swipe is the adapter's.** `touch_gt911.c` reads the two contacts and reports
  the midpoint's travel once per touch — ADR 0025's removed detector, restored. `screen_input`
  turns a mostly-vertical pair into `FULL_ON` (up) or `_OFF` (down), latches the rest of the
  touch so it cannot also take a rung, and reports nothing for a horizontal pair.
  `screen_display.c` — the one file allowed to name the board — registers the callback.

## Considered options

- **A fifth rung `CONTROLS_LEVEL_FULL`.** Breaks "the enum is the stack" and renumbers t24's
  exact masks, for a screen that is not on the ladder at all.
- **Animate the Card to the entry pose.** The Card has no pose and its mask is object-sized;
  a scaled mask would be sampled 1:1 and clip the wrong shape.
- **The porthole** (paint the window above the print instead of cutting it). Built and flashed
  on `ui/full-screen-real`; it starved the stream at 25 Hz and left the window flashing white
  now and then while the reels turned. Kept behind `CARD_MASK 0` for the record, off by default
  (`docs/research/full-screen-real-cassette.md` §4.1).
- **A long-press as a second door.** Left out: the gesture is the owner's, and a hidden door
  would be a second way to the same state before the first has been proven.

## Consequences

- 540,736 B of masks at screen start (on a heap with 1,923,724 B free after the cover with a
  card mounted), and the real cost of the flag is that every layout has its own buffer.
- A two-finger swipe at Initial fills the panel; a two-finger down, or any one-finger step off
  Initial, puts it away.
- The reels at the entry pose are the body's own, drawn once and seen through the Card's
  window: no second copy, and no per-tick repaint of the window.
- **The gesture is not proven on the panel.** The GT911's two-contact report is unrecorded —
  ADR 0014 said the board pass was owed and ADR 0025 removed the detector before it was paid.
  The shipping build logs `[GDBG] contacts=` on every change of the contact count, so the first
  press settles it; until it prints 2, the full-screen picture cannot be reached by hand.
- `t24_controls` gains the flag's transitions; the existing exact masks are unchanged because
  the new bit is zero everywhere they tread.

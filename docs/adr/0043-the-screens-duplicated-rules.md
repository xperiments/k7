# The screen's duplicated rules become values and tables

ADR 0042 split building from installing and moved the Peek's anchor into the layout. This
records the four deepenings that followed, because they are one decision seen four times: a
rule that exists twice is a rule that will drift, and the screen display had four of them.

## What was duplicated

- **The timeline's arithmetic.** The Peek and the footer band draw one instrument (ADR 0032
  put its *geometry* in `cassette_layout`), and each carried its own `format_time()`, its own
  permille and its own fill width. The band derived the fraction from `elapsed/duration`; the
  Peek trusted the model's `permille`; the model computed it a third time.
- **The chrome's entrance.** Seven pieces - two Bars, two bands, the timeline, the window, the
  Peek's bar - each cancelled its own animation, decided its own reversal start, kept its own
  "already shown" flag and remembered to hide at the end.
- **The knob panels' shell.** The EQ and RGB panels shared the 2x3 cell map, the two-cell plate
  rect (byte-identical), the five cell handlers, the tap guard, the editor's bank, the shown
  gate with its cell-swap exception, the page walk and the `cassette_panel_t`.
- **The Menu's open state.** It opened through the Controls model and closed itself from
  inside its own view, so the model adopted a polled fact and the view patched the latch the
  self-close left stale. The header documented the adoption as an exception, which is the
  interface admitting it had two owners.

## Decision

- **The timeline is one rule** (`components/portable/cassette/timeline.c`): the clock text, the
  permille from two numbers (64-bit widened), the fill width from a permille and a track's
  width. The model, the band and the Peek all call it; `t27_timeline` pins the edges. What
  stays per host is deliberate: the band has to derive a position because it has nothing else,
  the Peek keeps the model's throttled one, and each keeps its own cache and idle presentation.
- **The entrance is one value** (`components/portable/cassette/entrance.c`) and one display
  performer (`chrome_entrance.c`). A repeat is not a restart, a reversal starts at the pixel
  the piece has reached, and a piece is hideable only when it has finished leaving.
  `t28_entrance` pins those three, which is what seven hand-written copies had to get right.
- **A knob Group is a table** behind `knob_panel.c`: cell map, names and shorts, ranges and
  marks or slots, an optional extra cell and ink, and one publish hook. The EQ and RGB files
  are those tables now (197/221 code lines become 83/95 behind a 215-line shell). The bank
  gate that keeps the editor's bank while the Pad holds the cells is written once, with the
  panel threaded through `ctx` instead of a file-static.
- **The Menu has one owner.** `CONTROLS_INTENT_MENU_CLOSE` makes closing an intent like
  opening. `facts.menu_open`, the adoption exception, `menu_restored()`, the restore hook and
  the 40 ms poll that existed to notice a self-close are all gone; every close now produces a
  `MENU` bit, which is what the view applies.

## Considered and rejected

- **Sharing the Peek's objects with the band.** ADR 0032 rejected it and that stands: these
  changes share arithmetic and rules, never objects.
- **A common base for the panels.** Inheritance would make a Group a subclass; a table makes it
  data. The table is the smaller interface and the one a third Group is written in.
- **Keeping the Menu's adoption.** The adoption was one line and the exception was a paragraph
  of header; the cost of removing it was a new intent. The exception *was* the friction.
- **A generic performer with more callbacks.** The entrance takes two callbacks and no
  callbacks at all for the pieces that only hide; a third (begin/end) would have been interface
  for its own sake.

## Consequences

- Each of the four has a host test where it had none: `t27_timeline`, `t28_entrance`, and the
  rewritten `t24_controls`. `knob_panel` is display-tier and has none, which is the remaining
  gap and is why it was worth doing the other three this way.
- The plate geometry of a 2x3 Group is baked into the shell's map, as it was in both copies.
  A differently shaped Group needs it handed in.
- `cassette_card_window()` now returns an empty rect when no Card was built: the headless board
  was clipping the tape's packs to a window that has nothing behind it. That is a board-seam
  defect, and it is fixed at the Card rather than behind a capability value - the seam itself
  (card 6 of the review) is still open.
- Verified on the panel, per card: `tools/screen_over_http.py capture` before and after, and
  `compare` reports **0 px** outside the reels, the VU columns and the footer's rotating faces
  for all seven comparable scenes. The headless board compiles with all four.

**Status:** accepted and measured on the Sunton, 2026-10-30.

# The Home group is four keys

The right bar's default group was a picture: one 186x284 plate at the Pad's footprint with a
staggered grid of 4 px dots, painted by one draw callback, and an `inert` panel value that kept
its carcase out of the hit test so a swipe could pass through it. ADR 0026 chose that
deliberately - "Empty is a picture, and it is the default" - because the group had no content.

The owner gave it content, in his own layout:

> I will like to refactor the GROUPS_ID_EMPTY that now is a group without content to have
> 11 / 23 / 44 buttons, where 11 and 44 are 2x1 cells, and 2 and 3 normal cells. The 2x1 button
> is graphically exactly as the normal button with bg in 2x1 size and the circle/dome etc is
> aligned right.

...and then, on seeing it drawn:

> hide the dome & shadows in the 2x1 button

Three rows: a key across both columns, two ordinary keys, and a key across both columns again.
Both readings of the wide key are recorded here, because the second one is what shipped.
What the keys *do* is not decided; the decision here is the shape, and it is recorded because it
reverses 0026 in writing and because a merged cell is the first thing in the bars that is not one
cell to one carcase.

## Decision

- **The grille is deleted.** The dot grid, its draw callback and the module's one ink flag go
  with it. `cassette_empty` becomes `cassette_home` and the group is `GROUPS_ID_HOME` - still the
  default, still what a second tap on a lit group button returns to, still dropped when the
  chrome hides - because the group's meaning is "no mode is selected, and this is the way back",
  which outlives its contents.
- **A merged cell is the panel's business, and only the panel's.** `cell_rect()` returns the
  merged rect for the inside column and an **empty** rect for the cell it covers; the bar already
  skips a rect of no width. Same table trick `knob_panel` plays on the other axis for its 96x190
  meter plate, turned a quarter. `cassette_bars` needed no change: four keys are four carcases.
- **The four cells, in the bar's own numbers.**
  `1` is (594, 76, 190x96), `2` and `3` are (594, 170, 96x96) and (688, 170, 96x96), `4` is
  (594, 264, 190x96) - the lattice's 94 px pitch with its shared 2 px borders, the shared 8 px
  corner and 2 px stroke. Nothing new: a wide key is a normal key plus one cell of plate.
- **The ink, not the carcase, is the button's anchor.** `cassette_button`'s spec gains
  `disc_right`: the ink takes its `CIRCLE_INSET` (16 px) from the carcase's right edge instead
  of centring, so a wide key's ink is exactly where the outer column's key would have put it
  (x=110 inside the carcase, centre 142), and the pulse pivots on it. The module reads the
  carcase's width; the panel says "wide" and never writes 110.
- **A wide key has no dome.** The first reading kept the 64 px dome and moved it right; the
  second took it away, and that is what shipped. A `bare` key builds no ambient circle and no
  disc, so it has no ring on the dark page, no cap and neither shadow on the white one - it is
  the plate and the glyph, the glyph standing at the centre the dome would have had. The middle
  pair keep their domes, so the difference is per key and lives in the spec's two flags, not in
  two kinds of cell. `disc_right` alone is still the domed reading, which is what a wide cell
  means again the day a dome comes back.
- **The keys are a control surface.** The whole carcase is the hit target, the panel does not
  bubble gestures, and there is no latch: these are moments, not modes, and no key selects a
  group. Their actions are not wired yet - every spec's `on_click` is NULL, so a click is the
  pulse alone - and the four handles are the anchors those actions will hang from.
- **`inert` is retired.** With the grille gone no panel is a picture, so the flag is deleted
  from `cassette_panel_t` and `bar_cell_new()` always makes its carcase clickable. A panel that
  is up is a control surface; a panel that is not there has no carcase to swallow a touch.
- **The catalogue follows the name.** `SCENE_NAMES[1]` is `Art/Home` and
  `docs/images/screens/1-art-home.png` is the re-shot frame. The scene's script is unchanged: it
  aims Art and stands still.
- **The four glyphs are the keys' names.** `play` on the top key, `prev` and `next` on the middle
  pair, `mic` on the bottom. They came with the owner's font update (U+F014..U+F017), so the
  36 px cut's range in `package.json`'s `font:xtape` grew to `0xF000-0xF017` and the cut was
  regenerated; the 24 px cut keeps `0xF000-0xF013` because nothing in the Menu draws one, and the
  boot mark's single-glyph cut came out byte for byte the same. The font also carries `pause` at
  U+F018, which no part draws, so no cut carries it. What each key *does* is still to come.

## Considered and rejected

- **Keep the grille** behind the keys, or only in the wide keys' spare left half. Rejected by
  the owner: the four keys are the panel. A control surface does not need the picture it
  replaced underneath it.
- **Digits, or domes with no letter, instead of glyphs.** The owner chose the icon font, which is
  what the mode bar uses; digits would have read as a keypad and an empty dome as unfinished.
- **The dome on a wide cell.** Rejected by the owner at the second look: a circle inside a plate
  reads as a button inside a button, and the wide key is one key, not two.
- **The bare glyph centred on the 190 px plate.** Rejected: it would move three of the four
  glyphs off the column they share (736), so play, next and mic would no longer line up and the
  wide key would read as a different species of control.
- **A 96 px transparent container pinned right inside the wide carcase**, so `cassette_button`
  never learns about width. Rejected: the press ring would then thicken on the invisible child
  instead of the carcase, and the carcase would stop being the button.
- **The panel passing an absolute disc inset.** Rejected: it copies `CIRCLE_INSET` into the
  panel, where it can drift from the button's, and the panel would own a number about a dome.
- **The pulse pivoting on the carcase's middle.** Rejected: the dome would drift as the plate
  shrank, which reads as two things moving in different directions. A wide key breathes toward
  its dome.
- **Keep `inert` for a panel that is genuinely a picture.** It has no user today; it is one
  field and two lines to put back the day one arrives.
- **Rename only the model, or only the module.** The owner asked for the rename to be carried
  all the way through, so the id, the files, the comments and the scene all say Home.

## Consequences

- **A swipe-dead band on the default surface.** With `gesture_bubble` false, the 190x284 block
  no longer passes ladder swipes to the screen while the Home group is up - which it is by
  default at Art and Real. This is the exact thing ADR 0026's third bullet was there to prevent,
  and the owner confirmed it after being shown the cost: a control surface is not a picture. The
  window, the header, the footer and the cassette still move the ladder.
- **The wide key's pulse is a squeeze, not a swell.** The pulse is 256 -> 232 -> 256, so at the
  peak the plate is 90.6 % of its size. Pivoted on the ink at x=142, the wide cell's left edge
  pulls in about 13 px and its right edge about 5 px: 18 px narrower for 160 ms, toward its
  glyph. The square keys pull in about 5 px on every side, as they always did. A bare key's
  press is the carcase's ring alone - 2 to 3 px - because it has no disc's ring to thicken.
- **Twelve objects where there was one.** The grille was one carcase with a draw callback. A
  domed key is four objects (carcase, ambient, disc, letter), so the two domed keys and the two
  bare ones (carcase and letter) are 12 objects: +11 while the group is up. The panel is also no longer free to be repainted mid-slide by one
  invalidation - the dots were one object's draw, the keys are the bar's usual children - which
  is the same bill every other panel already pays.
- **The four rects land untested, like their cousins.** The merge is app-tier geometry, so it is
  not host-testable; `knob_panel`'s 96x190 plate has the same standing. What the host suite pins
  is the model's rules, under the new names (`t26_groups`), and `idf.py build` is what catches
  the geometry's mistakes today.
- **Flash: 2 KB for the four glyphs.** The image went from 1 714 512 B to 1 716 528 B - four
  36 px glyphs at 4 bpp - and `pause` would have been about another 0.5 KB had the range run to
  U+F018. The 24 px cut is byte-identical to what it was.
- **The first boot found the `shown` hook's trap.** The panel first dropped its four handles in
  `shown(false)` - "the cells are going" - and the keys came up dead: the bars start away, so a
  panel installed at boot is told false *once, with its cells alive and clickable*, and the
  handles were zeroed right after `build()` filled them. Every click then found no carcase (no
  ring, no pulse) and every page flip inked nothing, so at Real the glyphs kept the dark page's
  red. A swap to the EQ group and back rebuilt them with the bars up, which is why they only
  worked after that. The handles are now the build's alone - a key the bar could not build is
  zeroed, and nothing on the way out clears them - and `cassette_panel_t`'s `shown` is documented
  as the bar's *wish*, not the cells' lifetime.
- **The committed photo is a grille under a new name until the board re-shoots.** `npm run
  screens` writes `1-art-home.png` and the contact sheet; until it runs, the file in
  `docs/images/screens/` is the old frame renamed, which one commit is long enough to fix.

**Status:** accepted and built, host suite green (`make -C host check`), `idf.py build` clean and
2 KB over the previous image for the four glyphs. The on-device re-shoot of `Art/Home` is
outstanding: the four keys carry the owner's `play`, `prev`, `next` and `mic`, and what they do is
the next decision.

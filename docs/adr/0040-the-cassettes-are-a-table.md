# The cassettes are a table, and nothing is read off the Cover

**Amends [ADR 0039](./0039-the-cassette-is-a-card.md):** the Card's ink is no longer the Cover's
dominant hue. A cassette is a **row of a table you fill in** - a plastic and the ink of the label
printed on it - and the Track's path only picks the row.

ADR 0039 made the shell a reading of the artwork: the dominant hue of the decoded cover, at a
fixed saturation and value, with the drawn label in its complement. That put the palette in the
pixels, and it cost a module - a 24-bucket histogram with chroma and value culls, an HSV
conversion, a complement, and a luminance fallback for neutral plastics whose complement is
itself. The owner wants the opposite: a fixed set of colour combinations, filled in by hand, so
that the look is data and two Tracks can be given two exact cassettes.

## Decision

- **`k_col_cassette[]` in `cassette_style.c` is the palette.** One row per cassette:
  `{ plastic, label }` - the Card's fill and the ink of the drawn print. `k_col_cassette_count`
  is the row count, so adding a cassette is adding a row. Five to start with, one row per shell
  colour with a label that reads against it.
- **The Track's path picks the row**: `ink_seed % k_col_cassette_count`, unchanged from ADR
  0039 in how it chooses and changed in what it chooses from. A Track with no Cover still
  changes the cassette, and the choice survives a pause and a rebuilt Playlist.
- **Both inks land together, as a cut.** The view sets the Card's plastic and the label's ink
  from the same row when the frame's `ink_changed` says the Track moved. No colour animation -
  a colour animation would repaint the panel on every step.
- **Nothing is derived from a picture.** `cassette_cover_set_image()` uses the loader's buffer
  for the picture and nothing else. The `cassette_ink` module, its host test, and the Card's
  hue entry points (`cassette_card_set_hue()`, `cassette_card_ink()`) are deleted.
- **The pair is the filler's judgement.** Nothing checks that a label reads against its
  plastic; the table says light-on-dark or dark-on-light, and the panel says whether it worked.

## Consequences

- **The palette is bounded by the table.** N rows is N cassettes. With a Cover the print hides
  most of the plastic, but the 12/20/64 px mat is still the row's colour, so the choice still
  reads.
- **The neutral-plastic problem disappears with the complement.** Graphite's complement was
  itself, which needed a luminance fallback; a hand-filled row simply says which ink goes on
  graphite.
- **181 lines of portable code and 1492 test checks go**, and with them the one place this
  screen read an image's pixels - so a Cover's decode cost is the decode and nothing else.
- **The table is the single place to tune the screen's colour** on the panel, which is where
  that tuning belongs.

## Alternatives

- **Keep the dominant hue, with the table as the no-Cover fallback.** Rejected: it leaves the
  look unfillable without code, and it keeps a whole module for a colour the owner would rather
  choose. It also made two albums with the same dominant colour the same cassette.
- **Derive each label's ink from its plastic** (the complement, as ADR 0039 had it). Rejected:
  the filler is choosing the pair anyway, so the derivation only adds a rule that has to fall
  back on luminance for neutral plastics.
- **Keep `cassette_ink.c` for the histogram and call it nowhere.** Dead code with a test suite
  paying for it.

# The two-finger screen prints a packed label sheet

The owner designed cassette labels in an LVGL canvas builder — paper, bands, rules and text
bound to fields like the song's title, BPM and key. The two-finger screen (ADR 0068) is where
they belong: it already prints the print's 748x359 box at the entry grid, and since `20db1a8`
it prints the row's flat label colour there, wordless. The question is how a drawn label gets
onto the panel.

A design is data, and the panel is a screen task that shares core 0 with the decoder and runs a
25 Hz reel tick. The options were: parse the editor's JSON on the device, read a labels
directory off the card at play, generate C per design, or carry the design as a packed sheet.

## Decision

- **A design is a `label-template v1` JSON file** in `assets/labels/templates/`, written by the
  editor and kept in git: the editable, diffable record. Every `*.json` there is packed; the
  folder is the set, so adding a design is saving a file into it. It is not parsed by the
  firmware.
- **`tools/label_pack.py` packs every design into one binary sheet** — `label_sheet.bin` — at
  build time, and the sheet is embedded in the image and **read in place**: a pointer and a
  length, no copy, no allocation, no RAM. The byte layout is
  [`docs/specs/label-templates.md`](../specs/label-templates.md).
- **The packer refuses rather than substitutes.** An unknown slot, a face or size the build
  does not compile, a prim outside 748x359, a space that is not 748x359, an aperture that is
  not the Card's window, a template past the prim cap: each is an error naming the design and
  the layer. Nothing is silently dropped, rounded or recoloured.
- **A Track's design is `ink_seed % template_count`**, the same seed that picks its palette row
  (`cassette-card.md`, "The ink"). A Track therefore always wears the same shell and the same
  design, and nothing is configured.
- **The runtime renders the sheet as LVGL objects inside the print**: a styled box per rect, an
  `lv_label` per text, its slot's value and the compiled font of its role. They are children of
  the print, so they take the same treatment as the Cover: the Card's mask cuts the window and
  the print's box clips everything else.
- **The print is the two-finger screen's only.** The Controls pose keeps the Cover, or the
  palette's label fill when there is none, so a design never has to survive a 0.537x scale.
- **The design carries its own six role colours** — paper, ink, rule, band 0..3 — and its three
  font roles. The Card's plastic and the window's rim stay the palette's. The editor's export
  gains `inks` and `fonts`; without them a design is shapes with no colourway.
- **The slots are the Snapshot's fields**, resolved from the row: title, artist, album, genre,
  bpm, musical key, energy, danceability, valence, arousal, mood. No tags are read and no SD access
  happens on the show path; a card with no `k7.db` prints the design with blank fields.
- **The flat palette fill survives as the no-designs case**: a build with an empty sheet behaves
  exactly as before.

## Considered options

- **Parse the JSON on the device.** Rejected: a parser on the screen task for data that cannot
  change at runtime, a new failure mode on the play path, and the editor's JSON is not a
  contract the firmware should have to track field-by-field.
- **A `labels/` directory on the card**, read at boot, as the Snapshot is read from the card.
  Rejected by the owner: the artwork ships with the firmware, so a design change is a build. The
  card door stays open — the reader takes a pointer and a length, so where the bytes come from
  is one call site — but nothing in this change reads the card.
- **Generated C per design** (`genC` writes exactly that today). Rejected: the design stops
  being data, every design is code, and the fields, roles and fonts cannot be checked at pack
  time as a table.
- **An offscreen canvas, blitted as an image** like the Cover. Rejected: 748x359x2 = 537 KB of
  PSRAM for pixels that never change within a Track, and a heavy one-off draw on the core that
  decodes the audio. Objects cost nothing per frame and give text alignment, wrapping and
  clipping for free.
- **Scale the same design to the Controls print.** Rejected: 24 px text lands at ~13 px on an
  800x480 panel, and a second authored layout is a bigger editor change than the feature.

## Consequences

- **Changing a design is a rebuild and a flash.** The sheet is a build artifact; there is no
  field update path and no card channel.
- **Glyphs are flash.** A label's typography is compiled: the design's faces at the design's
  sizes are committed like `font_jura_36.c`, and the packer refuses anything the build does not
  carry. The app partition has 4 MB and the image 1.81 MB, so a handful of faces is affordable;
  it is not free.
- **The slot vocabulary is the Snapshot's, not the editor's.** `year` and `album_artist` leave
  the editor's list until the Snapshot carries them; `energy` and `danceability` print 0..100,
  `key` prints the Camelot code the MCP layer already prints, and `mood` prints one of the five
  names `snapshot_mood_name()` returns.
- **The sheet is validated once, at the pack, not at boot.** A corrupt or truncated sheet should
  be impossible: the CRC, the sizes and the offsets are checked by the reader, and a failure
  leaves the flat palette fill and a log line. That is a build bug, not a card state.
- **The gesture is still unproven.** ADR 0068 recorded that the GT911's two-contact report has
  never been observed; until `[GDBG] contacts=2` appears, the screen cannot be reached by hand
  and the label cannot be seen on the panel. The board pass owes that first.
- The design's paper is its own rectangle, so the print's background stays the palette's label
  fill: a design without a paper prim still reads as a cassette label.

## Supersedes

`0068-the-picture-takes-the-panel-behind-two-fingers.md`'s "prints the row's label background
colour, not the picture" — the fill is now a fallback, not the screen's design. The body of
`cassette-card.md`'s "The label" section is amended in the same change: the label is no longer
wordless on the two-finger screen, and the row's ink column is now the design's, not the
palette's, wherever a design prints.

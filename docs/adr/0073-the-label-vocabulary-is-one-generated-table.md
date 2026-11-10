# The label vocabulary is one table, generated into the editor and the firmware

ADR 0071 made a design data: `label-template v1` in `assets/labels/templates/`, packed into a
sheet, read in place. That left the *vocabulary* — which Slots exist, which faces the sheet
carries, how many bands, and the format's caps — with no owner. It was kept by hand in three
places at once: `SLOTS`, `SHEET_FONTS` and the role tables in the canvas builder's JavaScript,
`SLOT_NAMES` / `FONT_ROLES` / `BAND_MAX` / the caps in the packer's Python, and the `LABEL_SLOT_*`
enum, `LABEL_SPACE_*` and the caps in the C.

Three copies is three chances to disagree, and they had. The editor enforced none of the
format's caps — 48 prims, 16 slots, the int16 bounds on `x` and `y` — so a design too big for
the sheet exported clean and failed `./tools/build.sh`. Ticket 09 had already found one of
these: the editor offered a face at a size `fonts.json` did not carry, and nothing in the page
could have known. The fix then was `SHEET_FONTS` plus `label_pack.py --check-editor`, which
reads the page's *source text* with regular expressions. That check is the duplication
admitting itself: commit `ddcc14a` exists only to repair it after the page was reformatted, and
it could only see the tables that happened to be greppable — not the caps, not the window, not
the value ceiling.

The same check would also have missed a second drift, and the new one found it at once: the
page embedded four faces, and one of them, Oswald 600, was retired when the numeral face became
Barlow Condensed 700 (`cf3953b`). Twenty-two kilobytes of a face the build cannot compile,
drawn in the only place a design is judged by eye.

## Decision

- **The vocabulary is one committed file**, `assets/labels/vocabulary.json`: the Slots (id,
  human label, and the value the editor simulates with), the sheet's faces with `"editor": true`
  on the ones the page offers and can draw, the band count, the value ceiling, the print's
  space, the Card's window, the caps, the coordinate bounds, and the face roles a new design
  starts with. §4 of [`label-templates.md`](../specs/label-templates.md) is its contract.
- **Two copies are generated from it and committed.** `label_vocab.h` carries the space, the
  limits and the Slot list as one macro expanded twice — the enum the resolver indexes and the
  names the sheet carries — so a Slot cannot exist in one and not the other. The page gets a
  `VOCAB` block between generated markers.
- **`tools/label_pack.py` writes both** (`--generate-vocabulary`) and refuses them when they are
  stale (`--check-vocabulary`, run by `make -C host label` and so by CI). The check also
  compares the bytes of the faces the page embeds against `assets/fonts/`.
- **The editor enforces the caps**, from the same table: a design the sheet cannot hold refuses
  at Export instead of at the build.
- **`genC` is deleted.** It wrote C per design — which ADR 0071 rejected — and emitted
  identifiers (`LBL_W`, `ui.root`, `build_aperture_mask`) that exist in no C file in this
  repository. `tools/extract_builder_fonts.py` is deleted too: it searched for a marker the page
  no longer has, found nothing, and was invoked by nothing.

## Considered options

- **A sibling `vocabulary.js` the page loads.** Rejected: the builder is one self-contained
  file, opened from disk and sometimes published outside its directory, and a relative script
  would break both.
- **Generate `label_vocab.h` at build time**, beside the sheet and `label_fonts.h`. Rejected:
  `make -C host` builds the portable component and has no Python in it, and that is worth
  keeping. The repository already commits generated C — the `font_*.c` files — so a committed
  header is the same kind of object.
- **Keep the C hand-written and check it against a generated fixture.** Rejected: the Slot list
  is not only a list of strings. Each id maps to a Snapshot field in `label_values.c`, so the C
  is read and reviewed whatever the fixture says; a fixture would be a third copy to keep.
- **One rule engine both validators interpret.** Rejected: the two refuse at different moments
  for different readers — the editor while a design is drawn, the packer while a build runs —
  and that is a real seam with two adapters. They should share the facts, not be merged.
- **Keeping `--check-editor`'s regexes as well.** Rejected: it is the same drift check with a
  smaller blind spot, and it can only ever read text that happens to be shaped for it.

## Consequences

- **Adding a Slot or a face is one edit and one command.** The generated files show up in the
  diff and are reviewed like any other, which is the point of committing them.
- **The caps now refuse while a design is being drawn**, so the first time a designer meets
  "48 layers" is in the browser, not in `./tools/build.sh`.
- **The page lost 22 KB** of a face nothing could compile, and gained about 1.5 KB of generated
  block.
- **The editor's field list is in the vocabulary's order**, so it is now title, artist, album
  rather than album, title, artist. The order of `slots` is the resolver's, and there is one.
- **A stale generated file is a check failure, not a silent divergence.** `make -C host label`
  names it and says to run `--generate-vocabulary`; CI runs that make target.
- **This retires the mechanism of ticket 09**, not an ADR: `SHEET_FONTS` and `--check-editor`
  are gone, and the section of `label-templates.md` that described the tables as "one in both
  places" now describes one table generated into both.
- **The firmware build is still not in CI.** The sheet the device embeds stays byte-identical
  under this change, but a label's arrival on the panel is proven by flashing — and the
  two-finger gesture that reveals the print is still unproven (ADR 0071, "The gesture is still
  unproven").

**Status:** accepted, host suite green (`make -C host check`).

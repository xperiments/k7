# The artwork is one K7Art, and the label and the J-card are its sections

The Label Editor has authored a `label-template v1` file: the 748x359 design the K7 prints on
the Card at the two-finger pose, packed by `tools/label_pack.py` into one label sheet. The
webapp draws the covers of albums with no picture as five CSS faces
(`web/src/ui/album-art.tsx`), picked by the album's name. They are two unrelated systems for
one object: a release's artwork.

The request adds the other half. Every label design carries a **J-card**: the printed insert of
a cassette case, one side, 4.0625x4 in, scored at 1 in and 1.5 in into a **flap** (1 in), a
**spine** (0.5 in) and a **cover** (2.5625 in). The webapp's case is already the same shape:
its front is 16:25 and its depth 2.6/13 against 13 rem - within 0.1 % of the cover and the
spine. A coverless album should draw that J-card's cover on the case front and album card, and
its spine on the shelf's left face. The flap has no surface yet.

A label and its J-card are one design, not two that happen to match: they share the paper, the
ink, the rule and the eight bands, and the same three faces. Two files would let the colourway
and the type drift, and the packer and the webapp would each own half a design.

## Decision

- **One file, one id.** A design is a **`k7art` v1** container at
  `assets/k7art/k7.<slug>.json`, id `k7.<slug>`. It carries a **`label`** section and a
  **`jcard`** section. The two are one-to-one: a design without a J-card does not exist.
- **The label section is today's design**, minus the identity and the palette that move to the
  container: its `space` (748x359), `aperture`, `canvas`, `fonts`, `slots`, `notes` and
  `prims`. The label sheet's binary is unchanged - version 4, the same CRC, the same reader.
  ADR 0071's read-in-place and ADR 0072's bleed stand.
- **The J-card section is the new design**: one sheet of **1300x1280 at 320 units per inch**,
  so 4.0625x4 in exactly and every fold line an integer - flap 0-320, spine 320-480, cover
  480-1300. Its `parts` are those rectangles. Its texts bind to three **J-card fields**,
  `title`, `artist` and `meta`; it uses the same eleven colour roles and the same three faces,
  with its own three type sizes. There is no back side, no tracklist and no print export.
- **The palette and the faces are the container's.** `inks` (paper, ink, rule and band 0..7)
  and the three faces are shared. The label's paper is the LayerMask's colour, and the J-card's
  sheet is the same paper. A role is a role on both media.
- **Two readers, one file.** `label_pack.py` reads the `label` section and packs the factory
  sheet; the webapp imports the same files and reads the `jcard` section. The device never
  parses a K7Art (ADR 0071).
- **The editor edits the pair.** One document, two surfaces: the label surface is today's
  editor, and the J-card surface is the one sheet with its three guided regions and a focus for
  each. Export writes one K7Art; a publish writes a pack (`../specs/art-packs.md`, ADR 0111).
- **The old file retires.** `label-template v1` is no longer a file the packer accepts; the
  editor imports one to migrate it into a K7Art with a starter J-card.
  `assets/labels/templates/` is replaced by `assets/k7art/`; `assets/labels/` keeps the
  vocabulary and the K7's own pictures.
- **The webapp's drawn face is the J-card.** When an album has no cover picture, its case
  draws the J-card's cover part and its spine draws the spine part; the five CSS faces retire.
  A real cover picture still wins.

## Considered options

- **A sibling `assets/labels/jcards/` folder with a separate `jcard-template v1`, linked by
  id.** It leaves the packer's input untouched and is the smallest diff, but the design is then
  two files whose palette and type can drift, and the request is a container.
- **A `jcard` key inside the existing `label-template v1`.** It keeps the label file and the
  packer unchanged, but the file is still named a label template - the thing this retires - and
  the editor would still export a "label template" that contains a J-card.
- **Keep the label template and add a sixth CSS face.** No format and no editor work, but the
  J-card is then hand-written CSS and cannot be authored, which is the request.
- **A J-card with its own palette and type.** The halves could diverge freely, but then "each
  label associates a J-card" is only a pointer, not one design.
- **Three separate J-card artboards (cover, spine, flap).** Each part is easy to edit, but it
  loses the one sheet the printer scores and any art that crosses a fold. The guided-regions
  sheet is the physical object.

## Consequences

- **The packer changes input, not output.** `label_pack.py` reads `assets/k7art/*.json`,
  validates the `label` section with today's rules, and writes the same version-4 sheet bytes.
  The selftest fixtures and the `--check-vocabulary` path follow. ADR 0073's vocabulary is
  untouched.
- **The editor's model gains a surface.** The `Editor` store, the document autosave, the
  inspector, the stage and the WebMCP tools learn which surface is active; the label's
  behaviour is unchanged. The J-card's type sizes are the first editable type scale in the
  editor - the label's are the sheet's compiled pairs.
- **The webapp gains a renderer and loses five CSS faces.** It imports `assets/k7art/` at build
  time and draws the `jcard` prims in the cover and spine; the words and the album's hue are
  still the app's. The look's K19 amendment records it.
- **A K7Art is bigger than a label template, but the sheet is not**: the J-card is not packed,
  not embedded and not read by the firmware.
- **The flap has no surface yet.** It is authored and exported, and waits for a view - the
  case's back, or an unfolded sheet - before it is shown.
- **The one existing label**, `label.neon`, migrates to a K7Art with a starter J-card; the
  editor's autosaved documents migrate on load. `tpl.test.ts`'s byte-exact checks become the
  container's, with the label section compared byte for byte.
- **The docs change in the same commit:** the new [`k7art.md`](../specs/k7art.md);
  [`label-templates.md`](../specs/label-templates.md) becomes the label section's contract;
  the glossary's Label template, Label sheet, Slot and Cover entries; the look's K19; and the
  pack's [`art-packs.md`](../specs/art-packs.md) with ADR 0111.

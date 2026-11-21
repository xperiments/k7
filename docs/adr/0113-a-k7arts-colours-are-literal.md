# A K7Art's colours are literal, not roles

A K7Art v1 gives every design eleven named inks - `paper`, `ink`, `rule` and `band.0`..`band.7`
- and each label and J-card prim names one of them. The roles let a design be recoloured from
one place, but they also force every design into the palette's shape: a design that needs a
fourth brand colour spends a band on it, a text cannot print on the paper, and the one literal
colour a design may use needs a `notes` line to justify it. The label sheet carries all eleven
colours per design whether or not a prim uses them, and the editor's Colours panel makes the
designer pick from eleven names instead of choosing a colour. **The request is a colour on
every element.**

## Decision

- **Every prim carries its own colour.** A label or J-card `rect`'s `fill` and a `text`'s
  `color` are `#RRGGBB`, not a role name. The file is **`k7art` v2** and has no `inks`.
- **The label's paper is the LayerMask.** The label section carries an optional
  `mask: { "color": "#RRGGBB", "r": <corner radius> }`, the size of the print. It is the paper
  the design paints behind its layers and the clip the editor shows; absent means the design
  has no paper. The v1 full-print `fill: "paper"` rect, which carried it, is gone.
- **The J-card's sheet is its `background`.** `jcard.paper` is renamed: a `#RRGGBB` colour
  painted under the prims. A full-sheet background rect is no longer needed.
- **The colour type roles stay.** `title`, `numeral` and `caption` are faces and sizes, not
  colours; they and the three compiled label pairs are untouched.
- **The label sheet moves to version 5.** A template record is 16 bytes (it loses the eleven
  ink words); a prim keeps its 24 bytes and paints a colour at offset 20 instead of naming a
  fill index. `label_inks_t`, `label_inks()` and `label_fill_t` go.
- **The vocabulary moves to version 2**: `bands` goes, because a design no longer declares a
  band list.
- **The editor reads v1 and writes v2.** A v1 document or K7Art is migrated once, through its
  own inks, so an old design opens in the same colours. The factory designs in
  `assets/k7art/` are converted. The packer, the firmware and the App read v2 only, so **a
  pack built before this change is refused and republished** from the editor.
- **A pack's header stays version 1.** Its layout does not change; `sheet_version` is 5 and its
  J-cards are `v: 2`, which are the fields the readers gate on.
- **ADR 0110's one K7Art stands.** The two sections no longer share a palette, because there is
  no palette to share; they still share the three faces and the identity.

## Considered options

- **Keep the roles and add a literal.** A prim could name a role or a colour, and v1 files keep
  reading. The roles then stay in the sheet, in the App, in the firmware and in the editor's
  panel - exactly the concept the request removes - and a design is still asked to have a
  palette.
- **A per-design palette of N colours with indices.** Literal colours with a table to dedupe
  them. It keeps a small palette in the sheet, but it is a new concept in place of the old one,
  it needs its own bounds, and the 24-byte prim already has four bytes for the colour.
- **A colour on every prim.** Chosen: the sheet paints what the file says, and there is nothing
  to resolve.
- **Keep `paper` as a container colour and let prims be literal.** The label's paper and the
  J-card's sheet would remain names a design must fill in; the mask already says which is the
  label's, and the J-card's is one field. Renaming them is the smaller, plainer model.

## Consequences

- **The designer chooses a colour per element.** The Colours panel and the eleven swatches go;
  each layer's inspector gets a colour control (a shape's fill, a text's colour, a J-card
  layer's colour), the LayerMask its own colour, the J-card sheet its background. The editor's
  Pantone row stays as a convenience for new layers, not as a design palette.
- **The sheet is smaller and simpler**: 48 bytes less per design, no ink words to validate, and
  the painter is `lv_color_hex(prim.colour)`.
- **v1 is read once, in the editor.** Documents (`label-editor.docs.v3`) and K7Art files
  migrate with their own inks; the store's key becomes v4. The retired `label-template v1`
  import keeps working the same way.
- **An installed pack is not read.** Both the K7's pack reader and the App refuse a sheet that
  is not version 5; the App says an older editor made it and to publish it again. The factory
  designs are converted, so the factory art is unaffected.
- **The published J-cards carry no inks.** The App's renderer reads each prim's colour, and
  the pack's J-cards document is `v: 2`.
- **No field can recolour a design from one place.** That is the price of "any colour for any
  element"; a designer changes the layers, or the editor's swatch row, not a palette entry.

## Supersedes

ADR 0110's "the two sections share the paper, the ink, the rule and the eight bands". The one
file, the two sections, the three faces and the readers stand.

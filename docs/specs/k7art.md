# K7Art: one design, a label and a J-card

**Status:** accepted · **Updated:** 2026-11-25

A **K7Art** is one cassette artwork: the label the K7 prints on the Cassette at the two-finger
pose, and the J-card the cassette case is sleeved with - one file, one id, two sections. It is
authored in the **K7Art Editor** (`tools/LabelEditor/`, served at `/k7art`) and read by two
consumers: `tools/label_pack.py` packs its `label` section into the factory **Label sheet**, and
the webapp renders its `jcard` section as the cover of an album that has no picture. [ADR
0110](../adr/0110-the-artwork-is-one-k7art-and-its-label-and-j-card-are-sections.md) records
why the two are one file; [ADR 0113](../adr/0113-a-k7arts-colours-are-literal.md) records why
its colours are literal values on the prims; [`label-templates.md`](./label-templates.md) keeps
the label section's binary, its vocabulary and its Slots; [`art-packs.md`](./art-packs.md) is
the pack a user publishes and the device that stores it.

The vocabulary - **K7Art**, **J-card**, **J-card panel**, **J-card field** - is in
[CONTEXT.md](./CONTEXT.md). The Cassette, the print and the Window are
[`cassette-card.md`](./cassette-card.md)'s.

## 1. The file

One design is one file at `assets/k7art/k7.<slug>.json`, id `k7.<slug>`, and one id is one
pair: **the label and the J-card are one-to-one**. There is no design without a J-card and no
J-card without a label.

```json
{
  "format": "k7art",
  "v": 2,
  "id": "k7.neon",
  "name": "Neon Skyline",
  "label": {
    "space": { "w": 748, "h": 359 },
    "mask": { "color": "#F5F1E6", "r": 19 },
    "aperture": { "x": 95, "y": 101, "w": 556, "h": 166, "r": 83 },
    "canvas": { "backgrounds": ["#909090"], "active": 0 },
    "fonts": { "title":   { "ff": "Archivo Narrow 600", "px": 32 },
               "numeral": { "ff": "Barlow Condensed 700", "px": 78 },
               "caption": { "ff": "IBM Plex Mono 500", "px": 18 } },
    "slots": { "song_title": { "max": 96 } },
    "prims": [
      { "t": "rect", "x": 0, "y": 0, "w": 748, "h": 120, "fill": "#6B7A2F", "still": true },
      { "t": "text", "slot": "song_title", "x": 40, "y": 300, "w": 400,
        "font": "title", "color": "#242424", "align": "left", "preview": "Neon Skyline" }
    ]
  },
  "jcard": {
    "space": { "w": 1300, "h": 1280, "unit": 320 },
    "parts": {
      "flap":  { "x": 0,   "y": 0, "w": 320, "h": 1280 },
      "spine": { "x": 320, "y": 0, "w": 160, "h": 1280 },
      "cover": { "x": 480, "y": 0, "w": 820, "h": 1280 }
    },
    "background": "#F5F1E6",
    "fonts": { "title":   { "ff": "Archivo Narrow 600",   "size": 56 },
               "numeral": { "ff": "Barlow Condensed 700", "size": 136 },
               "caption": { "ff": "IBM Plex Mono 500",    "size": 31 } },
    "prims": [
      { "t": "rect", "x": 480, "y": 230, "w": 820, "h": 4, "fill": "#8E7E4A" },
      { "t": "text", "slot": "title",  "x": 540, "y": 900, "w": 700,
        "font": "numeral", "color": "#242424", "align": "left", "preview": "Neon Skyline" },
      { "t": "text", "slot": "artist", "x": 540, "y": 1060, "w": 700,
        "font": "title", "color": "#242424", "align": "left", "preview": "The Analog Sons" },
      { "t": "text", "slot": "meta",   "x": 540, "y": 1120, "w": 700,
        "font": "caption", "color": "#242424", "align": "left", "preview": "Side A · Side B" }
    ]
  }
}
```

- **The key order is the file's**: `format`, `v`, `id`, `name`, `label`, `jcard`. The sections'
  keys follow their own spec's order. The editor writes it deterministically; the tests
  compare bytes.
- **`format` and `v`** are `k7art` and `2`. The packer and the webapp refuse anything else by
  name. The id is `k7.<slug>`, the slug `[a-z0-9][a-z0-9-]*`; the folder is the set and the
  packer orders by id.
- **A v1 K7Art is the editor's to migrate.** It carries `inks` where v2 carries colours on the
  prims; the editor reads it, resolves each role through those inks, and writes v2 (§8). The
  packer and the App read v2 only, and a pack built from a v1 design is refused by version and
  republished ([`art-packs.md`](./art-packs.md) §1).
- **`name`** is the design's name, in the K7's sheet log and in the App's pack list.
- **The old file is not this one.** `label-template v1` is retired; a file with `v` at the top
  level and `prims` beside it is not a K7Art, and the packer refuses it. The editor imports one
  and migrates it into a K7Art with a starter J-card (§8).

## 2. The colours and the faces

A design's colours are literal `#RRGGBB` values on its prims. There are no role names: a
`rect` fills with its own `fill`, a `text` prints in its own `color`, and any colour may go on
any element - a text on the paper, a band in the ink, two shapes in two shades of the same
green. The container has no palette.

- **The label's paper is its `mask`, and every design has it.** The section's `mask` is the
  print's own box (748x359) painted in a colour and clipped to a corner radius. Every layer
  prints on it; the sheet paints it as the first prim (§3). The editor never offers a
  paperless design: it always writes the mask, and a file without one opens with the default
  paper (`#c7a867`, radius 10). The mask's colour is not shared with anything: the J-card's
  sheet is its own.
- **The J-card's sheet is its `background`.** A `#RRGGBB` colour painted under the prims, so a
  design is papered with or without a full-sheet rect; the case's moulding is the chrome's and
  never shows through a J-card.
- **The faces are the same three**: `Archivo Narrow 600`, `Barlow Condensed 700` and
  `IBM Plex Mono 500`. The label sizes them by the sheet's compiled pairs (`32`, `78` and `18`
  px) - only those pairs exist as firmware fonts. The J-card sizes them in its own units (§4).
- **The type roles are not colours.** `title`, `numeral` and `caption` name a face and a size
  on each medium; they carry no paint. A prim's colour is its own field, beside its font role.

## 3. The label section

The label section is the design the two-finger pose prints: 748x359 at the print's entry grid,
authored as it was in `label-template v1` and packed by `label_pack.py` into the factory Label
sheet. The sheet's binary, its version, its vocabulary, its Slots and every refusal are
[`label-templates.md`](./label-templates.md)'s; this section only says what moved.

- **The identity is the container's.** The section has no `v`, `id` or `name`; it has the
  `space`, the `mask` and `aperture`, the optional `canvas`, the `fonts`, the `slots` and the
  `prims`. The packer takes the id and the name from the container and validates the
  section by the rules the label spec states.
- **`space` is 748x359**, `mask` is the paper (§2) and `aperture` is the Cassette's window; both
  are always written. `canvas` is the optional list of up to eight cassette backgrounds,
  `fonts` is the three compiled pairs, `slots` is the eleven fields the design uses (each
  `max: 96`), and `prims` is the print. There is no `notes`: with every colour literal there
  is nothing for it to explain.
- **The paper is not a prim in the file, and is one in the sheet.** Both sheet writers
  synthesise it as `{ t: "rect", x: 0, y: 0, w: 748, h: 359, r: mask.r, fill: mask.color }` at
  the front of the prims, so the print's paper is a rect the reader paints like any other. The
  editor always writes it, so the sheet's prim count is the file's plus one, and the 48-prim
  cap is the sheet's.
- **The window is fixed, and it is not a prim.** The section's `aperture` is always the
  Cassette's window: (95,101) 556x166, radius 83. The editor draws it as an opaque black layer on
  top of the print, in Design and in Preview, so nothing drawn under it is seen; it cannot be
  selected, moved or removed. It is a placement aid, not a prim, and the sheet never paints
  it: the Cassette's mask cuts the window on the device. A file may omit it; the editor supplies
  it and writes it on Export.
- **It is one design per Track**, chosen by `ink_seed` (ADR 0071). The J-card is the same
  design on paper; the pair is the K7Art.
- **The sheet is the factory's**, packed at build from every `assets/k7art/*.json`. A user's
  pack adds a second sheet at runtime ([`art-packs.md`](./art-packs.md)); it does not change
  the factory sheet's binary or this section's format.

## 4. The J-card section

The **J-card** is the printed insert of a cassette case: one side, 4.0625x4 in, scored at 1 in
and 1.5 in into a **flap** (1 in), a **spine** (0.5 in) and a **cover** (2.5625 in). Its design
is one sheet of **1300x1280 units at 320 units per inch**, so every fold line is an integer and
the sheet's proportions are exact.

- **`space` is `{ "w": 1300, "h": 1280, "unit": 320 }`**: the unit is units per inch. 1300 / 320
  = 4.0625 in wide, 1280 / 320 = 4 in tall. A unit is 0.003125 in, and 320 is a print
  resolution if a PDF is ever added.
- **`parts` is the three panels**: `flap` 0-320, `spine` 320-480, `cover` 480-1300, all 1280
  tall. The webapp clips the cover and the spine from the same drawing; the editor draws the
  fold guides at x = 320 and x = 480 and a focus for each part. The panels are guides, not
  clipping: art may cross a fold, and the webapp's cover view clips at x = 480 and x = 1300.
- **It is one printed side.** There is no back, no tracklist and no print export: the three
  panels are the whole J-card's design. A future back is a format version, not a field.
- **`background` is the sheet's colour**, a `#RRGGBB` painted under the prims. It is the
  section's own; nothing else moves with it, and no full-sheet rect is needed to paper the
  design.
- **It uses the container's faces** (§2), and its `fonts` carry the same three faces with the
  J-card's own **sizes** in units. The defaults are the label's proportions on the sheet's
  width, rounded: `title` 56, `numeral` 136, `caption` 31 (0.175 in, 0.425 in and 0.097 in). A
  size is an integer 4..640 units; the editor's Type panel edits the three.
- **Its texts bind to the label's fields as well as its own.** A J-card text may name any of
  the eleven Slots (§3) - a tempo, a key, a genre - and the J-card's three: `title` (the
  album's name, or the Track's), `artist` (the byline: one artist, or "N artists" for a
  compilation) and `meta` (the line the App passes - `Side A · Side B` by default, the album's
  name on a Track's case). The App fills the fields the surface knows and prints nothing for
  the rest, as the label does with no value: an album's case has its name, its byline, its
  genre and the cassette line, and the tempo and the key are a Track's. `title` and `artist`
  stay accepted for the designs made before the vocabulary grew; the panel offers the Slots
  and `meta`.
- **The prims are the label's shapes, in literal colours**: a `rect` with
  `x, y, w, h, r?, rot?, fill`; a `circle` is a rect whose `r` is `min(w, h) / 2`; a `text`
  with `x, y, w, rot?, font, color, align` and either `slot` (dynamic) or `text` (static, at
  the back of the same array). `fill` and `color` are `#RRGGBB`. A dynamic text
  carries `preview` for the editor; the webapp ignores it. The primitive record is the label
  spec's, with the J-card's font roles and no `still`. **A text's height is its own lines'**:
  that box is what `rot` turns about, as the label's and the editor's canvas do, and the
  sheet's bottom caps it - the lines that do not fit end in an ellipsis.
- **The cover has a safe square.** The App's album card is square and shows the central
  820x820 of the cover - x 480-1300, y 230-1050 - as a centre crop. The design's words belong
  inside it; the rest is the case's portrait face. The editor does not draw it: the canvas is
  the sheet, and where the card crops is the App's fact, not a guide.

## 5. The webapp's face

An album with a cover picture shows the picture, as it does today. **An album without one shows
its pool's J-card** - the factory J-cards and the active user pack's, chosen by the album's
stable name hash ([`art-packs.md`](./art-packs.md) §6).

- **The cover part is the case front**, the Player's case and the album card. The case front is
  16:25 against the cover's 0.640625 - a 0.1 % difference, so the drawing fills it. The album
  card is square: the cover fills it as a cover image does (centre crop), which is why the
  editor draws the safe square.
- **The spine part is the shelf's left face.** The case's depth is 2.6/13 = 0.2 against the
  spine's 0.5/2.5625 = 0.195; the webapp draws the spine into that face, and the 2.5 % stretch
  is the case's own geometry. The spine's own type is the design's; the App's byline text is
  not drawn over it.
- **The flap is the case's back.** It folds around the spine, so on the back it is the strip
  at the spine's edge - 1 in of the 2.5625 in back, `320 / 820` of the case's width - and the
  App draws it there, on every case whose album has no picture. There is no turn-over: a shelf
  case faces front or shows its spine, never its back, so the editor's Preview shows those two
  faces and not the flap.
- **The renderer** draws the `jcard` prims in the DOM: each prim's own colour, over the
  section's `background`, the faces from the page's font loading, the sizes as a share of the
  sheet's width (a `size`
  of 56 on 1300 is 4.31% of the width, whatever the surface's pixel size), the rotation, the
  alignment and the wrapping/ellipsis of a text. The face is decorative (`aria-hidden`), as
  today's drawn cover is; the album or Track is named beside it.
- **The five CSS faces retire.** `web/src/ui/album-art.tsx` and the `.k7-album-art*` rules keep
  the no-cover slot, the hue and the accessibility they have, and lose the five template
  drawings; the look's K19 amendment records it ([`k7-look.md`](./k7-look.md) §5.3).

## 6. The editor's two surfaces

The **K7Art Editor** edits one K7Art as one document with two surfaces.

- **The label surface is today's editor**: the 748x359 stage, the Cassette's window, the layers
  and their colours, the type roles, the Slots and test values, the Design panel's paper, the
  fixed black window, the Canvas backgrounds, the LVGL preview and the label's export errors.
- **The J-card surface is the one sheet**: 1300x1280, the fold guides at 320 and 480, a focus
  for the sheet and for each of the flap, spine and cover, the sheet's background and its
  layers' colours and the Type panel's three J-card sizes; its texts take their test values
  from the label's Test values. It renders in the DOM - there is no LVGL for a paper design -
  with the safe-square guide on the cover.
- **The J-card surface's Preview is the App's shelf**, drawn by the App's own renderer: two
  cases, the turned one showing the cover part and the one beside it, spine out, the spine
  part - the drawing an album with no picture wears ([§5](#5-the-webapps-face)). It is a
  preview of the design, not of a library: the media page's arrows, album name, index rail,
  wheel and arrow keys are not part of it.
- **One document, one history, one export.** Switching surfaces does not clear the undo; the
  document autosaves both sections; Export writes the K7Art; Publish writes a pack
  ([`art-packs.md`](./art-packs.md) §3). The label's Canvas and Paper controls, and the
  window, are the label's only; the J-card's Sheet, Type and Template are the J-card's.
- **The WebMCP tools name the surface.** `k7art-read` returns both sections and the active
  surface; `k7art-add-layer`, `k7art-update-layer` and the rest take the surface, a colour
  (`#RRGGBB`) and, for a J-card text, a Slot or one of the J-card's fields; the guide is
  `k7art-guide` / `/k7art/skill.md`. The editor is the Site's page at `/k7art`
  ([ADR 0112](../adr/0112-the-k7art-editor-is-the-sites-k7art-page.md)); it does not reach the
  device.
- **A colour belongs to a layer.** There is no design-wide palette to edit: each label and
  J-card layer carries its own colour, the print's paper is the label's own, and the J-card its
  background. The editor's swatch row is a convenience for a new layer, not a design palette.
  The faces are the vocabulary's three; the label's sizes are the compiled pairs, the J-card's
  are the three sizes.

## 7. Validation

The editor refuses at Export and at Open; the packer refuses at the build; the webapp refuses
what it cannot draw. The facts are one table.

| Refusal | Why |
|---|---|
| `format` is not `k7art` or `v` is not 2 | a different file, and v1 is the editor's to migrate |
| the id is not `k7.<slug>`, or the same id twice | the folder is the set |
| no `label` or no `jcard` | the design is the pair |
| the label section fails [`label-templates.md`](./label-templates.md) §7 | the sheet cannot hold it |
| `jcard.space` is not 1300x1280 at 320 units per inch | the sheet is the case's |
| a `parts` rectangle is not the flap, spine or cover | the fold lines are the case's |
| `background` is missing or not `#RRGGBB` | what would the sheet be painted |
| a J-card text names a field that is not a Slot, `title`, `artist` or `meta` | the App carries the label's fields and its own three |
| a J-card font face is not one of the three, or a size is outside 4..640 | the design is the pair's |
| a J-card `fill` or text `color` is not `#RRGGBB` | there is nothing to print |
| a J-card prim outside the sheet's +/-4096 units | the file is bounded |
| more than 64 J-card prims, or a J-card over 16 KB | the App's bundle and the pack's cache |

The 320-units-per-inch space is exact: the packer never sees a J-card, and the webapp draws it
at whatever size its surface has. The label section's refusals are the label spec's, unchanged.

## 8. Migration and costs

- **v1 becomes v2 in the editor, once.** A v1 K7Art's `inks` resolve each prim's role to a
  colour (`paper` is the mask's, `ink`/`rule`/`band.N` are themselves), the full-print
  `fill: "paper"` rect becomes the label's `mask`, the J-card's `paper` becomes `background`,
  and a J-card full-sheet paper rect is dropped because the background paints it. A design
  or document without a full-print paper rect gets the default paper, and an aperture layer
  is dropped: the window is the Cassette's, not a layer. The editor writes v2; the packer, the
  firmware and the App never read v1.
- **The editor's documents** (`label-editor.docs.v3`) migrate the same way on load and the
  store's key becomes `v4`. A document without a `jcard` gets the starter, from its own paper
  and faces. The retired `label-template v1` import resolves its `inks` too.
- **The factory designs** in `assets/k7art/` are converted with the editor's own import and
  export, so their v2 bytes are what the editor would write for them.
- **An old pack is refused, not read.** Its sheet is version 4 and its J-cards `v: 1`; the
  K7's pack reader falls back to the factory sheet, and the App refuses the file with a line
  saying an older editor made it and to publish it again ([`art-packs.md`](./art-packs.md) §1).
- **The costs**: the sheet loses 48 bytes per design (the ink words); a K7Art is a few KB (the
  label section plus the J-card's prims); the webapp bundles the factory J-cards (a few KB
  each); the editor draws the J-card in the DOM, so the LVGL module is the label's only; the
  pack is [`art-packs.md`](./art-packs.md)'s.

## 9. Tests

- **The editor**: the container's build/import round-trips byte for byte (v2), a v1 K7Art and a
  v1 stored document migrate to the same colours, a document or file without a mask or an
  aperture opens with the defaults and exports them, a stored aperture layer is dropped, and a
  J-card's geometry, fields, sizes and refusals hold; the two surfaces' undo and autosave; the
  `/k7art` base.
- **The packer**: every `assets/k7art/*.json` packs to the sheet bytes the editor's own writer
  makes for it; a K7Art with a bad J-card still fails the build naming it (the editor refuses
  it first).
- **The webapp**: the renderer's colours, sizes, rotation and wrap; the cover and spine
  clips; the no-cover rule and the real cover; the pool; the album card's safe square.
- **The parity**: the editor's TypeScript sheet writer against `label_pack.py` for every
  factory K7Art ([`art-packs.md`](./art-packs.md) §3).
- **The board**: the host's `make -C host label` still packs and opens the sheet; the editor
  still boots at `/k7art/`.

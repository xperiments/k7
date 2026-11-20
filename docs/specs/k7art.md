# K7Art: one design, a label and a J-card

A **K7Art** is one cassette artwork: the label the K7 prints on the Card at the two-finger
pose, and the J-card the cassette case is sleeved with - one file, one id, two sections. It is
authored in the **K7Art Editor** (`tools/LabelEditor/`, served at `/k7art`) and read by two
consumers: `tools/label_pack.py` packs its `label` section into the factory **Label sheet**, and
the webapp renders its `jcard` section as the cover of an album that has no picture. [ADR
0110](../adr/0110-the-artwork-is-one-k7art-and-its-label-and-j-card-are-sections.md) records
why the two are one file; [`label-templates.md`](./label-templates.md) keeps the label
section's binary, its vocabulary and its Slots; [`art-packs.md`](./art-packs.md) is the pack a
user publishes and the device that stores it.

The vocabulary - **K7Art**, **J-card**, **J-card panel**, **J-card field** - is in
[CONTEXT.md](./CONTEXT.md). The Card, the print and the Window are
[`cassette-card.md`](./cassette-card.md)'s.

## 1. The file

One design is one file at `assets/k7art/k7.<slug>.json`, id `k7.<slug>`, and one id is one
pair: **the label and the J-card are one-to-one**. There is no design without a J-card and no
J-card without a label.

```json
{
  "format": "k7art",
  "v": 1,
  "id": "k7.neon",
  "name": "Neon Skyline",
  "inks": {
    "paper": "#F5F1E6",
    "ink": "#242424",
    "rule": "#8E7E4A",
    "band": ["#C68B2E", "#A24E2C", "#8A5D34", "#6B7A2F",
             "#3B6E8F", "#7A3B52", "#B9AC8E", "#2F4B3A"]
  },
  "label": {
    "space": { "w": 748, "h": 359 },
    "aperture": { "x": 95, "y": 101, "w": 556, "h": 166, "r": 83 },
    "canvas": { "backgrounds": ["#909090"], "active": 0 },
    "fonts": { "title":   { "ff": "Archivo Narrow 600", "px": 32 },
               "numeral": { "ff": "Barlow Condensed 700", "px": 78 },
               "caption": { "ff": "IBM Plex Mono 500", "px": 18 } },
    "slots": { "song_title": { "max": 96 } },
    "notes": ["the spike is the one literal colour: it is the sleeve's, not the sheet's"],
    "prims": [
      { "t": "rect", "x": 0, "y": 0, "w": 748, "h": 359, "r": 19,
        "fill": "paper", "still": true },
      { "t": "text", "slot": "song_title", "x": 40, "y": 300, "w": 400,
        "font": "title", "color": "ink", "align": "left", "preview": "Neon Skyline" }
    ]
  },
  "jcard": {
    "space": { "w": 1300, "h": 1280, "unit": 320 },
    "parts": {
      "flap":  { "x": 0,   "y": 0, "w": 320, "h": 1280 },
      "spine": { "x": 320, "y": 0, "w": 160, "h": 1280 },
      "cover": { "x": 480, "y": 0, "w": 820, "h": 1280 }
    },
    "paper": "#F5F1E6",
    "fonts": { "title":   { "ff": "Archivo Narrow 600",   "size": 56 },
               "numeral": { "ff": "Barlow Condensed 700", "size": 136 },
               "caption": { "ff": "IBM Plex Mono 500",    "size": 31 } },
    "prims": [
      { "t": "rect", "x": 0, "y": 0, "w": 1300, "h": 1280, "fill": "paper" },
      { "t": "rect", "x": 480, "y": 230, "w": 820, "h": 4, "fill": "rule" },
      { "t": "text", "slot": "title",  "x": 540, "y": 900, "w": 700,
        "font": "numeral", "color": "ink", "align": "left", "preview": "Neon Skyline" },
      { "t": "text", "slot": "artist", "x": 540, "y": 1060, "w": 700,
        "font": "title", "color": "ink", "align": "left", "preview": "The Analog Sons" },
      { "t": "text", "slot": "meta",   "x": 540, "y": 1120, "w": 700,
        "font": "caption", "color": "ink", "align": "left", "preview": "Side A · Side B" }
    ]
  }
}
```

- **The key order is the file's**: `format`, `v`, `id`, `name`, `inks`, `label`, `jcard`. The
  sections' keys follow their own spec's order. The editor writes it deterministically; the
  tests compare bytes.
- **`format` and `v`** are `k7art` and `1`. The packer and the webapp refuse anything else by
  name. The id is `k7.<slug>`, the slug `[a-z0-9][a-z0-9-]*`; the folder is the set and the
  packer orders by id.
- **`name`** is the design's name, in the K7's sheet log and in the App's pack list.
- **The old file is not this one.** `label-template v1` is retired; a file with `v` at the top
  level and `prims` beside it is not a K7Art, and the packer refuses it. The editor imports one
  and migrates it into a K7Art with a starter J-card (§8).

## 2. The palette and the faces

The container's `inks` are the design's eleven colour roles and the container's faces are the
three the editor draws. Both sections use them; a role is a role on both media.

- **`inks`** is `paper`, `ink`, `rule` and `band[0..7]`. `paper` is always a `#RRGGBB` colour:
  it is the label's LayerMask when the label has one. The J-card's sheet is its own
  `jcard.paper`, a `#RRGGBB` colour of the J-card section; the two start equal and move
  independently, so changing one never changes the other. **The sheet itself is painted in
  it**, so a design is papered with or without a full-sheet rect; the case's moulding is the
  chrome's and never shows through a J-card. `band` has four to eight colours; the
  J-card may use any of the eight, and a design with four leaves the rest to the editor's
  defaults.
- **The faces are the same three**: `Archivo Narrow 600`, `Barlow Condensed 700` and
  `IBM Plex Mono 500`. The label sizes them by the sheet's compiled pairs (`32`, `78` and `18`
  px) - only those pairs exist as firmware fonts. The J-card sizes them in its own units (§4).
- **A prim names a role, not a colour.** A shape paints with `paper`, `ink`, `rule` or
  `band.N`; a text prints in `ink` or a band, never on the paper. The label's one literal
  colour (a `#RRGGBB` fill with a `notes` line) stays the label's exception; the J-card's prims
  use the roles only.

## 3. The label section

The label section is the design the two-finger pose prints: 748x359 at the print's entry grid,
authored as it was in `label-template v1` and packed by `label_pack.py` into the factory Label
sheet. The sheet's binary, its version, its vocabulary, its Slots and every refusal are
[`label-templates.md`](./label-templates.md)'s; this section only says what moved.

- **The identity and the palette moved to the container.** The section has no `v`, `id`, `name`
  or `inks`; it has the `space`, `aperture`, `canvas`, `fonts`, `slots`, `notes` and `prims`.
  The packer takes the id, the name and the colours from the container and validates the
  section with the rules the label spec already states.
- **`space` is 748x359**, `aperture` is absent or exactly the Card's window, `canvas` is the
  optional list of up to eight cassette backgrounds, `fonts` is the three compiled pairs,
  `slots` is the eleven fields the design uses (each `max: 96`), `notes` is the literal
  colour's explanation, and `prims` is the print.
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
- **It uses the container's `inks` and faces** (§2), and its `fonts` carry the same three
  faces with the J-card's own **sizes** in units. The defaults are the label's proportions on
  the sheet's width, rounded: `title` 56, `numeral` 136, `caption` 31 (0.175 in, 0.425 in and
  0.097 in). A size is an integer 4..640 units; the editor's Type panel edits the three.
- **Its texts bind to three J-card fields**: `title`, `artist` and `meta`. `title` is the
  album's name, `artist` its byline (one artist, or "N artists" for a compilation), and `meta`
  the line the App passes - `Side A · Side B` by default, the album's name on a Track's case
  ([`../web/src/ui/album-art.tsx`](../../web/src/ui/album-art.tsx)'s `AlbumArtData`). The
  editor's simulations are `Neon Skyline`, `The Analog Sons` and `Side A · Side B`.
- **The prims are the label's shapes**: a `rect` with `x, y, w, h, r?, rot?, fill`; a `circle`
  is a rect whose `r` is `min(w, h) / 2`; a `text` with `x, y, w, rot?, font, color, align`
  and either `slot` (dynamic) or `text` (static, at the back of the same array). A dynamic text
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
- **The renderer** draws the `jcard` prims in the DOM: the roles from the container's `inks`,
  the faces from the page's font loading, the sizes as a share of the sheet's width (a `size`
  of 56 on 1300 is 4.31% of the width, whatever the surface's pixel size), the rotation, the
  alignment and the wrapping/ellipsis of a text. The face is decorative (`aria-hidden`), as
  today's drawn cover is; the album or Track is named beside it.
- **The five CSS faces retire.** `web/src/ui/album-art.tsx` and the `.k7-album-art*` rules keep
  the no-cover slot, the hue and the accessibility they have, and lose the five template
  drawings; the look's K19 amendment records it ([`k7-look.md`](./k7-look.md) §5.3).

## 6. The editor's two surfaces

The **K7Art Editor** edits one K7Art as one document with two surfaces.

- **The label surface is today's editor**: the 748x359 stage, the Card's window, the layers,
  the colours, the eight bands, the type roles, the Slots and test values, the LayerMask and
  the aperture, the Canvas backgrounds, the LVGL preview and the label's export errors.
- **The J-card surface is the one sheet**: 1300x1280, the fold guides at 320 and 480, a focus
  for the sheet and for each of the flap, spine and cover, the shared Colours, the Type panel's
  three J-card sizes, and the Fields panel's three test values. It renders in the DOM - there
  is no LVGL for a paper design - with the safe-square guide on the cover.
- **The J-card surface's Preview is the App's shelf**, drawn by the App's own renderer: two
  cases, the turned one showing the cover part and the one beside it, spine out, the spine
  part - the drawing an album with no picture wears ([§5](#5-the-webapps-face)). It is a
  preview of the design, not of a library: the media page's arrows, album name, index rail,
  wheel and arrow keys are not part of it.
- **One document, one history, one export.** Switching surfaces does not clear the undo; the
  document autosaves both sections; Export writes the K7Art; Publish writes a pack
  ([`art-packs.md`](./art-packs.md) §3). The label's Canvas, Mask and Aperture sections are the
  label's only; the J-card's Sheet, Type and Fields are the J-card's.
- **The WebMCP tools name the surface.** `k7art-read` returns both sections and the active
  surface; `k7art-add-layer`, `k7art-update-layer` and the rest take the surface (and, for a
  J-card text, a J-card field); the guide is `k7art-guide` / `/k7art/skill.md`. The editor is
  the Site's page at `/k7art` ([ADR 0112](../adr/0112-the-k7art-editor-is-the-sites-k7art-page.md));
  it does not reach the device.
- **The palette is edited once.** The Colours panel is the container's `inks`; a change repaints
  both surfaces. The faces are the vocabulary's three; the label's sizes are the compiled
  pairs, the J-card's are the three sizes.

## 7. Validation

The editor refuses at Export and at Open; the packer refuses at the build; the webapp refuses
what it cannot draw. The facts are one table.

| Refusal | Why |
|---|---|
| `format` is not `k7art` or `v` is not 1 | a different file |
| the id is not `k7.<slug>`, or the same id twice | the folder is the set |
| no `label` or no `jcard` | the design is the pair |
| the label section fails [`label-templates.md`](./label-templates.md) §7 | the sheet cannot hold it |
| `jcard.space` is not 1300x1280 at 320 units per inch | the sheet is the case's |
| a `parts` rectangle is not the flap, spine or cover | the fold lines are the case's |
| a J-card text names a field that is not `title`, `artist` or `meta` | the App carries three words |
| a J-card font face is not one of the three, or a size is outside 4..640 | the design is the pair's |
| a J-card role is not the container's, or a text is on the paper | there is nothing to print |
| a J-card prim outside the sheet's +/-4096 units | the file is bounded |
| more than 64 J-card prims, or a J-card over 16 KB | the App's bundle and the pack's cache |
| a literal colour in the J-card | the J-card is role-coloured |

The 320-units-per-inch space is exact: the packer never sees a J-card, and the webapp draws it
at whatever size its surface has. The label section's refusals are the label spec's, unchanged.

## 8. Migration and costs

- **The one existing design**, `label.neon`, becomes a K7Art: its label section is today's
  file's fields (the identity and `inks` at the container), and its J-card is a starter - the
  paper, the fold guides, and `title`, `artist` and `meta` at the defaults. The editor imports
  a `label-template v1` and does the same; the packer accepts only K7Arts.
- **The editor's documents** (`label-editor.docs.v2`) migrate on load: a document without a
  `jcard` gets the starter, from its own label's paper and faces. The store's key becomes the
  K7Art's version.
- **The folder moves** from `assets/labels/templates/` to `assets/k7art/`; `assets/labels/`
  keeps `vocabulary.json` and the K7's pictures (`cover.jpg`, `k7logo.png`, `k7b.png`). The
  editor's `vocab.generated.ts` and its check are unchanged.
- **The costs**: a K7Art is a few KB (the label section is today's file plus the J-card's
  prims); the webapp bundles the factory J-cards (a few KB each); the editor draws the J-card
  in the DOM, so the LVGL module is the label's only; the label sheet is unchanged; the pack is
  [`art-packs.md`](./art-packs.md)'s.

## 9. Tests

- **The editor**: the container's build/import round-trips byte for byte (the label section
  exactly as `tpl.test.ts` compares it today); the J-card's geometry, fields, sizes and
  refusals; the two surfaces' undo and autosave; the `/k7art` base.
- **The packer**: every `assets/k7art/*.json` packs to the same sheet bytes a `label-template
  v1` did for its label section; a K7Art with a bad J-card still fails the build naming it
  (the editor refuses it first).
- **The webapp**: the renderer's roles, sizes, rotation and wrap; the cover and spine clips;
  the no-cover rule and the real cover; the pool; the album card's safe square.
- **The parity**: the editor's TypeScript sheet writer against `label_pack.py` for every
  factory K7Art ([`art-packs.md`](./art-packs.md) §3).
- **The board**: the host's `make -C host label` still packs and opens the sheet; the editor
  still boots at `/k7art/`.

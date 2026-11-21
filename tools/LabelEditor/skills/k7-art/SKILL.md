---
name: k7-art
description: Design a K7 cassette artwork — the label the K7 player prints on its two-finger screen and the J-card the webapp draws on an album with no cover — through the K7Art Editor's WebMCP tools. Read the open K7Art, lay out the label's paper, bands and type and the J-card's flap, spine and cover, bind the fields, validate, export the K7Art and publish a pack. Use when asked to make, change, review or explain a K7 cassette label or J-card.
---

# A K7Art for the K7

The K7 is a cassette player. Hold a cassette up to its screen with two fingers and the print
becomes that Track's **label**: a design the player draws in the cassette's own box and fills
with what it knows about the track — title, artist, BPM, key, mood. One design has to read for
every Track in the library, so a label is a layout plus references to the player's fields.

The same design carries a **J-card**: the paper insert of a cassette case, one side, 4.0625 × 4
in, three panels — flap | spine | cover. The webapp draws its cover on an album with no
picture and its spine on the shelf; it fills `title`, `artist` and `meta`, not the label's Slots.

You design both in the **K7Art Editor**, a page that offers its actions to you as tools. This
file is the working guide; `tools/LabelEditor/README.md`, `docs/specs/k7art.md` and
`docs/specs/label-templates.md` are the deeper reference, and `docs/specs/art-packs.md` is the
pack. The editor serves this file at `/k7art/skill.md`, and `k7art-guide` returns it as a tool
result for a session where that route is blocked.

## What you are designing

The label half:

- The print is **748 × 359 design pixels**, origin top-left, x right, y down. The panel's
  screen is 800 × 480 and the print sits at (25,19) on it; in these numbers the print's own
  box is (0,0) to (748,359). There is no other size.
- A layer may **bleed** outside that box — a band to the edge and past it — and the print
  crops it. Only the record's bounds refuse (x and y are int16, w and h uint16).
- The **Card's window** is (95,101) **556 × 166, radius 83**. It is a hole in the design: the
  cassette's tape shows through it, so nothing drawn there is seen. What is left for type is
  the strip above y 101, the strip below y 267, and the narrow margins left of x 95 and right
  of x 651. All three roles fit either strip — 29 px of ink for a title, 17 for a caption,
  55 for a numeral's digits — so the discipline is only to keep the whole line inside one of
  them. Ornaments may cross the hole; they are simply not seen there.
- The **paper** is the design's background: a rounded rect filling the print, its colour and
  radius taken from the LayerMask. `set-design` gives you one, and a new document starts with
  `#c7a867` at radius 10. Without paper a design has no background, and a shape may not fill
  with `paper` when there is none.
- The **canvas background** is the cassette's body and the 800 × 480 screen behind it while the
  two-finger pose shows the tape: up to eight presets with one active, set through `set-design`'s
  `backgrounds` and `activeBackground` (`background` sets the active preset's colour). The whole
  cycle is packed; the K7 picks one by the Track's cassette row (`row % count`), so define seven
  to pin one per row. A design without one leaves the Track's cassette row as it is.
- Everything is drawn once, at one size. There is no zoom and no responsive layout.

The J-card half:

- The sheet is **1300 × 1280 at 320 units per inch** — 4.0625 × 4 in — with the **flap** 0–320,
  the **spine** 320–480 and the **cover** 480–1300; the folds are guides, and art may cross
  them.
- It uses the same eleven colour roles and the same three faces as the label, with its own three
  sizes; its texts bind to **`title`**, **`artist`** and **`meta`**, not to Slots.
- The sheet has **its own paper**, `jcardPaper` on `set-design`: it starts equal to the label's
  and moves independently, and the webapp paints the sheet with it. `paper` stays the label's
  (the LayerMask); the two are never set by the same argument.
- A text's box is **its own lines**, capped by the sheet's bottom, and `rotation` turns it about
  that box — a spine line at 90° runs up the panel rather than across it.
- The webapp draws the cover on the case front and the album card — a centre crop, so keep the
  words in the safe square (x 480–1300, y 230–1050) — the spine on the shelf's left face, and
  the flap on the case's back, folded around the spine.
- One printed side: no back, no tracklist, no print export.

## Before your first call

The tools arrive with the page and their names begin `k7art-`; `k7art-read` is
the one to call first. If you cannot see them, the editor page is not open, or the browser has
no WebMCP — ask the human to open the editor (http://localhost:5178 in development, or
https://k7.xperiments.dev/k7art from the Site) in a browser that has the API. The
same calls also live on `window.__labelEditorMcp` for a page script, which is how the repo's
own `scripts/test-label.mjs` drives them.

If the page's routes are blocked, call `k7art-guide` instead of reading `/k7art/skill.md`.
If the tools themselves cannot be reached, every action still has a visible control — see
*Driving the visible controls*, below — so a label is still finishable in this session.

- **Read first, every time.** `k7art-read` returns the board, the fields, the colour
  roles, the type roles, the open documents, and the active design's layers, groups and export
  errors.
- **The page is the truth; this file is the guide.** If a number here and the page disagree,
  the page wins: `fields` and `typeRoles` in the read are the live vocabulary.
- **A refused call throws and changes nothing.** The message names the argument and the
  reason; fix it and call again. Only `k7art-export-template` reports instead of
  throwing (`ok: false` with `errors`).
- Every edit updates the page and is undoable: `k7art-history` undoes it.

## The vocabulary

**Colour roles.** `paper`, `ink`, `rule`, `band.0` … `band.7`. A shape (rect, square, circle)
may use any of them; a text may print only `ink` or a band. `paper` is the LayerMask's colour.

**Groups.** Two or more layers of one surface can become a group: they must be a contiguous run
of that surface's stack, and on the label they must sit on one side of the LayerMask. A group
moves, locks and collapses as one, and copying a selected group and pasting it makes a second
group of its own. It is the editor's: the file carries the layers, not the grouping.
`k7art-group-layers` groups or ungroups the selection.

**Bands.** The eight band colours are the design's palette. Declare 4 to 8 of them; the sheet
always carries eight slots, so a design with four pads the rest.

**Type.** Three roles, and each is a face *and* a size. There are no others:

| Role | Face | Size |
|---|---|---|
| `title` | Archivo Narrow 600 | 32 px |
| `numeral` | Barlow Condensed 700 | 78 px |
| `caption` | IBM Plex Mono 500 | 18 px |

A face and size the sheet does not carry is refused by name, so a role is the pair, never a
face with a size typed beside it.

**Fields.** Eleven, filled by the player from the track's snapshot when the track changes:

| Field | Prints | Example |
|---|---|---|
| `song_title` | the title | Neon Skyline |
| `song_artist` | the artist | The Analog Sons |
| `song_album` | the album | Night Drive |
| `genre` | the genre | Synthwave |
| `bpm` | beats per minute | 124 |
| `musical_key` | the Camelot key | 8A |
| `energy` | 0–100 | 70 |
| `danceability` | 0–100 | 62 |
| `valence` | 0–100 | 55 |
| `arousal` | 0–100 | 48 |
| `mood` | happy, sad, aggressive, relaxed or party | relaxed |

A field the track has no value for prints as an empty string, so do not lean on one being
there. A text's `width` is its **wrap width**: a longer value wraps downward inside it and the
print crops what runs past the edge, so leave a long title room.

**Bounds.** 48 layers per design, 16 fields, 32 designs in the sheet, and a field's value is
cut at 96 characters. A design with no layer at all is refused.

## The workflow

1. **Read** — `k7art-read`; note the window's strips, and the active design's errors.
2. **A document** — `k7art-document { action: "new", name: "…" }` for a clean design, or
   work in the active one. A new document is the panel's page grey (`#909090`), a paper mask
   and no layers.
3. **The colourway** — `k7art-set-design`: `templateId` (`k7.my-name`), `name`,
   `backgrounds` and `activeBackground` (or `background` for the active one), `ink`, `rule`, the
   eight `bands`, `paper`, `paperRadius`. `paper` is the label's; `jcardPaper` is the sheet's
   own and is the one the J-card is painted with.
4. **Layers, back to front.** A layer added now prints over the ones before it;
   `k7art-move-layer` raises, lowers, or sends one to the top or bottom. Rects make
   bands, rules and blocks.
5. **Texts.** A text with `text` is static (ornaments: `SIDE A`, a numeral); a text with
   `field` prints the player's data. Give one or the other, never both. `fontRole` picks the
   type role, `align` its alignment, `role` its colour. A static text's box is measured from
   its ink at export, so its `width` is replaced rather than respected.
6. **Previews are yours, not the player's.** A dynamic text previews its simulated value;
   `k7art-set-field-value` changes that preview only, never what the player prints.
7. **The J-card** — the same document's other side. `k7art-add-layer { surface: "jcard" }`
   takes design units of its 1300 × 1280 sheet: the flap 0–320, the spine 320–480 and the
   cover 480–1300, with the folds as guides — art may cross them. Its texts bind to `title`,
   `artist` and `meta` through `field` (never a label Slot), its three sizes are `jcardFonts`,
   and its sheet colour is `jcardPaper`. Keep the cover's words inside the safe square,
   x 480–1300 and y 230–1050: the album card is exactly that centre crop.
8. **Validate** — `k7art-validate` checks every open design, both surfaces; fix until it is
   clean.
9. **Export** — `k7art-export-template` returns the `K7Art` object and its
   errors. Hand the JSON to the human: it is saved as `assets/k7art/<id>.json` and
   packed into the sheet by `make -C host label`.
10. **Publish** — `k7art-publish-pack { packId, name, author }` builds the `.k7artpack` the
   App installs on the K7 (Settings → Art packs). It refuses the same designs Export does, and
   its bytes are the sheet the K7 prints plus the J-cards the App draws.

Both surfaces are one document with one history: a J-card edit undoes like a label edit, and
Export and Publish write the two together. `k7art-read` always returns both sections and the
active surface, so read it again after switching.

## The tools

The complete reference — every tool, its sentence and its arguments — is written below from
the page's own registry. When you read this through `k7art-guide` that block is left
out, because your tool list already carries the same schemas.

<!-- generated: tools:start -->
### `k7art-read`

Read the K7Art editor: both surfaces — the label's 748x359 print, fields, roles and compiled type pairs, and the J-card's 1300x1280 sheet, folds, safe square, three fields and sizes — the active surface and focus, the open documents, both surfaces' layers, each surface's groups, the label's mask, the selection and the export errors.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `includeTemplate` | `boolean` |  | Also return the active design as a K7Art object. |

### `k7art-guide`

Read this once before designing: what a K7Art is for — the label's print and the J-card's sheet — how the print and the Card's window shape the label, the J-card's flap | spine | cover, the fields and the three faces, the workflow, what gets refused, and how to do all of it from the visible controls when the tools cannot be reached. The same guide is served at /k7art/skill.md.

Takes no arguments.

### `k7art-add-layer`

Add a layer to the active design, on the label's 748x359 print or the J-card's 1300x1280 sheet. Coordinates are design units of that surface, origin at its top-left; a layer may sit outside and still export, where the label's print crops it. A text is static unless a field is given; on the J-card the field is title, artist or meta.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `surface` | `label`, `jcard` |  | Which surface to add to. Defaults to the active surface. |
| `type` | `rect`, `square`, `circle`, `text` |  | What to add. Default `rect`. |
| `x` | `number` |  | Left edge in design units. Default 40. |
| `y` | `number` |  | Top edge in design units. Default 40. |
| `width` | `number` |  | Width in design units; for a text this is its wrap width. Minimum 1. |
| `height` | `number` |  | Height in design units, for a shape. A text's height follows its type. Minimum 1. |
| `text` | `string` |  | The string a static text prints. Do not combine with field. |
| `field` | `song_title`, `song_artist`, `song_album`, `genre`, `bpm`, `musical_key`, `energy`, `danceability`, `valence`, `arousal`, `mood`, `title`, `artist`, `meta` |  | The field a dynamic text prints. The label's: song_title, song_artist, song_album, genre, bpm, musical_key, energy, danceability, valence, arousal, mood. The J-card's: title, artist, meta. Do not combine with text. |
| `role` | `string` |  | The colour role. A shape takes paper, ink, band.0, band.1, band.2, band.3, band.4, band.5, band.6, band.7, rule; a text takes ink, band.0, band.1, band.2, band.3, band.4, band.5, band.6, band.7. |
| `fontRole` | `title`, `numeral`, `caption` |  | Which type role a text prints in. |
| `align` | `left`, `center`, `right` |  | A text's horizontal alignment. |
| `layerName` | `string` |  | The layer's name in the Layers panel. |
| `rotation` | `number` |  | Rotation in degrees, clockwise. |

### `k7art-update-layer`

Change one layer of the active design, on the label or the J-card: position or size in that surface's units, rotation, its text or field, colour role, font role, alignment, layer name, test value or lock. Only the fields given change.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `surface` | `label`, `jcard` |  | Which surface the layer is on. Defaults to the active surface. |
| `id` | `number` | yes | The layer id from k7art-read. Minimum 1. |
| `x` | `number` |  | Left edge in design units. |
| `y` | `number` |  | Top edge in design units. |
| `width` | `number` |  | Width in design units; a text's wrap width. Minimum 1. |
| `height` | `number` |  | Height in design units, for a shape. Minimum 1. |
| `rotation` | `number` |  | Rotation in degrees, clockwise. |
| `text` | `string` |  | Make the text static and print this string. |
| `field` | `song_title`, `song_artist`, `song_album`, `genre`, `bpm`, `musical_key`, `energy`, `danceability`, `valence`, `arousal`, `mood`, `title`, `artist`, `meta` |  | Make the text dynamic and print this field. The label's: song_title, song_artist, song_album, genre, bpm, musical_key, energy, danceability, valence, arousal, mood. The J-card's: title, artist, meta. |
| `role` | `string` |  | The colour role to print in. |
| `fontRole` | `title`, `numeral`, `caption` |  | The text's type role. |
| `align` | `left`, `center`, `right` |  | The text's horizontal alignment. |
| `layerName` | `string` |  | The layer's name in the Layers panel. |
| `testValue` | `string` |  | What a dynamic text previews in the editor and Preview. |
| `locked` | `boolean` |  | Lock or unlock the layer. Other changes need it unlocked. |

### `k7art-delete-layer`

Delete one layer of the active design, on the label or the J-card.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `surface` | `label`, `jcard` |  | Which surface the layer is on. Defaults to the active surface. |
| `id` | `number` | yes | The layer id from k7art-read. Minimum 1. |

### `k7art-select-layer`

Select one or more layers on the label or the J-card, so the person at the screen sees the same selection the tools are working on. Naming the surface switches the page to it.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `surface` | `label`, `jcard` |  | Which surface the layers are on. Defaults to the active surface. |
| `ids` | array of `number` | yes | Layer ids from k7art-read. The last one is the primary selection. |

### `k7art-move-layer`

Move a layer through its surface's stack: one step up or down, or straight to the top or bottom. The top of the stack prints last, over the layers below it.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `surface` | `label`, `jcard` |  | Which surface the layer is on. Defaults to the active surface. |
| `id` | `number` | yes | The layer id from k7art-read. Minimum 1. |
| `direction` | `up`, `down`, `top`, `bottom` | yes | Which way to move it. |

### `k7art-group-layers`

Group layers into one layer group, or ungroup a group, on either surface. Grouping needs two or more layers, and on the label it keeps them on one side of the LayerMask; a group is a contiguous run, which the tool checks. Groups belong to the surface they were made on.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `surface` | `label`, `jcard` |  | Which surface's layers to group: the label's print or the J-card's sheet. Groups do not span the two. |
| `action` | `group`, `ungroup` |  | Group or ungroup. Default group. Default `group`. |
| `ids` | array of `number` |  | The layers to group; omit to use the current selection. |

### `k7art-set-design`

Set the design's template id or name, its canvas background cycle (the K7 picks one by the Track's cassette row; the active one is only the editor's preview), its colourway (ink, rule, the eight bands, the label's paper and the J-card's own paper), its paper radius, one of the label's type roles' face and size, or one of the J-card's three type sizes. Every colour is a hex string; a label face and size must be a pair the sheet carries; a J-card size is 4..640 units.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `templateId` | `string` |  | The design id, like k7.my-design; it names the exported file. |
| `name` | `string` |  | The design's human name. |
| `background` | `string` |  | The active canvas background, as #rrggbb; it sets the editor's preview preset. |
| `backgrounds` | array of `string` |  | The canvas background cycle, as #rrggbb strings: one to 8. The K7 picks one by the Track's cassette row. |
| `activeBackground` | `number` |  | The index of the editor's preview preset. Minimum 0. |
| `ink` | `string` |  | The colour of the ink role, as #rrggbb. |
| `rule` | `string` |  | The colour of the rule role, as #rrggbb. |
| `bands` | array of `string` |  | The eight band colours, as #rrggbb strings, in band order. |
| `paper` | `string` |  | The label's paper, as #rrggbb: the LayerMask's colour. Adds the LayerMask paper if the design has none. |
| `jcardPaper` | `string` |  | The J-card sheet's own paper, as #rrggbb. It starts equal to the label's and moves independently; the webapp paints the sheet with it. |
| `paperRadius` | `number` |  | The paper's corner radius in design pixels. Minimum 0. |
| `fonts` | object |  | The label's type roles to change: a face from Archivo Narrow 600, Barlow Condensed 700, IBM Plex Mono 500 and one of that face's sizes. |
| `fonts.title` | object |  | The label's title role. |
| `fonts.title.face` | `Archivo Narrow 600`, `Barlow Condensed 700`, `IBM Plex Mono 500` |  | The face name. |
| `fonts.title.size` | `number` |  | The pixel size the sheet carries for that face. |
| `fonts.numeral` | object |  | The label's numeral role. |
| `fonts.numeral.face` | `Archivo Narrow 600`, `Barlow Condensed 700`, `IBM Plex Mono 500` |  | The face name. |
| `fonts.numeral.size` | `number` |  | The pixel size the sheet carries for that face. |
| `fonts.caption` | object |  | The label's caption role. |
| `fonts.caption.face` | `Archivo Narrow 600`, `Barlow Condensed 700`, `IBM Plex Mono 500` |  | The face name. |
| `fonts.caption.size` | `number` |  | The pixel size the sheet carries for that face. |
| `jcardFonts` | object |  | The J-card's type roles to change: the same three faces at the J-card's own sizes in units (4..640). |
| `jcardFonts.title` | object |  | The J-card's title role. |
| `jcardFonts.title.face` | `Archivo Narrow 600`, `Barlow Condensed 700`, `IBM Plex Mono 500` |  | The face name. |
| `jcardFonts.title.size` | `number` |  | The size in units, 4..640. Minimum 4. Maximum 640. |
| `jcardFonts.numeral` | object |  | The J-card's numeral role. |
| `jcardFonts.numeral.face` | `Archivo Narrow 600`, `Barlow Condensed 700`, `IBM Plex Mono 500` |  | The face name. |
| `jcardFonts.numeral.size` | `number` |  | The size in units, 4..640. Minimum 4. Maximum 640. |
| `jcardFonts.caption` | object |  | The J-card's caption role. |
| `jcardFonts.caption.face` | `Archivo Narrow 600`, `Barlow Condensed 700`, `IBM Plex Mono 500` |  | The face name. |
| `jcardFonts.caption.size` | `number` |  | The size in units, 4..640. Minimum 4. Maximum 640. |

### `k7art-set-field-value`

Set what a field prints in the editor and in Preview — its simulated value, shown until the resolver sends a real one. On the label the field is a Slot; on the J-card it is title, artist or meta. An empty string restores the default.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `surface` | `label`, `jcard` |  | Which surface's field to set. Defaults to the active surface. |
| `field` | `song_title`, `song_artist`, `song_album`, `genre`, `bpm`, `musical_key`, `energy`, `danceability`, `valence`, `arousal`, `mood`, `title`, `artist`, `meta` | yes | The field id. The label's: song_title, song_artist, song_album, genre, bpm, musical_key, energy, danceability, valence, arousal, mood. The J-card's: title, artist, meta. |
| `value` | `string` | yes | The value to preview; empty restores the default. |

### `k7art-import-template`

Open a K7Art design in a new document. Accepts the template object or its JSON text. A design the packer would refuse is refused here with the reason, and leaves no half-open document.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `template` | `object` | yes | The K7Art object (or pass JSON text in this field). |
| `name` | `string` |  | The document's tab name; defaults to the template's name. |

### `k7art-export-template`

Return the active design (or a named one) as a K7Art object, together with the validation errors that would stop it being exported.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `document` | `string` |  | The tab name or template id to export; defaults to the active design. |

### `k7art-publish-pack`

Build a .k7artpack from named designs (all open designs by default): the version-4 label sheet, the designs' J-cards and the 128-byte header with the CRC. Returns the pack as base64 with its header facts, or the packer's refusals; it does not download a file or call the device.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `designs` | array of `string` |  | The tab names or template ids to include; omit for every open design. |
| `packId` | `string` |  | The pack's slug id: lowercase letters, digits and dashes, up to 16 characters. Defaults to the active design's slug. |
| `name` | `string` |  | The pack's name for the App's list. Defaults to the active design's name. |
| `author` | `string` |  | The pack's author. Defaults to K7Art. |

### `k7art-validate`

Check every open design and return the packer's reasons for any that would not export.

Takes no arguments.

### `k7art-document`

Work with the open documents: create a new one, switch to one, rename the active one, or close one. Closing the last document leaves an empty one open.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `action` | `new`, `switch`, `rename`, `close` | yes | What to do. |
| `name` | `string` |  | A name, for new or rename. |
| `index` | `number` |  | The document's position in the tab strip, for switch or close. Minimum 0. |

### `k7art-history`

Undo or redo the last edit of the active design.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `action` | `undo`, `redo` | yes | Undo or redo. |
<!-- generated: tools:end -->

## Two worked designs

For more looks to aim at — the sleeve devices, and seven pasteable prompts, three of them
built — see `tools/LabelEditor/docs/label-prompts.md`.


The first is the smallest label that reads: a top strip of one band, and a numeral above the
window. The colour is set in `set-design` so the cream ink reads on the green band.

```js
await call("k7art-document", { action: "new", name: "HOLA" });
await call("k7art-set-design", {
  templateId: "k7.hola", name: "HOLA",
  ink: "#f2ecdf", paper: "#c7a867", paperRadius: 12,
});
await call("k7art-add-layer", {
  type: "rect", x: 0, y: 0, width: 748, height: 101, role: "band.7",
});
await call("k7art-add-layer", {
  type: "text", text: "HOLA", x: 0, y: 4, width: 748,
  role: "ink", fontRole: "numeral", align: "center",
});
const { template, errors } = await call("k7art-export-template", {});
```

The second is a song label: a colour block above the window carrying the title, artist and
BPM, and the small print below it.

```js
await call("k7art-document", { action: "new", name: "Neon Skyline" });
await call("k7art-set-design", {
  templateId: "k7.neon-skyline", name: "Neon Skyline",
  ink: "#f2ecdf", rule: "#8e7e4a",
  bands: ["#c68b2e", "#a24e2c", "#8a5d34", "#6b7a2f",
          "#3b6e8f", "#7a3b52", "#b9ac8e", "#2f4b3a"],
  paper: "#142538", paperRadius: 10,
});
// above the window: the title block
await call("k7art-add-layer", {
  type: "rect", x: 0, y: 0, width: 748, height: 101, role: "band.4",
});
await call("k7art-add-layer", {
  type: "text", field: "song_title", x: 24, y: 8, width: 520,
  role: "ink", fontRole: "title",
});
await call("k7art-add-layer", {
  type: "text", field: "song_artist", x: 24, y: 46, width: 520,
  role: "band.6", fontRole: "title",
});
await call("k7art-add-layer", {
  type: "text", field: "bpm", x: 580, y: 2, width: 144,
  role: "ink", fontRole: "numeral", align: "right",
});
// below the window: the small print
await call("k7art-add-layer", {
  type: "rect", x: 24, y: 300, width: 700, height: 2, role: "rule",
});
await call("k7art-add-layer", {
  type: "text", text: "SIDE A", x: 24, y: 272, width: 120,
  role: "band.6", fontRole: "caption",
});
await call("k7art-add-layer", {
  type: "text", field: "mood", x: 604, y: 272, width: 120,
  role: "band.6", fontRole: "caption", align: "right",
});
await call("k7art-add-layer", {
  type: "text", field: "song_album", x: 24, y: 316, width: 420,
  role: "ink", fontRole: "caption",
});
await call("k7art-add-layer", {
  type: "text", field: "musical_key", x: 660, y: 316, width: 64,
  role: "ink", fontRole: "caption", align: "right",
});
const { template, errors } = await call("k7art-export-template", {});
```

Both are ten layers or fewer, both fit the bounds, and both leave the window clear.

## What gets refused

A refusal is a sentence naming the design and the layer, so read it rather than guessing:

- `templateId must look like k7.my-design` — lowercase letters, digits and dashes after
  `label.`, and it names the exported file.
- `bands must be the eight band colours, in order` — pass all eight; the design may still
  declare fewer when imported, the editor writes eight.
- `role for a text must be one of ink, band.0, …` — a text never prints `paper` or `rule`.
- `field "…" is not one of …` — the eleven above are the whole list.
- `give text for a static text or field for a dynamic one, not both`, `text must not be
  empty`, `a text's height follows its type; set width (the wrap width)`.
- `aperture: the Card's window is 95,101 556x166 r83` — the window is the Card's, not the
  design's; the editor can mark it, but no export moves it.
- `… is not on the list of fields`, `static text is empty`, `dynamic text needs a slot name` —
  a design has a text it cannot print.
- `its box covers the whole aperture` — a rect over the entire window hides it.
- `48 layers, and the sheet holds 48 per design` — the caps are the sheet's.
- `… does not carry N px; it carries …` — only the three face-and-size pairs above exist.

The last gate is the packer, which the human runs: it refuses the same facts, and a file in
`assets/k7art/` that is not a design fails the firmware build by name. So check the
JSON you hand over: `v`, `id`, `space`, `prims` at least, and `errors: []` from the export.

## Driving the visible controls

If you cannot reach the tools — a browser agent that only clicks, or a session whose network
blocks the page's routes — every action above has a control. The page is the editor: a rail on
the left, the Layers panel and the print in the middle, the Inspector on the right, and the
documents' tabs across the top.

1. **Start clean.** The **+** at the end of the tab strip opens a new design. Every design
   autosaves in this browser, so the canvas may open on one from last time: start a new one
   rather than editing earlier work.
2. **Set the design.** With nothing selected the Inspector shows it: **Template** (Id, Name),
   **Colourway** (ink, rule, the eight bands), **Paper** (colour and radius — it is the
   LayerMask, and without it there is no background), **Type roles** (the three pairs), and
   **Test values** (what the fields preview). The **Label | J-card** switch at the top changes
   the surface: the J-card's stage draws the folds and the safe square, and its Inspector has
   **Sheet**, **Type** (the three sizes) and **Fields** (title, artist, meta) instead.
3. **Add layers.** The rail: **Rectangle (R)**, **Square (S)**, **Circle (O)**, **Text (T)** —
   pick one, then click the print where it goes; **Select (V)** to stop adding. Fine-tune in
   **Layout**: X, Y, Width (a text's *wrap width*), Height, Radius, Rotation °.
4. **Give a text its job.** **Content** → **Kind**: *Dynamic text (from a field)*, then
   **Field**; or *Static text*, then **Text**. **Test value** changes only the preview.
5. **Give it its look.** **Typography** → **Font role** (title, numeral, caption) and
   **Align**; **Appearance** → **Colour role** (ink or a band).
6. **Order the layers.** The Layers panel lists the front layer first: drag a row, or use
   **Raise** and **Lower** in its header, to put a band behind the type.
7. **Check and export.** **Preview** (the Design/Preview switch in the app bar) draws it with
   the LVGL build the panel runs; **Export** writes the JSON, or says what stops it.

That is the same design the tools build, one click at a time.

## When you are done

Say what you made, for which track's kind of music, and hand over the exported JSON — or tell
the human to press **Export** in the editor. Tell them one label is drawn only at the
two-finger pose: in the editor, the **Preview** pane already draws it with the real LVGL build;
on the player, it appears when the cassette is held up.

- **The design is the file** — `assets/k7art/<id>.json`; the folder is the set, and
  every design in it is packed.
- **The spec is the contract** — `docs/specs/label-templates.md`; the vocabulary of fields and
  faces is `assets/labels/vocabulary.json`, which the packer and the firmware are generated
  from.
- **The tool surface is the page's** — the status bar's **WebMCP** button (Agent tools) shows
  the human what is registered.

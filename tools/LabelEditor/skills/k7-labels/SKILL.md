---
name: k7-labels
description: Design the cassette labels the K7 player prints on its two-finger screen, through the Label Editor's WebMCP tools. Read the open design, lay out paper, bands and type, bind the player's track fields, validate, and export label-template v1. Use when asked to make, change, review or explain a K7 cassette label.
---

# A label for the K7

The K7 is a cassette player. Hold a cassette up to its screen with two fingers and the print
becomes that Track's **label**: a design the player draws in the cassette's own box and fills
with what it knows about the track — title, artist, BPM, key, mood. One design has to read for
every Track in the library, so a label is a layout plus references to the player's fields.

You design it in the **Label Editor**, a page that offers its actions to you as tools. This
file is the working guide; `tools/LabelEditor/README.md` and the repository's
`docs/specs/label-templates.md` are the deeper reference. The editor serves this file at
`/skill.md`, and `label-editor-guide` returns it as a tool result for a session where that
route is blocked.

## What you are designing

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
- Everything is drawn once, at one size. There is no zoom and no responsive layout.

## Before your first call

The tools arrive with the page and their names begin `label-editor-`; `label-editor-read` is
the one to call first. If you cannot see them, the editor page is not open, or the browser has
no WebMCP — ask the human to open http://localhost:5178 in a browser that has the API. The
same calls also live on `window.__labelEditorMcp` for a page script, which is how the repo's
own `scripts/test-label.mjs` drives them.

If the page's routes are blocked, call `label-editor-guide` instead of reading `/skill.md`.
If the tools themselves cannot be reached, every action still has a visible control — see
*Driving the visible controls*, below — so a label is still finishable in this session.

- **Read first, every time.** `label-editor-read` returns the board, the fields, the colour
  roles, the type roles, the open documents, and the active design's layers and export errors.
- **The page is the truth; this file is the guide.** If a number here and the page disagree,
  the page wins: `fields` and `typeRoles` in the read are the live vocabulary.
- **A refused call throws and changes nothing.** The message names the argument and the
  reason; fix it and call again. Only `label-editor-export-template` reports instead of
  throwing (`ok: false` with `errors`).
- Every edit updates the page and is undoable: `label-editor-history` undoes it.

## The vocabulary

**Colour roles.** `paper`, `ink`, `rule`, `band.0` … `band.7`. A shape (rect, square, circle)
may use any of them; a text may print only `ink` or a band. `paper` is the LayerMask's colour.

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

1. **Read** — `label-editor-read`; note the window's strips, and the active design's errors.
2. **A document** — `label-editor-document { action: "new", name: "…" }` for a clean design, or
   work in the active one. A new document is a white screen, a paper mask and no layers.
3. **The colourway** — `label-editor-set-design`: `templateId` (`label.my-name`), `name`,
   `ink`, `rule`, the eight `bands`, `paper`, `paperRadius`.
4. **Layers, back to front.** A layer added now prints over the ones before it;
   `label-editor-move-layer` raises, lowers, or sends one to the top or bottom. Rects make
   bands, rules and blocks.
5. **Texts.** A text with `text` is static (ornaments: `SIDE A`, a numeral); a text with
   `field` prints the player's data. Give one or the other, never both. `fontRole` picks the
   type role, `align` its alignment, `role` its colour. A static text's box is measured from
   its ink at export, so its `width` is replaced rather than respected.
6. **Previews are yours, not the player's.** A dynamic text previews its simulated value;
   `label-editor-set-field-value` changes that preview only, never what the player prints.
7. **Validate** — `label-editor-validate` checks every open design; fix until it is clean.
8. **Export** — `label-editor-export-template` returns the `label-template v1` object and its
   errors. Hand the JSON to the human: it is saved as `assets/labels/templates/<id>.json` and
   packed into the sheet by `make -C host label`.

## The tools

The complete reference — every tool, its sentence and its arguments — is written below from
the page's own registry. When you read this through `label-editor-guide` that block is left
out, because your tool list already carries the same schemas.

<!-- generated: tools:start -->
### `label-editor-read`

Read the label editor: the print's coordinate space, the fields, roles and type faces a design may use, the open documents, the active design's layers, mask, groups, selection and export errors.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `includeTemplate` | `boolean` |  | Also return the active design as a label-template v1 object. |

### `label-editor-guide`

Read this once before designing: what a K7 label is for, how the print and the Card's window shape a layout, the fields and the three type roles, the workflow, what gets refused, and how to do all of it from the visible controls when the tools cannot be reached. The same guide is served at /skill.md.

Takes no arguments.

### `label-editor-add-layer`

Add a layer to the active design. Coordinates are design pixels in the 748x359 print, origin at its top-left; a layer may sit outside the print and still export, where the print crops it. A text is static unless a field is given, and starts in the caption type role; set fontRole for title or numeral.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `type` | `rect`, `square`, `circle`, `text` |  | What to add. Default `rect`. |
| `x` | `number` |  | Left edge in design pixels. Default 40. |
| `y` | `number` |  | Top edge in design pixels. Default 40. |
| `width` | `number` |  | Width in design pixels; for a text this is its wrap width. Minimum 1. |
| `height` | `number` |  | Height in design pixels, for a shape. A text's height follows its type. Minimum 1. |
| `text` | `string` |  | The string a static text prints. Do not combine with field. |
| `field` | `song_title`, `song_artist`, `song_album`, `genre`, `bpm`, `musical_key`, `energy`, `danceability`, `valence`, `arousal`, `mood` |  | The field a dynamic text prints. Do not combine with text. |
| `role` | `string` |  | The colour role. A shape takes paper, ink, band.0, band.1, band.2, band.3, band.4, band.5, band.6, band.7, rule; a text takes ink, band.0, band.1, band.2, band.3, band.4, band.5, band.6, band.7. |
| `fontRole` | `title`, `numeral`, `caption` |  | Which type role a text prints in. |
| `align` | `left`, `center`, `right` |  | A text's horizontal alignment. |
| `layerName` | `string` |  | The layer's name in the Layers panel. |
| `rotation` | `number` |  | Rotation in degrees, clockwise. |

### `label-editor-update-layer`

Change one layer of the active design: position or size in design pixels, rotation, its text or field, colour role, font role, alignment, layer name, test value or lock. Only the fields given change.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `id` | `number` | yes | The layer id from label-editor-read. Minimum 1. |
| `x` | `number` |  | Left edge in design pixels. |
| `y` | `number` |  | Top edge in design pixels. |
| `width` | `number` |  | Width in design pixels; a text's wrap width. Minimum 1. |
| `height` | `number` |  | Height in design pixels, for a shape. Minimum 1. |
| `rotation` | `number` |  | Rotation in degrees, clockwise. |
| `text` | `string` |  | Make the text static and print this string. |
| `field` | `song_title`, `song_artist`, `song_album`, `genre`, `bpm`, `musical_key`, `energy`, `danceability`, `valence`, `arousal`, `mood` |  | Make the text dynamic and print this field. |
| `role` | `string` |  | The colour role to print in. |
| `fontRole` | `title`, `numeral`, `caption` |  | The text's type role. |
| `align` | `left`, `center`, `right` |  | The text's horizontal alignment. |
| `layerName` | `string` |  | The layer's name in the Layers panel. |
| `testValue` | `string` |  | What a dynamic text previews in the editor and Preview. |
| `locked` | `boolean` |  | Lock or unlock the layer. Other changes need it unlocked. |

### `label-editor-delete-layer`

Delete one layer of the active design.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `id` | `number` | yes | The layer id from label-editor-read. Minimum 1. |

### `label-editor-select-layer`

Select one or more layers, so the person at the screen sees the same selection the tools are working on.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `ids` | array of `number` | yes | Layer ids from label-editor-read. The last one is the primary selection. |

### `label-editor-move-layer`

Move a layer through the stack: one step up or down, or straight to the top or bottom. The top of the stack prints last, over the layers below it.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `id` | `number` | yes | The layer id from label-editor-read. Minimum 1. |
| `direction` | `up`, `down`, `top`, `bottom` | yes | Which way to move it. |

### `label-editor-group-layers`

Group layers into one layer group, or ungroup a group. Grouping needs the layers to be contiguous and on one side of the LayerMask, which the tool checks.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `action` | `group`, `ungroup` |  | Group or ungroup. Default group. Default `group`. |
| `ids` | array of `number` |  | The layers to group; omit to use the current selection. |

### `label-editor-set-design`

Set the design's template id, name or background, its colourway (ink, rule, the eight bands, the paper), its paper radius, or one of its type roles' face and size. Every colour is a hex string; a face and size must be a pair the sheet carries.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `templateId` | `string` |  | The template id, like label.my-design; it names the exported file. |
| `name` | `string` |  | The design's human name. |
| `background` | `string` |  | The screen background behind the print, as #rrggbb. |
| `ink` | `string` |  | The colour of the ink role, as #rrggbb. |
| `rule` | `string` |  | The colour of the rule role, as #rrggbb. |
| `bands` | array of `string` |  | The eight band colours, as #rrggbb strings, in band order. |
| `paper` | `string` |  | The paper's colour, as #rrggbb. Adds the LayerMask paper if the design has none. |
| `paperRadius` | `number` |  | The paper's corner radius in design pixels. Minimum 0. |
| `fonts` | object |  | Type roles to change: a face from Archivo Narrow 600, Barlow Condensed 700, IBM Plex Mono 500 and one of that face's sizes. |
| `fonts.title` | object |  | The title role. |
| `fonts.title.face` | `Archivo Narrow 600`, `Barlow Condensed 700`, `IBM Plex Mono 500` |  | The face name. |
| `fonts.title.size` | `number` |  | The pixel size the sheet carries for that face. |
| `fonts.numeral` | object |  | The numeral role. |
| `fonts.numeral.face` | `Archivo Narrow 600`, `Barlow Condensed 700`, `IBM Plex Mono 500` |  | The face name. |
| `fonts.numeral.size` | `number` |  | The pixel size the sheet carries for that face. |
| `fonts.caption` | object |  | The caption role. |
| `fonts.caption.face` | `Archivo Narrow 600`, `Barlow Condensed 700`, `IBM Plex Mono 500` |  | The face name. |
| `fonts.caption.size` | `number` |  | The pixel size the sheet carries for that face. |

### `label-editor-set-field-value`

Set what a field prints in the editor and in Preview — its simulated value, shown until the resolver sends a real one. An empty string restores the field's default.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `field` | `song_title`, `song_artist`, `song_album`, `genre`, `bpm`, `musical_key`, `energy`, `danceability`, `valence`, `arousal`, `mood` | yes | The field id. |
| `value` | `string` | yes | The value to preview; empty restores the default. |

### `label-editor-import-template`

Open a label-template v1 design in a new document. Accepts the template object or its JSON text. A design the packer would refuse is refused here with the reason, and leaves no half-open document.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `template` | `object` | yes | The label-template v1 object (or pass JSON text in this field). |
| `name` | `string` |  | The document's tab name; defaults to the template's name. |

### `label-editor-export-template`

Return the active design (or a named one) as a label-template v1 object, together with the validation errors that would stop it being exported.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `document` | `string` |  | The tab name or template id to export; defaults to the active design. |

### `label-editor-validate`

Check every open design and return the packer's reasons for any that would not export.

Takes no arguments.

### `label-editor-document`

Work with the open documents: create a new one, switch to one, rename the active one, or close one. Closing the last document leaves an empty one open.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `action` | `new`, `switch`, `rename`, `close` | yes | What to do. |
| `name` | `string` |  | A name, for new or rename. |
| `index` | `number` |  | The document's position in the tab strip, for switch or close. Minimum 0. |

### `label-editor-history`

Undo or redo the last edit of the active design.

| Argument | Type | Required | Notes |
|---|---|---|---|
| `action` | `undo`, `redo` | yes | Undo or redo. |
<!-- generated: tools:end -->

## Two worked designs

The first is the smallest label that reads: a top strip of one band, and a numeral above the
window. The colour is set in `set-design` so the cream ink reads on the green band.

```js
await call("label-editor-document", { action: "new", name: "HOLA" });
await call("label-editor-set-design", {
  templateId: "label.hola", name: "HOLA",
  ink: "#f2ecdf", paper: "#c7a867", paperRadius: 12,
});
await call("label-editor-add-layer", {
  type: "rect", x: 0, y: 0, width: 748, height: 101, role: "band.7",
});
await call("label-editor-add-layer", {
  type: "text", text: "HOLA", x: 0, y: 4, width: 748,
  role: "ink", fontRole: "numeral", align: "center",
});
const { template, errors } = await call("label-editor-export-template", {});
```

The second is a song label: a colour block above the window carrying the title, artist and
BPM, and the small print below it.

```js
await call("label-editor-document", { action: "new", name: "Neon Skyline" });
await call("label-editor-set-design", {
  templateId: "label.neon-skyline", name: "Neon Skyline",
  ink: "#f2ecdf", rule: "#8e7e4a",
  bands: ["#c68b2e", "#a24e2c", "#8a5d34", "#6b7a2f",
          "#3b6e8f", "#7a3b52", "#b9ac8e", "#2f4b3a"],
  paper: "#142538", paperRadius: 10,
});
// above the window: the title block
await call("label-editor-add-layer", {
  type: "rect", x: 0, y: 0, width: 748, height: 101, role: "band.4",
});
await call("label-editor-add-layer", {
  type: "text", field: "song_title", x: 24, y: 8, width: 520,
  role: "ink", fontRole: "title",
});
await call("label-editor-add-layer", {
  type: "text", field: "song_artist", x: 24, y: 46, width: 520,
  role: "band.6", fontRole: "title",
});
await call("label-editor-add-layer", {
  type: "text", field: "bpm", x: 580, y: 2, width: 144,
  role: "ink", fontRole: "numeral", align: "right",
});
// below the window: the small print
await call("label-editor-add-layer", {
  type: "rect", x: 24, y: 300, width: 700, height: 2, role: "rule",
});
await call("label-editor-add-layer", {
  type: "text", text: "SIDE A", x: 24, y: 272, width: 120,
  role: "band.6", fontRole: "caption",
});
await call("label-editor-add-layer", {
  type: "text", field: "mood", x: 604, y: 272, width: 120,
  role: "band.6", fontRole: "caption", align: "right",
});
await call("label-editor-add-layer", {
  type: "text", field: "song_album", x: 24, y: 316, width: 420,
  role: "ink", fontRole: "caption",
});
await call("label-editor-add-layer", {
  type: "text", field: "musical_key", x: 660, y: 316, width: 64,
  role: "ink", fontRole: "caption", align: "right",
});
const { template, errors } = await call("label-editor-export-template", {});
```

Both are ten layers or fewer, both fit the bounds, and both leave the window clear.

## What gets refused

A refusal is a sentence naming the design and the layer, so read it rather than guessing:

- `templateId must look like label.my-design` — lowercase letters, digits and dashes after
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
`assets/labels/templates/` that is not a design fails the firmware build by name. So check the
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
   **Test values** (what the fields preview).
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

- **The design is the file** — `assets/labels/templates/<id>.json`; the folder is the set, and
  every design in it is packed.
- **The spec is the contract** — `docs/specs/label-templates.md`; the vocabulary of fields and
  faces is `assets/labels/vocabulary.json`, which the packer and the firmware are generated
  from.
- **The tool surface is the page's** — the status bar's **WebMCP** button (Agent tools) shows
  the human what is registered.

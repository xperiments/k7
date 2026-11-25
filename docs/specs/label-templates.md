# Label templates

The cassette's **label** is the design the two-finger screen prints on the Card's print: the
`label` section of a **K7Art** ([`k7art.md`](./k7art.md)), authored in the **K7Art Editor**
(`tools/LabelEditor/`, served at `/k7art`), packed at build into the factory **Label sheet**,
and read in place by `components/portable/label/`. The same K7Art carries the design's
**J-card**, the paper face the App draws when an album has no picture; the two are one-to-one.
[ADR 0071](../adr/0071-the-two-finger-screen-prints-a-packed-label-sheet.md) records why the
device does not parse the JSON; [ADR 0113](../adr/0113-a-k7arts-colours-are-literal.md) why a
prim paints its own colour and the sheet is version 5; this document is the label's contract
with the editor, the packer and the reader. A user's **K7Art pack** ([`art-packs.md`](./art-packs.md)) adds a second
sheet of this format at runtime; it does not change this one.

The vocabulary — **Label**, **Label sheet**, **Slot**, **K7Art** — is in
[CONTEXT.md](./CONTEXT.md). The Card, the print and the window are
[`cassette-card.md`](./cassette-card.md)'s; the pose is
[ADR 0068](../adr/0068-the-picture-takes-the-panel-behind-two-fingers.md)'s.

## 1. The space

A label is drawn in the **print's box at the entry grid: 748x359**, with the origin at the
print's top-left. On the panel that box is (25,19) 748x359 — the Card at (-8,-23) 814x520 grown
by its print offset (+33,+42) — and the Card's A8 mask cuts the window at **(95,101) 556x166,
radius 83** in these coordinates.

- **A prim may bleed past 748x359, and the print crops it.** A label is drawn the way a sleeve
  is - bands to the page edge and past it, a tick above the top - so position is not validated:
  the label container is the print's box and LVGL clips its children to it, and what the panel
  prints is the label intersected with the print. The editor marks a layer that leaves the print
  with a grey dashed outline, and its row grey in the layer list, so the bleed is visible while
  it is drawn. The only positional refusals are the record's: `x` and `y` are int16, `w` and `h`
  uint16 (ADR 0072).
- **The Card's window is fixed, and the editor always marks it.** It is
  `(95, 101, 556, 166, r 83)`; the label section's `aperture` always names it, and the editor
  writes it for every design, giving one to a file that omits it. The packer refuses anything
  else. It is not stored in the sheet: the Card's mask already cuts the window, in both
  layouts, and a second cut would be a second source of truth. The editor draws it as an
  opaque black layer on top of the print, in Design and in Preview; that black is the editor's
  guide, not a prim.
- **Rotation is honoured**: `rot10` is tenths of a degree about the object's own centre
  (`transform_pivot` = width/2, height/2). A rotated object costs a layer per paint.
- **The cassette's background is the label's own, at the two-finger pose.** A label may carry a
  **canvas background** (§2): while the pose shows the tape, the Card's plastic, its mat and
  front plate, the window's rim and the 800x480 screen behind them are that colour. It is
  outside the print and the print never crops it; it is not the label's paper, which is the
  mask's rect inside the print. A label without one leaves the Track's cassette row alone. The
  J-card has no canvas: it is paper, and its sheet is the section's `background`
  ([`k7art.md`](./k7art.md) §4).

## 2. The label section — what a K7Art's `label` holds

What the editor writes inside a K7Art's `label`, and what the packer reads. The container's
`id` and `name` are [`k7art.md`](./k7art.md) §1's; the fields below are the section's, and the
ones the packer uses. The rest are the editor's.

```json
{
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
}
```

- **`mask` is the design's paper, and the editor always writes one.** It is the print's box
  (748x359) in a `#RRGGBB` colour, clipped to a corner radius `r`. It is not among the `prims`;
  both sheet writers put it in front of them as a rect
  (`x 0, y 0, w 748, h 359, r r, fill color`), so the print has a background the reader paints
  like any other prim. The packer still accepts a hand-written file without one; the editor
  gives such a file the default paper when it opens it and writes the mask back on Export. The
  mask's colour is its own; it is not the J-card's `background`
  ([`k7art.md`](./k7art.md) §2).
- **`canvas` is the cassette's background at the two-finger pose, chosen by the cassette row.**
  `backgrounds` is one to eight `#RRGGBB` colours — `assets/labels/vocabulary.json`'s `canvas` is
  the cap — and the sheet carries the whole list. The player uses
  `backgrounds[cassette_row % count]`, where `cassette_row` is the Track's palette row: a design
  with seven colours pins one per cassette, and a shorter list cycles. `active` is the editor's
  preview preset and is not the player's choice. The field is optional: a design without it
  leaves the Track's cassette row alone. A design that declares `backgrounds` must declare
  `active`, and `active` must index one of them. The editor writes the field for every design it
  exports, and a new design starts on the panel's page (`#909090`), so an untouched export is the
  page it replaces.
- **A prim paints its own colour.** A `rect`'s `fill` and a `text`'s `color` are `#RRGGBB`, both
  cases exactly seven characters. Any colour may go on any element: the type that sits on a band
  may print in the band's own colour, or in white, or in a colour nothing else uses. There is no
  role, no declared band list and no `notes`: the label's colours are the design's own.
- **The label's `fonts` are the sheet's.** `ff` and `px` must be a pair
  [`assets/labels/vocabulary.json`](../../assets/labels/vocabulary.json) carries: the sheet
  addresses a font by its index in that table, and the packer refuses a pair it does not. A
  label's faces are the three the editor embeds — `Archivo Narrow 600` at 32, `Barlow Condensed
  700` at 78 and `IBM Plex Mono 500` at 18 — and it offers only those, because a face it cannot
  draw is a face it cannot show. A type role is a face *and* a size, not a face with a size typed
  beside it. The J-card uses the same three faces at its own sizes
  ([`k7art.md`](./k7art.md) §4); those sizes are the J-card's units, not the sheet's pairs.
- **`prims` are painted in order**, first at the back, after the paper's rect. `still: true`
  means nothing animates it.
- **A `rect`** has `x, y, w, h, r?, rot?, fill`, and `fill` is a `#RRGGBB` colour.
- **A `text`** has `x, y, w, rot?`, `font` (`title`/`numeral`/`caption`), `color`, `align`
  (`left`/`center`/`right`), and either
  `slot` (a Slot name, dynamic) or `text` (static, at the back of the same array). Its `w` is a
  box: for a slot it must hold the longest value the slot can print, and for a static string the
  editor measures the ink and writes that, moving `x` by the difference so the glyphs do not
  move. The box is the design's, not a bound: the print crops whatever leaves it (ADR 0072), and
  a box that hugs its text is what keeps the canvas honest about where the ink is.
- **`preview`** is the editor's simulated value, and only that: the packer ignores it and the
  editor does not bound it. A longer real value is wrapped to the text's `w` and cropped by the
  print like anything else (ADR 0072).
- **A slot's `max` is the resolver's ceiling, not a design choice.** `label_values` carries 96
  characters per slot, so the editor writes `max: 96` for every slot a text uses and offers no
  control for it; a design imported with a smaller `max` is raised to the ceiling, because the
  box is where the layout is decided. The field stays in the file because the slot record carries
  it and the panel cuts a value there. The J-card's texts bind to the same Slots or to the
  three **J-card fields**, and carry no `max` ([`k7art.md`](./k7art.md) §4).

## 3. The sheet — `label_sheet.bin`

One file for every K7Art's label section in `assets/k7art/`, little-endian throughout, no
padding, no structure aliasing: the reader takes a pointer and a length and never copies. The
folder is the set: **every `*.json` in it is a K7Art**, packed by its `label` section in id
order, and a file that is not a K7Art fails the build naming itself - a design left out silently
would print the wrong label with no line saying why. A user's pack writes a second sheet of the
same format for the device's `art` partition ([`art-packs.md`](./art-packs.md) §1); the two
sheets are one pool, the factory first (§6).

```text
header        32 B
templates     template_count x 16 B, ascending by id
canvases      template_count x 8 x 4 B, flat; a template's eight colours start at template x 32
prims         prim_count x 24 B, flat; a template's slice starts at its prim_start
slots         slot_count x 8 B, flat; a template's slice starts at its slot_start
string pool   pool_size B, UTF-8, NUL-terminated; every offset is relative to its start
```

`total_size == 32 + 16 * template_count + 32 * template_count + 24 * prim_count + 8 * slot_count
+ pool_size`, checked rather than assumed.

### 3.1 Header (32 bytes)

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 8 | `magic` | `K7LBL`, NUL-padded to 8 |
| 8 | 2 | `version` | `5` |
| 10 | 2 | `header_size` | `32` |
| 12 | 4 | `total_size` | the file's whole length |
| 16 | 4 | `crc32` | CRC-32/IEEE over `[32, total_size)` |
| 20 | 2 | `space_w` | `748` |
| 22 | 2 | `space_h` | `359` |
| 24 | 2 | `template_count` | `1..32` |
| 26 | 2 | `prim_count` | total, `<= 32 * 48` |
| 28 | 2 | `slot_count` | total, `<= 32 * 16` |
| 30 | 2 | `pool_size` | bytes |

### 3.2 Template (16 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | `name_offset` | the design's `name` in the pool, for the log |
| 4 | 2 | `prim_start` | index into the prim array |
| 6 | 2 | `prim_count` | |
| 8 | 2 | `slot_start` | index into the slot array |
| 10 | 2 | `slot_count` | |
| 12 | 1 | `font[0]` | the `title` role's index into the font table |
| 13 | 1 | `font[1]` | the `numeral` role's |
| 14 | 1 | `font[2]` | the `caption` role's |
| 15 | 1 | `canvas_count` | `0..8`; the entries in the template's canvas row |

Version 5 is the literal colours: a template carries no inks, so the record is 16 bytes where
version 4's was 64. The eight-colour canvas row still follows the template array, and the count
is still the canvas list's: 0 for a design without a canvas, the row's entries past the count
0. The player picks `canvas[row % count]` by the Track's cassette row and paints the cassette's
body and the screen with it while the two-finger pose is up (§6).

### 3.3 Prim (24 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `kind` | `0` rect, `1` static text, `2` slot text |
| 1 | 1 | `reserved` | `0` |
| 2 | 1 | `font` | `0` title, `1` numeral, `2` caption; `0` for a rect |
| 3 | 1 | `align` | `0` left, `1` center, `2` right; `0` for a rect |
| 4 | 2 | `x` | signed |
| 6 | 2 | `y` | signed |
| 8 | 2 | `w` | |
| 10 | 2 | `h` | rect only; `0` for a text |
| 12 | 2 | `r` | rect only; `0` for a text |
| 14 | 2 | `rot10` | tenths of a degree, `0..3599` |
| 16 | 4 | `text_offset` | static text or Slot name; `0xFFFFFFFF` for a rect |
| 20 | 4 | `colour` | RGB888 in the low 24 bits, always painted |

A prim paints its own colour: a rect's fill, a text's colour. The paper's rect, which the
writer puts in front of the design's prims, is a rect like any other. There is no fill index
and no one-literal exception: a colour the design uses is a colour a prim carries.

### 3.4 Slot (8 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | `name_offset` | the Slot's name in the pool |
| 4 | 1 | `max` | the longest value the design allows, in characters, `1..96` |
| 5 | 3 | `reserved` | `0` |

Slot names are **strings, not an enum**: a name the reader does not know prints nothing, so a
slot can be added to the vocabulary without a second version of the format. The packer refuses
an unknown name anyway — the reader's tolerance exists for a sheet and a firmware that were
built apart, which is a bug, not a state.

### 3.5 Validation

The reader refuses the whole sheet — and the screen falls back to the flat palette fill — when:

`magic` is not `K7LBL`, `version` is not 5, `header_size` is not 32, `total_size` disagrees with
the counts, the CRC disagrees, `space_*` is not 748x359, a count is past its cap, a template's
`prim_start + prim_count` or `slot_start + slot_count` runs past its array, a prim's
`text_offset` is past `pool_size`, a string is not NUL-terminated inside the pool, a font index
is past the font table, a template's `canvas_count` is above eight, a prim's `reserved` byte is
not 0, a prim's `colour` is above `0xFFFFFF`, a canvas before the count is above `0xFFFFFF`, or
a canvas past the count is nonzero.

The factory sheet is a build product; a user pack's sheet that fails any of these is ignored,
and the factory sheet alone is used ([`art-packs.md`](./art-packs.md) §5).

## 4. The vocabulary — `assets/labels/vocabulary.json`

What a design may say, and the bounds the sheet and the panel hold it to, is one committed
file. The editor and the firmware do not each keep a copy of it:

```json
{ "v": 2,
  "space": { "w": 748, "h": 359 },
  "aperture": { "x": 95, "y": 101, "w": 556, "h": 166, "r": 83 },
  "value": 96, "canvas": 8,
  "limits": { "prims": 48, "slots": 16, "templates": 32 },
  "coords": { "x": [-32768, 32767], "y": [-32768, 32767],
              "w": [1, 65535], "h": [1, 65535] },
  "slots": [ { "id": "song_title", "label": "Song Title", "sim": "Neon Skyline" }, … ],
  "fonts": [ { "name": "Jura", "px": 20, "symbol": "jura_20" }, … ],
  "defaults": { "fonts": { "title": { "ff": "Archivo Narrow 600", "px": 32 }, … } } }
```

- **`slots`** is the vocabulary of §5, in the resolver's order: `id` is what a design names,
  `label` is what the editor's field list shows, and `sim` is the value the editor previews
  with, which is what the device would print for that Slot. The J-card's three fields are the
  App's, not the Snapshot's, and are not in this file ([`k7art.md`](./k7art.md) §4).
- **`fonts`** is the sheet's font table. The first three rows are what the screen already
  compiles; the last three are the editor's own faces. `"editor": true` marks the faces the
  editor offers — the ones it serves and can draw. A face it cannot draw is a face it cannot
  show, so a type picker never offers one. The J-card uses the same three faces at its own
  sizes; those sizes are not a table entry.
- **`space`, `aperture`, `value`, `canvas`, `limits` and `coords`** are the numbers the packer
  used to hold alone. `coords` is what a prim's record can say — `x` and `y` int16, `w` and `h`
  uint16 — which is a refusal, not a clamp (ADR 0072). `canvas` is the most colours a design may
  declare: eight. It bounds the design file and the sheet's per-template row; the player's
  cassette table has seven rows, so seven pins one per row and fewer cycles. Version 2 drops
  `bands`: with a colour on every prim there is no declared palette to count.
- **`defaults.fonts`** is the face roles a new label starts with. The J-card's default sizes are
  the editor's ([`k7art.md`](./k7art.md) §4).

Two copies are generated from it, and both are committed:

- **`components/portable/label/include/label_vocab.h`** carries the space, the limits, the
  canvas cap and the Slot list. The list is one macro, expanded twice: the enum the resolver
  indexes and the names the sheet carries cannot disagree, because they are the same list.
- **`tools/LabelEditor/src/engine/vocab.generated.ts`**, the editor's module, between the same
  generated markers. The editor imports it; there is no second copy of the table in its source.

`tools/label_pack.py --generate-vocabulary` writes both; `--check-vocabulary` refuses them when
they are stale, and `make -C host label` runs it. The check also compares each face the editor
offers — the `fonts` rows with `"editor": true` — with its file in
`tools/LabelEditor/public/fonts/` and with `assets/fonts/`: an editor that draws a face the
build cannot compile would show the designer glyphs the panel does not have. `make -C host
label` also builds the editor and boots the built page in headless Chrome, so one that throws at
load is a host failure.

- **The packer's font index is the table's index**, and it also writes `label_fonts.h` with
  `#define LABEL_FONT_<SLUG> <index>` and `LABEL_FONT_COUNT`, so the app's `k_label_font[]` is
  checked by the compiler rather than by hope.
- **A face or size not in the table is refused**, naming the design and the type role: a label
  cannot quietly wear a different face on the panel than in the editor. The J-card's sizes are
  its own; its faces are the three, and a fourth is refused with the design named.
- Adding a face is adding a converted font (a committed `font_<face>_<px>.c`, like
  `font_jura_36.c`) and a `fonts` row. The 4 MB app partition carries 1.81 MB today.

## 5. Slots: what the panel puts in them

The label's eleven fields. A J-card's texts bind to these Slots too, and may also name its own
three **J-card fields** - `title`, `artist` and `meta` - which the App fills from an album or a
Track; those three are not Slots and are not in this vocabulary
([`k7art.md`](./k7art.md) §4).

Every value comes from the **Snapshot row** for the playing Track — no tags, no SD read on the
show path, no second source. The row is found once per Track change, in RAM:

```text
status.path -> library_find() -> library_size() -> snapshot_path_hash() -> snapshot_find()
```

The fingerprint is over the path's **bytes**, and macOS hands a card's names out decomposed
(NFD) while FATFS reads the same card composed (NFC), so an image built on a Mac is keyed on
bytes the device never sees. The join therefore tries the path as the card gives it and, only if
that misses, once more with the Latin-1 accented letters decomposed; the size must agree either
way, so it is the same file. It is a tolerance, not a normalisation: the analyser should hash
the composed form, and an image that does makes the second try unnecessary.

A Track with no row (an unanalysed card, a file replaced under the same name, or a fingerprint
that disagrees) leaves every slot empty: the design prints its shapes and its paper with blank
text, and the screen says which half of the join missed.

| Slot | Value | Unknown |
|---|---|---|
| `song_title` | the row's title | `""` |
| `song_artist` | artist | `""` |
| `song_album` | album | `""` |
| `genre` | genre | `""` |
| `bpm` | `bpm_x10 / 10`, rounded — `124` | `""` |
| `musical_key` | the Camelot code the MCP layer prints: `1..12` + `A`, `13..24` + `B` — `8A` | `""` |
| `energy` | `0..100` | `""` when `0xFF` |
| `danceability` | `0..100` | `""` when `0xFF` |
| `valence` | `0..100` | `""` when `0xFF` |
| `arousal` | `0..100` | `""` when `0xFF` |
| `mood` | `snapshot_mood_name()` of the lowest set bit, in the image's order: happy, sad, aggressive, relaxed, party | `""` when no bit is set |

The names are the vocabulary's `slots` (§4), generated into the editor's field list and the
firmware's resolver: `song_title`, `song_artist`, `song_album`, `genre`, `bpm`, `musical_key`,
`energy`, `danceability`, `valence`, `arousal`, `mood`. A label's `preview` in the editor is
what the device would print for that slot, which is why the editor's simulations are `8A` and
`relaxed` rather than `A min` and `Warm`.

- **A value is cut at its slot's `max` characters**, on a character boundary — never mid-UTF-8 —
  (96 in every design the editor writes: the ceiling is the resolver's, not the design's)
  and only then wrapped: the label's `w` is the wrap width, so a longer title grows downward
  inside the design's own box, exactly as the editor previews it.
- `year` and `song_album_artist` are **not** in this vocabulary. The editor's field list is the
  table above; extending it is a Snapshot change first.

## 6. Rendering

At the **two-finger pose only** (`controls.full`, the entry grid). The label's objects are
children of the print — the 748x359 box the Cover uses — so they are cut by the Card's mask and
by the print's own box exactly as a picture is.

- **A rect** is a box with every style removed: `bg_color` = `lv_color_hex(p.colour)`, `bg_opa`
  cover, `radius` = `r`, no border, no padding, not clickable, not scrollable, and, when
  `rot10 != 0`, `transform_rotation` about the box's centre.
- **A text** is an `lv_label` with its slot's value (or the static text), `width` = `w`,
  `LV_LABEL_LONG_WRAP`, `text_align` = `align`, `text_color` = `lv_color_hex(p.colour)`, and
  `text_font` = `k_label_font[]` of its type role.
- **The print's background stays the palette's label fill**; a design's paper is the mask's
  rect, a prim like any other, and the editor writes one for every design.
- **The cassette's body is the label's canvas, and only at the two-finger pose.** When the
  design carries canvases, the Card's plastic, its mat and front plate, the window's rim and the
  800x480 screen behind them wear `canvas[cassette_row % count]` while the pose is up. The pose's
  exit restores the Track's cassette row (the plastic, the rim and the print's fallback); a Track
  change while the pose is up picks the next entry by the new row and repaints them. A design
  with no canvas leaves the row exactly as it is. The canvas is outside the print: the print's
  background is still the design's paper.
- **The label's template is picked from the pool.** The index is
  `ink_seed % (factory_count + user_count)`, the factory sheet first
  ([`art-packs.md`](./art-packs.md) §5). The app's label code maps it to a sheet and a template;
  the design is built once per template and re-texted per Track. A Track change sets the texts;
  a change of selected template (a new index) or a change of sheet deletes and rebuilds the
  objects, and repaints the page if the new design's canvas differs. Both happen under the LVGL
  lock, in one frame.
- **Nothing is drawn while the two-finger pose is not up.** The objects are hidden with the pose
  and the Cover's picture takes the print back at the Controls pose.
- **Nothing repaints the design while the pose is up.** The reel tick is parked there
  ([`cassette-card.md`](./cassette-card.md), "The reel tick"), so between the pose opening and a
  Track change the label is drawn exactly once - not because LVGL caches it, but because nothing
  invalidates the area it sits in.
- **The J-card is not on the device.** It is the App's cover ([`k7art.md`](./k7art.md) §5), not a
  font, an object or a byte of this sheet.

## 7. What the packer refuses

Named per design and per layer; the build fails, nothing is dropped.

The packer reads each K7Art's `label` section and refuses what the sheet cannot hold; the editor
refuses the same list at Export and at Open, and the J-card's own
([`k7art.md`](./k7art.md) §7); a user pack's sheet has the device's checks
([`art-packs.md`](./art-packs.md) §5). The two validators answer to different readers at
different moments — that is why there are two — but the facts they refuse by are one table.

| Refusal | Why |
|---|---|
| `format` is not `k7art`, or `v` is not 2 | a different file, and v1 is the editor's to migrate |
| the id is not `k7.<slug>`, or the same id twice in a sheet | the folder is the set |
| no `label` or no `jcard` | the design is the pair |
| `space` is not 748x359 | the print is one box |
| the aperture is neither absent nor `(95,101,556,166,r83)` | the window is the Card's |
| an aperture without a `mask` | there is no paper to cut the window out of |
| a `mask` whose colour is not `#RRGGBB`, or whose radius is not a non-negative integer | the paper has no paint, or no such corner |
| `canvas` is present but `backgrounds` is not one to eight `#RRGGBB` colours | there is no screen colour to paint |
| `canvas.active` is missing or outside `backgrounds` | which preset would the player paint? |
| a coordinate outside int16, or a size outside uint16 | the record cannot hold it |
| a prim with no `fill`/`color`, or one that is not `#RRGGBB` | what would it paint? |
| an unknown Slot name, a slot a text uses but does not declare, or a declared slot no text uses | the vocabulary is one table |
| a font role not in `vocabulary.json` | the glyphs are not compiled |
| a design with no prims, or a text with neither a slot nor a static text | there is nothing to draw |
| a slot whose `max` is 0 or over 96 | the resolver carries 96; the editor writes that |
| more than 48 prims (the paper's rect included), 16 slots or 32 designs per sheet | a bounded build, a bounded repaint, a bounded sheet |
| the same `id` twice | selection would be ambiguous |
| no designs at all in `assets/k7art/` | nothing to embed |
| a file in `assets/k7art/` that is not a K7Art | it would be a design nobody wrote |
| a `label-template v1` file (the retired format) | a different file |
| a J-card that fails [`k7art.md`](./k7art.md) §7 | the design is the pair |
| a user pack's sheet that fails its checks | the device ignores it ([`art-packs.md`](./art-packs.md) §5) |

## 8. Costs

| | |
|---|---|
| PSRAM | **0 B** — the factory sheet and a user pack's sheet are read in place from flash, like the Snapshot |
| flash | the factory sheet (a few KB for a handful of designs) **plus the fonts**, which are the real cost: a 4 bpp Latin face at 36 px is tens of KB, and `font_jura_36.c` is 230 KB of source. A user pack reserves up to 1 MB of the `art` partition ([`art-packs.md`](./art-packs.md) §5) |
| objects | at most `LABEL_PRIM_MAX` (48) per label design, alive only while the two-finger pose is up. The J-card is the App's DOM, not the firmware's |
| canvas | 32 B per design (its eight-colour row); no object; the Card's plastic/mat and the window's rim are repainted on pose entry and on a Track change that changes the colour, the 800x480 page behind them with them |
| per frame | **0** at the Controls pose. At the two-finger pose the reel tick redraws the print's area at 8 Hz (`CASSETTE_POSE_TICK_MS`), so the label is repainted with it - measured 11 ms a pass, and the label is not what that cost is made of |
| per Track change | the print's 748x359 = 268,280 px repainted, once, at the two-finger pose; the pose's own entry is one 75 ms frame |

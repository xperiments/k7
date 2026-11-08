# Label templates

The cassette's **Label template** is the design the two-finger screen prints on the Card's
print. It is authored in the LVGL canvas builder (`resources/LVGL canvas builder.html`), stored
as `label-template v1` JSON in `assets/labels/templates/`, packed at build into one **Label
sheet**, and read in place by `components/portable/label/`. [ADR 0071](../adr/0071-the-two-finger-screen-prints-a-packed-label-sheet.md)
records why the device does not parse the JSON; this document is the contract the editor, the
packer and the reader all speak.

The vocabulary — **Label template**, **Label sheet**, **Slot** — is in
[CONTEXT.md](./CONTEXT.md). The Card, the print and the window are
[`cassette-card.md`](./cassette-card.md)'s; the pose is
[ADR 0068](../adr/0068-the-picture-takes-the-panel-behind-two-fingers.md)'s.

## 1. The space

A design is drawn in the **print's box at the entry grid: 748x359**, with the origin at the
print's top-left. On the panel that box is (25,19) 748x359 — the Card at (-8,-23) 814x520 grown
by its print offset (+33,+42) — and the Card's A8 mask cuts the window at **(95,101) 556x166,
radius 83** in these coordinates.

- **Every prim is inside 748x359.** The packer refuses one that is not: the print clips its
  children, and a design that promises more than the panel does is a lie the packer will not
  tell.
- **The aperture is the Card's window, not the design's.** The editor's `aperture` must be
  absent, or exactly `(95, 101, 556, 166, r 83)`; the packer refuses anything else, and so does
  the editor at Export and at Open - the window is the Card's, and an aperture object is only a
  placement aid. It is not stored in the sheet: the Card's mask already cuts the window, in both
  layouts, and a second cut would be a second source of truth.
- **Rotation is honoured**: `rot10` is tenths of a degree about the object's own centre
  (`transform_pivot` = width/2, height/2). A rotated object costs a layer per paint.

## 2. The design file — `label-template v1`

What the editor's Export writes, and what `tplImport` reads back. The fields below are the ones
the packer uses; the rest are the editor's.

```json
{
  "v": 1,
  "id": "label.night-drive",
  "name": "Night Drive",
  "space": { "w": 748, "h": 359 },
  "aperture": { "x": 95, "y": 101, "w": 556, "h": 166, "r": 83 },
  "inks": { "paper": "#F5F1E6", "ink": "#242424", "rule": "#8E7E4A",
            "band": ["#C68B2E", "#A24E2C", "#8A5D34", "#6B7A2F"] },
  "fonts": { "title":   { "ff": "Archivo Narrow 600", "px": 24 },
             "numeral": { "ff": "Barlow Condensed 700", "px": 78 },
             "caption": { "ff": "IBM Plex Mono 500", "px": 18 } },
  "slots": { "song_title": { "max": 24 } },
  "notes": ["the spike is the one literal colour: it is the sleeve's, not the sheet's"],
  "prims": [
    { "t": "rect", "x": 0, "y": 0, "w": 748, "h": 359, "r": 19,
      "fill": "paper", "still": true },
    { "t": "text", "slot": "song_title", "x": 40, "y": 300, "w": 400,
      "font": "title", "color": "ink", "align": "left", "preview": "Neon Skyline" }
  ]
}
```

- **`inks` and `fonts` are the design's, not the firmware's.** The editor used to keep them out
  of the export; without them a design is shapes with no colourway, so both are part of the
  file. The seven colours are exactly the roles — `paper`, `ink`, `rule` and `band[0..3]` —
  which is why a prim names a role and not a colour. `paper` is the LayerMask's colour and is
  `null` when the design has no mask; a prim that fills with `paper` then has no colour, and the
  packer refuses it.
- **A font role is one of the sheet's pairs.** `ff` and `px` must be a pair
  [`assets/labels/fonts.json`](../../assets/labels/fonts.json) carries: the sheet addresses a
  font by its index in that table, and the packer refuses a pair it does not. A label's faces are
  the three the editor embeds — `Archivo Narrow 600` at 24, `Barlow Condensed 700` at 78 and
  `IBM Plex Mono 500` at 18 — and it offers only those, because a face it cannot draw is a face it
  cannot show. A role is a face *and* a size, not a face with a size typed beside it.
- **`prims` are painted in order**, first at the back. `still: true` means nothing animates it.
- **A `rect`** has `x, y, w, h, r?, rot?, fill` and, for the one literal colour a design may
  use, `fill` is a `#RRGGBB` instead of a role, and `notes` must say why.
- **A `text`** has `x, y, w, rot?`, `font` (`title`/`numeral`/`caption`), `color` (`ink` in
  v1 — the editor refuses any other), `align` (`left`/`center`/`right`), and either
  `slot` (a Slot name, dynamic) or `text` (static, at the back of the same array).
- **`preview`** is the editor's simulated value: it is validated against the slot's `max` at
  export, and ignored by the packer.

## 3. The sheet — `label_sheet.bin`

One file for every design in `assets/labels/templates/`, little-endian throughout, no padding,
no structure aliasing: the reader takes a pointer and a length and never copies. The folder is
the set: **every `*.json` in it is packed**, in id order, and a file that is not a design fails
the build naming itself - a design left out silently would print the wrong label with no line
saying why.

```text
header        32 B
templates     template_count x 48 B, ascending by id
prims         prim_count x 24 B, flat; a template's slice starts at its prim_start
slots         slot_count x 8 B, flat; a template's slice starts at its slot_start
string pool   pool_size B, UTF-8, NUL-terminated; every offset is relative to its start
```

`total_size == 32 + 48 * template_count + 24 * prim_count + 8 * slot_count + pool_size`, checked
rather than assumed.

### 3.1 Header (32 bytes)

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 8 | `magic` | `K7LBL`, NUL-padded to 8 |
| 8 | 2 | `version` | `1` |
| 10 | 2 | `header_size` | `32` |
| 12 | 4 | `total_size` | the file's whole length |
| 16 | 4 | `crc32` | CRC-32/IEEE over `[32, total_size)` |
| 20 | 2 | `space_w` | `748` |
| 22 | 2 | `space_h` | `359` |
| 24 | 2 | `template_count` | `1..32` |
| 26 | 2 | `prim_count` | total, `<= 32 * 48` |
| 28 | 2 | `slot_count` | total, `<= 32 * 16` |
| 30 | 2 | `pool_size` | bytes |

### 3.2 Template (48 bytes)

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
| 15 | 1 | `reserved` | `0` |
| 16 | 4 | `paper` | RGB888 in the low 24 bits |
| 20 | 4 | `ink` | |
| 24 | 4 | `rule` | |
| 28 | 4 | `band[0]` | |
| 32 | 4 | `band[1]` | |
| 36 | 4 | `band[2]` | |
| 40 | 4 | `band[3]` | |
| 44 | 4 | `reserved` | `0` |

### 3.3 Prim (24 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `kind` | `0` rect, `1` static text, `2` slot text |
| 1 | 1 | `fill` | `0` paper, `1` ink, `2` rule, `3..6` band 0..3, `7` literal |
| 2 | 1 | `font` | `0` title, `1` numeral, `2` caption; `0` for a rect |
| 3 | 1 | `align` | `0` left, `1` center, `2` right; `0` for a rect |
| 4 | 2 | `x` | signed |
| 6 | 2 | `y` | signed |
| 8 | 2 | `w` | |
| 10 | 2 | `h` | rect only; `0` for a text |
| 12 | 2 | `r` | rect only; `0` for a text |
| 14 | 2 | `rot10` | tenths of a degree, `0..3599` |
| 16 | 4 | `text_offset` | static text or Slot name; `0xFFFFFFFF` for a rect |
| 20 | 4 | `literal_colour` | RGB888; only read when `fill == 7` |

`fill == 1` (ink) is the only colour a text may carry in v1.

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

`magic` is not `K7LBL`, `version` is not 1, `header_size` is not 32, `total_size` disagrees with
the counts, the CRC disagrees, `space_*` is not 748x359, a count is past its cap, a template's
`prim_start + prim_count` or `slot_start + slot_count` runs past its array, a prim's
`text_offset` is past `pool_size`, a string is not NUL-terminated inside the pool, or a font index
is past the font table.

## 4. The font table — `assets/labels/fonts.json`

Type is compiled, so the designs' faces are chosen from a table rather than named freely:

```json
{ "fonts": [ { "name": "Jura", "px": 20, "symbol": "jura_20" },
             { "name": "Jura", "px": 28, "symbol": "jura_28" },
             { "name": "Jura", "px": 36, "symbol": "jura_36" },
             { "name": "Archivo Narrow 600", "px": 24, "symbol": "archivo_narrow_600_24" },
             { "name": "Barlow Condensed 700", "px": 78, "symbol": "barlow_condensed_700_78" },
             { "name": "IBM Plex Mono 500", "px": 18, "symbol": "ibm_plex_mono_500_18" } ] }
```

The first three are what the screen already compiles; the last three are the editor's own faces,
converted by the ticket that adds them. The table is the contract the packer checks against, not
a claim about what the current image links.

- **The packer's font index is the table's index**, and it also writes `label_fonts.h` with
  `#define LABEL_FONT_<SLUG> <index>` and `LABEL_FONT_COUNT`, so the app's
  `k_label_font[]` is checked by the compiler rather than by hope.
- **A face or size not in the table is refused**, naming the design and the role: a design
  cannot quietly wear a different face on the panel than in the editor.
- Adding a face is adding a converted font (a committed `font_<face>_<px>.c`, like
  `font_jura_36.c`) and a table row. The 4 MB app partition carries 1.81 MB today.

## 5. Slots: what the panel puts in them

Every value comes from the **Snapshot row** for the playing Track — no tags, no SD read on the
show path, no second source. The row is found once per Track change, in RAM:

```text
status.path -> library_find() -> library_size() -> snapshot_path_hash() -> snapshot_find()
```

A Track with no row (an unanalysed card, a file replaced under the same name) leaves every slot
empty: the design prints its paper, bands and rules with blank text.

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

The names are the editor's `SLOTS`, one table in both places: `song_title`, `song_artist`,
`song_album`, `genre`, `bpm`, `musical_key`, `energy`, `danceability`, `valence`, `arousal`,
`mood`. A design's `preview` in the editor is what the device would print for that slot, which is
why the editor's simulations are `8A` and `relaxed` rather than `A min` and `Warm`.

- **A value is cut at its slot's `max` characters**, on a character boundary — never mid-UTF-8 —
  and only then wrapped: the label's `w` is the wrap width, so a longer title grows downward
  inside the design's own box, exactly as the editor previews it.
- `year` and `song_album_artist` are **not** in this vocabulary. The editor's field list is the
  table above; extending it is a Snapshot change first.

## 6. Rendering

At the **two-finger pose only** (`controls.full`, the entry grid). The design's objects are
children of the print — the 748x359 box the Cover uses — so they are cut by the Card's mask and
by the print's own box exactly as a picture is.

- **A rect** is a box with every style removed: `bg_color` = its role's colour, `bg_opa` cover,
  `radius` = `r`, no border, no padding, not clickable, not scrollable, and, when `rot10 != 0`,
  `transform_rotation` about the box's centre.
- **A text** is an `lv_label` with its slot's value (or the static text), `width` = `w`,
  `LV_LABEL_LONG_WRAP`, `text_align` = `align`, `text_color` = ink, and `text_font` =
  `k_label_font[]` of its role.
- **The print's background stays the palette's label fill**; a design's paper is a prim like any
  other, so a design without one still reads as a cassette label.
- **The design is built once per template and re-texted per Track.** A Track change sets the
  texts; a change of selected template (a different `ink_seed % template_count`) deletes and
  rebuilds the objects. Both happen under the LVGL lock, in one frame.
- **Nothing is drawn while the two-finger pose is not up.** The objects are hidden with the pose
  and the Cover's picture takes the print back at the Controls pose.
- **Nothing repaints the design while the pose is up.** The reel tick is parked there
  ([`cassette-card.md`](./cassette-card.md), "The reel tick"), so between the pose opening and a
  Track change the label is drawn exactly once - not because LVGL caches it, but because nothing
  invalidates the area it sits in.

## 7. What the packer refuses

Named per design and per layer; the build fails, nothing is dropped.

| Refusal | Why |
|---|---|
| `v` is not 1 | a different format |
| `space` is not 748x359 | the print is one box |
| the aperture is neither absent nor `(95,101,556,166,r83)` | the window is the Card's |
| a prim outside 748x359 (bleed or not) | the print clips |
| a prim with no role and no literal colour, or an unknown role | what would it paint? |
| a `paper` prim when `inks.paper` is `null` | there is no paper without a mask |
| more than one literal colour, or one with no `note` | the sheet is role-coloured |
| a text whose `color` is not `ink` | v1's one text colour |
| an unknown Slot name, a slot a text uses but does not declare, or a declared slot no text uses | the vocabulary is one table |
| a `max` the `preview` already exceeds | the design does not fit its own bound |
| a font role not in `fonts.json` | the glyphs are not compiled |
| a design with no prims, or a text with neither a slot nor a static text | there is nothing to draw |
| a slot whose `max` is 0 or over 96 | the resolver carries 96; the design may not ask for more |
| more than 48 prims, 16 slots or 32 designs | a bounded build, a bounded repaint, a bounded sheet |
| the same `id` twice | selection would be ambiguous |
| no designs at all in `assets/labels/templates/` | nothing to embed |
| a file in `templates/` that is not a design (the Export's `labels.json`, say) | it would be a design nobody wrote |

## 8. Costs

| | |
|---|---|
| PSRAM | **0 B** — the sheet is read in place from flash, like the Snapshot |
| flash | the sheet (a few KB for a handful of designs) **plus the fonts**, which are the real cost: a 4 bpp Latin face at 36 px is tens of KB, and `font_jura_36.c` is 230 KB of source |
| objects | at most `LABEL_PRIM_MAX` (48) per design, alive only while the two-finger pose is up |
| per frame | **0** at the Controls pose. At the two-finger pose the reel tick redraws the print's area at 8 Hz (`CASSETTE_POSE_TICK_MS`), so the label is repainted with it - measured 11 ms a pass, and the label is not what that cost is made of |
| per Track change | the print's 748x359 = 268,280 px repainted, once, at the two-finger pose; the pose's own entry is one 75 ms frame |

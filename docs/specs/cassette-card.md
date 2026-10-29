# The Card

On the white page - the **Real** Level - the cassette is a **Card**: one tinted rounded
rectangle at 426x272, the Track's picture (the **Cover**) or a drawn **label** printed on it at
+12,+20, and the tape visible through a 291x87 pill window the two share. It is the white page's
body, and it is where the cassette's colour lives.
[ADR 0039](../adr/0039-the-cassette-is-a-card.md) records why the glow is gone and why the
cassette is painted rather than blitted; this document is the operational spec - the rectangles,
the mask, the ink, the tick. The vocabulary is in [CONTEXT.md](./CONTEXT.md): **Card**,
**Cover**, **Page**, **Controls**, **Level**.

It draws at the **Controls pose only** (134/256, panel origin 140,94), because Real is only
reachable once the drawer has settled there. Like the glow it replaces, the Card is placed in
**screen pixels** and does not scale with the pose.

## What it is

- A rounded rectangle, **426x272 at (136,82), radius 12**, filled with one ink, created after
  the tape body so it sits **above the mechanics and below the print**, and shown and hidden
  with the Page in one frame. It is a reading of the Page, not a Level of its own.
- The box is the tape's ink (402x232 at 148,102) grown by **12 px** at the sides and **20 px**
  at the top and bottom: 426 = 402 + 24, 272 = 232 + 40. It is the box the white page's glow
  used to occupy, and it is the only box on the panel that lies entirely outside the print.
- The print sits at **+12,+20** - (148,102) 402x188 - so the Card reads as a **12 px mat at the
  sides, 20 px above and 64 px below** the picture.
- Radius **12**: rounder than the print's 10 and than frame[0]'s 8, so the Card reads as a
  moulded Card around a loose label rather than as a second border.
- Both the Card and the print's container are masked by **one A8 buffer** 426x272 whose window
  is zeroed at (67,75). The window is therefore at (55,55) in the print.
- The mechanics are hidden by the Card except through the window; what shows is the two Ø86
  hub rims and a horizontal slice of each pack. **While a Cover is printed, the hubs' lines are
  white** (`k_col_hub_white`): the rim's border and both spokes, while the rim's face keeps the
  playing red - the white is the hub's drawing, not the spool. The rewind's ink fade leaves the
  border alone while it is on. With a drawn label the hub keeps the tape's inks.

## Geometry

All values are panel pixels at the Controls pose and are half-open - the right edge is x + w.

| Part | x, y | size | notes |
|---|---|---|---|
| Screen | 0, 0 | 800x480 | the panel |
| **Card** | **136, 82** | **426x272** | radius 12, one ink, A8-masked |
| **Print container** | 136, 82 | 426x272 | the same mask; shown and hidden by a cut |
| **Print** (cover or label) | **148, 102** | **402x188** | the container's **+12,+20**; radius 10; `clip_corner` |
| **Window** | **203, 157** | **291x87** | radius 43; zeroed in the mask |
| Window in the print | 55, 55 | 291x87 | 55 left, 55 top, 56 right, 46 bottom |
| Window in the Card | 67, 75 | 291x87 | |
| Mechanics (tape ink) | 148, 102 | 402x232 | design 768x444; untouched |
| Hub rims | 249, 200 and 451, 200 | Ø86 | design 168, `cassette_len_even` |
| Packs | the same centres | Ø86 … Ø186 | PACK_MIN_D 168, PACK_MAX_D 356 |

The window is **not** centred in the print: 55 px from the left and the top, 56 from the
right, 46 from the bottom - it sits 4.5 px below the print's centre line. Its ends are the two
hub circles: the rims are 86x86 boxes at (206,157) and (408,157), so the window is exactly their
envelope - flush on the right and the top, 3 px of slack on the left, 1 px at the bottom. Do not
centre it by eye and do not derive it from the print; it is frame[1]'s interior,
`trunc(106 x 134/256) = 55` in from frame[0]'s corner.

## The mask

One buffer, one descriptor, two objects.

- **426x272, A8, stride 428** (`MASK_SIZE(w) = (w + 3) & ~3`), so **116,416 B**. It is 255
  everywhere and 0 in the stadium. It is built once, at the Card's only size; there is no
  per-step mask.
- The window takes the **largest radius the rectangle allows**, `min(291, 87) / 2 = 43` in
  integer arithmetic: a 205 px straight middle and two r43 semicircular ends, zeroed with
  pixel-centre sampling (`y + 0.5`, `ceilf`/`floorf`) so the caps agree with the frame's pill.
- `frame[1]` keeps its design radius and sits under the Card, invisible. The pill it used
  to take while a cover was up, and the raise that kept the frames over the print, are
  deleted with `cassette_tapes_set_cover_print()` and `cassette_tapes_raise_frames()`.
- Both masked objects sit at (136,82) and therefore share a centre, **(349,218)**, which is the
  only reason one buffer serves both: LVGL aligns a bitmap mask to the centre of the object's
  area and clears everything outside the mask's rectangle
  (`lv_draw_sw_img.c:1019` and the `side_area` clears). A mask must cover its object, and two
  objects can share one only if their centres coincide.
- The print's own 402x188 mask is **deleted**; the Cover's container grows to 426x272 and its
  picture stays at 402 wide inside a `+12,+20` clip box, so the window lands at (55,55) and the
  print is cut in and out, never faded.

## The drawn label

When the Track carries no picture, the print is **drawn**: the Track's name in **Jura 28**, in the
row's **label** ink, in the print's **top band** - (12,10), 378 px wide - because the window is the
print's middle and leaves only 55 px above it. No fill: the Card's plastic is the label's paper,
which is what makes the label ink read as ink on it. `LV_LABEL_LONG_DOT` from Track names
that outrun the band. It is cut by the mask like anything else in the print, so the window is the
mask's window, not the label's.

The label is not a placeholder for a picture that is still decoding: `cassette_view_set_cover_pending()`
(what `cassette_levels` calls when it asks the loader for a Track) leaves the print empty until the
loader answers with a picture or with NULL, so no Track flashes a name before its cover.

## The ink

| | |
|---|---|
| Card ink | the row's **plastic** |
| Label ink | the same row's **label** |

`k_col_cassette[]` in `cassette_style.c` is the whole palette (ADR 0040): one row per cassette,
`{ plastic, label }`, with `k_col_cassette_count` as its length. Adding a cassette is adding a
row - nothing else changes, and nothing is derived.

```
{ COL_HEX(0x2F5E7A), COL_HEX(0xF2E8D5) },   /* petrol, cream */
{ COL_HEX(0x8A3A2A), COL_HEX(0xEFDCC0) },   /* rust, warm cream */
{ COL_HEX(0x4C6B39), COL_HEX(0xE8EAD8) },   /* olive, pale sage */
{ COL_HEX(0x67457A), COL_HEX(0xF0DFE4) },   /* plum, pale rose */
{ COL_HEX(0x2E2E33), COL_HEX(0xE6E6E0) },   /* graphite, off-white */
```

- **The Track's file name picks the row**: `ink_seed % k_col_cassette_count`, where `ink_seed`
  is an FNV-1a hash of the status's `file` - the path stands in only when a Track has no name -
  computed in the model. So a song keeps its cassette across a folder rename, two folders holding
  the same name share one, and a Track with no Cover still gets its own shell. The view logs
  `cassette N of M: <title>` when the row changes, which is how you see the mapping on a flash.
- **Both inks land together, as a cut.** The view sets the Card's plastic and the label's ink
  from the same row on `ink_changed`; a colour animation would repaint the panel on every step.
- **Nothing is read off a picture.** `cassette_cover_set_image()` uses the loader's buffer for
  the picture alone, and there is no ink module: the histogram, the HSV conversion, the
  complement and its luminance fallback are deleted with ADR 0040.
- **The pair is the filler's judgement.** Nothing checks that a label reads against its plastic
  - keep the label light on a dark shell and dark on a light one, and keep the plastics far
  enough apart that two Tracks in a row do not look like the same cassette.

## The reel tick

The hubs animate at 25 Hz and the Card is a masked layer over them, so the tick must not
invalidate anything the Card would have to re-render.

- `pack_place()` **clamps each pack's box to the window** - (203,157) 291x87 - whenever the Card
  is up. Nothing moves: `pack_draw_cb` draws its rings from `SCX/SCY(k_hub[i])`, the hub's
  screen centre, not from the object's box, and `lv_obj_redraw()` truncates the layer to the
  object's box before `LV_EVENT_DRAW_MAIN_END` (`lv_refr.c:131`), so the rings are clipped to
  the window for free.
- The union of the two pack boxes falls from **72,168 px** - 388x186, the union of two 186x186
  boxes at the packs' centres (PACK_MAX_D 356, `L(356) = 186`) - to the window's **25,317 px**,
  and the Card is re-composited over that alone.
- The rims (86x86 boxes at y 157) and their spokes are already inside the window; the strands
  move only on a **fill** update - a few times a second, not every tick - so they keep their
  full boxes.

## The tree

- **`components/app/screen_display/cassette_card.c` / `.h`** - new, and the one place the
  numbers live: the Card's rect, the print's offset, the window's offset, size and rim, the mask
  builder, the Card object, `cassette_card_set_ink()`, and `cassette_card_mask()` for the print
  container. It takes the glow's slot in `cassette_view.c`, created after `cassette_tapes_create()`.

The four objects are created in the order they stack, so **nothing is raised at runtime**: the
tape body, then the Card, then the print (the Cover's masked container), then the window's rim.
The rim is a child of the cassette container rather than of the print, because the print is
masked to the window and the rim's bottom row lies on the Card just outside it. It is **3 px of
the Card's own plastic** - `CASSETTE_CARD_WINDOW_RIM_W` - on a **291x88** box with the window's
43 px radius, drawn over the print's cut edge, so it covers the mask's last row of antialiasing
and the window reads as the shell moulded around it rather than as a soft cut. It is up whenever
Real is, picture or no picture, and it takes the row's plastic with the Card (there is no
separate colour: the frame and the body are one material, only the print is another).

- **`cassette_cover.c`** - the container becomes 426x272 at the Card's rect and the print is
  clipped at +12,+20. It stops reading `cassette_tapes_frame_rect()` for its geometry and stops
  building its own mask; it asks the Card for the descriptor and shows and hides by a cut.
- **`cassette_tapes.c`** - `pack_place()` gains the clamp; `raise_frames()` and
  `set_cover_print()` go, since the Cover only ever shows on this page and the frames are under
  the Card.
- **Deleted on the white page**: the glow (`s_page_edge`, `EDGE_W`/`EDGE_H`/`EDGE_R`,
  `EDGE_GLOW_*`, the 200 ms fade) and the frame raise with its pill. The frame rings, the three
  chips, both rollers, the run and both strands are still drawn - they are simply under the
  Card.
- **Untouched**: the dark page, the Initial 256/256 cassette, the Peek, the artwork loader, the
  cover picture's fitting, the 291x87 window's shape.

## Costs

| | |
|---|---|
| PSRAM | mask **116,416 B**, replacing 75,952 B → **+40,464 B** |
| flash | **0 B** - no art is embedded; the fifteen-label, 1.13 MB idea is gone |
| layers | the Card and the print each draw into a banded ARGB layer; `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE = 24576` gives about 14 rows at 426 px, so ~24 KB transient |
| tick | the invalidated area drops to ≤ 25,317 px, and nothing outside the window is repainted |

## Still open

- Whether the drawn label needs more than one design per ink pair, now that it costs code rather
  than bytes.
- The five rows are a first cut, not a measurement: they are what a 426x272 mat and a cream
  label look like on the white page, and they are the thing most likely to want tuning there.

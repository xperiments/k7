# A full-screen Real cassette: the mask's size, the two-finger swipe, and what the tape really costs per frame (2026-11-05)

> **Status: the porthole (§4e) is implemented on `ui/full-screen-real` with `CARD_MASK 0`
> (`cassette_card.c`), and the firmware was flashed on 2026-11-05.** The full-screen pose, the
> flag and the gesture are still proposals, and the two board measurements §10 asks for are
> still owed — one of them now has a first reading (§3.1).

Axis: **the owner wants Real's content — the Track's picture printed on the cassette, seen
through the pill window — at the entry pose instead of the Controls pose, reached by a
two-finger swipe up from Initial and dismissed by a two-finger swipe down, and asks whether the
tape's hub arcs could be drawn windowed-only to pay for it. This note prices the picture at that
size (one A8 mask of the Card's box), establishes what LVGL actually does with a bitmap mask per
frame, prices five ways to get the same window without a giant mask, checks the "full circle"
claim against the source, and finds that the gesture was written, shipped and removed in
`f6a1a4d` — but never confirmed with two fingers on the panel.**

Read out of this repository, the LVGL 9.6.0 tree vendored into `managed_components/lvgl__lvgl/`,
and the ADRs; the porthole build was then flashed and its boot log read (§3.1). Everything else
was read, not run.

Four kinds of number appear and are never mixed:

- **MEASURED (board)** — printed by the firmware on the panel. **One here, §3.1** (the loader's
  PSRAM line on the porthole build). Where a second is needed it inherits from
  `docs/research/panel-rgb-lvgl-performance.md` and says so.
- **MEASURED (link)** — read out of a built artefact. **None here**; the two existing notes'
  link figures are quoted where needed.
- **DOCUMENTED** — a line of source, a header, a Kconfig value or an ADR, cited `file:line`.
- **ARITHMETIC / ESTIMATE** — computed here from DOCUMENTED inputs cited beside it. An ESTIMATE
  states its rate; nothing below presents one as a measurement.

## Bottom line

1. **The mask is a resident cost, not a frame cost — and that is why it is worth refusing, but
   it is not what makes the view impossible.** The A8 buffer is 116,416 B today
   (`cassette_card.c:28`, `:86`; ADR 0039). At the design grid the Card's own box is 814x520
   design px, so a full-screen Real wants **334,400 B (760x440), 384,000 B (800x480) or
   424,320 B (814x520)** — 83 % of the 464,310 B cover buffer (`artwork_loader.c:22`). **The
   board measures 3,568,488 B of PSRAM free before the cover and 3,101,540 B after it (§3.1),
   so a 384 KB mask would fit on this build**; the loader's "two covers plus a band did not fit
   in ~1.7 MB" line (`artwork_loader.c:12-27`, `:560-566`) is a record of an earlier
   configuration and is stale. The owner's "memory constraints of the mask" is the right
   instinct for the wrong reason: the mask fits, it is simply 384 KB spent to make a hole that
   a clip can paint.
2. **LVGL does not iterate the mask per frame, and does not allocate per draw either — it
   allocates per *band*, and walks only the invalidated area.** `apply_mask()` intersects the
   mask with the layer's own `buf_area` (`lv_draw_sw_img.c:1024-1032`) and loops over that
   intersection only (`:1065-1076`); the layer's area is `clip ∩ obj` (`lv_refr.c:1201-1206`) and
   is banded to `LV_DRAW_LAYER_SIMPLE_BUF_SIZE` = 24,576 B (`lv_refr.c:557-563`). So a 384 KB
   mask costs **zero** while the Card is not invalidated and **9 B of PSRAM traffic per
   invalidated masked pixel** when it is. The transient layer never exceeds ~24 KB and never
   scales with the mask.
3. **The dangerous cost is not the mask's area; it is a card that covers everything that moves.**
   At the Controls pose the Card is above the tape but the packs are clamped to the window
   (291x87 = 25,317 px, `cassette_tapes.c:319-349`), so a fill change re-renders 25,317 masked px.
   Full-screen the Card is above the *whole* tape and the clamp rect is the entry window
   (556x166), so the same change re-renders 356x166 = 59,096 px **per pack** (118,192, ~4.7x
   today's) on the Card and again on the print above it. If the window is not re-pointed at the
   new pose, the clamp falls back to nothing and each pack's full box is 356x356 = 126,736 px
   (2 × 126,736 = 10x) — so §9's "the accessor must return the current pose's window" is
   load-bearing, not tidiness.
4. **The tape idea is already done, twice, and windowing at Initial has nothing to window to.** A
   0…360 arc is converted to a border and clipped (`lv_draw_sw_arc.c:66-78`), and the packs'
   boxes are already clamped to the window (`cassette_tapes.c:312-352`). The packs do **not**
   redraw per frame; only the two spokes do (`cassette_tapes.c:263-274`), ~1,600 px/cycle at the
   entry pose. The rings redraw when the *fill* changes — every ~1.5–4 s — and then cost their
   **bounding boxes, not their ink** (§5.2): the one place the owner's instinct is right, for a
   reason other than the one given.
5. **The gesture was built, verified nowhere, and removed.** `touch_two_finger_check()` — a 30 px
   midpoint displacement, once per touch, with a latch and a wake-only guard — is in
   `git show f6a1a4d^:…/touch_gt911.c:78-108`, and the driver already reads two points today
   (`touch_gt911.c:83-87`); `point[1]` is read and then unused (`:111-119`). **No note, log or
   ADR records the controller reporting two contacts on this panel** — ADR 0014:83-84 says "the
   board pass is owed" — so that is the first measurement, and the instrument is already in the
   shipping build (`touch_gt911.c:89-93`).
6. **It must be a flag, not a fifth rung, and the pose cannot deliver it.** The Card is authored
   in *Controls-pose panel pixels* (`cassette_card.h:31-54`) and ignores the pose
   (`cassette_view.c:232-234`, `cassette_card.h:10-12`). 426x272 at scale 134/256 is 814x520 in
   design units — **larger than the 800x480 panel** — so "full screen Real" means re-authoring
   the Card on the design grid, not animating a scale.

## 1. What the four rungs are, and what a fifth presentation would be

ADR 0025 fixes the stack bottom-first as `PEEK, INITIAL, ART, REAL` with `level + 1` a down
swipe (`controls.h:121-127`, `controls.c:43-58`). The room is a function of the level
(`controls.c:29-38`), and Initial is the entry pose (`cassette_layout.h:163-170`).

"The real cassette view with the cover" is three objects, all built once and hidden: the **Card**
(the white page's plastic, `cassette_card.c:135-165`), the **print** (the Track's picture in a
402x188 box at +12,+20, `cassette_cover.c:146-198`) and the **rim** (`cassette_cover.c:215-221`).
All three are placed in screen pixels at the Controls pose and none is handed the pose:

> `/* The print does not follow the pose any more: its box is the Card's, fixed in screen
> pixels, and it only ever shows at the Controls pose. */` — `cassette_view.c:232-234`

So the request is Real's **content** at the **entry room**: not a new rung (the ladder is a stack
of presentations and Real already tops it), not a pose change (the pose reaches the tape and the
Peek, not the Card). It is a second geometry for the same three objects (§7) behind a modal flag
shaped like the Menu's (§8).

## 2. What LVGL does with `lv_obj_set_style_bitmap_mask_src`

| Step | What happens | Where |
|---|---|---|
| 1 | A bitmap mask forces `LV_LAYER_TYPE_SIMPLE` | `lv_obj_style.c:1422` |
| 2 | The object is not drawn directly: a layer is made around it | `lv_refr.c:537-547` |
| 3 | The layer's area is **clip ∩ the object's ext coords** — the invalidated area only | `lv_refr.c:1201-1206` |
| 4 | That area is split into bands of `24576 / w / 4` rows | `lv_refr.c:557-563` |
| 5 | Each band gets its own `lv_layer_t` and its own ARGB8888 `lv_draw_buf` | `lv_draw.c:446-468`, `:495-529` |
| 6 | `apply_mask()` runs on each band after it renders | `lv_draw_sw_img.c:109-112` |
| 7 | It **re-opens the mask through the image decoder every band**: `CONFIG_LV_CACHE_DEF_SIZE=0` and `LV_IMAGE_HEADER_CACHE_DEF_CNT=0`, so `get_decoder_data()` mallocs per open | `lv_draw_sw_img.c:999`; `lv_bin_decoder.c:542`; `sdkconfig.sunton:4342,4344` |
| 8 | The mask is **aligned to the centre** of `draw_dsc->image_area` (the object's box) — why the Card and the print must share a centre, and why a mask cannot sit off-centre | `lv_draw_sw_img.c:1019-1021`; ADR 0039 |
| 9 | `masked_area = mask ∩ band`; four `side_area` clears wipe the band outside the mask | `lv_draw_sw_img.c:1029-1055` |
| 10 | A per-pixel loop: `img[x*4+3] = LV_OPA_MIX2(mask[x], img[x*4+3])` | `lv_draw_sw_img.c:1069-1076` |

The five sub-questions, answered:

- **Allocate per draw?** Not the mask (built once, resident) — but *per band*: one `lv_layer_t`,
  one ARGB8888 buffer (≤ 24,576 B), one decoder open/close.
- **Iterate per pixel?** Yes, once per masked pixel of the invalidated area: a byte read and a
  read-modify-write of a 4-byte ARGB pixel.
- **Per-frame cost?** Zero while nothing invalidates the Card. When something does: invalidated
  area × 9 B of PSRAM traffic, plus the per-band fixed costs.
- **Clip to the invalidated area?** Yes, twice — `layer_get_area` (`lv_refr.c:1201-1206`) and the
  intersection in `apply_mask` (`lv_draw_sw_img.c:1029`).
- **Size limit?** None in this build: `LV_DRAW_LAYER_MAX_MEMORY=0` (`sdkconfig.sunton:4096`), and
  the mask is only required to be A8/L8 (`lv_draw_sw_img.c:1006-1012`).

### 2.1 Band counts and transient memory (ARITHMETIC)

`rows = floor(24576/(w*4))`, band bytes = `w*4*rows`:

| Card box | stride | **mask bytes** | rows/band | band bytes | bands (full height) |
|---|---|---|---|---|---|
| 426x272 (today) | 428 | **116,416** | 14 | 23,856 | 20 |
| 556x166 (window only) | 556 | 92,296 | 11 | 24,464 | 16 |
| 760x440 (fitted) | 760 | **334,400** | 8 | 24,320 | 55 |
| 800x480 | 800 | **384,000** | 7 | 22,400 | 69 |
| 814x520 (Card proportion) | 816 | **424,320** | 7 | 22,848 | 75 |

The transient layer is bounded at ~22–24 KB in every row. **The resident mask is the cost; the
rendering machinery is a rounding error beside it.**

## 3. Three bills a full-screen Real would pay

1. **Resident:** 334,400–424,320 B, permanently, beside the 464,310 B cover. The board's own
   free-heap line (§3.1) says 3.1 MB is still free after the cover, so this fits — it is a bill,
   not a wall.
2. **Per invalidation that touches the Card:** 9 B per masked pixel. Controls pose, a fill
   change, clamped to the window: 25,317 px → 227,853 B. Full-screen, same change, clamped to
   the *entry* window: 118,192 px (two packs at 356x166) → 1,063,728 B on the Card, and the same
   again on the print above it; the packs themselves add two ring sets (§5.2).
3. **Transient:** one band (~24 KB) plus 7–20 small mallocs. Not a scaling problem.

For scale: the whole-screen repaint already documented for this board is 1,536,000 B of PSRAM
traffic per cycle (`docs/research/panel-rgb-lvgl-performance.md` §6, lever 7). One full-screen
masked object's mask pass over its whole area is 384,000 × 9 = 3,456,000 B — **2.25x a
full-screen RGB565 repaint**, before anything is drawn.

### 3.1 MEASURED (board): the heap the mask would come out of

The porthole build (no mask allocated, `CARD_MASK 0`) prints, on the Sunton board at boot with
no card in the slot:

```
I artwork: artwork memory before the cover: psram free 3568488 B, largest 3538944 B
I artwork: artwork memory after the cover:  psram free 3101540 B, largest 3080192 B
```

`artwork_loader.c:564-570` prints these two lines; the values are this build's, 2026-11-05, and
the mask would have been taken out of the same heap before them. **With a card mounted** the same
build reads `psram free 2933228 B` / `2466280 B` — the card's FAT, its buffers and the mounted
library take ~600 KB — so the card is the case that matters, and it is the one below. Two
consequences:

- **A 384 KB mask fits either way**: 16 % of the 2.4 MB free after the cover with a card in, and
  the largest free block is still 2,424,832 B. The note's earlier inference from
  `artwork_loader.c:12-27` ("two 464 KB buffers plus a band did not fit") describes an older
  build; the comment is stale and should not be used to refuse the mask without re-measuring.
- **The largest block is the number that matters**: the mask is one contiguous allocation, so
  fragmentation, not the total, is what would fail first — the same lesson ADR 0004 records for
  Wi-Fi. A 424 KB mask is 17 % of the card-in block.

So the argument for the porthole is not "the mask cannot fit". It is "the mask is 384 KB of
permanent PSRAM spent to make a hole that a clipped child can paint, and it costs 9 B per masked
pixel every time the picture or the tape under it is redrawn".

## 4. Five ways to get the window without a giant mask

| | Memory | Per redraw of the window area | Quality lost |
|---|---|---|---|
| **(a)** `clip_corner` on a stadium container | **0 B resident** | 2 × (w×rout×4) transient + a radius-mask pass: 2 × **50,052 B** at 291x87 r43 | none — same AA as the Card's corners |
| **(b)** mask at lower resolution | — | — | **does not exist**: a mask is aligned 1:1 to the object's box (`lv_draw_sw_img.c:1019-1021`); half-size clears half the object |
| **(c)** window-only A8 mask | **92,296 B** (556x166) | the same 9 B/px | none, but the *Card* cannot be that object — it needs an interior hole, so you would split the Card into pieces |
| **(d)** rectangular clip, square corners | **0 B** | 0 | the pill (§ADR 0039) — though the existing 7 px rim would hide most of it |
| **(e)** porthole above the print | **0 B resident** | 2 × (w×rout×4), unbanded: 2 × **50,052 B** at the Controls pose, 2 × **182,368 B** at the entry pose | none |

**(a) is not free of layers.** `clip_corner` rounds an object's own four corners
(`lv_refr.c:159-257`); it cannot subtract an interior shape, so it cannot make the hole. What it
does is clip **children** to the rounded rect: the top `rout` rows and bottom `rout` rows are each
rendered into an **ARGB8888 layer sized w × rout, unbanded**, masked by `lv_draw_mask_rect()`
(`lv_refr.c:196-256`; `lv_draw_sw_mask_rect.c:41-116`).

**(e) is the strongest, and it deletes two of the three terms.** Leave the Card an ordinary
opaque rounded rect with **no mask**, and put a capsule at the window rect *above* the print with
`radius = 43`, `clip_corner = true`, and a **child** that draws the tape slice. Three checks:

- **Does `clip_corner` clip children and a custom draw callback?** Children: yes
  (`lv_refr.c:159-257`). A `DRAW_MAIN_END` on the porthole *itself*: **no** — the object's own draw
  is clipped only to its rectangle (`lv_refr.c:118-129`); the rounded clip happens when children
  are recursed. So the tape slice needs a **child** object, not the porthole's own callback. With
  `rout = min(43, 87/2) = 43` the un-masked middle band is `87 − 2×43 = 1` row, so effectively the
  whole porthole is correctly rounded.
- **Does it get an ARGB8888 layer?** The object does not (`clip_corner` is not part of
  `calculate_layer_type`, `lv_obj_style.c:1412-1424`); the two corner bands do, at full
  `w × rout`, unbanded — **50,052 B per band at the Controls pose** (100,104 B for the two) and
  **182,368 B per band at the entry pose** (364,736 B for the two). The
  second number is the porthole's own scaling problem, and it is transient rather than resident.
- **Cost per pixel vs the bitmap mask?** `lv_draw_sw_mask_rect` does a memset + `mask_apply` per
  row and an alpha multiply on rows that are not `FULL_COVER`
  (`lv_draw_sw_mask_rect.c:94-113`) — the same order per pixel as `apply_mask`'s `LV_OPA_MIX2`
  loop, but over **the window only** (25,317 px), not over the Card *and* the print (116,416 px
  each today, 384,000 at full screen).

**So the porthole does not move the cost — it removes the resident mask and both per-pixel mask
mixes.** It pays with a painted capsule (the window stops being a hole and becomes an opaque dark
capsule — visually the same, because the backdrop behind the window is `k_col_bg` on both pages,
`cassette_view.c:164-166`) and with the corner layers above. The 7 px rim already exists to cover
the edge (`cassette_card.h:56-61`).

### 4.1 Built on `ui/full-screen-real` (2026-11-05)

The porthole is implemented at the **Controls pose only**; the full-screen pose, the flag and the
gesture are untouched. What changed:

- **`cassette_tapes.c/.h`** — the three draw bodies the body's objects used (`pack_draw_rings`,
  `strand_draw`, and the new `hub_draw`) are now functions a layer can be handed, and one public
  `cassette_tapes_draw_slice(layer)` paints packs, strands and both hubs in screen coordinates.
  It reads the module's own state (`s_pack_d`, `s_shown_hub`, `s_tape_ink`, `s_hubs_white`), so
  the slice cannot drift from the body: both go through the same functions.
- **`cassette_cover.c/.h`** — owns the porthole (`s_window`, a `cassette_box` at the window rect
  with radius 43 and `clip_corner`) and its one child `s_slice`, and exposes
  `cassette_cover_set_window_draw()` (the view hands it `cassette_tapes_draw_slice`) and
  `cassette_cover_window_invalidate()`. It is created before the rim, so the rim still covers
  its edge, and shown/hidden with `cassette_cover_set_shown()`.
- **`cassette_card.c`** — `CARD_MASK 0`: `mask_build()` is not called, `cassette_card_mask()`
  returns NULL, and the Card is an ordinary opaque rounded rect. Flip to 1 for the A/B.

**The first build starved the stream and flashed the screen.** With the whole capsule invalidated
on every frame that moved, the porthole rebuilt its two unbanded ARGB corner layers — 291×43×4 =
50,052 B each — and redrew ten rings per pack twice per band, at up to 25 Hz. On a device whose
HTTP stream shares core 0 and the PSRAM heap this is exactly the shape of a stall: the audio
broke up, and the panel showed white repaints as frames could not be finished. This is the
measurement §10 asked for, taken the hard way.

The fix, in this commit:

- **The tapes report what moved.** `cassette_tapes_take_slice_dirty()` hands the view up to four
  screen rectangles per frame — the union of a spoke's old and new boxes (a few dozen pixels), one
  pack's old and new box on a fill change, a hub's box when its ink changes — and the view
  invalidates exactly those (`.c` §"the window's slice"). A frame that moves nothing invalidates
  nothing.
- **The slice culls by the layer's clip area.** `ring_in_clip()` compares a ring's radius with the
  nearest and farthest distance from its hub to the dirty box, so a 12 px box next to one hub
  creates no draw tasks for the other hub's ten rings, nor for the rings that pass outside it.
- **The body's reels are hidden while the Card is up.** At the Controls pose both packs (Ø186)
  and both rims (Ø88) sit entirely inside the Card's 426×272 box — that is the only pose Real
  has — so `cassette_tapes_set_window()` hides them and the slice is the only copy drawn. A fill
  change then repaints the slice and not the body, and the 25 Hz spoke tick cannot reach the Card
  at all.

**Verified:** `idf.py build` clean (no warnings), flashed to the Sunton board, boots with a card
in, the loader's PSRAM lines printed (§3.1). **Not yet verified:** the picture itself — the
capsule's edge under the 7 px rim has not been looked at — and whether the audio is clean at Real
now. The next reading is a listen, and the frame line (`screen: 50 frames: worst N ms`) while the
reels turn.

## 5. The tape and the hub: is anything drawing a full circle it could skip?

### 5.1 What the code draws

- A pack is **10 concentric full circles**, from the pack's radius down to the rim, 10 design px
  apart, 1 px wide, all from one draw callback (`cassette_tapes.c:282-306`).
- Each is a real `lv_draw_arc` with `start_angle = 0, end_angle = 360` (`:294-295`).
- LVGL special-cases exactly that into a border: *"Draw a full ring"* —
  `if(dsc->start_angle + 360 == dsc->end_angle …)` → `lv_draw_border_dsc_t` with
  `radius = LV_RADIUS_CIRCLE` (`lv_draw_sw_arc.c:66-78`), which allocates **one scanline** of mask
  and works a row at a time (`lv_draw_sw_border.c:92-97`).
- **They are not drawn full and then covered.** They are full circles *clipped* to the object's
  box: `lv_obj_redraw()` truncates the layer's clip area to `obj coords + ext`
  (`lv_refr.c:118-129`), and `pack_place()` sets that box to the intersection with the Card's
  window whenever the white page is up (`cassette_tapes.c:319-349`). The source says so:

  > "While the white page's Card is up the box is intersected with its window … a clamped box
  > clips the rings to the window and moves none of them." — `cassette_tapes.c:312-318`

- The hub itself is a **rim** (168 px circle border, painted once, `:585-589`) plus **two spokes**
  (`:591-599`), and only the spokes move (`:263-274`).

### 5.2 The per-frame cost at the entry pose (ARITHMETIC)

- **Per frame:** only `hub_frame_set()`, and only when the frame index changes — 25 Hz. Each spoke
  is a 6 px segment (design 66…72, `:70-71`), width 4, rounded caps: box `6·|cos|+7` by
  `6·|sin|+7` = 91–121 px. Over a 10° step the far end moves 72 × 0.1745 = 12.6 px, so
  old ∪ new ≈ 400 px; **4 spokes ≈ 1,600 px/cycle**, ~0.1–0.24 ms at the hub note's 60–150 ns
  per drawn pixel (`hub-drawn-vs-blitted.md` §5) — the same order as that note's 2,200 px/cycle.
- **On a fill change:** `pack_set()` changes the diameter and **one pack repaints all 10 rings**
  (`:354-360`). Because `draw_border_complex` walks every row of the ring's own box — `max_h =
  rout`, row width `draw_area_w` (`lv_draw_sw_border.c:193-218`) — the cost is the **bounding
  box**, not the ink:

| | rings/pack | mask-eval px | row-walk px | both packs |
|---|---|---|---|---|
| entry (max fill, no clamp) | 10 (r 88…178) | Σ 2r² ≈ **370,280** | Σ 4r² ≈ 740,560 | ~1.5 M px |
| Controls (clamped to the window) | 10 (r 48…93) | ≈ **162,000** | ≈ 324,000 | ~0.65 M px |
| full-screen Real (clamped to the 556x166 entry window) | 10 (r 88…178) | ≈ **221,000** | Σ 2r·min(2r,166) ≈ **441,560** | ~0.88 M px |

The third row is the case the owner is asking for: the same ten rings, clipped vertically by the
window, so they cost ~60 % of the unclamped entry figure — and ~1.4x the Controls row that is paid
today, on the same ~1.5–2.8 s period, plus the Card and print re-composite above them (§3).

- **How often:** `pack_fill` is 80 + 920(1000−q)/1000 (`cassette.c:455-457`), the diameter rounds
  to even (`cassette_tapes.c:365-370`) and `dd/dfill = 98.512/(2d)`, so a 2 px step is every
  **8–16 permille** of track position — **every 1.5–2.8 s on a 3-minute track** (ARITHMETIC from
  those three). It is a periodic spike, not a steady cost; at the wide rate bands above it prices
  at single-digit to low-tens of milliseconds. **That is the number to measure, not to argue** (§10).

### 5.3 Would "windowed-only" drawing help?

- **At Initial: no, and there is nothing to window to.** No Card and no window exist at the entry
  pose (`cassette_view.c:160` sets a window only on the white page), and the rings *are* the
  picture. Clipping them would remove tape the owner wants to see.
- **At Real: already done**, by `pack_place()` — ~2.3x on a fill change (table above), and far
  more on what must be re-rendered *above* the packs, which is the reason ADR 0039 gives.
- **The remaining honest win is not "windowed" but "fewer rings":** each ring costs its box because
  `draw_border_complex` steps a row at a time across the full width. Ten 1 px rings are ~740 k px
  of mask work for ~1,100 px of ink. One filled disc plus a few lines, or wider spacing at low
  fill, would cut it — but that is a look change, like ADR 0039's other alternatives.

**Verdict: the instinct that a full circle is wasteful is right, but the waste is ten rings'
bounding boxes on a fill change — not a per-frame full-circle draw, and not something a window
fixes at Initial.**

## 6. The two-finger swipe

### 6.1 What the driver has today

- `touch_read()` reads **two** points: `esp_lcd_touch_point_data_t point[2]`, count in `points`
  (`touch_gt911.c:83-87`). The controller can report up to 5
  (`CONFIG_ESP_LCD_TOUCH_MAX_POINTS=5`, `sdkconfig.sunton:3891`) and the driver passes the count
  through (`esp_lcd_touch_gt911.c:270-291`).
- **`point[1]` is read and then never used** — only `point[0]` becomes the pointer
  (`touch_gt911.c:111-119`). The comment says why: *"A second contact is not a gesture: the
  two-finger swipe that used to toggle the VU meter is gone (ADR 0025), so the pointer reports the
  first point and nothing else."* (`:70-72`).
- A `[GDBG] contacts=%u` line already logs the contact count on every change (`:89-93`) — **in the
  shipping build.**
- `touch_read()` runs on the port's task **inside `lv_timer_handler()`, under the LVGL lock**
  (`:62-64`, `:73`), every 10 ms (`:51`, `:225`).

### 6.2 The detector that was deleted, and how far to trust it

`git show f6a1a4d^:components/app/screen_display/touch_gt911.c` has the whole gesture:

```c
#define TOUCH_TWO_FINGER_PX 30          /* :56 */
/* midpoint of the first two contacts, latched once per touch: :80-108 */
const int32_t mx = ((int32_t)point[0].x + point[1].x) / 2;
const int32_t my = ((int32_t)point[0].y + point[1].y) / 2;
if (ax < TOUCH_TWO_FINGER_PX && ay < TOUCH_TWO_FINGER_PX) return;   /* latched, one per touch */
if (s_two_finger_cb) s_two_finger_cb((int)dx, (int)dy);
```

plus a wake-only guard keeping a wake press out of it (`:165-170`) and a latch in `screen_input.c`
(`f6a1a4d^:…/screen_input.c:44-46`, `:110-113`) so the same touch could not also fire LVGL's
one-finger gesture. ADR 0025 removed it end to end, and `screen_input.h:22-24` now says a
two-finger swipe is not an intent.

**Was it ever seen working? No record.** ADR 0014's closing consequence is explicit — *"The board
pass is owed: the new order, the up-swipe peek and the two-finger no-op need a finger, because the
host tier does not compile the LVGL view"* (`docs/adr/0014-…md:83-84`) — and nothing in `docs/`
logs `contacts=2`. **This is UNKNOWN, not MEASURED.** The measurement is one press: open the
serial log, put two fingers down, read `[GDBG] contacts=`. It should print 2 — the driver reads the
status nibble (`esp_lcd_touch_gt911.c:270`), fills that many coordinates (`:291-297`) and `get_xy`
copies `min(points, max)` (`:316`), `max` being 2 from the app.

### 6.3 Where the detection belongs

**In `touch_read()`, nowhere else**: it is the only place with both points in one read, it already
runs under the LVGL lock so it may call the sink directly, and 10 ms gives 20–40 samples in a
200–400 ms gesture.

What must **not** change: LVGL's pointer. It still gets contact 0 and still recognises its 50 px
one-finger gesture (`lv_indev_set_gesture_min_velocity(0)`, `touch_gt911.c:231`), so a two-finger
swipe **will** also fire `LV_EVENT_GESTURE` on the screen, which today takes a rung
(`screen_input.c:48-63`). The latch is not optional: set it on report, clear it when `points == 0`,
and have the gesture handler return early while it is set — the shape `f6a1a4d^` had.

One structural constraint the old code violates: it named the board symbol from `screen_input.c`
(`#if CONFIG_APP_BOARD_SUNTON_8048S050C`), and `host/tests/layering.sh` A2 forbids that — *"any
other module naming the board symbol is a capability that has not been named yet"*
(`layering.sh:196-208`). The gesture must reach `screen_input` through a callback registered by
the adapter (the old `touch_gt911_set_two_finger_cb`, which lives in the app tier) or a
`screen_caps` field.

### 6.4 Tuning it to the requested gesture

The old rule is "the midpoint moved 30 px on **either** axis". The request is "two fingers,
together, mostly vertical", so add three cheap tests in the same function:

- **Side-by-side:** `40 < |x0 − x1| < 400` and `|y0 − y1| < 60` — rejects a pinch and a diagonal
  two-hand grab;
- **Vertical:** `|dy| > 2·|dx|`;
- **Direction:** `dy < 0` is up (to the full-screen Real), `dy > 0` down (back).

Keep the 30 px threshold and the once-per-touch latch: a two-finger midpoint moves less than
either finger, and 30 samples in 300 ms is plenty. A per-contact rule (both fingers moving > X px)
is available — `track_id` is filled per point (`esp_lcd_touch_gt911.c:292`) — but the midpoint was
enough before and is simpler.

### 6.5 Risk to the existing gestures

| Gesture | Risk | Why |
|---|---|---|
| One-finger swipes (the ladder) | covered by the latch | same touch; `screen_gesture_cb` returns early while it is set |
| Long-press / double-tap on cells | low | need one contact and a still finger; the detector needs 30 px of travel |
| Drawer, Pad ratchet, knob drags | low | one-finger, local to their parts; the detector only *reports* an intent |
| The Menu | none | the Menu swallows intents before any rule (`controls.c:116-125`) |
| A wake press | must stay excluded | keep the `wake_only` guard (`f6a1a4d^:…:165-170`) |

The residual risk is a **phantom second contact**: if the GT911 sometimes reports 2 with one finger
down, a normal swipe could be swallowed by the latch. That is the second thing the `[GDBG]` log
settles (§10).

## 7. The pose, the Card's geometry, and the cover

### 7.1 The Card cannot be posed

`cassette_card.h:10-12`: *"The Card places itself in screen pixels and never scales: it exists only
at the Controls pose … so the pose's animation never resizes it."* Its constants (426x272 at 136,82;
window 291x87 at +67,+75; print 402x188 at +12,+20; rim 291x88) are all **Controls-pose panel
pixels** (`cassette_card.h:34-72`).

### 7.2 The same rectangle at the design grid (ARITHMETIC)

With `origin = (800 − 418)/2 − 51 = 140` and `(480 − 251)/2 − 20 = 94` (`cassette_layout.c:31-38`,
`:53-56`) and scale 134/256 = 0.5234:

| Object | Controls-pose screen rect | Equivalent in design units |
|---|---|---|
| Card | 136,82 426x272 | **−8, −23, 814x520** |
| Window | 203,157 291x87 | 120, 120, 556x166 (r 82) |
| Print | 148,102 402x188 | 15, 15, 768x359 |

**814x520 is bigger than the 800x480 panel**: a pure pose scale hangs the Card off every edge (8 px
left, 6 right, 23 top, 17 bottom). So "full screen" must be an *authored* rect — 760x440 is a
comfortable fit (20 px margins), 800x480 is edge to edge with the radius clipped — and window,
print, rim and plate must be re-derived with it. A trap worth naming: `cassette_len()` truncates,
so 814×134/256 = 426.06 → 426 and 520×134/256 = 272.19 → 272 round-trip, but 166×134/256 =
86.90 → **86, not 87**.
Re-authoring in design units therefore moves the Controls pose by a pixel unless the numbers are
chosen to round-trip; `t16_layout` pins the grid at all three poses and is the test that catches it
(ADR 0042).

### 7.3 The cover

- One 603x385 RGB565 buffer, 464,310 B, allocated once for the process (`artwork_loader.c:66-69`,
  `:22`, `:560-570`).
- At the Controls pose the print is 402 px wide, so the buffer is **downscaled 0.67x** with
  antialiasing (`cassette_cover.c:79-88`, `:193-195`), the print clipping 257 px of image into 188.
- At the entry pose the print would be 768 px wide: the same buffer **upscaled 1.27x**, drawn into
  768x359 of visible print — **3.6x the pixels** (275,712 vs 75,576) on the renderer's most
  expensive path (scaled + anti-aliased), and visibly softer. A native buffer would be
  768x490x2 = **752,640 B** — 1.6 cover buffers, and §3.1's measurement says it would fit, at the
  cost of a third of the free heap (`artwork_loader.c:22-27`).
- No cheap middle exists: the loader fits *cover-style* to one fixed shape (`:66-69`), and ADR 0021
  already rejected decoding at the display width because it ties the buffer to a pose.

## 8. The model surface: a flag, not a fifth rung

Adding `CONTROLS_LEVEL_FULL` would break the one thing ADR 0025 rests on — "the enum is the stack",
down is `+1` and up `−1` (`controls.h:121-127`, `controls.c:43-58`) — and would renumber every
mask in `host/tests/t24_controls.c`, which pins the mask as an **exact diff** per state × intent
(`t24_controls.c:9`, `:156-159`, `:316`).

The shape that fits is the Menu's: one orthogonal bit, valid at exactly one level, with its own
transitions.

```
bool full;                          /* struct controls */
CONTROLS_CHANGED_FULL = 1u << 5,    /* a new bit; t24 gains a state, not a renumbering */
```

- **Set:** two-finger swipe up, only at `CONTROLS_LEVEL_INITIAL`, and only with
  `facts.at_entry_pose` — the same guard the Initial→Peek up already has (`controls.c:52-58`).
- **Clear:** two-finger swipe down from any level (the owner's way back), **and every way off
  Initial** (DOWN, UP, the Menu), because the flag's content is a reading of Initial exactly as the
  Menu is a reading of the chrome. `controls.c:133` repairs the Menu's equivalent invariant.
- **Invariant, in the same place as the Menu's:** `if (level != INITIAL) full = false;`
- **No ROOM bit either way:** both sides are the entry room, so entering and leaving is a cut —
  which is what the Card's arrival already is (ADR 0039: "cut in and out").
- **Does it block the ladder?** Simplest is no: a one-finger down while full leaves full and takes
  the rung, which reads as "put it away and go to the controls".

`t24` then grows one state (Initial + full) and five transitions; the existing exact-mask cases stay
valid because the new bit is zero everywhere they tread.

## 9. What would have to change

- **`cassette_layout.h`** — a second Card/window/print/rim/plate rect on the design grid (or the
  existing ones re-expressed there, rounding checked), with accessors so the geometry keeps one
  owner. `t16_layout` gains the new pose.
- **`cassette_card.c`** — `mask_build()` takes a size instead of the constants; either two masks
  (Controls + full) or the porthole (§4e), which deletes the mask entirely. `place()` becomes
  pose-aware, and `cassette_card_window()` must return the *current* pose's window, since
  `pack_place()` clamps to it (`cassette_tapes.c:325-345`) and a stale window clips the reels to a
  hole that is not there — the exact bug ADR 0044 records.
- **`cassette_cover.c`** — the print follows the Card's rect instead of the constants; the mask is
  applied only while one exists; the fallback pill re-bases on the active window.
- **`cassette_tapes.c`** — nothing, if the window accessor is right: `cassette_tapes_set_window()`
  already takes the rect (`:439-448`) and `pack_place()` already clamps (`:319-349`).
- **`cassette_view.c`** — `cassette_place()` re-places Card and print on the pose while full;
  `page_set()`/`cassette_card_set_shown()` treat "full" as "Card shown" though the page stays dark
  (`:152-167`); the `CHANGED_FULL` bit is applied beside PAGE.
- **`controls.h` / `controls.c`** — the `full` flag, `CONTROLS_CHANGED_FULL = 1u << 5`, the
  transition rules and the `level != INITIAL` repair (§8). `t24_controls.c` gains its states.
- **`screen_input.h` / `.c`** — `SCREEN_INTENT_FULL_ON` / `_OFF`, the latch, and the early return
  in `screen_gesture_cb` while it is set. **No board symbol here** (§6.3).
- **`touch_gt911.c`** — the detector from `f6a1a4d^:80-108` returns, tuned per §6.4, with the
  wake-only guard and a callback registered by the adapter; `point[1]` finally gets a use. Keep the
  `[GDBG]` line until the gesture is proven, then decide.
- **`host/tests/layering.sh`** — A2 must still pass; it is the check that keeps the board symbol
  out of `screen_input.c`.

## 10. What to prototype first

1. **Nothing at all, until one press.** Flash the current build with a serial console, put two
   fingers on the panel, read `[GDBG] contacts=`. If it never prints 2, the feature is a
   panel/controller question and everything below is idle. Five minutes, no code. *MEASURED
   (board), to be recorded.*
2. **The porthole, at the Controls pose only** (§4e): it is the one change that deletes the mask
   rather than enlarging it, and it is testable at the size everything already is. Card without a
   mask, a 291x87 capsule with `clip_corner` + `radius 43` above the print, a child drawing the
   same rings, the existing 7 px rim on top. Two captures plus `CONFIG_LV_USE_PERF_MONITOR_LOG_MODE=y`
   `render` ms (`lv_sysmon.c:358-375`; how to read it in `panel-rgb-lvgl-performance.md` §8). If the
   picture is indistinguishable and `render` does not rise, the 116 KB is already free and a 384 KB
   mask never has to be allocated.
3. **One board reading of the tape's periodic spike**, before optimising it: open Real, play, watch
   `render` for a step every ~2 s — that is the fill change of §5.2. Small step: ten rings are fine
   and the answer to the tape question is "already done". Large step: the fix is the ring count, not
   a window.
4. **Only then** the full-screen Card: author the 760x440 rect on the design grid, pin it in
   `t16_layout`, and choose between two masks (≈670 KB of PSRAM — it fits, §3.1, but it is a
   fifth of the free heap for a hole) and the porthole (0 B, already built at the Controls pose
   on this branch).

## Sources

Repo (`components/app/screen_display/` unless noted; line ranges are the working tree's):

- `cassette_view.c` (`:152-167`, `:160`, `:164-166`, `:229-245`, `:232-234`), `cassette_card.c`,
  `cassette_card.h`, `cassette_cover.c` (`:64-88`, `:146-224`), `cassette_cover.h`
- `cassette_tapes.c` (`:263-306`, `:312-370`, `:390-448`, `:486-514`, `:518-606`),
  `cassette_tapes.h`, `screen_input.c`, `screen_input.h`, `touch_gt911.c` (`:50-126`, `:214-240`)
- `artwork_loader.c` (`:12-28`, `:60-110`, `:555-570`), `screen_display.c`, `screen_caps.h`,
  `screen_caps.c`
- `components/portable/controls/include/controls.h` (`:110-127`), `…/controls/controls.c` (`:22-58`,
  `:116-133`); `components/portable/cassette/include/cassette_layout.h`, `…/cassette_layout.c`,
  `…/cassette.c` (`:435-467`); `host/tests/t24_controls.c`, `host/tests/layering.sh` (A2)
- `sdkconfig.sunton`: `LV_DRAW_LAYER_SIMPLE_BUF_SIZE=24576`, `LV_CACHE_DEF_SIZE=0`,
  `LV_IMAGE_HEADER_CACHE_DEF_CNT=0`, `LV_DRAW_LAYER_MAX_MEMORY=0`, `LV_DRAW_SW_COMPLEX=y`,
  `LV_DEF_REFR_PERIOD=33`, `ESP_LCD_TOUCH_MAX_POINTS=5`
- `docs/adr/`: 0004, 0014, 0021, 0025, 0039, 0042, 0043, 0044 ·
  `docs/research/hub-drawn-vs-blitted.md` (§3, §5, §7) ·
  `docs/research/panel-rgb-lvgl-performance.md` (§2, §6 lever 7, §8)
- git: `f6a1a4d` and its parent (`git show f6a1a4d^:…/touch_gt911.c`, `…/screen_input.c`),
  `3958da5`

LVGL 9.6.0 (`managed_components/lvgl__lvgl/src/`): `draw/sw/lv_draw_sw_img.c` (`:101-118`,
`:991-1081`) · `core/lv_refr.c` (`:105-129`, `:159-257`, `:537-620`, `:1180-1212`) ·
`core/lv_obj_style.c` (`:1412-1424`) · `core/lv_obj.c` (`:1568-1580`) · `draw/lv_draw.c` (`:446-468`,
`:495-545`) · `draw/sw/lv_draw_sw_arc.c` (`:55-100`) · `draw/sw/lv_draw_sw_border.c` (`:49-79`,
`:85-240`) · `draw/sw/lv_draw_sw_mask_rect.c` (`:41-116`) ·
`draw/sw/blend/lv_draw_sw_blend_to_rgb565.c` (`:895-985`) · `image/lv_image_decoder.c` (`:113-188`) ·
`image/lv_bin_decoder.c` (`:179-300`, `:537-553`) · `widgets/image/lv_image.c` (`:820-900`)

Espressif: `espressif__esp_lcd_touch_gt911/esp_lcd_touch_gt911.c` (`:221-330`) ·
`espressif__esp_lcd_touch/esp_lcd_touch.c` (`:112-136`), `include/esp_lcd_touch.h` (`:76-90`)

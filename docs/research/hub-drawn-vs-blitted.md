# The hub: 36 blitted frames or LVGL-drawn primitives — what the space really is, and what the draw costs (2026-10-23)

> **Status: implemented.** Branch `ui/drawn-hub` replaces the blob with the drawn hub that
> §5 prices: a static rim, two opposite radii from r 20 to 82, and a dot off the divider.
> §1–§2 below describe the arrangement *before* that change — they are the baseline the
> measurements are against, and `hub_frames.bin` no longer exists in the tree.
> `docs/research/panel-rgb-lvgl-performance.md` is a dated snapshot of an earlier build and
> still cites the blob for the same reason.

Axis: **the reels are animated by blitting a 96x96 RGB565 crop out of a 36-frame blob. The proposal
is to delete the blob and draw each hub as LVGL geometry — a 168 px circle of width 2 with two
opposite circles inside. This note answers the two halves of that trade separately: how much space is
actually freed, and what the drawing costs per cycle — and shows that the answer to the second half
depends on how the animation is structured, not on the primitives chosen.**

Everything below is read out of artefacts on this machine: this repository's sources, the LVGL 9.6.0
tree vendored into `managed_components/lvgl__lvgl/`, and the Sunton link map. No `idf.py` was run and
nothing was flashed while writing it.

Four kinds of number appear here and they are never mixed:

- **MEASURED (board)** — printed by the firmware's own telemetry on the board. **This note contains
  none of these.** Where it needs a board number it inherits it from
  `docs/research/panel-rgb-lvgl-performance.md` and says so at that point.
- **MEASURED (link)** — read out of `build_sunton/sd_stream_server.map` and
  `build_sunton/sd_stream_server.bin`, both named at the point of use.
- **DOCUMENTED** — a line of source, a header comment, a Kconfig `help` string, or a `.rst` page,
  always cited by file and line.
- **ARITHMETIC / ESTIMATE** — computed here from DOCUMENTED inputs cited next to it. An ESTIMATE
  states the rate it assumes; nothing in this note presents one as a measurement.

`build_sunton/config/sdkconfig.h` and `sdkconfig.sunton` are rewritten by every build, so they are
cited by the `CONFIG_…` symbol they carry. Hand-written sources are cited by `file:line`.

## Bottom line

1. **The space is real, and it is paid twice.** `assets/hub_frames.bin` is 663 552 B: 15.8 % of the
   4 MB app partition and 19 % of the current 3 488 656 B image (§2). Because
   `CONFIG_SPIRAM_RODATA=y`, it is also **resident in PSRAM at runtime** — the map puts its first
   byte at `0x3c155fec`, and the cost is the 648 KiB of PSRAM the framebuffers and the library arena
   share. Deleting it frees both copies.
2. **The drawn ring is cheap; the drawn *hub* is not.** LVGL 9.6 special-cases a full 360° arc into
   `lv_draw_sw_border()`, which clips to the invalidated area and works one scanline at a time
   (`lv_draw_sw_arc.c:66-78`, `lv_draw_sw_border.c:88-98`). So "a 168 px circle, width 2" costs the
   ~1 100 px of ink in the ring, not the 28 224 px of its bounding box (§5).
3. **The animation, not the shape, decides the cost — and it is not a wash with the blit.** A 2 px ring
   is not cheaper per pixel: every drawn pixel pays a radius mask and a 16→16 channel mix, 1.5–4× the
   cost of the memcpy the blit uses (§4). What varies is how many pixels a cycle touches. Two opposite
   holes as two objects that *move* invalidate 8 patches of ~900 px — a fifth of the bytes the current
   two 18 432 B blits move, i.e. **~1.5× faster**. Repainting the whole 168 px hub each frame instead
   invalidates 56 448 px — ~3× the blit's traffic at that higher rate, i.e. **~3–5× slower** (§5, §6).
   So: no perf win to be had here; a perf *knob*, which the blit does not have.
4. **Both structures fit the budget.** By ESTIMATE (§5) the moving-holes version lands at or below the
   current blit and the full-repaint version at several ms per cycle, against a 33 ms refresh period
   (`CONFIG_LV_DEF_REFR_PERIOD`) with the UI polling at 40 ms (`screen_display.c:61`). Neither is the
   reason to refuse the change.
5. **What you would actually be trading is the look, not the space.** The art in `assets/hub/` is a
   1 565-colour rendered hub — six teeth, a sheen, four concentric rings, an orange index mark (§1).
   "A circle of width 2 with two circles inside" is a different, flatter hub: a design decision worth
   taking deliberately, not a like-for-like re-encoding.
6. **There are cheaper ways to keep the art** — and the two obvious encodings are traps. Indexed I8/I4
   cannot be drawn in this build at all (§7), and enabling it costs more PSRAM than it saves flash. But
   the art is 97.5 % achromatic, so **L8 halves the blob** for a barely visible change, and generating
   the rotations at boot from one stored frame frees almost all of it with no change to the picture
   (§7). Both are the honest comparison for a drawn hub.

## 1. What the hub was (the baseline this note measures against)

| Object | Value | Source |
|---|---|---|
| Source art | 36 RGBA PNGs, 168x168, one revolution | `assets/hub/001.png` … `036.png` |
| Colours in one frame | **1 565 unique** RGBA values | MEASURED (link): counted over `assets/hub/001.png` |
| Stored | only the 96x96 centre of each frame, opaque RGB565 | `include/hub_frames.h:9-22` |
| Blob | `assets/hub_frames.bin`, 36 × 18 432 = **663 552 B** | MEASURED (link): file size, `hub_frames.h:16` |
| Descriptors | `k_hub_crops[36]`, all pointing into the one blob | `include/hub_frames.h:27-33` |
| Static disc behind | a flat `COL_HUB_RING` 168 px circle, drawn once | `cassette_view.c:1024` |
| Frame swap | `lv_image_set_src(s_hub[i], &k_hub_crops[n])` | `cassette_view.c:625-628` |
| Geometry | hub centre 210/594,226; ring 168; crop 96 | `cassette_view.c:64-68` |

The design of that blob is already a space optimisation, and the file says so: only the 96 px centre
is exported, and no 168 px frame is stored or rotated (`hub_frames.h:18-22`). The reasoning is
explicit — `lv_image_set_rotation()` is LVGL's per-pixel software transform and was measured at over
100 ms per frame on this part, so the frame sequence *is* the rotation (`cassette_view.c:30-34`).

**The 36 is not arbitrary, and it should not be reduced offhand.** The model derives the hub index
from tape geometry: the tape moves at 47.625 mm/s over a hub of radius 11 mm
(`include/cassette.h:38-40`), so

- ω = 47.625 / 11 = 4.3295 rad/s → one revolution in **1.451 s** (ARITHMETIC from those two constants);
- 36 frames over 1.451 s = **one new frame every 40.31 ms** (ARITHMETIC).

The UI polls at `UI_FRAME_MS` = 40 ms and sets the LVGL refresh timer to the same period
(`screen_display.c:61`, `:343`). The frame count is matched to the refresh cadence: at playback speed
exactly one new frame is due per tick, 10° of rotation. Halving the count would make the hub step 20°
every other tick and read as judder, not as rotation.

## 2. What 663 552 B actually costs

| Budget | Now | Without the hub blob | Source |
|---|---|---|---|
| App partition | 4 194 304 B | 4 194 304 B | `partitions.csv` (4M at 0x10000) |
| Image | 3 488 656 B (83.2 % used) | 2 825 104 B (67.4 % used) | MEASURED (link): `build_sunton/sd_stream_server.bin` |
| Free headroom | **705 648 B** | **1 369 200 B** | ARITHMETIC |
| Hub blob, share of partition | 15.8 % | — | ARITHMETIC |
| Hub blob, share of image | 19.0 % | — | ARITHMETIC |

That headroom is thinner than the ADR that sized the partition assumed: `docs/adr/0004-ui-memory-is-external.md:52`
records the 4 MB choice against an image of 1.28 MB with "68 % is free". The image is 2.7× larger now;
most of the growth is embedded art — the label cover is 1 152 000 B, the hub 663 552 B, the Artwork
backdrop 45 051 B.

**The second copy is the one that is easy to miss.** The blob does not stay in flash: with
`CONFIG_SPIRAM_RODATA=y` the linker places `.rodata` in external RAM, and the map confirms it —

```
.rodata.embedded  0x3c155fec  0xa2004  libscreen_display.a(hub_frames.bin.S.obj)
                  0x3c155fec  _binary_hub_frames_bin_start
                  0x3c1f7fec  _binary_hub_frames_bin_end
```

(`build_sunton/sd_stream_server.map:91709-91713`, MEASURED (link); `0xa2004` = 663 556 B with
alignment). The address is in the ESP32-S3's 0x3C000000 external-RAM window, so the blob occupies
648 KiB of the 8 MB PSRAM the framebuffers, the LVGL heap and the library arena also draw on. On this
board PSRAM is the contended resource — the scan-out alone reads 29.98 MB/s out of it
(`docs/research/panel-rgb-lvgl-performance.md` §2) — so freeing 648 KiB there may matter more than
the flash arithmetic that usually gets quoted.

## 3. What the drawn hub would be

As stated: a 168 px circle of stroke width 2, with two opposite circles inside. In LVGL terms that is
either

- **one `lv_obj` with `radius = LV_RADIUS_CIRCLE`, `border_width = 2`, `bg_opa = 0`**, plus two small
  child objects inside it, or
- **`lv_draw_arc()` calls in a draw callback**, the way the VU and the tape edge are already drawn
  (`cassette_view.c:405-424`, `:456-466`, `:488-513`).

Both reach the same software renderer, and the renderer's shape matters more than the API:

| Shape | Code path | Cost is proportional to |
|---|---|---|
| Full 360° ring, any width | `lv_draw_sw_arc.c:66-78` → `lv_draw_sw_border()` → `draw_border_complex()` | the **ring's ink**, clipped to the invalidated area, one scanline mask at a time |
| Arc fragment (e.g. the VU dashes) | `lv_draw_sw_arc.c:98-141` — angle mask + outer/inner radius masks | the arc's **bounded area** intersected with the clip area |
| Filled circle (the existing `disc()`) | `lv_draw_sw_fill.c:86` — radius mask over the filled area | the **whole disc** |

The full-ring special case is the decisive line for this design: `dsc->start_angle + 360 == dsc->end_angle`
is converted into `lv_draw_border_dsc_t` with `radius = LV_RADIUS_CIRCLE` and handed to
`lv_draw_sw_border()` (`lv_draw_sw_arc.c:67-77`). `draw_border_complex()` then intersects with the
draw task's clip area and allocates a **single scanline** of mask (`lv_draw_sw_border.c:92`, `:97`) —
not a 168×168 mask. A 2 px ring therefore costs the ~1 100 px of ink in it, and when only a 30×30
patch is invalidated it costs only that patch.

The circle descriptors behind those masks are cached by radius, four deep
(`LV_DRAW_SW_CIRCLE_CACHE_SIZE` = 4, `lv_conf_internal.h:710-714`), and computed by `circ_calc_aa4()`
into a per-radius table when missing (`lv_draw_sw_mask.c:1211-1226`). This UI already spends that
cache on the pack discs and the VU's radii, so adding hub radii is a real (if small) perturbation
worth watching rather than a free operation.

## 4. The current blit, in the same currency

An opaque RGB565 image with `opa = LV_OPA_COVER` and no mask takes the cheapest possible path: one
`lv_memcpy` per row, no per-pixel work at all (`lv_draw_sw_blend_to_rgb565.c:898-907`). So the frame
swap moves

- 18 432 B read from PSRAM (the blob, §2) + 18 432 B written to the PSRAM framebuffer, per hub;
- **73 728 B per cycle** for two hubs (ARITHMETIC), plus the invalid area's bookkeeping.

That is the floor the drawn hub has to beat, and it is a hard floor: memcpy is the best case this
renderer offers for any RGB565 source.

## 5. The three structures, per cycle

Numbers are pixels invalidated per cycle at 25 Hz and the PSRAM traffic they imply. Pixel counts are
ARITHMETIC from the geometry in §1 and §3; the per-pixel rate is an ESTIMATE, stated below.

| | Pixels invalidated per cycle | PSRAM bytes per cycle | ESTIMATE per cycle |
|---|---|---|---|
| **C. Today** — 2 × 96x96 memcpys | 18 432 (2 × 9 216) | 73 728 | 0.7–1.5 ms |
| **B. Static ring + 2 moving holes** | ~7 200 (2 hubs × 2 holes × old∪new ≈ 30×30) | ~14 400 | 0.4–1.1 ms |
| **A. Whole hub repainted each frame** | 56 448 (2 × 168×168) | ~225 800 | 3.4–8.5 ms |

- The estimate applies two rates at 240 MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`): **40–80 ns per
  pixel** for C, which is memory-bound (4 B of PSRAM traffic per pixel and no arithmetic,
  §4), and **60–150 ns per blended pixel** for A and B, which pay the radius-mask lookup and the 16→16
  channel mix on top of that traffic. The 1 536 000 B full-screen cycle already documented for this
  board is the same order (`docs/research/panel-rgb-lvgl-performance.md` §10, lever 7). It is an
  ESTIMATE — no per-pixel rate has been measured on this part — and the bands are deliberately wide.
  The ordering is not sensitive to them: B stays at or below C, and A stays several times above both.
- **B is the family of structures that can beat the blit**, and all of them need the static part to be
  truly static: the rim is drawn once at build and never invalidated again, so a cycle touches only the
  patches the moving marks vacate and occupy. How well it beats the blit depends on what moves — see
  the table below.
- **A is what a naive implementation produces.** Changing anything about the hub object itself —
  `lv_obj_invalidate(hub)`, or redrawing the ring's start angle to fake rotation — invalidates its
  whole 168×168 area — 3.06× the area of the current crop, at an order of magnitude more work per
  pixel. That is the one configuration clearly worse than what ships today.
- Neither B nor A trips LVGL's area joining: `lv_inv_area()` keeps up to `LV_INV_BUF_SIZE` = 32
  separate rectangles (`lv_display_private.h:29-30`, `lv_refr.c:331-344`) and `lv_refr_join_area()`
  merges two only if their rectangles actually intersect (`lv_refr.c:655-668`, `lv_area.c:261-271`).
  Two opposite holes are far apart, so they stay two cheap patches — **unless** the implementation
  gives them a single parent object and invalidates that parent, which puts you back in structure A.

### What the rotating element costs — the only number that matters

Every candidate design is a static rim plus *something that turns*. The something is what you pay for,
and the payment is its **bounding box**, not its ink, because `lv_obj_invalidate()` invalidates an
object's area and `lv_line` resizes itself to the extent of its points on every update
(`lv_line.c:172-184`: `lv_obj_refresh_self_size()` then `lv_obj_invalidate()`). Union of the old and new
boxes over one 10° tick, at 2 px, hub radius 84:

| The rotating element | bbox per tick | 2 hubs / cycle | ESTIMATE |
|---|---|---|---|
| A dot, 8 px, at r = 42 (moves 7.3 px) | ~234 px | 468 px | **0.03–0.07 ms** |
| A radial mark, 26 px long at r 59…85 | ~690 px | 1 380 px | 0.08–0.21 ms |
| A radius, centre → rim, one object | ~3 100 px | 6 200 px | 0.37–0.93 ms |
| A diameter, both halves in **one** object | ~11 950 px | 23 900 px | 1.4–3.6 ms |
| The same diameter as **two opposite radii, two objects** | ~6 200 px | 12 400 px | 0.75–1.9 ms |
| The whole hub repainted (structure A) | 28 224 px | 56 448 px | 3.4–8.5 ms |
| *today, for reference: 2 × 96x96 memcpy* | 18 432 px | 18 432 px | 0.74–1.5 ms |

The consequences that are easy to get wrong:

- **A diameter is the expensive way to draw a line.** Its box is the hub's full extent (119×119 at 45°)
  even though the line is 2 px thick. Split into two opposite radii in **two objects** and the cost
  nearly halves — 6 200 px against 11 950 — because each object is clipped to its own quadrant and the
  two boxes are disjoint, so `lv_refr_join_area()` never merges them (`lv_refr.c:655-668`).
- **A dot is essentially free**, and a short mark nearly so. Any design whose only moving piece is a
  small mark, or a small mark plus one radius, lands *below* today's blit.
- Shortening the inner ends of the line does shrink every box, but by less than the line's length
  suggests: at r 20…84 the same two-radius divider is **14 400 → 10 100 px/cycle** (a factor of 1.45,
  not the ~3 an eyeball estimate gives), because a tick's box is the *old ∪ new* swept box and the
  segment still reaches the rim. Computed from the code's geometry, both hubs, 4 spokes + 2 dots.

**The design at issue — a rim, a dividing line and a dot in one half — therefore prices as:**

| Variant (r 20…84, 2 hubs, per 10° tick) | px/cycle | vs today's 18 432 px | ESTIMATE |
|---|---|---|---|
| divider as **one** `lv_line` + dot | ~24 400 | 1.3× more | 1.5–3.7 ms |
| **as built: two opposite radii, two objects, + dot** | **~10 100** | **1.8× less** | **0.6–1.5 ms** |
| divider **static**, only the dot turns | ~500 | 37× less | 0.03–0.08 ms |

The middle row is what this branch builds. Note how the two currencies disagree, and why: it moves
**1.8× fewer pixels** than the blit, but a drawn pixel costs 60–150 ns against the memcpy's 40–80 ns, so
in *time* the change is roughly a wash (0.6–1.5 ms against 0.74–1.5 ms). The gain here is the 648 KiB,
not the microseconds — and in pixels the insetting from r 0…84 to r 20…84 is worth 14 400 → 10 100, a
factor of 1.45.

The last row is the cheapest possible animation and still reads as rotation — but a static divider with
an orbiting dot reads as a dial rather than a hub that turns, so it is a look decision, not an
optimisation one.

For the six-mark variant (a rim, a crosshair and six radial marks), the marks are ~690 px each and the
crosshair is 4-fold symmetric — it carries no rotation cue and can be painted once, so the whole
animation is ~1 380 px/cycle, ~0.08–0.21 ms.

## 6. What A versus B is really betting on

A draws the rotation by redrawing geometry; B draws it by moving small objects. B is not a
simplification of the art — it is a different animation model:

- **B does not strobe, and the reason is worth stating correctly.** A rotating symmetric pattern only
  reads backwards when the per-frame step approaches half its repeat period. Two opposite holes repeat
  every 180°, so ambiguity would need ~90° per frame; six marks repeat every 60°, so ~30°. The UI
  steps 10° per frame (§1), comfortably inside both — which is why the art's orange marker
  (`assets/hub/001.png`) is an aesthetic of the real hub, not a fix for an animation bug. What a
  two-hole version actually loses is the 6-fold tooth pattern, which is the visual signature of a
  cassette hub, not a directional cue.
- **A rotates everything, including the teeth.** If the drawn hub keeps a rotating pattern, A must
  redraw it every frame; B splits it into one small object per mark, each invalidating only its own
  patch — fine at six marks (≈ 10 800 px/cycle, §5) but no longer cheaper than C once every ring,
  tooth and sheen becomes an object.

The trade is therefore: **the geometry you can draw cheaply is the geometry that has little to
rotate**, and the art's own detail is what makes the blit unnecessary.

**What B looks like in code**, because the difference between B and A is two API calls:

```c
/* once, at build: the ring, drawn only when the screen is first painted */
lv_obj_t *ring = lv_obj_create(scr);
lv_obj_remove_style_all(ring);
lv_obj_set_size(ring, 168, 168);
lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
lv_obj_set_style_border_width(ring, 2, 0);
lv_obj_set_style_border_color(ring, lv_color_hex(0x4A4A49), 0);

/* per tick: move the two holes; LVGL invalidates only their old and new boxes */
lv_obj_set_pos(hole[i][k], cx + lroundf(r * cosf(a)) - h / 2,
                             cy + lroundf(r * sinf(a)) - h / 2);

/* the trap that turns B into A: */
/* lv_obj_invalidate(hub);  -- do not do this */
```

The one alternative worth knowing: if the ring and the holes must stay a **single** object (a styled
draw callback, the way `pack_draw_cb` works), the precise patches still survive — invalidate them
explicitly with `lv_obj_invalidate_area(obj, &patch)` per hole instead of invalidating the object.
Either way the rule is the same: the invalidated rectangles decide the cost, and they must be the
holes, never the hub.

## 7. The options that keep the art

If the look matters more than the last few hundred kilobytes, the blob can be shrunk rather than
replaced. The obvious encodings are mostly dead ends in this build; two options survive:

- **Indexed colour (I8/I4) is not available here.** `LV_BIN_DECODER_RAM_LOAD` is 0 in this
  configuration (`# CONFIG_LV_BIN_DECODER_RAM_LOAD is not set`), and `load_indexed()` refuses outright
  when it is: *"LV_BIN_DECODER_RAM_LOAD is disabled"*, returning `LV_RESULT_INVALID`
  (`lv_bin_decoder.c:696-702`). Turning it on is worse than the status quo: the indexed image is then
  expanded to ARGB8888 *"since sw renderer cannot render it directly"* (`lv_bin_decoder.c:651-669`) —
  96×96×4 = 36 864 B per frame, i.e. ~1.3 MB of PSRAM for all 36, against 331 776 B of flash saved.
- **Compressed images are off too.** `CONFIG_LV_USE_LZ4` is not set, and the compressed-image path
  needs it (`lv_bin_decoder.c:16-18`, `:588`).
- **L8 does work, and the art is nearly achromatic.** The hub crop has a mean saturation of **6.5/255**
  and only **113 of 9 216 px** above saturation 80 — the orange index mark (MEASURED (link), same
  crops). Converting to luminance and back gives a mean absolute error of **1.36/255** with 0.92 % of
  pixels above 16, so 8 bits per pixel keeps the teeth, the sheen and the anti-aliased edges at
  **331 776 B for all 36 frames** — half. L8 also has a real blend path
  (`lv_draw_sw_blend_to_rgb565.c:759`, `l8_to_rgb565()`), not a decoder round trip. What it costs: the
  orange marker becomes grey, and the per-pixel conversion replaces the memcpy of §4. One small
  orange bar drawn in a callback would put the marker back.
- **Generate the 35 rotations at boot from one stored frame.** One 96×96 crop is 18 432 B; rotating it
  35 times into a PSRAM buffer frees **645 120 B of flash** and keeps the art exactly, at today's PSRAM
  cost plus a boot-time pass. It is the only option here that removes almost all the flash without
  touching the picture; it is also the only one whose cost has not been bounded on the board (LVGL's
  own `lv_image_set_rotation()` is far too slow at ~100 ms per frame, `cassette_view.c:30-34`, so this
  needs a purpose-written rotator, and a nearest-neighbour one would re-alias the art's edges).
- **Fewer frames** is the wrong lever (§1): 18 frames would step 20° per tick.
- **Smaller crop** buys nothing: the union of consecutive-frame differences covers the whole 96×96
  crop, because the entire disc turns (MEASURED (link), same analysis).

The numbers to hold the drawn hub against: deletion saves 100 % of the blob, L8 saves 50 % and keeps
the picture minus one hue, boot-time rotation saves ~97 % and keeps the picture exactly.

## 8. The one measurement that settles the rendering half

The firmware already carries the instrument. With

```
CONFIG_LV_USE_PERF_MONITOR_LOG_MODE=y
```

the `sysmon:` lines print `total ms (render | flush)` with `refr_cnt`/`redraw_cnt`
(`docs/research/panel-rgb-lvgl-performance.md` §10, `lv_sysmon.c:358-375`). Take three readings on the
same screen and playback state:

1. today's blitted hub;
2. structure B — ring drawn once, two holes moving;
3. structure A — whole hub invalidated each frame.

What matters is `render` ms, and whether `redraw_cnt` rises with `refr_cnt`. If B lands at or under
the current `render`, the space is free and the change is a clear win; if A is where the first
implementation lands, the ~3.4–8.5 ms ESTIMATE above predicts a visible drop in headroom for the
encoder and the network stack that share this core.

## 9. Verdict

- **Space: yes, and more than the flash figure suggests.** 648 KiB off a partition that is 83 % full,
  and the same 648 KiB out of contended PSRAM. That is the strongest part of the proposal and it does
  not depend on any measurement.
- **Rendering: not free, but not the obstacle — provided the ring is static and only the holes move.**
  The full-circle special case keeps the ring's cost proportional to its ink; the danger is structure,
  not primitives. The design rule that comes out of it: *never invalidate the hub object itself.*
- **The real cost is the picture.** The proposal replaces 1 565 colours, six teeth, a sheen and an
  index mark with a flat two-hole spindle. If that is the intended new look, the space win is
  essentially free; if the art is meant to survive, L8 (§7) buys half the blob back with no
  measurable loss, and boot-time rotation buys nearly all of it at a complexity cost.

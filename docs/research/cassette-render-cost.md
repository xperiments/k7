# What every cassette area costs to draw: the two poses, the rectangle count and the mask's tax (2026-11-09)

> **Status: measured on the board.** The Sunton 8048S050C ran an instrumented build of this
> tree (the bench in `components/app/screen_display/cassette_bench.h`, behind
> `CONFIG_APP_UI_RENDER_BENCH`, off in every normal build) on 2026-11-09. Two idle runs and one
> run with a Track playing were captured from the serial log; the tables below are those logs,
> not estimates. The geometry every area is measured on was read out of the source and is cited
> where it is used.

Axis: **the cassette draws in many separate areas - two rotating hubs, two pack rings, two
strands, the label, the Card and its mask, the window, the chrome - and the owner wants to know
which of them actually costs the frame.** This note prices each area on its own, at the default
entry screen and at the two-finger pose, and separates what the panel wait adds. It also prices
the two real animation steps (a hub tick and a tape-fill tick), the frame the two-finger pose
costs to enter, and the direct-mode buffer copy that a one-off change leaves for the next frame.

Four kinds of number appear and are never mixed:

- **MEASURED (board)** - the bench's own `bench:` lines, printed over the serial console by the
  firmware on the panel. Every number in the tables is this kind unless it says otherwise.
- **DOCUMENTED** - a line of source, a Kconfig value or a header comment, cited `file:line`.
- **ARITHMETIC** - a value computed here from MEASURED or DOCUMENTED inputs cited beside it.
  The multiplication is mine and is marked.
- **MEASURED (link)** - none: this note prices drawing at run time, and reads no linker map.

The bench's log is not committed (it is a serial capture); the instrument and the commands that
produce it are (§7).

## Bottom line

1. **The two-finger pose's cost is the mask, not the ink.** Every area measured there lands at
   **1.4-1.7 us per invalidated pixel** (the small ones pay more fixed cost), against
   **0.23-0.5 us/px** at the default screen: the Card's A8 mask and the print's copy of it are
   applied to the whole invalidated rectangle, whatever is drawn in it. The label's words are
   within the noise of the blank design: **431 ms** blank (idle run 2) against a filled label
   that normalises to about **445 ms** (1274 ms while playing, divided by the 2.86x the Card
   showed in the same run). The label is a **~0.42-0.45 s** area because it is 748x359 of
   masked print, not because of its type.
2. **The full panel is 112-142 ms on the default screen and ~545 ms at the two-finger pose
   (idle); 1.54 s while a Track plays.** The two-finger pose's whole-screen frame and its Card
   frame measure the same **544.7 ms**: the Card's 814x520 box is clipped to the 800x480 panel,
   and LVGL then redraws the screen's area for it. Entering the pose is **548 ms idle** and
   **2.09 s with a Track playing** - the `2.3 s` the mask draw is reported at in `d4768bc` is the
   playing number, reproduced here (§4.3).
3. **The rectangle count, not the pixel count, is what the small animations pay.** A hub tick
   dirties up to **8 rectangles of about 13x8 px** (2 spokes x old+new x 2 hubs); it costs
   **9.4-21.5 ms** at the default screen and **20-35 ms** at the two-finger pose. One hub rim of
   29,584 px costs 14-20 ms: the tick pays almost as much for ~800 px as a rim does for 29,584,
   because each rectangle is a separate `refr_area()` pass, layer set-up and flush call
   (`lv_refr.c:805-860`, `:1459`). A tape-fill tick dirties 4-5 boxes and costs **77-98 ms**
   (default) / **183-386 ms** (full).
4. **Direct-mode double buffering charges a changed area a second time, on the next frame.**
   LVGL copies the previous frame's dirty areas into the buffer it is about to draw into
   (`lv_refr.c:677-787`): measured at **0.14-0.47 us/px**, i.e. **40-125 ms** for a Card- or
   screen-sized area. A repeating tick escapes it - the next tick re-covers the same boxes and
   `lv_refr_join_area`/`refr_sync_areas` subtracts them - but a one-off change (label, page flip,
   pose entry) pays the draw and then the copy.
5. **While a Track plays, the two-finger pose's long frames stretch ~2.8-3.8x** (Card 545 ms ->
   1.56 s, label 431 ms -> 1.27 s, screen 545 ms -> 1.54 s), while the default screen's short
   frames stretch only ~1.3x (screen 112 ms -> 153 ms). The device's own report in the same run
   is **70.4% of one core** for the encoder (`icecast: encoder: 1126 frames, 18391 us/frame`);
   LVGL's refresh task shares core 0 with it. The bench itself starves the stream hard enough to
   trip the task watchdog during the 1.5 s frames - an artefact of the instrument, not a
   shipping claim.
6. **The recurring bill at the default screen is the hub tick.** At the model's cadence (36
   frames a revolution, `cassette_tapes.h:43`; 1.5-3 s a revolution, `cassette_view.c:360-370`:
   one frame every 42-83 ms) a tick costs ~19 ms draw + ~13 ms panel wait, so 10-24 ticks/s is
   **0.3-0.7 of a core** before the decoder's share (ARITHMETIC from the two measured costs).
   The fill, at ~1 s cadence, is another ~0.1 core.
7. **The lever is the number of dirty rectangles and whether the Card is up, not the pixels.**
   For the hub, fewer rectangles is the only attractive direction - a whole-rim repaint is
   14-20 ms for one hub at the default screen (vs 9-21 ms for both hubs' 6-8 tiny boxes), and
   47-80 ms per hub at the two-finger pose (vs 20-35 ms). For the two-finger pose, every
   redraw is expensive because the mask is up: keeping the pose off the film, or keeping the
   tape still under it, is worth more than any raster tweak.

## 1. The instrument

`CONFIG_APP_UI_RENDER_BENCH` (Kconfig, `components/app/screen_display/Kconfig`) builds
`render_bench.c` and the view's bench hooks (`cassette_view.c:877-1032`). A build-time constant
after the screen is built (6, 20 and 40 s in the three runs; `BENCH_START_MS`) it:

1. **freezes the view** - `cassette_view_update()` returns before it steps the model or applies a
   frame (`cassette_view.c:804`), so no station update invalidates anything between samples;
2. pauses the performance monitor if one is built, and hides its label;
3. puts the picture in the pose through the same intents a finger sends
   (`cassette_bench_pose()` -> `screen_intent_cb()`, `cassette_view.c:896-904`);
4. fires one area at a time and times the refresh cycle LVGL answers with.

The clock is LVGL's own event stream (`managed_components/lvgl__lvgl/src/core/lv_refr.c`):

| Event | Where it is sent | What the bench does with it |
|---|---|---|
| `LV_EVENT_REFR_START` | `:403` | starts the clock: layout, buffer sync and draw all follow |
| `LV_EVENT_RENDER_START` | `:802` | (kept for the record; not used as t0) |
| `LV_EVENT_RENDER_READY` | `:870` | closes the cycle |
| `LV_EVENT_FLUSH_START` | `:1459` | starts a flush |
| `LV_EVENT_FLUSH_FINISH` | `:1481` | ends it |

**`draw` = (REFR_START -> RENDER_READY) - sum(flush)**. It includes the layout pass, the
direct-mode buffer sync (§4.4) and every object LVGL walks for the invalidated rectangle.
**`flush`** is the panel wait. In direct mode the port's flush callback pushes the *whole*
framebuffer only on the last invalidated area of the cycle and then blocks on the RGB frame's
completion (`esp_lvgl_port_disp.c:750-755`), so one rendered cycle pays one panel wait whatever
its rectangle count; `nflush` counts the rectangles, not the waits.
**`total`** = draw + flush = the frame's wall time.

**A frame is one sample, and a step is several samples.** Most areas take 6 samples (3-4 for
areas over 120,000 px, because their frames are long) and the table shows their mean and their
min and max. The min is the least-contended sample on a board that is also running Wi-Fi and, in
one run, a failing SD-card retry; it is the cleaner number for a per-frame cost, and the mean is
what the device actually did.

**The 1x1 drain.** A step's samples are separated by one frame that invalidates a single pixel
and is thrown away (`bench_sync_drain()`, `render_bench.c:231-238`). Its only job is to spend the
previous area's buffer sync (§4.4) on a frame nobody measures; without it, the label's 123 ms
copy would land in the Card's sample. The drain's own `sync:<area>` line is the measurement of
that copy.

**What is measured is a rectangle, not an object.** The bench invalidates the box the area's
animation dirties and lets LVGL redraw whatever is in it: the tape under the Card, both masked
layers, the chrome on top. That is the cost a change in that area really pays. It also means the
rect count is part of the answer (§3).

**Content.** In idle run 2 the SD card was out (the reader retried and failed), so the
cassette held its no-Track state: fills at 80/1000, hubs at frame 0, and the label was the boot
design ("retro", `label: design 1 of 3`) with blank slot fields. The playing run had the card,
the Track and its real label ("Black Stripes", `label: design 0 of 3`), with the bench's fixture
writing a sample title into every slot (`cassette_label_bench_fill()`,
`cassette_label.c:252-259`) so the label's text objects were full. The playing run's build adds
the label-text step and the fixture; neither changes the tape, the Card or the chrome.

## 2. What is on the screen, and where it is

At the entry pose the design grid is the panel, pixel for pixel (scale 256, `cassette_layout.c`
`cassette_origin_x/y`), so every design coordinate below is a screen coordinate. The composition
is built in `cassette_view.c:684-790`; the Card's two layouts are `cassette_card.c:52-63`.

| Area | What paints it | Rect at the entry pose |
|---|---|---|
| left / right hub rim | `s_rim[0..1]`, disc + 2 px border (`cassette_tapes.c:920-924`) | 168x168 at 124,122 and 510,122; the module's dirt box is 172x172 (`hub_box()`) |
| hub spokes | two `lv_line`s per hub, moved on a frame change (`cassette_tapes.c:392-432`) | two boxes about 13x8 each at r 66..72 |
| left / right pack | one object each, every ring in its draw callback (`cassette_tapes.c:501-547`) | the fill's diameter, e.g. 190x190 at fill 80 |
| left / right strand | one polyline object each (`cassette_tapes.c:185-239`) | about 104x283 / 102x283 |
| tape body | the tape ink box (`cassette_tape_ink()`, `cassette_layout.c:203-208`) | 16,16,768x444 |
| the Card | `s_card`, A8-masked (`CARD_MASK 1`, `cassette_card.c:21`) | layout 0: 136,82,426x272; layout 1: -8,-23,814x520 |
| print container | `s_masked`, the same mask (`cassette_cover.c:87-137`) | = the Card's rect |
| print / label | `s_print`, and the label's root inside it (`cassette_label.c:214-226`) | layout 0: 148,102,402x188; layout 1: 25,19,748x359 |
| Card window | the mask's zeroed stadium | layout 0: 203,157,291x87; layout 1: 120,120,556x166 |
| card front plate | `s_plate`, two triangles (`cassette_card.c:159-190`) | inside the Card |
| header / footer / bars / meter | chrome, hidden at Initial | 110,6,674x72 / 16,358,768x116 / ... |

The chrome (header, footer, the window frame, the two bars, the VU meter) is hidden at both
poses the owner asked about: `chrome_shown_now()` is true only at Art and Real
(`cassette_view.c:391-394`), and the two-finger pose is only reachable from Initial
(`controls.c:88-90`). The VU meter is not measured here; it is a Controls-pose area.

**The two poses are the same pose.** The two-finger flag does not scale or move the cassette: it
is Initial plus the white page, the Card at layout 1, the label, and `set_preview()` on the tape
(`cassette_view.c:211-230`). So the hub and the strands are at the same pixels in both; what
changes is the Card's box over them and the pack clamp to the window
(`cassette_tapes.c:580-611`).

At the two-finger pose the pack is not the ring stack: `pack_draw_rings()` paints one edge and,
under it, a **filled disc** in the darker red (`cassette_tapes.c:511-540`), so the reel reads as a
wound body. That disc is what the A/B in §4.6 takes away; the default screen's ten rings are not
drawn at this pose.

### 2.1 Why the Card is 814x520 on an 800x480 panel

The Card is not the panel: it is a full-bleed plastic face whose moulded corners are meant to
sit outside the glass. At the Controls pose it is 426x272 at (136,82) - the tape's ink box,
402x232, grown by a 12 px side and 20 px top/bottom mat (`docs/specs/cassette-card.md`). The
two-finger pose puts the same design on the entry grid, scale 256 instead of 134: 426 x 256/134 =
813.97 -> **814** and 272 x 256/134 = 519.64 -> **520**, at (-8,-23) (`cassette_card.c:52-63`).
It overhangs the panel by 8 px on the left, 6 on the right, 23 on the top and 17 on the bottom,
so its 23 px corner arcs land outside the screen and no card edge is visible at the panel's
corners.

The mask has to be the object's own box. LVGL aligns a bitmap mask to the centre of the object's
area (`lv_draw_sw_img.c:1021`), and the two masked objects - the Card and the print's container -
share one buffer, so they must share one centre. Cropping the mask to 800x480 would move its
alignment by the 8/23 px the Card overhangs and would bring the four corner arcs inside the screen
(the white page would show in four 23 px notches). The mask also has to be A8 or L8:
`apply_mask()` rejects anything else (`lv_draw_sw_img.c:1006-1008`). At stride 816, 814x520 is
**424,320 B**; the Controls mask is 116,416 B; both are built in `cassette_card_create()`
(`cassette_card.c:204-209`), so **540,736 B is resident from boot** whether the pose is ever
opened or not. That is the real cost of the overhang.

What the panel does not see costs nothing per frame. The invalidated area is clipped to the
800x480 screen, and `apply_mask()` only walks the intersection of the mask with the layer's
rendered band (`lv_draw_sw_img.c:1029-1032`): the Card's 423,280 px box costs the 384,000 visible
pixels, which is why the Card and the whole-screen rows in §4.2 read the same 544.7 ms, and why
the per-pixel figures in §5.1 divide by 384,000, not 423,280. If the resident 424 KB ever matters,
the lever is building the full mask on the gesture (it would add a 424 KB fill to the 0.55-2.09 s
entry) rather than cropping the Card, which saves only 40,320 B and changes its look.

## 3. What each animation dirties

| Event | Invalidated rectangles | Cadence |
|---|---|---|
| hub tick | 2 spokes x old box + new box, per hub = up to **8** (`hub_frame_set()`, `cassette_tapes.c:419-432`; `spoke_set()` `:392-413`) | up to one per 40 ms pass when the reel runs fast (36 frames / 1.5-3 s) |
| tape fill | per changed side: the pack's old U new box and the strand's old U new box = **4**; a page flip re-clamps both packs first | the model steps the bar every 1000 ms (`cassette.h:35`), and the diameter changes with it while the tape moves |
| label, Track change | one box per text (about 11), joined where they touch - **5** in the playing run | on a Track change |
| label, design change | the print's box, one pass - **1** | when the design changes |
| page flip | the screen's background: **the whole panel** | Real / the two-finger pose |
| two-finger entry | the page flip + Card show + label show + tape preview + pack clamp, all in one frame | once |
| peek, VU meter | not measured here (not up at either pose) | |

## 4. Results

All numbers are milliseconds. `draw` is the frame's render, `flush` the panel wait, `total` their
sum. The primary table is idle run 2 (no card, no decoder); the ranges across the runs are in
§4.5.

### 4.1 The default entry screen (idle)

| Area | Rect calls | Invalidated px | draw mean | draw min | draw max | sync | flush | total |
|---|---|---|---|---|---|---|---|---|
| hub tick (both hubs) | 6-8 | ~800 | 21.5 | 9.4 | 27.1 | 16.4 | 22.5 | 44.0 |
| tape fill (both packs) | 4-5 | ~190,000 | 86.1 | 77.3 | 93.9 | 29.4 | 15.0 | 101.1 |
| hub rim, left | 1 | 29,584 | 19.1 | 14.2 | 31.1 | 13.8 | 18.0 | 37.1 |
| hub rim, right | 1 | 29,584 | 29.0 | 25.2 | 30.9 | 19.7 | 17.6 | 46.5 |
| pack, left | 1 | 102,400 | 43.1 | 34.6 | 50.0 | 14.8 | 17.5 | 60.6 |
| pack, right | 1 | 59,536 | 23.6 | 22.6 | 27.5 | 18.6 | 18.2 | 41.8 |
| strand, left | 1 | 9,112 | 17.1 | 9.7 | 24.9 | 17.2 | 15.4 | 32.5 |
| strand, right | 1 | 20,513 | 19.2 | 12.9 | 22.7 | 9.2 | 11.8 | 31.0 |
| tape body | 1 | 340,992 | 78.7 | 78.2 | 79.1 | 46.6 | 22.4 | 101.1 |
| screen | 1 | 384,000 | 112.5 | 112.2 | 112.6 | 64.8 | 13.4 | 125.9 |

The `sync` column is one sample (one drain), not a mean; §4.4 explains it.

### 4.2 The two-finger pose (idle)

| Area | Rect calls | Invalidated px | draw mean | draw min | draw max | sync | flush | total |
|---|---|---|---|---|---|---|---|---|
| **pose entry** (page flip + Card + label + tape) | 1 (screen) | 384,000 | **548.3** | 548.3 | 548.3 | 6.0 | 6.3 | 554.6 |
| hub tick (both hubs) | 6-8 | ~800 | 21.1 | 20.0 | 22.1 | 1.4 | 9.1 | 30.2 |
| tape fill (both packs) | 4-5 | ~105,000 | 223.4 | 183.5 | 255.9 | 76.1 | 12.8 | 236.2 |
| hub rim, left | 1 | 29,584 | 79.7 | 47.0 | 115.4 | 5.3 | 12.5 | 92.2 |
| hub rim, right | 1 | 29,584 | 74.7 | 48.6 | 107.3 | 70.0 | 15.4 | 90.1 |
| pack, left | 1 | 38,014 | 81.8 | 60.5 | 123.9 | 6.3 | 10.2 | 92.0 |
| pack, right | 1 | 36,354 | 103.7 | 59.0 | 124.4 | 62.3 | 8.0 | 111.7 |
| strand, left | 1 | 15,180 | 27.9 | 25.6 | 33.4 | 3.4 | 21.4 | 49.3 |
| strand, right | 1 | 15,789 | 69.4 | 26.7 | 90.5 | 68.6 | 11.5 | 80.9 |
| label (print box) | 1 | 268,532 | 431.2 | 419.7 | 443.0 | 123.3 | 15.2 | 446.4 |
| Card (masked) | 1 | 423,280 | 544.7 | 544.0 | 545.4 | 121.9 | 16.6 | 561.3 |
| window (masked) | 1 | 92,296 | 181.0 | 142.8 | 213.1 | 14.0 | 9.1 | 190.1 |
| tape body | 1 | 340,992 | 482.9 | 482.1 | 484.0 | 112.5 | 6.8 | 489.7 |
| screen | 1 | 384,000 | 544.7 | 544.0 | 545.6 | 103.2 | 16.7 | 561.5 |

The Card's `px` is its unclipped box (814x520); LVGL clips it to the 800x480 panel, so its real
area is the same 384,000 as the screen's. The measured identity of their two `draw` numbers
(544.7 ms both) is the clipping, not a coincidence.

### 4.3 Entering the two-finger pose

One frame does all of it: the white page (the screen's background), the Card at layout 1, the
label's show, the tape's preview inks and the packs' re-clamp to the 556x166 window
(`cassette_view.c:211-230`, `cassette_card.c:235-239`, `cassette_label.c:228-232`).

| Run | Content | draw | total |
|---|---|---|---|
| idle 2 | no card; label "retro", blank fields | **548.3 ms** | 554.6 ms |
| idle 1 | no card; label not yet built | 736.7 ms | 745.1 ms |
| playing | Track + real label "Black Stripes", fixture text | **2093.1 ms** | 2102.6 ms |

The 2.09 s is the owner's 2.3 s: the same full-pose mask draw, measured with the decoder
running. The idle 1 figure (737 ms) is a check, not the truth: that run began 6 s after boot and
its pose-entry step overlapped the boot's own load. The idle 2 figure (548 ms) is the one to
quote for the draw itself.

### 4.4 The buffer copy a one-off change leaves behind

Direct mode with two framebuffers (`panel_rgb.c:159`, `esp_lvgl_port_disp.c:433`) means frame
N+1 draws into the buffer frame N was not in, so before it draws LVGL copies the areas frame N
invalidated into it (`refr_sync_areas()`, `lv_refr.c:677-787`; the copy is
`lv_draw_buf_copy`, `:781-787`). The drain frame measures exactly that:

| Area | idle 1 | idle 2 | playing | idle 2, us/px |
|---|---|---|---|---|
| boot frame | 73.4 | 1.1 | 1.3 | - |
| hub tick | 1.8 | 16.4 | 2.6 | the copy's per-rectangle cost, not its pixels |
| tape fill | 41.6 | 29.4 | 42.3 | 0.15 |
| hub rim | 7.8 | 13.8 | 7.6 | 0.47 |
| pack, left | 18.5 | 14.8 | 18.1 | 0.14 |
| tape body | 64.5 | 46.6 | 67.0 | 0.14 |
| label | 37.9 | 123.3 | 38.2 | 0.46 |
| Card | 74.0 | 121.9 | 58.4 | 0.29 |
| screen | - | 64.8 | 75.8 | 0.17 |

The spread is real: the copy is a PSRAM memcpy (2 bytes per pixel) and Wi-Fi, the SD retries and
the decoder all take their share of the bus. The useful statement is the **range: 0.14-0.47
us/px, 40-125 ms for a Card- or screen-sized change**, and the fact that a *repeated* tick that
re-covers the same rectangles does not pay it (the `refr_sync_areas()` diff removes the covered
part, `lv_refr.c:700-728`).

### 4.5 While a Track plays, and the run-to-run spread

Run 3 had the card mounted and a Track playing; the device's own report in the same run is
`encoder: 1126 frames, 18391 us/frame = 70.4% of one core` at 62.2 s. The same areas:

| Area | idle 2 | playing | factor |
|---|---|---|---|
| default hub tick | 21.5 | 16.4 | 0.8 |
| default tape fill | 86.1 | 97.8 | 1.1 |
| default tape body | 78.7 | 102.6 | 1.3 |
| default screen | 112.5 | 152.7 | 1.4 |
| full hub tick | 21.1 | 35.0 | 1.7 |
| full tape fill | 223.4 | 385.9 | 1.7 |
| full label | 431.2 | 1274.3 | **3.0** |
| full Card | 544.7 | 1556.5 | **2.9** |
| full tape body | 482.9 | 1393.5 | **2.9** |
| full screen | 544.7 | 1538.7 | **2.8** |
| full pose entry | 548.3 | 2093.1 | **3.8** |

The pattern is the point: **short frames interleave with the decoder; long masked frames do
not.** A 20 ms hub tick barely notices the encoder, while a 0.5 s Card frame is stretched almost
3x and the 0.55 s pose entry nearly 4x. The playing run's default-pose numbers agree with the
idle run within about a third; its full-pose numbers are the ones that move.

The spread across the two idle runs is 0.67-1.34x per area (the Card 451 vs 545, the screen 466 vs
545, the label 413 vs 431, the tape body 401 vs 483). The idle 1 run started 6 s after boot, while
the boot's remaining tasks (the artwork task, Wi-Fi) were still starting; the idle 2 run started
20 s in. **Treat
one run as +-25% and read the ratios, not the last digit.** The smallest areas are the noisiest:
strand-right reads 26.7 ms at its min and 90.5 ms at its max in the same run.

### 4.6 What the wound body costs: the border-only A/B

The owner asked whether the two-finger pack could give up its fill and keep only the outer edge.
The bench can answer it without a second build: `cassette_tapes_bench_pack_body(false)` leaves the
body out, and two extra steps price it in the same run, at the same pose, seconds apart
(`tape-fill-border` and `pack-border`). The numbers are from the playing run, so both sides carry
the decoder's load.

| Step (two-finger pose) | draw mean | draw min | draw max |
|---|---|---|---|
| tape fill, body on | 393.1 | 316.3 | 426.5 |
| tape fill, border only | 278.2 | 272.7 | 298.9 |
| pack-left box, body on | 81.3 | 59.8 | 98.1 |
| pack-left box, border only | 89.7 | 50.8 | 240.2 |

The border steps' means are inflated: the first sample of each pays the body leaving (both packs
are invalidated when the flag flips), and the 240.2 max is a stalled frame. The **mins** are the
steady values.

- **One pack's body costs about 9 ms** of its 60 ms repaint (38,180 px, 15%); both packs ~18 ms.
- **The fill tick's min falls from 316.3 to 272.7 ms** - 44 ms, 14% - when both bodies go. The
  gap is wider than 2 x 9 ms; part of it is sample variance (the body-on step's own samples span
  316-426 ms), and part is the fill's union of the old and new pack boxes against the single
  current box the pack-left step invalidates.
- **The other ~270-300 ms is the rectangles**, not the tape: 4 masked areas (two pack boxes and
  two strand boxes) at 1.4-1.7 us/px on the Card's and print's masks. Dropping the body cannot
  remove those rectangles, so it cannot remove that cost.

So the answer to "can we sacrifice the tape fill?" is: yes, and it buys about **10-15% of the fill
tick** while turning the reel from a wound body into a hoop. The lever that keeps the look is the
invalidated area: a fill change moves only the outer edge, so a pack whose object box stayed at
its maximum could invalidate the annulus between the old and new radii (ARITHMETIC: about
pi(r_old^2 - r_new^2) = ~7,000 px at a 9 px step, against 38,180) and the strand's box could be
left alone on a sub-pixel tangent move. That is the experiment to run next, not the body.

### 4.7 The live frame mix at the two-finger pose

The sweep prices one area per frame. The live bench (`CONFIG_APP_UI_RENDER_BENCH_LIVE`) leaves
the view running, labels every real frame with what it carried, and aggregates 60 s of them.
The run: the 44 s Track playing, the two-finger pose up, the window from 44.7 s to 104.7 s.

| What the frame carried | Frames | draw mean | draw total | share of draw |
|---|---|---|---|---|
| hub tick only | 315 (83%) | 35.8 ms | 11.27 s | 34.5% |
| hub tick + tape fill | 61 (16%) | 332.3 ms | 20.27 s | 62.1% |
| tape fill only | 1 | 409.6 ms | 0.41 s | 1.2% |
| ink + label (a Track change) | 1 | 586.2 ms | 0.59 s | 1.7% |
| nothing the bits know | 1 | 52.6 ms | 0.05 s | 0.1% |
| **window** | **379 frames, 6.3 fps** | **86.0 ms** | **32.59 s** | **100%** |

The window spent **32.6 s of its 60 s inside draw** (54%) and another 4.7 s in the panel wait
(8%) - at 6.3 fps, because the fill frames are 300 ms each. The **fill is 16% of the frames but
63% of the drawing**: it adds ~296 ms to the hub frame it lands on (332.3 - 35.8). The **hub is
83% of the frames but 35% of the drawing**.

This is the number the cadence question needed. At this Track's speed the fill fires about once a
second, because the even-pixel diameter changes on every 1 s bar step (~4.5-6.7 px/s). An 8 px
threshold would make it every ~2 s for this Track - one frame in ~12 - and halve the fill's
share (ARITHMETIC: about 10 s of the 32.6 s of draw in the window). A 3.5 minute Track moves the
diameter at ~0.9 px/s, where the even-pixel change is already every ~2.2 s and an 8 px step would
make it every ~8.9 s: **four times fewer fill frames**. The trade is the visible one: the pack's
edge jumps 8 px (4 px of radius) at once, 2-4% of its diameter at this pose.

The alternative keeps the 1 s smoothness: invalidate only the annulus the edge moves through
(~7,000 px instead of 38,180, §4.6) and leave the strands alone on a sub-pixel tangent move. The
fill's marginal ~296 ms should then fall to tens of milliseconds, with the edge still moving on
every bar step. §4.8 runs it: the fill's marginal becomes 87.8 ms (4.1x cheaper) and the live
draw falls 30%, with every bar step still drawn.

### 4.8 The annulus alternative, A/B against the base fill

The alternative §4.6 and §4.7 pointed at was implemented and measured, then promoted - and is
now **parked** behind `CONFIG_APP_UI_PACK_ANNULUS`, default **n**. It keeps each pack's box at
the maximum, dirties only the **four strips** of the band between the old and new outer edges
(clipped to the Card's window), and re-places the strand only every 8 px. The bench keeps a
switch to force the old path, so the live bench can run the two back to back, 30 s each, at the
same pose and Track. It was measured twice: as a variant, and again after promotion.

| 30 s window | frames | fps | draw total | fill frames | fill mean | fill marginal |
|---|---|---|---|---|---|---|
| base (run 1) | 173 | 5.7 | 16.46 s | 29 | 396.3 ms | **361.8 ms** |
| annulus (run 1) | 280 | 9.3 | 11.53 s | 31 | 117.3 ms | **87.8 ms** |
| base (promoted build) | 182 | 6.1 | 16.97 s | 28 | 405.7 ms | **369.5 ms** |
| annulus (promoted build) | 234 | 7.8 | 11.34 s | 34 | 120.6 ms | **87.1 ms** |

The two runs agree on the thing that matters: the fill's marginal cost goes from 362-370 ms to
87-88 ms, and the window's draw from 16.5-17.0 s to 11.3-11.5 s - **when the switch is on**.
After two rewind photographs showed artefacts the captures did not reproduce (a fan of arcs,
then a rectangular seam and a tab), the switch was turned off: the shipping path is the plain
one, one `lv_obj_t` per pack, `lv_obj_set_size()` on a fill change and LVGL's automatic old U new
invalidation. The tape is one object; the strips were an optimisation on top of that, not a
requirement, and they have not earned their complexity yet. The frame-rate gain is noisier
(+28% to +63%) because the panel wait and the decoder's share move between windows.

The fill marginal is the hub+fill mean minus the hub-only mean (34.5 ms base, 29.5 ms annulus).
**The fill fires the same number of times (29 vs 31), so the 1 s edge movement is kept; it is the
frame that becomes 4.1x cheaper.** The window's draw falls 30% (16.46 -> 11.53 s; the annulus
window also carries the Track change, 0.57 s, so 10.96 s without it) and the frame rate rises 63%
(5.7 -> 9.3 fps).

Why it is not cheaper still: a ring band needs four strips per pack, so a fill dirties **eight
rectangles instead of four** (`rects_mean` on the fill frames is 8 against 4). At ~11 ms a
rectangle the fixed per-rectangle cost is now the limit, not the ~7,000 px of band. The strands
are re-placed only every 8 px, so about half the fills still carry their two boxes; skipping them
entirely at this pose (the strand is mostly behind the Card) would take another ~10-20 ms.

The path is valid only at the two-finger pose. The preview pack is one filled body with one edge,
so the band is all that changes; the default screen's ring stack shifts every ring with the
radius, and its full-box invalidation stays. That is why the gate is `s_preview`, not a global
switch. The one visual debt is the strand's 8 px quantisation: it can sit up to ~4 px off the
pack's edge between updates. It is mostly behind the Card at this pose; lower the threshold to
4 px if it reads on the panel.

### 4.9 Two bugs the promotion surfaced

A photograph of the rewind (`.scratch/cassette-render-cost/rewind-fan.jpg`) showed a fan of red
arcs between the reels. It was the annulus path, twice:

1. **The outer pad.** The strips' **outer** boundary sat exactly on the old radius, and the
   pack's outer edge is a 1 px stroke centred there: half of it, and its antialiasing, fell
   outside the invalidated rectangle. Each shrinking step left the old edge's own pixel behind;
   a rewind shrinks the supply reel many times and the residues stack into the fan. The inner
   boundary was already padded for the new edge; the outer one was not. Fixed by padding the
   outer square by the stroke width + 2, and growing the pack's maximum box by the same pad so
   the pad itself is not clipped.
2. **The strand drew the live fill, not its box.** `strand_draw()` recomputed the path from
   `s_pack_d` on every draw, while the annulus path re-sizes the strand object only every 8 px.
   Between updates the strand was drawn at the new path inside the old box, and any partial
   redraw (the pack's strips overlap the strand near the pack) left the old strokes behind. Now
   `strand_set()` stores the path with the box (`s_strand_pts`, `s_strand_n`) and the draw uses
   that copy; it also invalidates the object explicitly, because the box can stay the same size
   while the path moves inside it.

The strand fix is on the shipping build; the outer pad belongs to the parked annulus. The rewind
is **8 s and 24 pack steps**
(`CASSETTE_REWIND_MS`, `CASSETTE_REWIND_PACK_STEPS`), which is why the photograph had about
two dozen fingers: one residue per step. A capture of the fixed build through a Track change
(`/api/screen.bmp` with the new scene 10, frames 0.5-6 s after the change) is clean: the pack
edge steps with the fill and no residues accumulate. The capture is in
`.scratch/rewind/cap/`. The photograph that started this was of the build before the pads.

### 4.10 A coarser pack step, at the two-finger pose only

The shipping code now quantises the **preview's** pack diameter to 8 px (`PACK_PREVIEW_STEP`,
`cassette_tapes.c`); the default screen keeps the even-pixel step because its ring stack shows
a 4 px radius jump as a broken ring. On this 44 s Track the fill offers a new diameter once a
second and moves 3-7 px, so the 8 px grid takes the fill from ~1.0 draw/s to ~0.5 draw/s (the
60 s live window measured 61 fill frames; the expected count is ~30). On a 3.5 min Track it goes
from one every ~2.2 s to one every ~8.9 s. The fill was 63% of the live drawing, so the pose's
draw should fall by roughly 30% - the annulus's win without the strips.

The price is an 8 px diameter jump (4 px of radius) on the reel edge, and the grid ends at 352,
4 px short of the full pack (the 168-anchored range does not divide evenly). The conservative
setting is 4; the live fill-frame count on 8 is owed.

## 5. Where the cost is

### 5.1 Per pixel, the mask is a 5-6x tax

Using the means (idle 2) and the clipped area:

| Area | default us/px | two-finger us/px | factor |
|---|---|---|---|
| screen | 0.29 | 1.42 | 4.9x |
| tape body | 0.23 | 1.42 | 6.1x |
| Card | - (hidden) | 1.42 | - |
| label | - (hidden) | 1.61 | - |
| pack, left | 0.42 | 2.15 | 5.1x |
| hub rim | 0.64 | 2.70 | 4.2x |
| window | - | 1.96 | - |

The full pose's two masked objects - the Card and the print container, each 426x272/814x520 of
A8 - are applied to every invalidated rectangle in the Card's box, so the tax is proportional to
the *rectangle*, not to what is drawn inside it. This is why the label (words and rules) and the
Card (one flat plastic fill) cost almost the same per pixel, and why the small rectangles are the
dearest per pixel (the hub rim 2.70 us/px, the window 1.96): the two masked layers are set up
whatever the rectangle's size, so a 168x168 box pays the set-up over 28,224 px while a screen pays
it over 384,000.

### 5.2 Per rectangle, the small animations pay the fixed cost

| Area | rectangles | draw per rectangle (default) | per rectangle (full) |
|---|---|---|---|
| hub tick | 6-8 | 1.3-3.1 ms | 5.0-5.3 ms |
| tape fill | 4-5 | 17-22 ms | 37-46 ms |
| label, Track change (playing) | 5 | - | 333 ms total (loaded) |
| rim / pack / strand / Card / screen | 1 | 17-43 ms | 28-545 ms |

A hub tick is 6-8 rectangles of about 13x8 px: the ink is under 1,000 pixels, and it costs
**9.4-21.5 ms** because each rectangle is a separate `refr_area()` with its own layer set-up,
tree walk and flush call. The same is true of the Track-change label: 11 small text boxes join
into 5 rectangles and cost **333 ms while playing** (~116 ms normalised, ARITHMETIC: 333 / 2.86),
even though the text itself is a few thousand pixels. **The count of refreshed rectangles, not their pixels, is what
a tick costs** - the code comment in `cassette_tapes.c:77-82` said it before the bench measured
it.

### 5.3 The panel wait is a floor on every rendered frame

`flush` reads 6-24 ms across the areas. In direct mode it is paid once per rendered cycle, on
the last invalidated rectangle: the port pushes the *whole* 800x480 framebuffer and waits for the
RGB frame (`esp_lvgl_port_disp.c:750-755`), a 25.6 ms panel frame at 16 MHz (`panel_rgb.c:31`).
So a rendered cycle is never cheaper than about 6-20 ms, whatever the area: an "idle" frame that
draws one pixel still pays it.

### 5.4 What this means for the drawing

- **The hub tick is the recurring cost.** At 19-21 ms draw + ~13 ms wait, and with the model able
  to tick on nearly every 40 ms pass, it is worth roughly a third to two-thirds of core 0 before
  the decoder. It is the first thing to look at, and its pixels are not the problem: 6-8 rectangles
  are.
- **Fewer rectangles, not fewer pixels.** One object holding both marks of a hub (a diameter)
  would dirty one 168x168 box per hub: measured at 14-20 ms (default) and 47-80 ms (full) per
  hub, against 9-21 ms (default) and 20-35 ms (full) for both hubs' 6-8 small boxes. The split
  is still the better shape, but only just; a change that *reduces* the count (one box for both
  hubs' marks is not possible - they are 386 px apart) or the tick rate is the only lever that
  wins. A slower tick was already tried and rejected for the visible jump
  (`cassette_view.c:360-369`).
- **The two-finger pose is a multiplier on everything.** The pose entry is 0.55 s idle, and while
  a Track plays it is 2.09 s; every area under it is 4-6x its default cost per pixel. A redraw
  while the pose is up costs the mask twice (Card + print) over the whole rectangle. Keeping the
  film still under the pose, or not entering it during playback, is worth more than any raster
  optimisation.
- **The wound body is not the cost either.** Dropping the two-finger pack's filled disc saves
  ~18-44 ms of a 316-393 ms fill tick (10-15%); the four masked rectangles are the rest. Shrinking
  the pack invalidation to the annulus that moves is the lever that keeps the look (§4.6).
- **The fill is the expensive frame, the hub is the frequent one.** In the live window the fill
  is 16% of the frames but 63% of the drawing; the hub is 83% of the frames but 35% (§4.7). The
  annulus invalidation keeps the 1 s movement and takes the fill's marginal cost from 361.8 to
  87.8 ms (4.1x) and the live draw down 30% (§4.8), re-confirmed on the promoted build at
  369.5 -> 87.1 ms. A cadence change would trade smoothness for
  the same frequency win the annulus already gets without one.
- **The label's type is not the cost.** A blank design and a filled one are within the noise of
  each other (431 ms vs a normalised ~445 ms); a design change is a full 748x359 masked pass
  (~0.42-0.45 s), and a Track change is the 11 text boxes (~0.12 s idle, ARITHMETIC: 333 ms
  divided by the Card's 2.86x). If the label ever needs
  to be cheaper, the lever is the masked area, not the font.
- **Any one-off change costs a second frame.** The buffer sync (§4.4) copies the changed area on
  the next frame: 40-125 ms for the Card, the label or the screen. A page flip or a pose entry is
  therefore two frames, not one.

## 6. Caveats

- **One board, one build.** The numbers are for the Sunton 8048S050C at `-Og`, LVGL 9.6,
  ESP-IDF 6.1, 16 MHz pclk, two PSRAM framebuffers, direct mode with `avoid_tearing`. A
  different optimisation level, pclk or buffer arrangement moves them.
- **The instrument is not free.** The bench blocks the screen task for up to 2 s, starves the
  decoder and tripped the task watchdog twice in the playing run. Those are the bench's frames
  back to back; the shipping app pays one such frame when the pose is entered, not a stream of
  them. The `starved for ... rebasing` warnings in the capture are the instrument's, not the
  app's.
- **The label's content.** The idle label was the boot design with blank fields; the playing
  label was the real design with a fixture title in every slot. §1 says which is which where it
  matters.
- **The `px` column is the invalidated rectangle, not the ink.** The Card's 423,280 is its
  814x520 box before the panel clips it; the useful per-pixel figure uses the clipped 384,000.
  The hub tick's ~800 px is computed from the spoke geometry, not from the bench (a mutator step
  reports `px=0`).
- **A rectangle is not an object.** Invalidating the label's print box also redraws the tape and
  the Card inside it; that is the real cost of a change there, but it is not "the label alone".
- **The tick cadence is ARITHMETIC.** 36 frames a revolution at 1.5-3 s a revolution
  (`cassette_view.c:360-369`) is one frame every 42-83 ms; how many of those the 40 ms poll
  actually catches is the model's, and the bench does not measure the rate.

## 7. What was measured, and how to reproduce it

The instrument is `components/app/screen_display/cassette_bench.h` and `render_bench.c`; the
view's steps are `cassette_view.c:877-1032`; the tape's hub step is
`cassette_tapes.c:967-987`; the label's fixture and text step are
`cassette_label.c:252-268`. The Kconfig is `CONFIG_APP_UI_RENDER_BENCH`, off by default.

```sh
# The numbers are the shipping configuration's, not a defaults-only build's: other
# symbols differ. sdkconfig.sunton is the shipping build's config
# (`./tools/build.sh sunton build` regenerates it). Add the bench to a copy of it.
cp sdkconfig.sunton sdkconfig.bench
printf '\nCONFIG_APP_UI_RENDER_BENCH=y\n' >> sdkconfig.bench

export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf6.1_py3.14_env"
export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
. ~/esp/esp-idf-6.1/export.sh

idf.py -B build/bench -D SDKCONFIG=sdkconfig.bench \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.sunton" build
idf.py -B build/bench -p /dev/cu.usbserial-8344440 flash

# read the console; the bench starts BENCH_START_MS after the screen is built (40 s)
python -m serial.tools.miniterm /dev/cu.usbserial-8344440 115200
```

Every result is a `bench: area=...` line. The bench runs the default pose first, then enters the
two-finger pose and runs the same list there; it finishes by putting the picture back and
unfreezing the view. The three raw captures are kept, for the moment of this note, in
`.scratch/cassette-render-cost/` (transient, not the record). The live mix of §4.7 is the same
build with `CONFIG_APP_UI_RENDER_BENCH_LIVE=y`: it enters the two-finger pose, lets the animation
run for 60 s, and prints one `bench: live` line per change mask; that capture is
`bench_live.log` beside the others. The runs behind the tables were:

- **idle 1** - 6 s start; the card had not been polled (the bench freezes the poll), so the
  screen was in its no-card state.
- **idle 2** - 20 s start, no card (the reader retried every 5 s and failed). The primary table.
- **playing** - 40 s start, card mounted at 22 s, Track at 26 s, the decoder's 70.4% report at
  62 s.
- **live** - 40 s start, the two-finger pose up at 40.0 s, the 60 s window from 44.7 s; the 44 s
  Track was playing throughout and changed at 81 s.
- **annulus** - the same build with two 30 s windows: base 44.6-74.6 s, then the annulus variant
  75.6-105.6 s (the Track change falls inside the second window).
- **annulus promoted** - the same two-window run after the annulus became the shipping path (the
  bench forces base for the first window): 182/234 frames, 16.97/11.34 s,
  `bench_annulus_promoted.log`.

## 8. Three things this exercise found that are not about drawing

1. **A fresh sunton config did not compile - now fixed.** `artwork_loader.c` includes
   `tjpgd.h` unconditionally, but `CONFIG_LV_USE_TJPGD` was set only in the generated, untracked
   `sdkconfig.sunton`; it was not in `sdkconfig.defaults.sunton`, and LVGL's Kconfig defaults it
   to `n`, so a clean `build/sunton` failed in `artwork_loader.c` (`unknown type name 'JDEC'`).
   `CONFIG_LV_USE_TJPGD=y` is now in `sdkconfig.defaults.sunton` and a fresh configure + build
   from the defaults only (`build/fresh`, its own `sdkconfig.fresh`) completes.
2. **The screen-capture build had no HTTP server.** `HTTP_MAX_ROUTES` was 18: the appliance
   registers 17 routes without the screen, and the two screen diagnostics make 19, so the last
   registration (the CORS preflight) failed with `ESP_ERR_NO_MEM`, `app_services_start()`
   aborted, and the listener never started - "listening on :80" was simply absent from the boot
   log, and the device answered pings but refused every connection. Raised to 20; the capture
   build serves again. This is why the first attempt to photograph the rewind from the device
   found nothing.
3. **The bench's long frames starve the stream.** Two `task_wdt` triggers (`IDLE0`, `taskLVGL`)
   and five `icecast: starved ... rebasing` warnings appear in the playing capture. They are the
   instrument's back-to-back full-pose frames, but they are also a preview of what a 2 s frame on
   core 0 does to the encoder.

## Sources

- The bench: `components/app/screen_display/cassette_bench.h`,
  `components/app/screen_display/render_bench.c` (clock `:148-186`, drain `:231-238`, run
  `:264-338`), `cassette_view.c:877-1032`, `cassette_tapes.c:967-987`,
  `cassette_label.c:252-268`, `Kconfig` (`APP_UI_RENDER_BENCH`).
- The composition and geometry: `cassette_view.c:684-790` (build), `:211-230` (FULL/PAGE),
  `:343-382` (apply_frame); `cassette_card.c:21` (`CARD_MASK 1`), `:52-63` (the two layouts),
  `:195-287` (the Card, its plate and its window); `cassette_cover.c:87-137` (print placement);
  `cassette_label.c:214-244`; `cassette_tapes.c:392-432` (spokes), `:501-630` (rings, strands,
  packs); `cassette_layout.c` (pose, tape box); `cassette.h:35` (the bar period).
- LVGL: `managed_components/lvgl__lvgl/src/core/lv_refr.c:403` (REFR_START), `:430`
  (`refr_sync_areas()` call), `:677-787` (the copy), `:802` (RENDER_START), `:805-860` (the
  per-area loop), `:870` (RENDER_READY), `:1459`/`:1481` (FLUSH_START/FINISH).
- The panel and port: `components/app/screen_display/panel_rgb.c:159` (avoid tearing), `:31`
  (16 MHz); `managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c:433`
  (direct mode), `:667` (flush callback), `:750-755` (the full-frame push and the semaphore
  wait); `screen_display.c:534` (the 40 ms refresh period).
- The controls: `components/portable/controls/controls.c:88-90` (FULL_ON only at Initial and only
  at the settled entry pose), `cassette_view.c:394-397` (chrome at Art/Real only).
- The earlier notes this one corrects or prices: `docs/research/panel-rgb-lvgl-performance.md`
  (the 15 FPS / 66 ms cycle, the panel frame), `docs/research/hub-drawn-vs-blitted.md` (the
  hub's per-pixel estimate, which this note shows is not what the tick pays),
  `docs/specs/cassette-card.md` (the Card's geometry and the mask).

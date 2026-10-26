# Cover Flow and one-row finger scrolling on xtape — verified against the vendored LVGL 9.6 and Apple's own documents (2026-10-23)

Axis: **the owner has chosen touch (no drawn wheel) and wants a normal finger drag whose content
advances only in blocks of one row; he also doubts that Cover Flow is viable. This note answers both
questions from primary sources: what Cover Flow is and needs, what LVGL 9.6 can actually do on this
ESP32-S3, why a native scroll step does not cost one row, and which scroll mechanics satisfy "finger
drag, one-row blocks" — including a stripped-down 2D cover strip if Cover Flow is abandoned.**

"Scroll ambos" is read here as **both axes/lists** (the vertical menu list and a horizontal cover
strip) and **both drag and fling**. If the owner meant only both drag and fling, the vertical list
section is the whole answer and the horizontal strip section is the extra. The note states the
reading so it can be corrected.

Definitions used below:

- **True Cover Flow** — the album-art browser described by Apple and claimed in US 8,230,360 B2:
  a flat centre cover with tilted/overlapping covers on both sides, changed by the wheel, with the
  album's title and artist on screen. The familiar product rendering also uses a perspective
  (trapezoid) projection and a reflection under the centre cover.
- **2D cover strip** — a flat horizontal row of cover thumbnails with a larger centre card, no tilt,
  no perspective, no reflection. It is not Cover Flow; it is a carousel.
- **Content scroll** — the list's items move and a new item appears at the edge, as the owner asked.
- **Selection band** — the highlight moves while the items stay still; the content only moves when the
  band reaches the viewport edge. This is the iPod-wheel-like alternative and is explicitly compared
  because it is ~3x cheaper per step.

Evidence labels are the ones the repo already uses:

- **MEASURED (board)** — printed by this firmware on the panel and recorded in this repo; not re-run
  for this note.
- **MEASURED (link/source)** — read out of the linker map, the built binary, the generated font, or
  a vendored source file; the artefact is named.
- **DOCUMENTED** — a line of source, a Kconfig `help`, a header comment, an Apple manual page, or a
  patent claim.
- **ARITHMETIC** — a value computed here from cited inputs. The arithmetic is mine.
- **UNVERIFIED** — a claim I could not trace to a primary source in this pass. It is not used to
  justify a decision.

No code, config or asset was changed for this note. The only file created is this one.

---

## Bottom line

1. **Cover Flow is not viable on this device as true Cover Flow.** The first hard failure is the
   projection: LVGL 9.6's image widget has only 2D rotation/scale around a pivot, no perspective, and
   this ESP32-S3 has no PPA at all. Even a 2D affine approximation fails throughput: the repo's
   recorded >100 ms per frame was a 135x135 hub frame; a 603x385 cover is 12.7x the pixels, so
   >1.27 s per cover per frame by arithmetic, and three covers >3.8 s per frame. Memory then fails
   (one 603x385 cover leaves 39,990 B; three need 1.39 MB), and the album index and per-cover decode
   are additional blockers. **Verdict: no true Cover Flow.**
2. **A 2D cover strip is technically possible but not worth P0/P1.** Five cards (240x240 centre plus
   four 160x160 sides) need 320,000 B of PSRAM before the decode band; that fits in 504,300 B for a
   603-wide source, but a 4000-wide source's band pushes it to 512,000 B. It draws as plain unscaled
   images, but loses the perspective and reflection that make Cover Flow, still needs an album index
   that the Library does not have, and five covers at the measured 691 ms each are ~3.46 s of decode
   before it is populated. Keep the existing one-cover Artwork Level; revisit a 2D strip only
   as a P2 experiment after a tag/album index and a thumbnail cache exist.
3. **For the scroll, use a custom detent (option C), not native snap.** Native scrolling invalidates
   the whole list viewport on every move (`lv_obj_scroll_by_raw()` → `lv_obj_invalidate(obj)`), so a
   scroll frame is **1,075,200 B** (537,600 B painted + the direct-mode sync copy), not 0.35 MB. The
   0.35 MB figure is a two-row *selection-band* move. A custom detent sits still between steps, steps
   exactly one row per 56 px of finger travel, can cross several rows in one drag, and is independent
   of which row objects exist.
4. **"Normal finger scroll" and "only one-row blocks" are mutually exclusive at pixel level.**
   Continuous finger-follow necessarily shows partial rows; exact blocks necessarily jump by one row.
   The custom detent keeps the finger gesture but makes the content ratchet; if pixel-follow is more
   important than exact blocks, native `LV_SCROLL_SNAP_*` + `LV_OBJ_FLAG_SCROLL_ONE` is the closest
   native approximation, but it shows partial rows, stops hard at the next row, and is only safe on a
   bounded non-virtualized list.
5. **Native snap/scroll-one is unsafe on the 20,000-row virtualized Playlist.** `find_snap_point_y()`
   iterates only the children that currently exist (`src/indev/lv_indev_scroll.c:578-600`), and
   `init_scroll_limits()` computes the previous/next snap area once at scroll start (`:448-509`).
   A recycled 8-row window has no snap point for the next unseen row, and rebinding rows moves the
   snap points after the area was computed. Use native snap only on a bounded page where every row
   exists, or use the custom detent.
6. **A one-row *scroll* is not a one-row *selection move*.** One row painted is 87,808 B (with the
   sync copy, 175,616 B); a selection band moving off one row and onto the next is 351,232 B; a scroll
   of the list is the whole 800x336 viewport, 1,075,200 B. Do not budget 0.35 MB per scroll step.
7. **The cost of native snap is the settle animation, not only the finger frames.** LVGL's scroll
   animation is 200-400 ms (`SCROLL_ANIM_TIME_MIN/MAX`, `src/core/lv_obj_scroll.c:20-25`); at the
   25 Hz/40 ms timer that is 5-10 frames, each 1,075,200 B: ~6.45 MB for a one-row snap and ~10.75 MB
   for a page settle. An instant custom step is one frame, 1,075,200 B. If the custom detent is chosen,
   make the step instant, not animated.
8. **Fallback if the custom detent feels wrong:** native scroll-one on a bounded page (all rows
   created, spacer non-snappable), or a 6-row paged list with a page flip. Both are robust; neither
   gives one-row blocks during a continuous drag.

---

## 1. Cover Flow: what Apple documents, and what it requires

### 1.1 The Apple manual: an album browser, driven by the wheel

The primary description is in the two Apple user guides the earlier note already cites:

- **iPod classic 160 GB (Late 2009) User Guide**, p.26, section **"Browsing music using Cover
  Flow"**: "Cover Flow displays your albums alphabetically by artist name. You see the album artwork,
  title, and artist name." The steps: from the Music menu choose Cover Flow; use the Click Wheel (or
  Next/Previous) to move through the album art; select an album and press Center; then select a song.
- **iPod nano 5th generation User Guide**, p.37-38, section **"Browsing Music Using Cover Flow"**:
  the same "albums alphabetically by artist name" sentence; "Hold iPod nano horizontally to view
  Cover Flow"; "Rotate iPod nano 90 degrees to the left or right. Cover Flow appears"; use the Click
  Wheel to move through the album art; moving the thumb quickly browses by letter.

What the manuals establish for xtape:

1. **The unit is the album, not the Track.** The browser groups by album and orders by artist name.
   The Library here is a directory tree of Tracks and has no tag index, so this grouping is not free
   (§1.5).
2. **The screen shows album artwork, title and artist.** Track title alone is not the Cover Flow
   content.
3. **The interaction is one-dimensional browsing with a wheel and a select button.** The covers are
   arranged in a row; the wheel moves along it; select opens the album's songs. The nano guide
   explicitly rotates the device to landscape; the classic guide describes no rotation. Neither manual
   says anything about projection, reflections or frame rate.

The manuals do **not** document the perspective math or the reflection. Those are properties of the
product's rendering and the design patents, not of the user-facing text.

### 1.2 The patent: flat centre, tilted sides

**US 8,230,360 B2, "User interface for selection from media collection"**, Apple Inc., inventors
Jeffrey Ma, Gregory Dudey, Rachel Clare Goldeen, Justin Henzie and Rainer Brodersen; priority
2008-01-04; granted 2012-07-24. Claim 1 is the arrangement:

> "…wherein a starting state initially includes a flat content item on the left and one or more tilted
> content items on the right, wherein a middle state includes one or more tilted content items on the
> left and on the right, wherein an ending state initially includes one or more tilted content items on
> the left and a flat content item on the right…"

This is the primary claim behind the "flat centre + tilted sides" look. It claims a *tilted content
item*, not a specific projection; the familiar product look draws those tilted items as trapezoids
(converging top/bottom edges) with a reflection under the flat item. The claim does not mention a
reflection or a 3D matrix.

Apple also owns two **design patents** commonly associated with Cover Flow's appearance, both by
Imran Chaudhri and both titled "Animated graphical user interface for a display screen or portion
thereof":

- **USD613300S1**, filed 2007-06-28, granted 2010-04-06.
- **USD624932S1**, filed 2007-06-28, granted 2010-04-06.

A design patent has a single claim to the ornamental design; it has no method claim and says nothing
about how the pixels are rendered. It is not a source for an implementation, and it does not give
LVGL a perspective transform. These design patents are cited here only so the note does not pretend
no Apple patent exists.

### 1.3 What the vendored stack can draw

The relevant capability is the image widget in LVGL 9.6.0, not the vector/canvas API.

- `lv_image_set_rotation()` rotates around a pivot; `lv_image_set_scale()` / `_x` / `_y` scales;
  `lv_image_set_pivot()` sets the centre (`include/lvgl/widgets/lv_image.h:115-188`). The scale
  argument is `uint32_t`; the draw path rejects zero or negative scale
  (`src/draw/lv_draw_image.c:72-73`, `:113-114`). This is 2D affine only: a rotated/scaled rectangle,
  not a perspective trapezoid.
- The software transform computes `sin/cos`, `scale_x/scale_y`, pivot and integer stepping per pixel
  (`src/draw/sw/lv_draw_sw_transform.c:18-28`, `:141-161`, `:223-233`). There is no 3x3 projective
  matrix in this path.
- `CONFIG_LV_USE_MATRIX` is **off** (`sdkconfig.sunton:4081`); `LV_DRAW_TRANSFORM_USE_MATRIX`
  defaults to off and depends on it (`Kconfig:377-389`). `CONFIG_LV_USE_FLOAT` is also off
  (`sdkconfig.sunton:4286`). Even if the matrix API were enabled, the image widget exposes no
  perspective; a custom draw with a projective texture map would be new rendering code, not a flag.
- The ESP32-S3 has **no PPA**: `CONFIG_LV_USE_PPA` is off (`sdkconfig.sunton:4191`), and
  `SOC_PPA_SUPPORTED` is defined for the ESP32-P4 and ESP32-S31, not the ESP32-S3
  (`~/esp/esp-idf-6.1/components/soc/esp32p4/include/soc/soc_caps.h:116`,
  `esp32s31/include/soc/soc_caps.h:103`; the S3 `soc_caps.h` has no such define). The `esp_lvgl_port`
  PPA option is only for screen rotation (`managed_components/espressif__esp_lvgl_port/Kconfig:3-8`).
  LVGL's PPA image path only accepts "simple cases: unscaled, unrotated, fully opaque and without
  masks" (`managed_components/lvgl__lvgl/Kconfig:947-952`), so it cannot help a tilted cover either.
- **The repo's own measured transform cost:** `components/app/screen_display/cassette_view.c:31-34`
  says there is deliberately no `lv_image_set_rotation()` because "LVGL's software transform is per
  pixel and was measured at over 100 ms per frame on this part; the frame sequence is the rotation."
  The experiment in commit `d39e1a2` rotated **135x135 hub frames** (53 KB each, versus 724 KB for the
  shell), not a full cover. This note does not present a 603x385 transform time as measured.

From that measurement, the arithmetic is:

| Item | Value | Basis |
|---|---|---|
| Measured transform cost | **>100 ms/frame for a 135x135 hub** | MEASURED (board, historical, commit `d39e1a2`; comment at `cassette_view.c:31-34`) |
| 603x385 pixels | 232,155 px | ARITHMETIC |
| 135x135 pixels | 18,225 px | ARITHMETIC |
| Pixel ratio | 12.74x | ARITHMETIC |
| Extrapolated 603x385 transform | **>1.27 s per cover per frame** | ARITHMETIC, assuming pixel-linear cost; not measured |
| Three covers, same frame | **>3.82 s** | ARITHMETIC |
| 25 Hz budget per frame | 40 ms | DOCUMENTED (`screen_display.c:61`; timer `:342-343`, `:356-357`) |

The extrapolation is a lower-bound style estimate: it assumes cost scales with pixels and ignores
cache, overhead and layer allocation. It is still enough to rule out a 25 Hz frame with one cover,
let alone three. A 160x160 thumbnail is 25,600 px, 1.40x the 135x135 pixel count, so a transformed
thumbnail would also be >140 ms; only an **unscaled** 160x160 image (decoded/resampled once to its
final size) avoids the per-frame transform. That is what the 2D strip in §1.7 would have to do.

### 1.4 Memory: one full cover already consumes almost all the free PSRAM

The current artwork path allocates a single 603x385 RGB565 cover buffer and keeps it for the life of
the process:

| Item | Bytes | Source |
|---|---|---|
| One 603x385 RGB565 cover | 603 × 385 × 2 = **464,310** | ARITHMETIC; `artwork_loader.c:60-63`, `:89-93` |
| Free PSRAM after the Case and that one cover | **504,300** | MEASURED (board), `README.md:605-609`; `docs/adr/0013-artwork-level-and-case-window.md:99-101` |
| Free after one full cover | 504,300 − 464,310 = **39,990** | ARITHMETIC |
| Three full covers | 3 × 464,310 = **1,392,930** | ARITHMETIC |
| Five full covers | 5 × 464,310 = **2,321,550** | ARITHMETIC |
| Seven full covers | 7 × 464,310 = **3,250,170** | ARITHMETIC |

Three full covers do not fit; five and seven are far beyond the 504,300 B. A reflection buffer would
make it worse, and `lv_image_set_scale()` cannot be negative to mirror an image, so a reflection
would be a second asset or a second buffer, not a free transform.

The only way to get several covers into the free PSRAM is to make them small:

| Set | Bytes | Free after (504,300) |
|---|---|---|
| 160x160 RGB565, one | 51,200 | — |
| 3 cards: 300x300 centre + 2 × 160x160 | 282,400 | 221,900 |
| 5 cards: 240x240 centre + 4 × 160x160 | **320,000** | **184,300** |
| 7 cards: 240x240 centre + 6 × 160x160 | 422,400 | 81,900 |
| 56x56 thumbnail, one | 6,272 | — |
| 6 × 56x56 list-row thumbnails | 37,632 | — |

These are buffer-only numbers. A decode needs a band or crop buffer in addition: the current band is
`width × 16 rows × 3 B` (`artwork_loader.c:279-300`), 28,944 B for a 603-wide picture and 48 KB for a
1000-wide one; a 4000-wide JPEG would need 192,000 B. The LVGL heap for the strip (objects, labels,
images) also comes out of the same 504,300 B. The 5-card strip fits on paper for a 603-wide source
(320,000 + 28,944 = 348,944 B), but a 4000-wide source's 192,000 B band brings it to 512,000 B, above
the 504,300 B free before the LVGL heap; the strip is source-size-dependent unless the band is narrowed
or the source is downscaled (which TJpgDec cannot do today). The 7-card strip is too tight to promise
without a board measurement.

### 1.5 Album grouping: the Library does not have it

Cover Flow's unit is the album, ordered by artist name (classic p.26; nano p.37). The Library is an
immutable tree built by walking the Music directory:

- It stores directories, Tracks, names, sizes, formats and the depth-first Playlist
  (`components/portable/library/include/library.h:1-15`, `:107-132`). `library_relpath()` can build
  `"Artist/Album/track.mp3"` (`:116-118`), but that is a path convention, not parsed metadata; nothing
  in the scan reads `TPE1`/`TALB`.
- The status struct carries title, file, path, format, elapsed/duration, track index and library
  count; it has no album or artist field (`components/portable/mini_icecast/include/mini_icecast.h:92-104`).
- `components/portable/artwork` locates an `APIC`/`PICTURE` block per Track; it does not build an
  album group or choose a representative cover. The loader has one request path and one 603x385
  buffer; a new request replaces the previous one (`artwork_loader.c:82-93`, `:579-588`).

An album index would need one of two things, both scan-time work:

1. Parse ID3v2 `TALB`/`TPE1` and FLAC `ALBUM`/`ARTIST` blocks for every Track, group by
   `(artist, album)`, choose a cover, and add a secondary index to the Library arena. That is a new
   scan capability and a new data structure, not a menu change.
2. Assume `Music/Artist/Album/` directory naming and group by directory. That works on a curated
   card and fails on a flat card; it must be stated as an assumption, not a guarantee.

Either way, Cover Flow is blocked on data, not just drawing.

### 1.6 Decode: one cover takes 691 ms, and thumbnails are not free

- **MEASURED (board):** the first card Track's 239x236 `APIC` (22,080 B) was decoded into the
  603x385 window in **691 ms** (open 5 ms), with 504,300 B free after the cover buffer
  (`README.md:605-609`; `docs/adr/0013-artwork-level-and-case-window.md:99-101`).
- **TJpgDec downscale is disabled in this build.** `JD_USE_SCALE` is `0`
  (`managed_components/lvgl__lvgl/src/libs/tjpgd/tjpgdcnf.h:15`), and `jd_decomp()` returns
  `JDR_PAR` for any `scale > 0` (`src/libs/tjpgd/tjpgd.c:1113`). The loader always passes `0`
  (`artwork_loader.c:300`, `:321`, `:383`). There is therefore no 1/2, 1/4 or 1/8 decode path.
  Enabling it would be a patch to the vendored decoder plus a new thumbnail loader, and for the
  measured 239x236 picture 1/8 is 30x30 (useless for a 160x160 card) and 1/4 is 60x59 (blurry);
  1/2 is 120x118 and would need upscaling. For large 4000x4000 covers downscale would help, but it is
  not the measured case and it is not available today.
- There is **no thumbnail cache**: the loader has one `s_art_buf`, one request path and one decode at a
  time (`artwork_loader.c:82-93`; `:507-551`). A five-card strip would have to decode five covers; at
  the measured 691 ms each that is **3.46 s serial** (ARITHMETIC; 3 = 2.07 s, 7 = 4.84 s). Card
  contention and the single loader task make this a lower bound, not a promise. A strip could show
  placeholders and fill in, but it would not be an instant Cover Flow.
- A 56x56 list-row thumbnail is 6,272 B; six of them are 37,632 B, so memory is not the problem. The
  decode is: six full decodes at ~691 ms each are ~4.1 s; without a downscaled decode or a cache,
  a thumbnail in every visible row is not viable. One selected-row thumbnail would still pay ~691 ms.

### 1.7 Verdict: no true Cover Flow; a 2D strip is possible but not worth it

**Verdict: true Cover Flow is not viable at 25 Hz on this device.** The requirements fail in this
order:

1. **Projection.** LVGL's image path is 2D affine (rotation/scale/pivot) and the ESP32-S3 has no PPA;
   there is no perspective/trapezoid texture mapping, and no image-widget matrix. A custom projective
   renderer would be new code, not a flag. This fails before any performance number.
2. **Transform throughput.** Even a 2D affine tilt of a full cover is >1.27 s/frame by the pixel
   extrapolation from the measured 135x135 hub; three covers >3.8 s/frame. The 40 ms frame budget is
   missed by more than an order of magnitude (≈32x).
3. **Memory.** One full cover leaves 39,990 B; 3/5/7 full covers need 1.39/2.32/3.25 MB against
   504,300 B. Even thumbnails need a deliberate cache and a new loader.
4. **Album index.** The Library has directories, not album/artist tags; Cover Flow's unit is the
   album, ordered by artist name. A tag index is a scan-time project.
5. **Decode latency.** 691 ms per cover measured; five covers ~3.5 s; TJpgDec scale is off and there
   is no thumbnail cache.

**The 2D alternative.** A flat horizontal strip can be built without perspective:

- 5 cards: 240x240 centre, four 160x160 sides, at y≈120-360; no rotation, no reflection, no
  perspective. Each card is an unscaled `lv_image`/`lv_imagebutton` (or a placeholder while decoding).
- Buffers: 240x240 × 2 = 115,200 B; four 160x160 = 204,800 B; total **320,000 B**, leaving 184,300 B
  before the decode band and LVGL heap.
- Interaction: horizontal drag with one-item detents (custom `dx` phase, or native snap-one if only 5
  cards exist); select opens the album's Tracks.
- What it loses: the tilted sides, the vanishing point, the reflection, and the large centre cover.
  It is a carousel, not Cover Flow. It still needs the album index of §1.5 and five decodes of
  §1.6. The existing Artwork Level already gives the one-large-cover experience at 464,310 B and one
  decode, with no new data model.
- Verdict: **technically possible, not worth P0/P1.** Revisit only if a tag/album index and a
  thumbnail cache are built for another reason, and only as a 2D strip.

Comparison of the options:

| Option | What it shows | Memory | Decode / latency | Album index | At 25 Hz | Verdict |
|---|---|---|---|---|---|---|
| True Cover Flow, 3-7 full covers | flat + tilted + reflection | 1.39-3.25 MB | 2.1-4.8 s serial | required | no: projection, transform, memory | **reject** |
| 2D strip, 5 cards | flat carousel | 320,000 B | ~3.5 s for five | required | possible once loaded; no transform | **P2 at most** |
| Existing Artwork Level | one 603x385 cover | 464,310 B | 691 ms, already measured | none | already working | **keep** |
| List row, 56x56 thumbnail | tiny cover per row | 6,272 B each | ~691 ms each; no downscale/cache | none | not with six rows | not P0 |
| No cover in menu | text list + now-playing strip | 0 | 0 | none | yes | **P0** |

---

## 2. The scroll: what LVGL actually does, and what one-row blocks require

### 2.1 The current input surface, and the gates a scroll must not trip

- The GT911 is polled every **10 ms** (`components/app/screen_display/touch_gt911.c:50-51`), reports
  sparsely while a finger is down and leaves the last point in the driver. The release is the absence
  of a point. LVGL is fed contact 0; the two-finger swipe is detected from the two contacts at a
  30 px midpoint threshold (`touch_gt911.c:53-56`, `:80-108`, `:125-177`).
- LVGL's one-finger gesture limit is **50 px** (`CONFIG_LV_INDEV_DEF_GESTURE_LIMIT=50`,
  `sdkconfig.sunton:4262`); the scroll limit is **10 px** (`CONFIG_LV_INDEV_DEF_SCROLL_LIMIT=10`,
  `:4250`); the scroll throw is 10% (`:4252`); long-press is 400 ms (`:4256`).
- The screen owns tap/gesture at the screen level: a tap is next Track, left/right is next/previous,
  up/down changes Level, two-finger is reserved for the menu (`cassette_view.c:911-973`, `:1149-1151`).
  Every one of those must be gated while the menu is open, and a drag must not reach them:
  `LV_OBJ_FLAG_GESTURE_BUBBLE` is on by default for any object with a parent
  (`src/core/lv_obj.c:1503-1508`), so a row/tile drag bubbles to the screen unless the screen handler
  is gated. (The menu design in `docs/research/player-menu-ipod-style.md` §2.3-2.4 already states this;
  this note only adds the scroll-specific consequences.)
- `make_decorative()` clears clickable on the existing tree (`cassette_view.c:235-250`). Menu objects
  must be created after that walk, or they will be dead to touch.

### 2.2 Native snap: the mechanics

`lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_START/CENTER/END)` sets a snap mode
(`src/core/lv_obj_scroll.c:98-145`). Snap points are **children**:

- `find_snap_point_y()` loops over `obj->spec_attr->children`, skips hidden/floating children, and
  considers only `lv_obj_is_snappable(child)` (`src/indev/lv_indev_scroll.c:578-600`).
- `SNAP_START` aligns the child's top to the parent's top plus padding; `CENTER` aligns the child's
  centre to the viewport centre; `END` aligns the child's bottom to the viewport bottom
  (`:580-610`).
- `lv_obj_set_scroll_one()` is a separate flag: "Allow scrolling only one snappable child"
  (`include/lvgl/core/lv_obj.h:59`, `:302-306`). It only affects pointer scrolling.
- All `lv_obj` children are snappable by default (`obj->snappable = 1`,
  `src/core/lv_obj.c:1496-1502`), so a spacer is a snap point unless it is explicitly made
  non-snappable (`lv_obj_set_snappable(spacer, false)`, `include/lvgl/core/lv_obj.h:340-342`).

On release/fling with a snap mode, `lv_indev_scroll_throw_handler()` predicts the throw, clamps it to
the scroll limits, finds a snap point and animates there (`src/indev/lv_indev_scroll.c:125-195`).
`lv_obj_scroll_by(..., LV_ANIM_ON)` uses a speed-derived duration clamped to **200-400 ms**
(`src/core/lv_obj_scroll.c:20-25`, `:347-383`); the animation's `LV_EVENT_SCROLL_BEGIN/END` are sent
by `lv_obj_scroll_by()` (`:365`, `:380`, `:391-397`).

### 2.3 Native `LV_OBJ_FLAG_SCROLL_ONE`: one snap point per gesture

When a pointer scroll starts, `lv_indev_scroll_handler()` calls `init_scroll_limits()`
(`src/indev/lv_indev_scroll.c:48-65`). With `scroll_one` off, the scroll area is unbounded. With it
on, the area is the previous/next snap point in each axis (`:448-509`):

- For `SNAP_START`, `y1` is the next snap point below the top and `y2` the previous one above it
  (`:458-461`).
- Every drag delta is clamped by `scroll_limit_diff()` so the accumulated scroll stays inside that
  area (`:627-642`).
- On fling, the predicted `diff` is clamped by the same area, then `find_snap_point_y()` finds the
  nearest snap within it and `lv_obj_scroll_by(..., LV_ANIM_ON)` animates (`:125-195`).

Consequences:

- **One gesture moves at most one snap point.** The content follows the finger *within* that one row,
  so partial rows are visible; when it reaches the next snap point it stops hard. It is not a
  block-by-block jump, and it is not free pixel scrolling beyond one row.
- **A fling also moves at most one row** (on a well-formed, static snap list). Moving through 100 rows
  takes 100 gestures; on a 20,000-track Playlist that is unusable. This is a UX cost independent of
  the board.
- **It works only if the snap points are static and complete.** That is the virtualization problem
  below.
- It is a native, small change: two calls plus the row/spacer flags. That is why it is the natural
  fallback for a bounded page.

### 2.4 Why native snap/scroll-one breaks on the virtualized list

The menu list is windowed: ~8 row objects plus one spacer, rebound to a moving `first` index. The
snap scan sees only those children:

1. **The next unseen row has no snap point.** If the window shows rows `first..first+7`, the row at
   `first+8` does not exist, so `find_snap_point_y()` cannot return it. At the window edge,
   `init_scroll_limits()` converts the "no snap point" `LV_COORD_MAX` to `LV_COORD_MIN`/`MAX`
   (`:500-509`), which can allow a free scroll past the intended one-row limit, or leave the area
   asymmetric. In the throw path, a missing snap point comes back as `LV_COORD_MAX` and is added to
   `diff_y` before the animation (`:162-163`), so the "snap" can aim far past the list.
2. **The scroll area is computed once, at scroll start.** `init_scroll_limits()` runs when the scroll object
   is first found (`:56-63`); it does not recompute if rows are rebound during `LV_EVENT_SCROLL`.
   `lv_obj_scroll_by_raw()` moves the children and then sends `LV_EVENT_SCROLL` before invalidating
   (`src/core/lv_obj_scroll.c:462-479`). A handler that rebinds/repositions rows changes the
   coordinates the snap scan will use later; the cached area is stale.
3. **The throw re-scans the new children.** On release, `find_snap_point_y()` scans the children as
   they are *then* (`lv_indev_scroll.c:162`, `:188`). If the window moved during the drag, the
   nearest snap point can be the wrong row, or none, and the animation can land between rows.
4. **The spacer is a child too.** Snappable by default, it becomes a snap point (typically at content
   y=0). It must be made non-snappable, and if it is hidden it is skipped but then no longer sets
   content height.

Conclusion: **native snap and `SCROLL_ONE` are safe only on a bounded, non-virtualized list** where
every item exists as a child and no row is rebound. For the 20,000-track Playlist, they are a
source of wrong snaps and hard stops. A paged list (all rows of the current page exist) is the
middle ground; the 20,000-item list is not.

### 2.5 The invalidation cost: a scroll step is the whole viewport, not one row

This corrects the "one row = ~0.35 MB" shorthand used in the request: the 0.35 MB is a two-row
selection-band move, not a scroll.

- `lv_obj_scroll_by_raw()` updates the scroll offset, calls `lv_obj_move_children_by()`, sends
  `LV_EVENT_SCROLL`, then calls `lv_obj_invalidate(obj)` (`src/core/lv_obj_scroll.c:462-479`). It
  invalidates the **scroll object**, not only the newly exposed row.
- The list viewport is 800x336 px (6 × 56). At RGB565 that is 268,800 px × 2 B =
  **537,600 B painted per scroll frame**.
- In direct double-buffered mode, LVGL copies each dirty area to the other framebuffer
  (`src/core/lv_refr.c:677-684`, `:782-784`); the same 537,600 B is copied again, so one scroll frame
  is **1,075,200 B (1.075 MB)** of PSRAM traffic. At 25 Hz that is **26.9 MB/s**, about 80 % of the
  panel's **33.7 MB/s** scan-out rate (`docs/research/player-menu-ipod-style.md` §4.2;
  `panel_rgb.c:32`), added on top of it while the list moves. The board must say whether the broadcast
  can tolerate it.
- A one-row *selection band* move invalidates the old and new row: 2 × 784 × 56 = 87,808 px × 2 B =
  175,616 B painted, and 351,232 B with the sync copy. That is the 0.35 MB figure — a band move, not
  a scroll.
- One row painted is 87,808 B; with the sync copy, 175,616 B (0.176 MB). A full-screen sheet change is
  800 × 480 × 2 = 768,000 B painted; 1,536,000 B with the sync copy.

The custom detent is cheaper than native scroll only if it invalidates less than the viewport:

- **Six labels only:** 6 × 640 × 42 = 161,280 px × 2 B = 322,560 B painted; 645,120 B with the sync
  copy. The scrollbar adds ~5 KB. This is the case if the row backgrounds are static and the selection
  band does not move.
- **Six labels plus a moving band:** add the two-row band 351,232 B, so ~996,352 B (~1.0 MB).
- **All six row slots repainted:** the conservative budget is the viewport, 1,075,200 B.

So a content step is **≤1.075 MB**, and a label-only step can be ~0.65 MB; the exact number depends on
which objects are invalidated and must be measured. What the detent changes is **how many frames
move**: native scrolling invalidates every frame while the finger moves, while a detent sits still in
the dead zone and invalidates only when a row boundary is crossed. One instant step is one frame; six
rows crossed in one drag are at most six frames (6.45 MB), not six native snap animations.

### 2.6 The options

| Option | How it works | One-row blocks? | Finger feel | Fling | Virtualized 20k list | Redraw per step | Code / risk |
|---|---|---|---|---|---|---|---|
| **A. Native snap on release** | `SNAP_START` + snappable rows; content follows finger; `find_snap_point` on release; animate | **No** during drag; release only | Continuous, familiar; partial rows | Can cross many rows | **No**: snap points only current rows | 1.075 MB per moving frame + 6-10 snap frames | Low; wrong for the requirement |
| **B. Native scroll-one** | A + `lv_obj_set_scroll_one(list, true)`; drag/fling clamped to next/previous snap point | **One per gesture**, not a block: partial rows until the hard stop | Follows finger within one row, then hard stop; 100 rows need 100 gestures | One row max (static list) | **No**: cached snap area, window edge, stale coords | 1.075 MB per moving frame + 6 snap frames | Low; only for bounded pages |
| **C. Custom detent** | no native scroll; fixed 6 row slots; logical `first`; accumulate finger `dy`; every 56 px step one row; rebind labels | **Yes**, exactly, and several rows per drag | Ratchet: finger travel is continuous, content jumps one row per 56 px | Optional, one row per predicted step; cap in P0 | **Yes**: independent of row objects | **≤1.075 MB per row crossed (label-only ~0.65); zero in dead zone** | Medium ~100-200 lines; input arbitration/tap-vs-drag |
| **D. Quantize native** | native scroll; in `LV_EVENT_SCROLL` round `scroll_y` to nearest row | Unstable | Jitter/partial rows; LVGL already moved the children | Fights snap | No | 1.075 MB per moving frame, no saving | **Reject**: re-entrancy, no frame saving |
| **Paged list** | 6 rows per page; no native scroll; a gesture flips one page | No: blocks of 6 | Page jumps; no partial rows | One page | **Yes** (page window) | 1.075 MB per page (one frame if instant) | Low/medium; robust; not one-row |
| **Selection band** | content fixed; highlight moves one row; content scrolls only at the edge | Yes, for the band; content only at edge | iPod-wheel-like; not content scroll | One row/step or edge scroll | **Yes** | **0.351 MB per band step** | Medium; cheaper, but not the owner's "content scroll" |

**What "normal finger scroll" can mean.** There are two incompatible readings:

- *Pixel-follow*: the content moves exactly with the finger, so partial rows are visible and the
  landing is a snap. A is the pure form; B is the one-row-capped form. Neither can guarantee
  "only one-row blocks" during the drag.
- *Ratchet*: the finger drags, but the content only changes when the finger has travelled a row
  height. C is this form. It is a normal finger gesture, but the content does not track the finger
  continuously. It is the only way to satisfy "elements move only in blocks of one row".

This tension cannot be designed away; it is the central decision the note puts to the owner.

### 2.7 Recommendation: C, the custom detent

**Recommendation: option C for the vertical list and, if a horizontal strip is ever added, the same
option there.** The exact algorithm:

```
state: first (top index), phase (px of finger travel), y_total, y_prev, max_move, dragging
count = number of rows/tracks
visible = 6
STEP = 56 px (one row), HYST = 8 px, TAP_SLOP = 10-16 px
```

- `LV_EVENT_PRESSED`: `phase = 0`, `y_total = 0`, `max_move = 0`, `dragging = false`, record
  `y_prev`.
- `LV_EVENT_PRESSING`: read the current point (`lv_indev_get_point()`) or the per-read vector
  (`lv_indev_get_vect()`); `dy = y_prev - y` (finger up is positive), `y_total += dy`,
  `y_prev = y`; `phase += dy`; `max_move = max(max_move, abs(y_total))`; once
  `max_move >= TAP_SLOP`, set `dragging = true`.
  - While `phase >= STEP + HYST`: `first++`, `phase -= STEP`; clamp
    `first` to `0..max(0, count - visible)`.
  - While `phase <= -(STEP + HYST)`: `first--`, `phase += STEP`; clamp at 0.
  - After the loops, rebind the six visible row labels and the scrollbar once; do not move the row
    objects. A fast read that crosses several thresholds therefore lands on the final `first` in one
    frame, not on each intermediate row.
- `LV_EVENT_RELEASED`:
  - if `max_move < TAP_SLOP`: hit-test the release y to a row and activate it;
  - else: no tap. Optionally add a fling step (see below).
- `LV_EVENT_PRESS_LOST`: reset `phase`, `dragging`, no activation.
- The list is `lv_obj_set_scrollable(list, false)`, `lv_obj_set_clickable(list, true)`. Rows and labels
  are non-clickable. The list ignores `LV_EVENT_CLICKED` (no-op); all activation is computed on
  release. This removes the LVGL click-vs-drag arbitration from the path.
- The content is a fixed pool of **6 rows** (the visible slots), not 8 and no spacer. The logical
  content height and the scrollbar are computed from `first/count`; the scrollbar is a 4 px child
  whose position/height is updated on a step (2,624 B of pixels, negligible). There is no native
  scroll, no `LV_EVENT_SCROLL`, and no snap point to misread.

**Jump, not slide.** Because the six row objects stay in their slots, a step reads as the list
**jumping** one row, not as six rows sliding. That is the cheap label-only frame (~0.65 MB). If the
owner wants a visible slide, add a seventh row, move all seven by -56 px for one frame, rebind and
reset; that pays the whole viewport (~1.075 MB) per step. Both are one-row blocks; the slide costs
~1.65x the traffic, and an animation would multiply the frames. Choose the jump or the slide; do not
animate it.

**Fast drag: coalesce or queue.** At 2,000 px/s a 10 ms read carries ~20 px, so a flick can cross
several thresholds in one event. The loop above coalesces them into the final `first`: one frame, but
the user sees a multi-row jump. If the literal invariant is "each visible move is exactly one row",
queue the steps and advance one per frame (one label frame per row). Coalescing is cheaper and
fling-like; queueing is the strict block-by-block reading. Pick one and do not mix them; the cost
table below assumes coalescing.

**Threshold.** 56 px is one row and gives a 1:1 "finger travels a row, content moves a row" feel.
A 40-48 px threshold feels more responsive but steps faster than the finger; a 28 px half-row doubles
the rate. Use 56 px + 8 px hysteresis; tune on the board. The touch poll is 10 ms; at 2,000 px/s a
read carries 20 px, so the `while` loop must handle multiple steps per event. Because the rebind is
once per event, no cap is needed: the list lands on the final `first` and invalidates one frame.

**Fling.** A custom fling is optional and must not become a continuous throw:

- P0: **one extra row** if the release velocity exceeds a threshold (e.g. 400 px/s), capped to 1.
  This keeps "one row per block" and one extra viewport frame.
- P2: predict `n` rows from velocity, cap at 3-5, and step them one at a time on subsequent frames;
  each step is one frame of 1.075 MB. This is the custom analogue of the patent's acceleration, but
  it must be measured before promising it.
- Do not animate the step. A 200 ms ease would turn one step into 5 frames of 1.075 MB; an instant
  step is one frame. The existing Level changes already use a one-frame swap for the same reason
  (`docs/research/player-menu-ipod-style.md` §4.2, §5.4).

**Why C is the recommendation:**

1. It is the only option that produces **exactly one-row blocks** and can cross several rows in one
   drag. It satisfies the owner's wording better than B.
2. It is the only option that is **safe with a 20,000-row virtualized Playlist**: the logical `first`
   index does not depend on which row objects exist, and there are no snap points to go stale.
3. It avoids LVGL's click/scroll arbitration: the list handles press/release, rows are not clickable,
   and a drag cannot produce a row click.
4. It has no dead-zone redraw: a slow drag that has not crossed a row boundary costs nothing; a native
   scroll costs a viewport frame on every touch read.

**Costs and risks of C:**

- ~100-200 lines of input/model code plus a hit test and a scrollbar; more than B.
- The content does not follow the finger pixel-for-pixel; it ratchets. This is the price of exact
  blocks and must be accepted by the owner.
- Input arbitration: the screen's 50 px gesture still bubbles; gate it. The list's press must not be
  stolen by a child; make rows non-clickable. A tap must be distinguished from a drag by `max_move`.
- The first row/last row must clamp; no elastic overscroll (there is no native scroll to provide it).
- The scrollbar is custom; if omitted in P0, the list still works, but the user loses position
  feedback on a long list.
- The row pool is fixed at six; if the design later wants a partially visible row for continuity, it
  needs a seventh object and a different model; do not mix the two.

**Fallbacks, in order:**

1. **B on a bounded page.** If a page contains all its rows (e.g. one folder's ≤50 Tracks, all row
   objects created, spacer non-snappable), `SNAP_START` + `SCROLL_ONE` gives one row per gesture and
   native finger-follow. It still shows partial rows and a hard stop, but it is little code and
   robust. It is not suitable for the 20k Playlist.
2. **Paged list.** Six rows per page, a gesture flips one page; no native scroll, no snap points;
   one instant frame per page (1.075 MB), exact page blocks. It is robust and simple, but it is not
   one-row scrolling and it loses the continuous browse feel.
3. **A or D only if the owner relaxes the requirement.** A is free scrolling with a snap; it is
   pleasant but can fling many rows. D is not recommended; the re-entrancy and jitter are not worth
   the experiment.

### 2.8 Vertical list and horizontal strip: the same detent, different axis

**Vertical menu list (the owner's main case).**

- Six rows, 56 px, viewport 800x336; `first` is the top index; step is 56 px.
- Content scroll (C): ≤1.075 MB per row crossed (label-only ~0.65); a 10-row drag is at most 10.75 MB
  over 10 frames (400 ms at 25 Hz). This is the price of content scroll.
- Selection band model: content does not move; only the band moves; 0.351 MB per step. If the owner
  wants the cheapest and most iPod-like feel, this is the alternative; it is not the "elements rise"
  content scroll he asked for.
- A page flip: 6 rows at once, 1.075 MB per page, one frame; cheap per distance, but jumps.

**Horizontal cover strip (only if Cover Flow is replaced by a 2D carousel).**

- A horizontal scroll container with 5 cards; one-card detents; no 3D, no reflection.
- Use the same custom detent on `dx`; step = card pitch (e.g. 160 px + 16 px gap = 176 px), or half
  pitch for a snappier feel; each step rebinds the 5 cards and the centre card.
- If only 5 cards exist and never virtualize, native `SNAP_CENTER` + `SCROLL_ONE` is safe and simpler;
  if the strip browses many albums, use the custom detent for the same reasons as the vertical list.
- Memory for 5 cards: 320,000 B (§1.4); decode: ~3.5 s; album index: required. This is not P0.
- The cards must be drawn **unscaled** at their final size; any per-frame scale/rotation reintroduces
  the >100 ms transform cost.

**Drag and fling for both axes:**

- Drag: the custom detent crosses as many row/card thresholds as the finger travels; each threshold is
  one block. This is "normal finger drag, one-row blocks".
- Fling: P0 one extra block; P2 velocity-scaled, capped. Native B is one block maximum on a static
  list; native A is unbounded.
- No inertial pixel throw in C: the content is always at a row boundary after a step, never between
  rows. This is the invariant that satisfies the owner.

### 2.9 Integration details that decide whether it works

1. **Gate the screen handlers.** The screen's `LV_EVENT_CLICKED`/`LV_EVENT_GESTURE` handlers must
   return while the menu is open (`cassette_view.c:911-973`; the main note's §2.3 table). A drag on
   the list still bubbles a gesture to the screen because `gesture_bubble` is default for children
   with a parent (`src/core/lv_obj.c:1503-1508`). Without the gate, a 50 px drag can change a Track or
   a Level while the list steps.
2. **Create menu objects after `make_decorative()`.** The walk clears clickable on the existing tree
   (`cassette_view.c:235-250`). The list and its parent sheet must be created after it, or the list
   receives no press. The rows in the custom detent are deliberately non-clickable; the list is
   clickable.
3. **Tap vs drag.** Use `max_move < TAP_SLOP` (10-16 px; LVGL's scroll limit is 10,
   `sdkconfig.sunton:4250`) as the tap threshold. A drag that crosses a row but returns to the start still counts as a drag
   (`max_move`), so it does not activate a row. A tap on a row must not reach the screen's click.
4. **Library lease.** The menu is the first UI consumer of the Library. Hold `library_acquire()` while
   the list shows `library_name()`/`library_relpath()` pointers, and release on exit; a rescan or card
   change can publish a new generation and free the old arena. The main note's P0/P1 risk covers this
   (`docs/research/player-menu-ipod-style.md` §6.1); it applies to the custom list as well.
5. **No spacer in C.** The custom list has no native scroll, so it needs no spacer and no
   `LV_OBJ_FLAG_SNAPPABLE`; if a native fallback (B) is used, the spacer must be explicitly
   non-snappable and every row must exist.
6. **Row pool and labels.** Six fixed row objects; rebind labels with `lv_label_set_text()` (copy) or
   `lv_label_set_text_static()` only while the Library lease is held. Long labels use
   `LV_LABEL_LONG_MODE_DOTS`; never `SCROLL_CIRCULAR` (it invalidates every frame, and `lv_list`
   forces it — the main note §3.1).
7. **Scrollbar.** A custom 4 px bar updated from `first/count` is cheap (2,624 B per update); the
   native `LV_PART_SCROLLBAR` is not available without native scroll. If P0 omits it, add a simple
   "12 / 64" count in the title bar instead.
8. **The now-playing strip.** It is a child of the menu sheet and updates at 25 Hz; only touch its
   labels when values change, or it becomes its own per-frame invalidation. The strip is not part of
   the scrolling list.
9. **Two-finger and backlight.** The two-finger callback is reserved for menu on/off
   (`cassette_view.c:888-900`); keep it as the global escape. The first press after a dim only wakes
   the screen (`touch_gt911.c:145-168`); a drag cannot both wake and step, which is correct.

---

## 3. Decision table for the owner

| # | Decision | Options | Recommendation | Why / cost |
|---|---|---|---|---|
| 1 | True Cover Flow | build / drop | **Drop** | no perspective; >1.27 s/cover transform extrapolated; 3 covers memory 1.39 MB > 504,300 B; no album index; 691 ms/cover decode |
| 2 | Cover Flow substitute | 2D strip / list thumbnails / none | **None for P0/P1; 2D strip only P2** | 5-card strip 320,000 B + ~3.5 s decode + album index; loses the look; Artwork Level already gives one large cover |
| 3 | Scroll mechanics | A native snap / B scroll-one / C custom detent / D quantized / paged | **C custom detent**; B only on a bounded page; paged as a fallback | C is the only exact one-row, virtualized-safe option; B is simpler but partial-row/hard-stop and unsafe at 20k |
| 4 | Content vs selection band | content scroll / band moves | **Content scroll (C)**, per the owner | content: ≤1.075 MB/step (label-only ~0.65); band: 0.351 MB/step but the items do not rise |
| 5 | Drag vs fling | drag only / one-row fling / velocity fling | **Drag + one extra row in P0; velocity P2** | each row is one frame; velocity fling multiplies frames; measure |
| 6 | Vertical vs horizontal | vertical only / both | **Vertical first; horizontal only with a 2D strip** | horizontal needs album data/cache; no 3D |
| 7 | Row threshold | 40 / 48 / 56 px | **56 px + 8 px hysteresis** | 1:1 with a row; tune on the board |
| 8 | Fallback if C feels bad | B / paged / A | **B on a bounded page, else paged** | B is native and small; paged is robust; A violates the one-row intent |
| 9 | Cover Flow re-open conditions | — | album/tag index + thumbnail cache + measured 2D transform (or a future accelerator) | without all three, it is a demo, not a product |

---

## 4. What is proved, and what only the board can settle

### Proved, with the proof

| Claim | How it is proved |
|---|---|
| Classic p.26 and nano p.37-38 describe Cover Flow as album artwork/title/artist, albums alphabetically by artist, wheel-driven | Apple guides, p.26 and p.37-38 |
| The Cover Flow arrangement is flat centre + tilted sides | US 8,230,360 B2, claim 1 |
| Apple's Cover Flow appearance patents are design patents whose single claim is ornamental (no method claim) | USD613300S1, USD624932S1 (design patents) |
| LVGL's image widget is 2D rotation/scale/pivot only; no perspective | `include/lvgl/widgets/lv_image.h:115-188`; `src/draw/sw/lv_draw_sw_transform.c:18-28`, `:141-161`, `:223-233`; `LV_USE_MATRIX` off (`sdkconfig.sunton:4081`) |
| The ESP32-S3 has no PPA and this build disables use | `SOC_PPA_SUPPORTED` absent from S3 `soc_caps.h`; only P4:116/S31:103; `CONFIG_LV_USE_PPA` off (`sdkconfig.sunton:4191`) |
| The repo measured >100 ms/frame for a 135x135 hub transform | `cassette_view.c:31-34`; commit `d39e1a2` records the 135x135 hubs |
| One 603x385 cover is 464,310 B and only 504,300 B is free | `README.md:605-609`; `artwork_loader.c:60-63`, `:89-93`; ADR 0013:99-101 |
| TJpgDec downscale is disabled; `jd_decomp(scale>0)` returns `JDR_PAR` | `tjpgdcnf.h:15`; `tjpgd.c:1113`; loader passes 0 (`artwork_loader.c:300`, `:321`, `:383`) |
| The Library has no album/artist tag index | `library.h:1-15`, `:107-132`; `mini_icecast.h:92-104` |
| A scroll step invalidates the whole list object, not one row | `lv_obj_scroll.c:462-479` |
| A scroll snap is 200-400 ms; a 56 px row step is ≈233 ms (≈6 frames at 40 ms) | `lv_obj_scroll.c:20-25`, `:347-383`; `screen_display.c:61`, `:342-343` |
| Native snap points are current children only; `SCROLL_ONE` uses them at scroll start | `lv_indev_scroll.c:448-509`, `:578-600` |
| A drag suppresses the click in native LVGL | `lv_indev.c:1569-1581` |
| Gestures bubble from a row to the screen unless gated | `core/lv_obj.c:1503-1508`; `lv_indev.c:1839-1844` |

### Only the board can settle

1. **The feel of C.** Does a 56 px ratchet feel like a normal finger scroll, or too sticky? Test 40,
   48 and 56 px with 0/6/8 px hysteresis; 20 drags each. This is subjective and cannot be settled
   from source.
2. **The real frame time of a 6-row rebind.** The 1.075 MB is traffic, not milliseconds; enable the
   sysmon label (`CONFIG_LV_USE_SYSMON` + `CONFIG_LV_USE_PERF_MONITOR`) and read `render | flush`,
   `refr_cnt` vs `redraw_cnt`. Does one step fit in 40 ms? Does a 10-row drag hold the cadence?
3. **Tap latency and false gesture classification** on the list with the GT911 at 10 ms; log
   `LV_EVENT_CLICKED` vs `LV_EVENT_GESTURE` for taps and drags.
4. **Whether native B actually breaks at the window edge** on the real 20k list: does it stop, snap
   wrong, or free-scroll? The source says it can; the board should confirm before dismissing it.
5. **The 2D strip, if pursued:** five-cover memory with a decode band, decode time, and whether a
   cache can be populated fast enough. No board number exists for a thumbnail decode.
6. **Whether TJpgDec `JD_USE_SCALE` can be enabled profitably** for large covers, and what it costs in
   code/flash; not assumed here.
7. **PSRAM contention/encoder starvation** with the menu open; the menu's 1.075 MB/frame shares the
   bus with the panel scan-out and the broadcast. The existing soak reports encoder % and starvation
   rebases; use it.

### What this note could not verify

- The exact projection/reflection of the product Cover Flow rendering: the manual describes the
  behavior, the utility patent claims flat/tilted items, the design patents' single ornamental claims
  show the appearance but do not describe a rendering method. The perspective/reflection is treated as
  the product look, not as a documented algorithm.
- The exact decode time of a 160x160 or 56x56 thumbnail: no such measurement exists; the 691 ms figure
  is for a 239x236 APIC fitted to 603x385. Any thumbnail time is a must-measure.
- The feel of native `SCROLL_ONE` on this panel; no finger measurement exists.
- Whether the album/tag grouping could be added cheaply: it is a scan-time project whose cost depends
  on the tag parser and the card; not estimated as a number here.

---

## 5. Artefacts cited

### Local, by path and line

- `components/app/screen_display/cassette_view.c` — `:31-34` (no rotation, >100 ms measurement),
  `:235-250` (`make_decorative`), `:888-900` (two-finger reserved), `:911-982` (screen gesture/click),
  `:1149-1151` (event registration).
- `components/app/screen_display/touch_gt911.c` — `:50-56` (10 ms, 30 px two-finger), `:125-177`
  (contact handling, wake latch).
- `components/app/screen_display/artwork_loader.c` — `:60-62` (603x385), `:71-73` (JD_USE_SCALE off),
  `:82-93` (one request, one cover buffer), `:279-325` (stream/crop fit, `jd_decomp(...,0)`),
  `:376-383` (Case decode), `:507-551` (one cover, memory logs).
- `components/app/screen_display/screen_display.c` — `:61` (40 ms), `:342-343`, `:356-357` (refresh
  timer), `:411-430` (screen task).
- `components/app/screen_display/panel_rgb.c` — `:32` (18 MHz), `:107-122` (two PSRAM framebuffers,
  direct mode, avoid_tearing).
- `components/portable/library/include/library.h` — `:1-15` (tree, no tags), `:107-132` (browse/playlist).
- `components/portable/mini_icecast/include/mini_icecast.h` — `:92-104` (status fields; no album/artist).
- `README.md` — `:605-609` (691 ms, 971,248 B, 504,300 B, 768,000 B).
- `docs/adr/0013-artwork-level-and-case-window.md` — `:99-101` (decode/memory).
- `docs/research/player-menu-ipod-style.md` — the menu, levels, gesture gates and the earlier
  invalidation arithmetic; this note separates that note's 0.351 MB band move from a scroll step in
  §2.5.
- `docs/research/panel-rgb-lvgl-performance.md` — direct-mode sync and the panel's invalidation
  background; used through the earlier note's §4.2.

### Vendored LVGL 9.6.0, by path and line

- `include/lvgl/widgets/lv_image.h:115-188` — rotation/scale/pivot, `uint32_t` scale.
- `src/draw/sw/lv_draw_sw_transform.c:18-28`, `:141-161`, `:223-233` — 2D `sin/cos/scale/pivot`.
- `src/draw/lv_draw_image.c:72-73`, `:113-114` — no zero/negative scale.
- `src/core/lv_obj_scroll.c:20-25` — 200-400 ms scroll animation; `:98-145` snap setter/getter;
  `:293-331` bounded scroll; `:347-383` animated scroll; `:462-479` raw scroll moves children, sends
  `LV_EVENT_SCROLL`, invalidates the object.
- `src/indev/lv_indev_scroll.c:48-65` — scroll start/limits; `:125-195` throw/snap; `:448-509`
  `init_scroll_limits`; `:578-642` `find_snap_point_y`/clamp.
- `src/core/lv_obj.c:1496-1502` — default scroll/snappable flags; `:1503-1508` gesture bubble.
- `src/indev/lv_indev.c:1569-1581` — drag suppresses click; `:1839-1844` gesture bubble.
- `include/lvgl/core/lv_obj.h:59`, `:65`, `:302-306`, `:340-342` — `SCROLL_ONE`, `SNAPPABLE`,
  `lv_obj_set_scroll_one`, `lv_obj_set_snappable`.
- `sdkconfig.sunton:4081` (LV_USE_MATRIX off), `:4191` (LV_USE_PPA off), `:4250` (scroll limit 10),
  `:4252` (throw 10), `:4262` (gesture limit 50), `:4286` (float off).
- `src/libs/tjpgd/tjpgdcnf.h:15` (`JD_USE_SCALE 0`); `src/libs/tjpgd/tjpgd.c:1113`
  (`scale > 0` → `JDR_PAR`).
- `Kconfig:377-389` (matrix), `:936-952` (PPA, simple images only).

### Apple, by document and page

- **iPod classic 160 GB (Late 2009) User Guide**, Apple, 2009, 58 pp. PDF:
  <https://cdsassets.apple.com/live/6GJYWVAV/user/ma1195_ipod_classic_160gb_user_guide.pdf>.
  Cover Flow section p.26 ("Browsing music using Cover Flow").
- **iPod nano 5th generation User Guide**, Apple, 2009, 104 pp. PDF:
  <https://cdsassets.apple.com/live/6GJYWVAV/user/ma1194_ipod_nano_5th_gen_userguide.pdf>.
  Cover Flow section p.37-38 ("Browsing Music Using Cover Flow").
- **US 8,230,360 B2**, "User interface for selection from media collection", Apple Inc., priority
  2008-01-04, granted 2012-07-24. Claim 1 (flat/tilted states).
  <https://patents.google.com/patent/US8230360B2/en>.
- **USD613300S1**, "Animated graphical user interface for a display screen or portion thereof",
  Imran Chaudhri, Apple Inc., filed 2007-06-28, granted 2010-04-06. Design patent; its single claim is
  the ornamental design.
  <https://patents.google.com/patent/USD613300S1/en>.
- **USD624932S1**, same title/inventor/assignee, filed 2007-06-28, granted 2010-04-06. Design patent;
  its single claim is the ornamental design. <https://patents.google.com/patent/USD624932S1/en>.

### ESP-IDF, for the PPA negative

- `~/esp/esp-idf-6.1/components/soc/esp32p4/include/soc/soc_caps.h:116` and
  `esp32s31/include/soc/soc_caps.h:103` define `SOC_PPA_SUPPORTED`; the ESP32-S3
  `components/soc/esp32s3/include/soc/soc_caps.h` has no such define. The S3 therefore has no PPA to
  enable, independently of `CONFIG_LV_USE_PPA=n`.

# 01 — Cassette body: the "Bare" screen (800×480)

Implementation spec for a pixel-for-pixel HTML/SolidJS redraw. Every number below is
either read off the C or computed from it with the C's own integer rules; the computed
ones are marked *(computed)* and reproduced as worked values so the front-end never has
to run C arithmetic in their head.

**"Bare" = the resting entry view on the dark page**: screen backdrop + back cover +
the whole tape body at pose **256/0/0**, no chrome. Everything else in the firmware is
absent or hidden in this state and is documented here only where the same module owns
it (edge mark, cover, meter, the other two poses).

| hidden / absent on Bare | why |
|---|---|
| VU meter | created hidden; only a two-finger swipe shows it (`cassette_meter.c:132-136`) |
| white-page edge mark | hidden until the page turns white (`cassette_view.c:218-219,499`) |
| Track cover | hidden, and has no image until the loader hands one over (`cassette_cover.c:337-338`) |
| window / drawer / DJ block / bands / peek / menu | chrome, all created hidden and raised by gestures |

**Two calls to verify by eye if the sim disagrees:**
1. "Bare" is read as *the resting entry view, dark page, 256/0/0, no chrome* (the state the
   firmware boots into and returns to on every up-swipe). If the sim's "Bare" means
   something else, §1.5 has the rectangles for the other two poses.
2. The firmware's "white page" is `#909090`, not `#FFFFFF` (`style.h:28` → `style.c:7` →
   `view:217`). The `.scratch/controls-white-preview.py` mock uses `#FFF`; the panel code
   does not.

**Source legend** (paths relative to the repo root):

| short | file |
|---|---|
| `tapes` | `components/app/screen_display/cassette_tapes.c` |
| `tapes.h` | `components/app/screen_display/cassette_tapes.h` |
| `meter` | `components/app/screen_display/cassette_meter.c` |
| `view` | `components/app/screen_display/cassette_view.c` |
| `cover` | `components/app/screen_display/cassette_cover.c` |
| `window` | `components/app/screen_display/cassette_window.c` |
| `widgets` | `components/app/screen_display/cassette_widgets.c` |
| `style.c` / `style.h` | `components/app/screen_display/cassette_style.c` / `.h` |
| `layout.c` / `layout.h` | `components/portable/cassette/cassette_layout.c` / `include/cassette_layout.h` |
| `model.c` / `model.h` | `components/portable/cassette/cassette.c` / `include/cassette.h` |
| `artwork` | `components/app/screen_display/artwork_loader.c` |
| `buttons.h` | `components/app/screen_display/cassette_buttons.h` |
| `lvgl` | `managed_components/lvgl__lvgl` |

Panel: 800×480. 1 CSS px = 1 device px. Stage origin is the panel's top-left; all
coordinates below are stage coordinates. LVGL box convention: a box at `(x,y,w,h)`
covers columns `x … x+w-1`, rows `y … y+h-1`; **borders and strokes are drawn inside
the box** (`lvgl/src/draw/sw/lv_draw_sw_border.c:53-76`) — i.e. CSS `box-sizing:border-box`.

---

## 1. Design grid and pose

### 1.1 Constants

| constant | value | source |
|---|---|---|
| design box | 800 × 480 | `layout.h:22-23` |
| `CASSETTE_SCALE_NONE` | 256 (pixel-for-pixel) | `layout.h:24` |
| scale clamp | 32 … 1024 | `layout.h:25-26` |

### 1.2 Math (exact, integer)

```
L(pose, v)   = trunc( v * pose.scale / 256 )          // C int division, v ≥ 0  (layout.c:21-24)
Le(pose, v)  = L(pose, v) & ~1                        // round DOWN to even       (layout.c:26-34)
originX(pose)= trunc( (800 - L(pose,800)) / 2 ) + pose.dx   (layout.c:36-39)
originY(pose)= trunc( (480 - L(pose,480)) / 2 ) + pose.dy   (layout.c:41-44)
X(pose, x)   = originX(pose) + L(pose, x)                    (layout.c:46-49)
Y(pose, y)   = originY(pose) + L(pose, y)                    (layout.c:51-54)
```

Rounding rules that matter:
* lengths **truncate**; they do not round. At 134/256 the hub's design y 206 becomes
  `107` (206·134/256 = 107.80) and the hub's design x 210 becomes `109` (109.92) — a
  `Math.round` implementation would be 1 px off on both hubs, and the ring radii would
  drift with it;
* a *diameter* that must stay centred on the pixel grid goes through `Le` (even);
* the origin's division is integer division of a non-negative number → floor;
* `dx/dy` are added **after** the division, in raw pixels, and are not scaled.

### 1.3 The three poses

| pose | scale | dx | dy | originX | originY | scaled box | used for | source |
|---|---|---|---|---|---|---|---|---|
| rest / entry | 256 | 0 | 0 | 0 | 0 | 800×480 | **Bare** | `view:116`, `layout.h:24` |
| peek ("Label") | 200 | 0 | −60 | 87 | −8 | 625×375 | peek up | `view:71,138-140` |
| controls ("buttons") | 134 | −51 | −20 | 140 | 94 | 418×251 | drawer + DJ block + white page | `view:72-74,134-137` |

*(computed)* origins: `256 → (0,0)`, `200 → (87,−8)`, `134 → (140,94)`.

Only one pose is live; the drawer wins over the peek (`view:129-143`).

### 1.4 Pose animation

`lv_anim` on 0…1024, duration **260 ms** (`CASSETTE_ROOM_MS = CASSETTE_BUTTONS_SLIDE_MS`,
`view:75, buttons.h:55`), path `ease_out` = `cubic-bezier(0, 0, 0.58, 1)`
(`lvgl/src/misc/lv_anim.c:310-315`). Per step:

```js
scale = from.scale + Math.trunc((to.scale - from.scale) * step / 1024);
dx    = from.dx    + Math.trunc((to.dx    - from.dx)    * step / 1024);
dy    = from.dy    + Math.trunc((to.dy    - from.dy)    * step / 1024);
```
(`layout.c:56-68` — integer lerp, so the pose is quantised to whole pixels/scale units.)
A change in flight starts from the pose currently on screen (`view:294-306`).

### 1.5 Every rectangle, all three poses

Design values are the 256 pixels; the other two columns are `L`/`X`/`Y` applied
*(computed)*.

| element | design (256/0/0) | 200/0/−60 | 134/−51/−20 |
|---|---|---|---|
| back cover (fixed, **not** posed) | (16,16) 768×448 | same | same |
| frame[0] outer | (16,16) 768×360, r16 | (99,4) 600×281, r12 | (148,102) 402×188, r8 |
| frame[1] inner | (122,122) 556×168, r16 | (182,87) 434×131, r12 | (203,157) 291×87, r8 |
| chip 0 | (120,440) 50×20, r8 | (180,335) 39×15, r6 | (202,324) 26×10, r4 |
| chip 1 | (360,430) 80×30, r8 | (368,327) 62×23, r6 | (328,319) 41×15, r4 |
| chip 2 | (630,440) 50×20, r8 | (579,335) 39×15, r6 | (469,324) 26×10, r4 |
| roller ring Ø | 38 | 28 | 18 |
| roller 0 box | (20,412) 38×38 | (103,315) 28×28 | (151,311) 18×18 |
| roller 1 box | (742,412) 38×38 | (667,315) 28×28 | (529,311) 18×18 |
| runner (line) | (39,450)→(761,450) | (117,343)→(681,343) | (160,329)→(538,329) |
| hub 0 centre | (210,206) | (251,152) | (249,201) |
| hub 1 centre | (594,206) | (551,152) | (450,201) |
| rim 0 box | (126,122) 168×168 | (186,87) 131×131 | (206,158) 87×87 |
| rim 1 box | (510,122) 168×168 | (486,87) 131×131 | (407,158) 87×87 |
| pack ring gap | 10 | 7 | 5 |
| pack ring stop radius | 84 | 65 | 43 |
| spoke radii r0/r1 | 66 / 72 | 51 / 56 | 34 / 37 |
| rim border | 2 | 2 | 2 |
| stroke widths | 1 / 2 / 4 | **same** | **same** |

**Stroke widths never scale.** Only positions, sizes and radii go through `L`/`X`/`Y`:
the frames, chips and rollers keep border 1, the rim 2, the pack rings 1, the run and the
strands 2, the spokes 4 at every pose (they are set once in `tapes:502-579`; `place()`
re-sets only geometry, `tapes:346-390`).

Note `pack ring stop radius` is `trunc(L(pose,168)/2)` — integer division, so 87/2 = 43
at 134 (`tapes:290`).

The back cover and the `window` frame are **fixed screen furniture** and do not move with
the pose (`view:442`); every other row above is a child of the cassette container and is
re-placed on every pose step (`tapes:346-390`).

---

## 2. Palette (exact)

`COL_HEX(c)` = `#rrggbb`; `COL_DARKEN_HEX(c,pct)` multiplies each channel by `pct/100`
with integer truncation (`style.h:19-24`). `COL_PRIMARY = 0xF15A22`, `COL_BLACK = 0x111111`,
`COL_WHITE = 0x909090` (`style.h:26-28`).

| name | hex | derivation | used by |
|---|---|---|---|
| `k_col_bg` | `#111111` | `COL_BLACK` | screen backdrop, back cover, chips' fill, roller fill (unused), meter letters |
| `k_col_frame` | `#111111` | `COL_BLACK` | chips' fill (bg_opa 255) |
| `k_col_tape` | `#F15A22` | `COL_PRIMARY` | pack rings, strands, run, roller ring, chips' **border** |
| `k_col_tape_dark` | `#F15A22` | `COL_PRIMARY` | hub rim border, chips' border |
| `k_col_hub_ring` | `#301206` | primary × 20 % | hub rim **fill** |
| `k_col_hub_ink` | `#C0481B` | primary × 80 % | hub spokes |
| `k_col_back` | `#60240D` | primary × 40 % | frame[0]/frame[1] **border** |
| `k_col_glass` | `#8A8A8A` | `0x909090 × 96 %` | white-page edge mark border |
| `k_col_white` | `#909090` | `COL_WHITE` | the "white" page backdrop — **grey, not #fff** |
| `k_col_meter_dim` | `#7F2C18` | literal | meter track bars |
| `k_col_meter_bar` | `#CC4C1C` | primary × 85 % | meter lit fill |
| `k_col_meter_lit` | `#F15A22` | `COL_PRIMARY` | L/R badge |
| `k_col_meter_back` | `#481B0A` | primary × 30 % | meter row background |

(`style.c:6-39`.) **Gotcha:** `k_col_tape == k_col_tape_dark == #F15A22`, so the rewind's
"dim the rim" mix (`tapes:458-459`, `lv_color_mix(tape_dark, tape, ink)`) is a visual
no-op — the hub rim never changes colour. Do not animate it.

---

## 3. Elements

Coordinates are the **256/0/0** design values unless stated. All are drawn with
`box-sizing: border-box`. Opacity `COVER` = 255 unless stated.

### 3.1 Screen backdrop

| element | rect | fill | source |
|---|---|---|---|
| screen root | (0,0) 800×480 | `#111111`, opa 255 | `view:438` |

### 3.2 Back cover

| element | rect | radius | fill | border | source |
|---|---|---|---|---|---|
| back cover | (16,16) 768×448 | 0 | `#111111` opa 255 | none | `view:442` |

Fixed (not posed). It is the cassette's shell: on the dark page it is invisible against
the backdrop; on the grey page it reads as the dark body. The frames' transparent fill
lets it show through.

### 3.3 The two frames (borders only, no fill)

| element | rect | radius | bg | border |
|---|---|---|---|---|
| frame[0] outer | (16,16) 768×360 | 16 | **transparent** (`bg_opa 0`) | 1 px `#60240D`, all sides |
| frame[1] inner | (122,122) 556×168 | 16 — or **43** while the cover is up (see §6) | transparent | 1 px `#60240D`, all sides |

`tapes:502-503` (style: `cassette_border_box(parent,…, k_col_frame, 0, k_col_back, 1)`),
`tapes:353-357` (placement), `tapes:356` (`r1 = cover_print ? min(L(556),L(168))/2 : L(16)`).

Consequences worth copying literally:
* the outer frame is only 360 tall; the shell (`#111111`) continues to y=463 behind it;
* frame[1] height 168 = the hub diameter, and its rounded ends (radius 84 = hub radius)
  are concentric with the hubs when the cover is up — but at rest it is a 16 px rect;
* the frames are 1 px hairlines: outer occupies columns 16 and 783 and rows 16 and 375.

### 3.4 The three bottom chips

| i | rect | radius | fill | border |
|---|---|---|---|---|
| 0 | (120,440) 50×20 | 8 | `#111111` opa 255 | 1 px `#F15A22` |
| 1 | (360,430) 80×30 | 8 | `#111111` opa 255 | 1 px `#F15A22` |
| 2 | (630,440) 50×20 | 8 | `#111111` opa 255 | 1 px `#F15A22` |

`tapes:119-125` (rects, `CHIP_R 8`), `tapes:507-509`
(`cassette_border_box(parent,0,0,1,1,0, k_col_frame, 255, k_col_tape_dark, 1)`).

Chip 1 is 10 px higher and 10 px taller than its neighbours; chips 0 and 2 are mirrored
about x=400 (120 and 800−120−50 = 630). They are drawn **before** the tape, so the 2 px
red run crosses all three — that overlap is intended.

### 3.5 Tape rollers (the two small rings under the tape)

| element | rect | diameter | radius | fill | stroke |
|---|---|---|---|---|---|
| roller 0 | (20,412) 38×38 | 38 | circle (19) | transparent | 1 px `#F15A22` (inside) |
| roller 1 | (742,412) 38×38 | 38 | circle | transparent | 1 px `#F15A22` |

Geometry chain (`tapes:80-96`, `tapes:369-373`):
```
gd  = Le(pose, 38)          // even diameter:  256→38, 200→28, 134→18
gr  = gd / 2                // 19, 14, 9
gy  = Y(pose, 450) - gr     // centre y: 431, 329, 320
gx0 = X(pose, 39)           // centre x: 39, 117, 160
gx1 = X(pose, 761)          // centre x: 761, 681, 538
box = (gx - gd/2, gy - gd/2, gd, gd)
```
So the roller box's bottom row (431+19−1 = **449**) is the run's first row, and the ring's
outer edge sits **20 px** from the panel edge (39−19 = 20; the "16 px margin" in the C
comment at `tapes:84` is stale — trust the numbers). Style: `cassette_box(…,
LV_RADIUS_CIRCLE, k_col_bg)` then `bg_opa TRANSP`, `border_color k_col_tape`,
`border_opa COVER`, `border_width 1` (`tapes:529-535`). The 1 px border is inside the
38 px box: outer radius 19, inner 18.

### 3.6 The run (bottom horizontal tape)

A single `lv_line` from (39,450) to (761,450), stroke 2, `#F15A22`, butt caps, object at
0,0 with screen-space points (`tapes:539-543`, `tapes:378-384`, `tapes:203-219` for the
stroke style). Axis-aligned LVGL lines are pixel-snapped rects
(`lvgl/src/draw/sw/lv_draw_sw_line.c:107-121`): a 2 px line at `y` covers rows `y−1, y`
and columns `x1 … x2−1`.

**Exact pixels:** rect `(39, 449) 722×2` (columns 39…760, rows 449…450).
At 200: `(117,342) 564×2`; at 134: `(160,328) 378×2`.

It is drawn after the rollers and before the strands, so it covers the rollers' bottom
1 px row.

### 3.7 Side tape paths (strands)

One polyline per side: outer tangent from the pack → tangent point on the roller → a
10-step wrap around the roller to its bottom. Stroke **2 px**, `#F15A22`, **butt caps, no
joins** — LVGL draws each segment separately with square ends
(`lvgl/src/draw/sw/lv_draw_sw_line.c:54-77`, `round_start/round_end` are 0 because
`lv_draw_line_dsc_init` zeroes them, `lvgl/src/draw/lv_draw_line.c:36-45`). Joint gaps are
< 0.2 px at 9° per step; ignore them or use `stroke-linejoin:miter`.

Algorithm, exactly as `tapes:145-178` (all inputs already in **screen** pixels):

```
cx, cy = X/Y(pose, hub[i])              // (210,206) or (594,206) at 256
R      = L(pose, packD) / 2             // pack outer radius, float divide
gx     = X(pose, i ? 761 : 39);  gr = gd/2;  gy = Y(pose,450) − gr
dx = gx−cx;  dy = gy−cy;  len = hypot(dx,dy);  phi = atan2(dy,dx)
alpha = acos((R − gr) / len)
a     = phi + (i ? −alpha : +alpha)     // i=0: left side, i=1: right side
sweep = π/2 − a;  wrap into (−π, π]
pts[0] = (round(cx + R·cos a),  round(cy + R·sin a))     // on the pack
pts[1] = (round(gx + gr·cos a), round(gy + gr·sin a))    // on the roller
for k = 1..10:  t = a + sweep·k/10
                pts[1+k] = (round(gx + gr·cos t), round(gy + gr·sin t))
```
`round` = C `lround` (half away from zero; for these positive values, `Math.round` matches).

**Bare-screen values** (fill 1000 left = `packD` 356, fill 80 right = `packD` 190):

| side | polyline (256/0/0) |
|---|---|
| 0 (left, fill 1000) | (32,197) (20,430) (20,433) (21,436) (22,439) (23,442) (25,444) (28,446) (30,448) (33,449) (36,450) (39,450) |
| 1 (right, fill 80) | (683,172) (779,424) (780,428) (780,431) (780,435) (778,439) (777,442) (774,445) (771,447) (768,449) (765,450) (761,450) |

Mid-fill reference (fill 500 → `packD` 278): side 0 starts (74,177), side 1 starts
(730,179); the last two points are unchanged because the roller does not move.

**134 pose reference** (fill 1000 / 80):
side 0 `(156,197) (151,320) (151,321) (151,322) (152,324) (153,325) (154,326) (155,327)
(156,328) (157,329) (159,329) (160,329)`; side 1 `(543,198) (547,320) (547,321) (547,323)
(546,324) (545,325) (544,326) (543,327) (542,328) (541,329) (539,329) (538,329)`.

### 3.8 Packs (the tape wound on each hub)

A pack is **not a filled disc**: it is a stack of 1 px concentric **line circles** in
`#F15A22`, spaced `gap` apart, from the fill's radius inward, stopping at the hub rim
(which covers what is under it). Only rings outside the rim's radius are drawn, so an
empty pack draws two and a full one draws ten.

```
D    = packDiameter(fill)               // §7.2, even, design px
R0   = L(pose, D) / 2                   // outer ring radius, screen px
gap  = max(1, L(pose, 10))              // 10 / 7 / 5 for 256 / 200 / 134
stop = trunc(L(pose, 168) / 2)          // 84 / 65 / 43
radii = R0, R0−gap, R0−2·gap, …  while r > stop
```
(`tapes:270-294`, `tapes:287-291`.) Each ring is a **1 px inside-aligned circle at radius
r**: LVGL's arc becomes a border of width 1 on the box `(cx−r, cy−r, 2r, 2r)` with radius
`min(w,h)/2` (`lvgl/src/draw/lv_draw_arc.c:64-71`,
`lvgl/src/draw/sw/lv_draw_sw_arc.c:66-77`). So in CSS each ring is identical to the rim:

```html
<div style="position:absolute; left:CX−r+px; top:CY−r+px; width:{2r}px; height:{2r}px;
            box-sizing:border-box; border:1px solid #F15A22; border-radius:50%"></div>
```
(or SVG `circle r="{r−0.5}" stroke-width="1" fill="none"`).

Ring radii at 256 for representative fills *(computed)*:

| fill | D | radii |
|---|---|---|
| 80 (min) | 190 | 95, 85 |
| 200 | 218 | 109, 99, 89 |
| 400 | 260 | 130, 120, 110, 100, 90 |
| 500 | 278 | 139, 129, 119, 109, 99, 89 |
| 700 | 312 | 156, 146, 136, 126, 116, 106, 96, 86 |
| 900 | 342 | 171, 161, 151, 141, 131, 121, 111, 101, 91 |
| 1000 (max) | 356 | 178, 168, 158, 148, 138, 128, 118, 108, 98, 88 |

**Bare:** pack 0 (left) fill 1000 → D 356; pack 1 (right) fill 80 → D 190.
The object box is `(CX − L(D)/2, CY − L(D)/2, L(D), L(D))`, no fill, no border
(`tapes:298-303`, `515-525`); the rings are the only paint and the screen shows between
them.

### 3.9 Hubs

| part | geometry | paint |
|---|---|---|
| rim | box `(CX − L(168)/2, CY − L(168)/2) L(168) × L(168)` — 168×168 at 256 — radius circle | fill `#301206` opa 255; border **2 px** `#F15A22`, inside (at 256: outer r 84, inner r 82) |
| spoke ×2 | line from radius `L(66)` to `L(72)` at angle `a` and `a+π` | stroke **4 px** `#C0481B`, `line_rounded true` → round caps, radius 2 |

`tapes:562-579` (styles), `tapes:386-389` (placement), `tapes:228-262` (spoke math).

Rim boxes at 256: hub0 `(126,122) 168×168`, hub1 `(510,122) 168×168` — hub0's top/bottom
are flush with frame[1] (122…289) and hub1's right edge is flush with frame[1]'s (677).

Spoke drawing (LVGL convention: 0 rad = 3 o'clock, positive = clockwise because y is down):

```js
const a = -frame * (2 * Math.PI / 36);              // tapes:72,257
for (const k of [0, 1]) {
  const ang = a + (k ? Math.PI : 0);                // tapes:261
  const x0 = cx + Math.round(L(66) * Math.cos(ang));   // tapes:230-233
  const y0 = cy + Math.round(L(66) * Math.sin(ang));
  const x1 = cx + Math.round(L(72) * Math.cos(ang));
  const y1 = cy + Math.round(L(72) * Math.sin(ang));
  // 4px stroke, round caps, #C0481B, from (x0,y0) to (x1,y1)
}
```
Note the **offset** is rounded, then added to the centre (not the endpoint). Round caps
extend the drawn dash to radius 64…74 and ±2 px laterally.

Hub 0 spoke endpoints at 256, frames 0…11 *(computed)*; hub 1 is the same table shifted
by +384 in x; the other spokes are the `a+π` partners:

| frame | a | spoke k0 (x0,y0)→(x1,y1) | spoke k1 |
|---|---|---|---|
| 0 | 0° | (276,206)→(282,206) | (144,206)→(138,206) |
| 1 | −10° | (275,195)→(281,193) | (145,217)→(139,219) |
| 2 | −20° | (272,183)→(278,181) | (148,229)→(142,231) |
| 3 | −30° | (267,173)→(272,170) | (153,239)→(148,242) |
| 4 | −40° | (261,164)→(265,160) | (159,248)→(155,252) |
| 5 | −50° | (252,155)→(256,151) | (168,257)→(164,261) |
| 6 | −60° | (243,149)→(246,144) | (177,263)→(174,268) |
| 7 | −70° | (233,144)→(235,138) | (187,268)→(185,274) |
| 8 | −80° | (221,141)→(223,135) | (199,271)→(197,277) |
| 9 | −90° | (210,140)→(210,134) | (210,272)→(210,278) |
| 10 | −100° | (199,141)→(197,135) | (221,271)→(223,277) |
| 11 | −110° | (187,144)→(185,138) | (233,268)→(235,274) |

**Bare (frame 0):** hub0 `(276,206)→(282,206)` and `(144,206)→(138,206)`;
hub1 `(660,206)→(666,206)` and `(528,206)→(522,206)`.

The rim is painted once; only the two spokes are re-drawn per frame (`tapes:251-262`).

### 3.10 VU meter — **hidden on Bare**

Created as a child of the cassette container after the tape body, `opa_layered
LV_OPA_TRANSP` and `hidden` (`meter:132-136`); a two-finger swipe fades it in over
**220 ms** `ease_in_out` (`meter:151-168`). Geometry at 256:

| element | rect (256) | radius | fill |
|---|---|---|---|
| panel | (544,178) 100×56 | 0 | transparent, no border |
| row L | (544,178) 100×24 | 12 | `#481B0A` |
| row R | (544,210) 100×24 | 12 | `#481B0A` |
| badge L/R | (544,178) / (544,210) 24×24 | 4 | `#F15A22` |
| letter | centred in badge | — | montserrat_14, `#111111`, "L" / "R" |
| bars ×7 | x = 544+30+10k (k=0…6), y = 178 / 210, 10×24 | 0 | `#7F2C18` (touching) |
| lit fill | (574,178) / (574,210), `10·lit` × 24 | circle | `#CC4C1C`, hidden when lit = 0 |

`meter:31-45` (constants), `meter:180-217` (placement), `meter:94-137` (children).
Rows clip to their rounded corners (`clip_corner`, `meter:111`) so the badge's left
corners come out at radius 12 and the track's right end follows the row. Panel origin =
`(X(pose,594) − L(100)/2, Y(pose,206) − L(56)/2)`; row `y` offsets are `L(32)`; badge and
bar offsets are lengths scaled by the pose. `lit = round((db+48)/48·7)` with
`db = 20·log10(vu)` clamped to [−48,0] (`meter:63-70`).

---

## 4. Draw order (z)

Screen root children, bottom → top (`view:435-533`):

1. **screen backdrop** (the root's own background)
2. `back cover` — (16,16) 768×448, `#111111`
3. `window` — (110,76) 486×284, r8, fill `#111111`, 2 px `#111111` border; hidden at rest
   (`window:20-28`) — only in the controls view, and it is *under* the cassette on purpose
4. `cassette container` — 0,0 800×480, transparent, no border (`view:456-464`)
5. `peek`, `drawer`, `DJ block`, `menu`, `header`, `footer` — all hidden at rest

Inside the cassette container, bottom → top (`view:470-505`, `tapes:495-583`):

| # | part | source |
|---|---|---|
| 1 | Track cover (hidden) | `view:470-472` |
| 2 | white-page edge mark (hidden) | `view:478-499` |
| 3 | frame[0] | `tapes:502` |
| 4 | frame[1] | `tapes:503` |
| 5 | chip 0, chip 1, chip 2 | `tapes:507-509` |
| 6 | pack 0, pack 1 | `tapes:515-525` |
| 7 | roller 0, roller 1 | `tapes:529-535` |
| 8 | run | `tapes:539-543` |
| 9 | strand 0, strand 1 | `tapes:547-556` |
| 10 | rim 0, spoke 0a, spoke 0b, rim 1, spoke 1a, spoke 1b | `tapes:562-579` |
| 11 | VU meter panel (hidden) | `view:504` |

So: **frames → chips → packs → rollers → run → strands → rims+spokes → meter**, and
cinta < glow < label whenever the cover is up (§6). The packs are under the rims, so the
rim's opaque disc hides anything that would fall inside it; the strands are over the
rollers and over the run; the run is over the rollers and over the chips.

---

## 5. The white-page edge mark

An otherwise transparent rounded rectangle with only **two** borders lit, plus a white
gradient wash fading to nothing. Created once, **positioned at the controls pose and never
re-placed** — it does not follow the live pose (`view:478-484`).

| property | value | source |
|---|---|---|
| size | 426 × 272 | `view:81-82` |
| radius | 10 | `view:83` |
| border | **1 px** `#8A8A8A` (`k_col_glass`), opa COVER | `view:84-85` |
| borders lit | **TOP and LEFT only** | `view:86` |
| fill | linear gradient, white `alpha 90/255` → white `alpha 0` | `view:92-95,490-497` |
| position | `(X(controls,400) − 213, Y(controls,238) − 136)` = **(136, 82)** | `view:105-106,480-484` |
| hidden | yes, until the page is white | `view:499` |

The position math (`TAPE_CENTRE_X/Y` = 400/238 is the centre of the tape's ink box,
16…784 × 16…460) is evaluated at the **controls** pose (134/−51/−20, origin 140,94):
`X = 140 + trunc(400·134/256) − 213 = 140 + 209 − 213 = 136`;
`Y = 94 + trunc(238·134/256) − 136 = 94 + 124 − 136 = 82`.

So the mark covers **x 136…561, y 82…353**, centred on the cassette as the controls pose
draws it.

### 5.1 The gradient (exact axis)

`lv_grad_linear_init(&g, LEFT, TOP, RIGHT, BOTTOM, EXTEND_PAD)` with stops
`frac 0 → white @ opa 90`, `frac 255 → white @ opa 0` (`view:490-495`). LVGL's gradient
vertices are percentages of the box, so the axis is **corner-to-corner in box space**:
`(0,0) → (426,272)`, i.e. 32.6° below horizontal, *not* 45°.

For a 426×272 box the CSS-equivalent angle is `122.6deg`, but do not hand-compute:
use SVG, which reproduces LVGL exactly:

```svg
<linearGradient id="edge" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="426" y2="272">
  <stop offset="0"   stop-color="#fff" stop-opacity="0.353"/>
  <stop offset="1"   stop-color="#fff" stop-opacity="0"/>
</linearGradient>
```
(`90/255 = 0.3529`.) Over `#111111` the top-left corner reads ≈ `#656565`.
The object's opacity multiplies both the gradient stops and the border.

### 5.2 The lit sides, exactly

With `border_side = TOP|LEFT`, LVGL builds the inner rect expanded outward on the right
and bottom by `width+radius` (`lvgl/src/draw/sw/lv_draw_sw_border.c:62-67`), so the carved
region is:

* the **top row** (`y = 82`), columns 136…561 — but clipped by the 10 px corner circles,
  so visibly lit from x ≈ 142 to x ≈ 556, with the last ~2–3 px tapering out along the
  top-right corner arc's AA;
* the **left column** (`x = 136`), rows 82…353 — visibly lit from y ≈ 92 to y ≈ 345, same
  taper at the bottom-left arc;
* the **top-left corner arc** of radius 10, fully lit and antialiased;
* **nothing** on the right or bottom (their corner arcs are not drawn).

An SVG path is the faithful way to draw it:

```svg
<path d="M136 345 L136 92 A10 10 0 0 1 146 82 L551 82"
      fill="none" stroke="#8A8A8A" stroke-width="1" stroke-linecap="butt"/>
```
(i.e. left edge, top-left quarter arc, top edge; add `A10 10 0 0 1 556 85` after `L551 82`
if you want the 2–3 px AA tail into the top-right arc. A plain CSS `border-top` +
`border-left` with `border-radius:10px` is acceptable and differs only in those few
antialiased pixels at the two free ends.)

### 5.3 Timing

```
hidden = true;  opacity = 0;  show()
show:  hidden = false;  opacity: 0 → 1
       delay 260 ms, duration 200 ms, path ease_in = cubic-bezier(0.42, 0, 1, 1)
hide:  cancel the animation, hidden = true  (no fade out)
```
`view:97-101` (`EDGE_GLOW_DELAY_MS 260`, `EDGE_GLOW_FADE_MS 200`), `view:180-206`
(`edge_reveal`, `edge_hide`), `view:185-196`. The delay is the drawer/block/bands/window
entrance (260 ms, the drawer's slide); the mark is re-revealed from 0 every time, so a
re-entry cannot flash.

CSS: `transition: opacity 200ms cubic-bezier(.42,0,1,1) 260ms;`

---

## 6. Cover overlay (Track picture)

Only exists on the panel build, only at the controls pose, only while the white page asked
for it. Stack: **cinta < glow < label < frames** (`view:466-469`, `cover:1-21`).

| property | value | source |
|---|---|---|
| clip container | frame[0]'s live rect: **controls pose → (148,102) 402×188** | `cover:147-154` |
| container radius | `frame[0].radius + 2` → **10** at the controls pose | `cover:156-157` |
| container fill/border | none, transparent; `clip_corner` on | `cover:307-320` |
| picture | 603×385 source, scaled to **402 × 257**, top-left at the container's (0,0), top-aligned, `LV_IMAGE_ALIGN_STRETCH` | `artwork:66-67`, `cover:160-168` |
| picture height formula | `ih = trunc((w0·srcH + srcW/2) / srcW)` → `(402·385 + 301)/603 = 257` | `cover:161-165` |
| picture AA | on (`lv_image_set_antialias`) | `cover:329` |
| mask | A8, container-sized (402×188), **255** everywhere, **0** in the hole | `cover:39-49,170-174` |
| hole | frame[1]'s rect **relative to frame[0]** = **(55,55) 291×87**, stadium r = 43 | `cover:172`, `cover:55` |
| frame[1] border while shown | radius `min(L(556),L(168))/2` = **43** (the pill) | `tapes:356`, `tapes:422-430` |
| fade | 1 s, `ease_in_out`, on `opa_layered` (whole subtree) | `cover:185-188,190-193` |
| z while shown | cover moved to foreground **then** frame[0] then frame[1] raised | `cover:216-222`, `tapes:432-448` |
| fallback (no mask alloc) | opaque `#111111` box at the hole, radius 43 | `cover:175-182` |

The hole is a real hole: the picture is cut there, so the glow and the tape below show
through. Its stadium rasterizer is `cover:49-110` (central band + two semicircular caps of
radius `min(hw,hh)/2`, hard-edged, no AA).

The 188 px tall container clips the 257 px picture at row 187, so ~70 px of the bottom of
the artwork is cropped. The image is stretched to `402×257` (aspect 0.66667 vs 0.66753 —
a 0.13 % vertical squeeze); keep the exact integer height.

Show/hide: `delay 0`, duration **1000 ms**, `cubic-bezier(0.42,0,0.58,1)`, from/to the
current layered opacity; a page flip or a Track change cuts instead (`cover:185-188`,
`cover:243-265`). `cover_show` sets the pill on frame[1] and raises the frames *before* the
first faded frame (`cover:209-239`); on hide completion the frames go back under the body
and frame[1] returns to r16 (`cover:197-205`).

**On Bare the cover is not drawn**: it has no descriptor until the loader supplies one, and
it is hidden with `opa_layered = 0` (`cover:337-338`).

---

## 7. Reel and pack animation

The model (`model.c`) owns the numbers; the view only maps them to pixels. All of it is
driven by the status's `elapsed_ms`, never by a wall clock while stopped.

### 7.1 Position → pack fill

```js
const MIN_FILL = 80, LOAD = 1000 - MIN_FILL;         // model.h:46 ; model.c:420-422
const fills = q => [ MIN_FILL + Math.trunc(LOAD * (1000 - q) / 1000),   // supply, left
                     MIN_FILL + Math.trunc(LOAD * q / 1000) ];          // take-up, right
```
`q` = displayed position, 0…1000 (`model.c:337-340,414-415`). Fill range is **80…1000**
(the empty pack never goes below 80 so its edge stays outside the rim,
`model.h:44-46`). On Bare, `q = 0` → **[1000, 80]**.

### 7.2 Fill → pack diameter (screen)

```js
const packD = fill => {                                  // tapes:319-324
  const d = Math.sqrt(168*168 + (356*356 - 168*168) * fill / 1000);   // 28224 + 98512·f
  return Math.round(d) & ~1;                             // lround, then round down to even
};
```
Range: fill 80 → **190**, fill 1000 → **356**. (`PACK_MIN_D 168` / `PACK_MAX_D 356`,
`tapes:54-55`; the 168 min only matters for the formula, never for a drawn diameter.)
The inner rings are then generated per §3.8.

### 7.3 Hub phase

One revolution = **36 frames** = 10° (`tapes.h:35`, `tapes:61`). The art frame is the
phase's fraction (`model.c:101-108`):

```js
const hubFrame = phase => {
  const frac = phase - Math.floor(phase);
  return Math.floor(frac * 36) % 36;
};
```
Drawing angle per §3.9: `a = -frame · 2π/36` — negated so the reels visibly turn
counter-clockwise as the tape runs (`tapes:67-73`).

### 7.4 Speed

Each reel integrates the tape speed at **its own** pack radius, so supply and take-up turn
at different rates (`model.c:355-372`):

```js
const HUB_R_MM = 11.0, FULL_R_MM = 24.0, TAPE_MM_S = 47.625;   // model.h:41-43
const rMM   = fill => Math.sqrt(HUB_R_MM**2 + (FULL_R_MM**2 - HUB_R_MM**2) * fill / 1000);
const revS  = fill => TAPE_MM_S / (2 * Math.PI * rMM(fill));   // model.c:26,366-369
phase[i] = (phase[i] + revS(fill[i]) * dt) % 1;                // dt = Δelapsed/1000
```
Numbers: fill 1000 → r 24 mm → **0.316 rev/s (3.17 s/rev)**; fill 80 → r 12.55 mm →
**0.604 rev/s (1.66 s/rev)**. A stopped track holds the frames; a new track (or a restart)
resets both phases to 0 (`model.c:356-358`).

### 7.5 The rewind window (8000 ms) — only on a Track change

Opens when `playing && wasPlaying && elapsed < lastElapsed`; a window is **8000 ms** of
status elapsed time, half-open (`model.h:68-72`, `model.c:346,378-392`).

```js
const REW_MS = 8000, REW_EASE_MS = 1200, REW_FADE_MS = 1000, REW_REVS = 11;
// model.c:70-83
const rewindEase = p => {
  const tail = REW_EASE_MS / REW_MS, hold = 1 - tail, total = hold + tail * 2 / 3;
  if (p <= 0) return 0; if (p >= 1) return 1;
  if (p <= hold) return p / total;
  const u = (p - hold) / tail;
  return (hold + tail * (u - u*u*u / 3)) / total;
};
const ink = rp => {                       // model.c:89-99 ; rp = 0..1000 through the window
  const e = rp * REW_MS / 1000, back = REW_MS - REW_FADE_MS;
  if (e < REW_FADE_MS) return 1 - e / REW_FADE_MS;
  if (e > back) return (e - back) / REW_FADE_MS;
  return 0;
};
// per frame (model.c:400-425):
const left = 1 - rewindEase(since / REW_MS);
q      = Math.trunc(shown_q + (q0 - shown_q) * left + 0.5);
disp[i]= phase[i] + offset[i] * left;     // offset = (fromPhase - phase) + round(11 - that)
```
`offset` carries a whole number of revolutions so the first frame of the window shows the
same art frame that was already on screen, then unwinds 11 revolutions plus the normal
drift (`model.c:382-385`). `ink` fades the hub rim over the first second and back over the
last — **but see §2: the rim's mix is between two identical colours, so nothing changes
visually.** Implement it as a no-op.

Rewind is irrelevant to the static Bare frame; it matters only if the sim plays a track
change.

---

## 8. LVGL visual nuances to reproduce

1. **Corners are antialiased** (an exact-coverage circle mask, `lv_draw_sw_mask.c:280-310`,
   `circ_calc_aa4`), radius clamped to `min(w,h)/2`. CSS `border-radius` is close enough;
   SVG `rx` is exact.
2. **Circles** (`LV_RADIUS_CIRCLE`) are the same radius mask at `min(w,h)/2` — a 168 px box
   gives an 84 px circle, and the 2 px border gives outer 84 / inner 82.
3. **Every 1 px ring** (pack rings, rollers, frames, chips, edge mark) is an *inside*
   stroke: outer edge at r (or at the box edge), the pixel just inside it. Use
   `border-box` + border, not an SVG stroke centred on the path (offset by 0.5 px).
4. **Axis-aligned lines are hard-edged and pixel-snapped**; a 2 px horizontal line at `y`
   fills rows `y−1` and `y` and stops one pixel short at the far end
   (`lv_draw_sw_line.c:107-121`). Skew lines get exactly **one pixel of AA** on one edge
   and nothing at the ends (butt) — `lv_draw_sw_mask.c:401-470`.
5. **Line caps**: strands and the run are `butt` (square, flush at the endpoint); only the
   hub spokes are rounded (`line_rounded`), with a disc of radius 2 at each endpoint
   (`lv_draw_sw_line.c:68-91`).
6. **Full-circle arcs** (pack rings) are drawn as a 1 px *border* on the `2r` box
   (`lv_draw_sw_arc.c:66-77`) — identical to a CSS circle with a 1 px border.
7. **Object opacity** (`opa`) multiplies its own paint only; `opa_layered` renders the
   subtree to a layer first — used by the cover, the meter and the edit-mode dim. For the
   edge mark, `opa` scales border + gradient together.
8. **`clip_corner`** (cover container, meter rows) is a rounded-rect clip on the children,
   same AA as the fill.
9. **No shadows, no blur, no transformations** anywhere in the tape body. The only filters
   in the app are the drawer's button shadows (`widgets.c:13-22`), not part of Bare.
10. **Colour blending**: LVGL blends in 8-bit per channel with `mix = (fg·a + bg·(255−a))/255`.
    Object/stop alphas used here are only 0, 90 and 255 (plus the edge gradient's ramp
    between 90 and 0), so plain CSS alpha compositing matches.
11. **The panel is not gamma-managed**; treat all hex values as sRGB straight.

---

## 9. Reference implementation sketch (geometry only)

```ts
// ---- pose ---------------------------------------------------------------
type Pose = { scale: number; dx: number; dy: number };
const DESIGN_W = 800, DESIGN_H = 480, NONE = 256;
const L  = (p: Pose, v: number) => Math.trunc((v * p.scale) / NONE);
const Le = (p: Pose, v: number) => L(p, v) & ~1;
const originX = (p: Pose) => Math.trunc((DESIGN_W - L(p, DESIGN_W)) / 2) + p.dx;
const originY = (p: Pose) => Math.trunc((DESIGN_H - L(p, DESIGN_H)) / 2) + p.dy;
const X = (p: Pose, x: number) => originX(p) + L(p, x);
const Y = (p: Pose, y: number) => originY(p) + L(p, y);

export const POSES = {
  rest:     { scale: 256, dx:   0, dy:   0 },
  peek:     { scale: 200, dx:   0, dy: -60 },
  controls: { scale: 134, dx: -51, dy: -20 },
};

// ---- packs --------------------------------------------------------------
const MIN_FILL = 80, LOAD = 920;
export const fillsFor = (q: number) =>
  [MIN_FILL + Math.trunc(LOAD * (1000 - q) / 1000),
   MIN_FILL + Math.trunc(LOAD * q / 1000)] as const;

export const packD = (fill: number) =>
  Math.round(Math.sqrt(168 * 168 + (356 * 356 - 168 * 168) * fill / 1000)) & ~1;

export const ringRadii = (p: Pose, D: number) => {
  const out: number[] = [];
  const gap = Math.max(1, L(p, 10));
  const stop = Math.trunc(L(p, 168) / 2);
  for (let r = Math.trunc(L(p, D) / 2); r > stop; r -= gap) out.push(r);
  return out;
};

// ---- strands ------------------------------------------------------------
const HUBS = [{ x: 210, y: 206 }, { x: 594, y: 206 }];
const GUIDE_D = 38, RUN_Y_DESIGN = 450, GUIDE_X = [39, 761];

export function strand(p: Pose, side: 0 | 1, D: number) {
  const gd = Le(p, GUIDE_D), gr = gd / 2;
  const cx = X(p, HUBS[side].x), cy = Y(p, HUBS[side].y);
  const R  = L(p, D) / 2;
  const gx = X(p, GUIDE_X[side]), gy = Y(p, RUN_Y_DESIGN) - gr;
  const dx = gx - cx, dy = gy - cy, len = Math.hypot(dx, dy), phi = Math.atan2(dy, dx);
  const alpha = Math.acos((R - gr) / len);
  const a = phi + (side ? -alpha : alpha);
  let sweep = Math.PI / 2 - a;
  while (sweep >  Math.PI) sweep -= 2 * Math.PI;
  while (sweep < -Math.PI) sweep += 2 * Math.PI;
  const pts: [number, number][] = [
    [Math.round(cx + R * Math.cos(a)),  Math.round(cy + R * Math.sin(a))],
    [Math.round(gx + gr * Math.cos(a)), Math.round(gy + gr * Math.sin(a))],
  ];
  for (let k = 1; k <= 10; k++) {
    const t = a + sweep * k / 10;
    pts.push([Math.round(gx + gr * Math.cos(t)), Math.round(gy + gr * Math.sin(t))]);
  }
  return { pts, gd, gx, gy, gr };
}

// ---- hubs ---------------------------------------------------------------
export const hubFrame = (phase: number) =>
  Math.floor((phase - Math.floor(phase)) * 36) % 36;

export function spokes(p: Pose, hub: 0 | 1, frame: number) {
  const cx = X(p, HUBS[hub].x), cy = Y(p, HUBS[hub].y);
  const r0 = L(p, 66), r1 = L(p, 72), a = -frame * (2 * Math.PI / 36);
  return [0, 1].map(k => {
    const ang = a + (k ? Math.PI : 0);
    return {
      x0: cx + Math.round(r0 * Math.cos(ang)), y0: cy + Math.round(r0 * Math.sin(ang)),
      x1: cx + Math.round(r1 * Math.cos(ang)), y1: cy + Math.round(r1 * Math.sin(ang)),
    };
  });
}
```

---

## 10. The Bare screen, as a checklist

| # | element | rect | paint |
|---|---|---|---|
| 0 | stage | 0,0 800×480 | `#111111` |
| 1 | back cover | 16,16 768×448, r0 | `#111111` |
| 2 | frame[0] | 16,16 768×360, r16 | 1 px `#60240D` |
| 3 | frame[1] | 122,122 556×168, r16 | 1 px `#60240D` |
| 4 | chip 0 | 120,440 50×20, r8 | `#111111` + 1 px `#F15A22` |
| 5 | chip 1 | 360,430 80×30, r8 | `#111111` + 1 px `#F15A22` |
| 6 | chip 2 | 630,440 50×20, r8 | `#111111` + 1 px `#F15A22` |
| 7 | left pack | centre (210,206), D 356 | 10 rings r 178…88 step 10, 1 px `#F15A22` |
| 8 | right pack | centre (594,206), D 190 | 2 rings r 95, 85, 1 px `#F15A22` |
| 9 | roller 0 | 20,412 38×38 | 1 px `#F15A22` ring |
| 10 | roller 1 | 742,412 38×38 | 1 px `#F15A22` ring |
| 11 | run | 39,449 722×2 | `#F15A22` |
| 12 | strand 0 | §3.7 left polyline | 2 px `#F15A22`, butt |
| 13 | strand 1 | §3.7 right polyline | 2 px `#F15A22`, butt |
| 14 | rim 0 | 126,122 168×168 | `#301206` + 2 px `#F15A22` |
| 15 | rim 1 | 510,122 168×168 | `#301206` + 2 px `#F15A22` |
| 16 | spoke 0a / 0b | (276,206)→(282,206) / (144,206)→(138,206) | 4 px `#C0481B`, round caps |
| 17 | spoke 1a / 1b | (660,206)→(666,206) / (528,206)→(522,206) | 4 px `#C0481B`, round caps |
| — | meter, edge mark, cover | not drawn | — |

That is the whole picture: the backdrop, the shell, the two frames, three chips, two packs
(12 rings: 10 + 2), two rollers, the 2 px run, two strands, two rims and four spokes.
Nothing else is on screen.

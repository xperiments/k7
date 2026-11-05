# 02 — Controls chrome: drawer, DJ block, bands, window

Implementation spec for a pixel-for-pixel HTML/SolidJS redraw of the **controls state**
of the cassette screen: the transport drawer at the left, the DJ block at the right, the
header and K7 badge across the top, the footer band across the bottom, and the (mostly
invisible) window around the tape.

Scope: the **settled** controls state, on the **dark page** and on the **white page**.
Knob *editing* (the 3× virtual knob, the edit overlay, the blink) is out of scope; the
block is drawn in its default state. The Menu is out of scope except for the two things
the Menu re-uses here: the drawer's labels and the Pad's ink (§10). The track cover and
the edge mark are covered by `01-cassette-body.md`; the page-flip/gesture sequence by
`04-peek-and-gestures.md`.

Every number below is read off the C, or computed from it with the C's own integer rules
(marked *(computed)*) and reproduced as a worked value.

---

## Sources

| short | file |
|---|---|
| `view` | `components/app/screen_display/cassette_view.c` |
| `buttons` | `components/app/screen_display/cassette_buttons.c` |
| `buttons.h` | `components/app/screen_display/cassette_buttons.h` |
| `eq` | `components/app/screen_display/cassette_eq.c` |
| `eq.h` | `components/app/screen_display/cassette_eq.h` |
| `header` | `components/app/screen_display/cassette_header.c` |
| `footer` | `components/app/screen_display/cassette_footer.c` |
| `window` | `components/app/screen_display/cassette_window.c` |
| `widgets` / `widgets.h` | `components/app/screen_display/cassette_widgets.c` / `.h` |
| `style.c` / `style.h` | `components/app/screen_display/cassette_style.c` / `.h` |
| `layout.c` / `layout.h` | `components/portable/cassette/cassette_layout.c` / `include/cassette_layout.h` |
| `icons.h` | `components/app/screen_display/cassette_icons.h` |
| `menu.c` | `components/app/screen_display/cassette_menu.c` |
| `ui-readme` | `components/app/screen_display/README.md` |
| `menu.md` | `docs/specs/menu.md` |
| `lvgl` | `managed_components/lvgl__lvgl` |

Normative source is the **C in this tree**. Several README/comment values are stale; they
are listed in Appendix A. Where the code and a doc disagree, this spec uses the code and
says so.

---

## 0. Conventions that decide every pixel

* Panel **800×480**, 1 CSS px = 1 device px, stage origin top-left.
* LVGL box `(x,y,w,h)` covers columns `x … x+w-1`, rows `y … y+h-1`; a CSS
  `left/top/width/height` box is equivalent. "Right edge" below means the *boundary*
  `x+w`, so a box at `(560,401,200,30)` has its right boundary at 760 and its last pixel
  at 759.
* **Borders are inset** (drawn inside the box). CSS: `box-sizing: border-box`.
  (`lvgl/src/draw/sw/lv_draw_sw_border.c:53-76`)
* Every painted box here is built with `lv_obj_remove_style_all()` → no default padding,
  margin, shadow or outline. The labels are plain `lv_label_create()` with only font and
  colour set; a label's box is exactly its text advance × `line_height` (`LV_SIZE_CONTENT`).
* LVGL rectangles are not anti-aliased at their edges (rounded corners are rasterised
  hard); circles/rounded corners are hard-edged, text is 4 bpp grayscale AA. The sim may
  AA; for a pixel diff, compare at the given integer boxes.
* `k_col_white` is **not white**: it is `#909090` (`style.c:7`). "White page" is a
  grey-on-dark design; see §6.
* Opacity is LVGL 0…255. CSS `opacity` = value/255. Shadow opacities are given as
  `value/255`.

### 0.1 The white page is a *dark ground* with light plates, not a white screen

This is the single most common mistake. `page_set()` only recolours the **screen root**
(`view:217`). The 768×448 back cover at (16,16) is `#111111` on both pages and is never
hidden (`view:442`, `ui-readme:374-376`). Therefore, on the white page:

* the page colour (`#909090`) is visible **only as a 16 px ring** around the back cover
  (left 0…15, right 784…799, top 0…15, bottom 464…479), plus wherever a page-coloured
  chrome plate covers the ring/ground;
* everything inside the back cover is still `#111111` unless a chrome plate or the tape
  paints over it.

The white-page look is therefore: dark ground → grey chrome plates (`#909090` bands,
drawer rectangles, M/R cells) and darker grey knob plates (`#727272`) → orange line art →
burnt-orange badge (`#CC4C1C`). The white 16 px ring merges with the page-coloured bands
and plates that touch it.

`ui-readme:357-376` states this ("The white is a margin"), `ui-readme:423-425` confirms the
chips stay `#111111` on both pages.

### 0.2 Paint order at 10 000 ft

| # | layer | dark page | white page |
|---|---|---|---|
| 1 | stage | `#111111` | `#909090` |
| 2 | back cover (16,16) | `#111111` | `#111111` (visible → dark ground) |
| 3 | window (110,76) | `#111111` | `#111111` (invisible, §5) |
| 4 | cassette container: cover → edge mark → tapes → meter | orange line art | orange line art (same); edge mark delayed |
| 5 | peek | hidden | hidden |
| 6 | drawer plates + caps | `#111111` + primary line art | `#909090` plates + grey/black/red caps |
| 7 | DJ block plates + knobs | `#111111` + primary line art | `#909090`/`#727272` plates + caps |
| 8 | big knob / edit overlay | hidden | hidden |
| 9 | Menu page + Pad | absent | absent |
| 10 | header band + title | `#111111` + primary | `#909090` + black |
| 11 | K7 badge + mark | `#111111` plate, primary border/mark | `#CC4C1C` plate, black border/mark |
| 12 | footer band + texts | `#111111` + primary | `#909090` + black |

(§8 gives the exact sibling/child list and sources.)

---

## 1. The controls pose and the composition

### 1.1 Pose constants

| constant | value | source |
|---|---|---|
| `CASSETTE_SCALE_BUTTONS` | **134** (of 256) | `view:72` |
| `CASSETTE_BUTTONS_DX` | **−51** | `view:73` |
| `CASSETTE_BUTTONS_DY` | **−20** | `view:74` |
| scale factor | `134/256 = 0.5234375` | — |
| pose animation | 260 ms, ease-out, from the current pose | `view:75,129-143,287-318` |

### 1.2 Design→screen math (exact, integer)

```
L(v)   = trunc(v * 134 / 256)                 // C int division, v ≥ 0   (layout.c:21-24)
ox     = trunc((800 - L(800)) / 2) + (-51)     // = 140                   (layout.c:36-39)
oy     = trunc((480 - L(480)) / 2) + (-20)     // = 94                    (layout.c:41-44)
X(x)   = ox + L(x)                             //                        (layout.c:46-49)
Y(y)   = oy + L(y)                             //                        (layout.c:51-54)
```

Worked: `L(800) = 418`, `L(480) = 251`, so `ox = (800−418)/2 − 51 = 140`,
`oy = (480−251)/2 − 20 = 94`. Lengths **truncate**, never round: `L(16) = 8` (8.375),
`L(460) = 240` (240.78), `L(594) = 310` (310.92).

### 1.3 The cassette itself at this pose

| part | design rect | screen rect *(computed)* |
|---|---|---|
| cassette **ink box** | (16,16) 768×444 | **(148,102) 402×232** |
| outer frame (frame 0) | (16,16) 768×360 | (148,102) 402×188 |
| inner frame (frame 1) | (122,122) 556×168 | (203,157) 291×87 |
| hub centres | (210,206) (594,206), ring D 168 | (249,201) (450,201), D 87 |

The ink box is what the chrome must frame: **x 148…549, y 102…333** (`x+w` boundaries 550
and 334). `ui-readme:285-287` and `view:66-70` both state 402×232 at x=148..550.

The chrome below is **fixed screen furniture**: it does **not** scale, rotate or move
with the pose. Only the cassette shrinks into it.

### 1.4 Every chrome rect at the controls pose

| part | x | y | w | h | r | hidden pose | source |
|---|---:|---:|---:|---:|---:|---|---|
| screen | 0 | 0 | 800 | 480 | — | — | `view:437-439` |
| back cover | 16 | 16 | 768 | 448 | 0 | — | `view:442` |
| window | 110 | 76 | 486 | 284 | 8 | opacity 0 | `window:20-28,70-74` |
| drawer panel | **0** | 0 | 120 | 480 | 0 | **x = −120** | `buttons:43-45,340` |
| P button | 16 | 76 | 96 | 96 | 8 | with panel | `buttons:47-52` |
| `<` button | 16 | 170 | 96 | 96 | 8 | with panel | `buttons:52,357` |
| `>` button | 16 | 264 | 96 | 96 | 8 | with panel | `buttons:52,357` |
| header band | 110 | 6 | 674 | 71 | 8 | **y = −71** | `header:38-46,69` |
| K7 badge | 16 | 6 | 96 | 71 | 8 | y = −71 (rides header) | `header:45-53,153-155` |
| K7 mask | 34 | 25 | 60 | 32 | — | with badge | `header_logo.h:9-10`, `ui-readme:342` |
| footer band | 16 | 359 | 768 | 115 | 8 | **y = 480** | `footer:29-32,41` |
| GAIN cell | **594** | 76 | 96 | 96 | 8 | x + 208 | `eq:52-56,139-145` |
| M cell | **594** | 170 | 96 | 96 | 8 | x + 208 | `eq:109-110` |
| R cell | **594** | 264 | 96 | 96 | 8 | x + 208 | `eq:109-110` |
| HI cell | **688** | 76 | 96 | 96 | 8 | x + 208 | `eq:109-110` |
| MED cell | **688** | 170 | 96 | 96 | 8 | x + 208 | `eq:109-110` |
| LOW cell | **688** | 264 | 96 | 96 | 8 | x + 208 | `eq:109-110` |
| edge mark | 136 | 82 | 426 | 272 | 10 | hidden (dark page) | `view:81-86,478-484` |
| Menu Pad | 596 | 76 | 186 | 284 | 8 | hidden | `menu.c:62-66` |

The header/footer/window/pad rows are boundaries: header rows `6…76`, footer rows
`359…473`, window rows `76…359`, buttons/cells rows `76…171`, `170…265`, `264…359`. The
shared rows are **76** and **359** by construction. Columns: header `110…783`, footer
`16…783`, badge `16…111`, drawer buttons `16…111`, block `594…783`.

Overlaps that matter for drawing (all same-ink, so they only matter at rounded joints):

| overlap | geometry | who covers whom |
|---|---|---|
| badge vs header | columns **110–111** (2 px) | badge is created after the band → badge wins (`header:146,153`) |
| header vs P / GAIN | row **76** | header after both → header wins |
| footer vs `>` / LOW | row **359** | footer after both → footer wins |
| `>`/`<`/P | rows 170–171, 264–265 (2 px) | later child wins: P < `<` < `>` (`buttons:355-357`) |
| DJ cells | col 688–689, rows 170–171 / 264–265 (2 px) | creation order GAIN, HI, MED, LOW, M, R → later wins (`eq:725-731`) |
| window vs block | col 594–595 (2 px) | block/Chrome above; window invisible anyway |
| window vs Pad | window right boundary 596, Pad x 596 | they abut, no overlap |

**Painted seam thickness** (a pixel-diff detail): internal 2 px overlaps — button-to-button,
cell-to-cell, badge-to-band — are exactly **2 px**. The bands are inset-border rectangles
whose edges abut the carcases, so the union is wider at the band/block junctions:

* header bottom border **rows 75–76** over the GAIN/HI top border **rows 76–77** → a
  **3-row** line (75–77) across the DJ block. The header starts at x=110, so it only
  touches the P button at x110–111; the drawer's top edge stays 2 rows everywhere else.
* footer top border **rows 359–360** over the `>`/LOW bottom border **rows 358–359** → a
  **3-row** line (358–360) across both, because the footer spans x16–783.
* all these rows are `#60240D`; the extra row is invisible in ink but visible in a pixel
  count.

Layout sketch (not to scale; boundaries are x+w / y+h):

```
     0        16   110                 688  784  800
   0 ┌───────────────────────────────────────────────┐
     │ 6 px page margin                              │
   6 │   ┌──────┬─────────────────────────────────┐  │
     │   │ K7   │ header 110,6 674×71             │  │
  76 │   ├──────┼───────────────┬─────────────┬────┤  │
     │   │ P    │ window        │ GAIN  │ HI  │    │
     │   │      │ 110,76        │ 594   │ 688 │    │
 170 │   │ <    │ 486×284       │ M     │ MED │    │
     │   │      │   cassette    │       │     │    │
 264 │   │ >    │   148,102     │ R     │ LOW │    │
     │   │      │   402×232     │       │     │    │
 359 │   ├──────┴───────────────┴───────┴─────┴────┤  │
     │   │ footer 16,359 768×115                   │  │
 474 │   └─────────────────────────────────────────┘  │
 480 └───────────────────────────────────────────────┘
```

---

## 2. The transport drawer

### 2.1 Panel and slide

| property | value | source |
|---|---|---|
| panel | 120×480 at (0,0) when shown | `buttons:43-44,340-341` |
| hidden pose | `x = −120` (its own width) | `buttons:45,340` |
| fill/border | none; the panel is invisible | `buttons:342-344` |
| children | three 96×96 button roots at x=16 | `buttons:51,357` |
| button pitch | 94 (96 + gap −2) → y 76, 170, 264 | `buttons:50,52,357` |
| slide | x `current → 0` (show) or `current → −120` (hide) | `buttons:322-327` |
| duration/ease | **260 ms**, ease-out | `buttons:323-324`, `buttons.h:55` |
| hide completion | `lv_obj_set_hidden(panel,true)` | `buttons:301-305` |
| show start | unhide + `lv_obj_move_foreground(panel)` | `buttons:310-314` |
| gesture | the panel absorbs a tap; gestures/events bubble to the screen | `buttons:346-352` |

The panel itself paints nothing; what you see slide is the three opaque button
rectangles (and their content). The 8 px panel margin right of the buttons and the 76 px
above/below them are transparent, so the tape is visible there while the drawer moves.

### 2.2 One button

Each button is a transparent, clickable 96×96 root at `(16, 76 + 94·i)` with two
prebuilt state children, only one visible (`buttons:255-290,112-117`). A state is:

| child | rect (button-local) | paint |
|---|---|---|
| rectangle | (0,0) 96×96, r8 | fill = page, border = `#60240D`, width 2 (unpressed) / 3 (pressed) |
| ambient circle | (16,16) 64×64, r32 | transparent; carries the broad shadow on the white page |
| disc / ring | (0,0) of the ambient → button (16,16) 64×64, r32 | dark: 2/3 px `#F15A22` ring, no fill; white: face + 4 px `#929292` rim + shadows |
| letter | centred in the disc | Jura 36, colour per page |

Sources: rectangle `buttons:235-236`, ambient `buttons:239-241`, disc `buttons:242-245`,
letter `buttons:246-248`; widths `buttons:61-62,283-284`; cap `buttons:279`.

So the absolute rects for P are: rectangle `(16,76,96,96)`, circle `(32,92,64,64)`. For
`<`: `(16,170,96,96)`, circle `(32,186,64,64)`. For `>`: `(16,264,96,96)`, circle
`(32,280,64,64)`.

### 2.3 Letters

Font **Jura 36** (Jura variable, wght 300, rasterised 36 px / 4 bpp; `font_jura_36.c:1-6`),
`line_height 39`, `base_line 8`, no letter spacing. The label is `LV_SIZE_CONTENT`
(width = advance, height = 39) and centred on the 64 px circle. Because the circle's
border is symmetric, the label lands on the same spot on both pages and in both states:

```
label_left = circle_x + 32 - floor(advance/2)
label_top  = circle_y + 32 - floor(39/2)          // = circle_y + 13
glyph_left = label_left + ofs_x
glyph_top  = label_top + (39 - 8) - box_h - ofs_y
```

| glyph | advance (px) | bitmap box | ofs_x | ofs_y | label box (absolute) | glyph box (absolute) |
|---|---:|---:|---:|---:|---|---|
| `P` (U+0050) | 20 | 16×23 | 3 | 0 | (54,105) 20×39 | **(57,113) 16×23** |
| `<` (U+003C) | 20 | 14×16 | 3 | 4 | (54,199) 20×39 | **(57,210) 14×16** |
| `>` (U+003E) | 20 | 13×16 | 3 | 4 | (54,293) 20×39 | **(57,304) 13×16** |

Sources: `font_jura_36.c:4663,4665,4683` (glyph dsc), `:5241-5242` (line/base). Advances are
`(adv_w + 8) >> 4` (`lvgl/src/font/fmt_txt/lv_font_fmt_txt.c:252-258`); glyph top is
`pos.y + (line_height − base_line) − box_h − ofs_y`
(`lvgl/src/draw/lv_draw_label.c:668-672`).

The letters are Jura 36 in both modes' geometry; the Menu swaps only the font/glyphs (§10).

### 2.4 Inks per page (the button rectangles stay carcase on both pages)

| part | dark page | white page |
|---|---|---|
| rectangle fill | `#111111` (`k_col_bg`) | `#909090` (`k_col_white`) |
| rectangle border | `#60240D`, 2 px / 3 px pressed | same |
| circle dark | 2 px `#F15A22` ring, **no fill**; 3 px when pressed | — |
| circle white | — | flat face `#929292` (`k_col_dome`), **4 px** rim `#929292`, shadows |
| letter | `#F15A22` | `#111111` (the cap's letter) |

Sources: `buttons:377-392` (page repaint), `buttons:279` (default cap
`{#929292,#111111}`), `widgets.h:42-45` (cap), `widgets.h:35` (rim = 4).

The rectangle fill is deliberately the page colour, so on the dark page the button is just
its orange outline, and on the white page it is a `#909090` plate (the page's white) with a
`#60240D` outline. The disc on the white page is 2 units lighter than the plate
(`#929292` vs `#909090`) — the edge is carried by the shadows, not the fill.

### 2.5 Cap / dome shadows (white page only)

`cassette_circle_face()` gives the disc **two** outer shadows, one per object
(`widgets.c:83-115`):

| shadow | colour | `shadow_width` | dx | dy | opa | CSS (see note) |
|---|---|---:|---:|---:|---:|---|
| ambient (on the transparent wrapper) | `#000000` | **24** | **+2** | **+5** | **60/255 = 0.2353** | `2px 5px 8px rgba(0,0,0,.2353)` |
| contact (on the disc itself) | `#FFFFFF` | **16** | **−2** | **−2** | **80/255 = 0.3137** | `-2px -2px 5px rgba(255,255,255,.3137)` |

Sources: `widgets.c:13-22` (constants), `:85-103` (applied), `:104-114` (dark page: both
widths/opacities zeroed, no fill).

LVGL semantics: `shadow_width` is the **full penumbra**; the shadow area is the core
rect expanded by `width/2` on every side (`lvgl/src/draw/sw/lv_draw_sw_box_shadow.c:53-67`),
and the mask is blurred twice with roughly half that window (`:587-671`). CSS's blur radius
`B` gives a Gaussian with σ = B/2 and a visible reach of about 1.5 B, so the equivalent
reach is `B ≈ width/3`. The table's CSS column uses that mapping; **do not** scale the
offsets or the opacity if it looks too soft — adjust only the blur.
CSS paints the first shadow on top, and LVGL draws the wrapper's ambient shadow first and
the disc's contact shadow after, so list the **white contact shadow first** if layering
order matters:

```css
.dome {
  border-radius: 50%;
  box-shadow: -2px -2px 5px rgba(255,255,255,.3137),
               2px  5px 8px rgba(0,0,0,.2353);
}
```

Use the same pair on the 44 px knobs (§3.3). Both shadows are masked *outside* their own
silhouette (LVGL shadows are outer-only), so they fall on the button plate and never tint
the face.

> Verification note: LVGL clips a child to its parent's box by default
> (`lvgl/src/core/lv_refr.c:147-155`). The disc is a child of the 64×64 wrapper, so a
> strict LVGL render clips the disc's own contact shadow to the wrapper and only the
> wrapper's ambient shadow reaches the 96 px bezel. The C comment and the author's PIL
> previews show both. Draw both; if a board capture later shows only one, drop the
> contact shadow.

**Rim**: the white-page disc's border is `CASSETTE_CAP_RIM_W = 4` px, inset, always
`#929292` (`buttons:386-387`, `widgets.h:35`). On a `#929292` face it is invisible; on M
and R it is the grey ring that makes the coloured disc read as "let into" the cap. The
dark page uses the state's 2/3 px `#F15A22` ring instead and no rim.

### 2.6 Pressed vs unpressed

There are two prebuilt states; `PRESSED` shows the pressed one, `RELEASED`/`PRESS_LOST`
shows the other (`buttons:166-185,112-117`).

| page | unpressed | pressed | visible difference |
|---|---|---|---|
| dark | rect 2 px, ring 2 px | rect **3 px**, ring **3 px** | the whole 96/64 outline thickens by 1 px inward |
| white | rect 2 px, rim 4 px | rect **3 px**, rim 4 px | only the **rectangle** thickens; the disc looks identical |

The pressed state's letter and fills do not change. (For the drawer both state rectangles
are built with the state's width, `buttons:235-236`; for M/R the carcase is built once, so
only their ring differs — see §3.5.)

### 2.7 The 160 ms scale pulse

On a successful click (not a gesture, not a moved press, not the Menu's modal gate):

```
pulse: transform scale 256 → 232 → 256 over 160 ms, linear triangle
scale(t) = 1 - (24/256) * (1 - |2t/T - 1|)    T = 160 ms
peak at t = 80 ms: 232/256 = 0.90625
pivot = button centre (48,48 button-local)
```

Source: `buttons:66-68,142-164`; pivot `buttons:267-269`. A new press deletes a pulse in
flight and resets scale to 256 before swapping states (`buttons:170-174`). CSS:
`transform-origin: 48px 48px; transition: transform 160ms linear` with a keyframe/triangle
(or two 80 ms linear halves). `LV_EVENT_CLICKED` also runs the action: `<` →
`mini_icecast_prev()`, `>` → `mini_icecast_skip()`, P → nothing (`buttons:201-211`).

---

## 3. The DJ block

### 3.1 The six cells

Two 96 px columns with pitch **94** (2 px shared border), three rows with pitch 94. The
first column is nudged **+2** px from the nominal 592; the second stays at 688
(`eq:52-56,139-145,109-110`).

| slot | id | x | y | x…x+w−1 | y…y+h−1 |
|---|---|---:|---:|---|---|
| 0 | GAIN | **594** | 76 | 594…689 | 76…171 |
| 1 | HI | **688** | 76 | 688…783 | 76…171 |
| 2 | MED | **688** | 170 | 688…783 | 170…265 |
| 3 | LOW | **688** | 264 | 688…783 | 264…359 |
| 4 | M | **594** | 170 | 594…689 | 170…265 |
| 5 | R | **594** | 264 | 594…689 | 264…359 |

Grid formula: `x = 592 + (col ? 96 : 2)`, `y = 76 + 94·row`, with
`col = {0,1,1,1,0,0}`, `row = {0,0,1,2,1,2}` (`eq:109-110`). Footprint: **x 594…783, y
76…359 = 190×284**. Creation order is slot 0→5, so later cells paint over earlier ones in
the 2 px overlaps (`eq:725-731`). Radius 8 on every carcase (`eq:57`).

### 3.2 Cell paint per page

| page | four knob cells | M / R cells |
|---|---|---|
| dark | fill `#111111`, 2 px border `#60240D` | fill `#111111`, 2 px border `#60240D` |
| white | fill **`#727272`**, 2 px border **`#111111`** — the "knob plate" | fill **`#909090`**, 2 px border `#60240D` |

Sources: `eq:793-817` (page), `:654-656` (creation). The dark page is the drawer's line art
throughout; on the white page the four knob cells are the only cells whose border changes
ink (to `#111111`) and whose fill is the darker plate. The 2 px overlaps mean the plate
seams are exactly 2 px, no gaps.

### 3.3 The four knobs

| item | value | source |
|---|---|---|
| knob disc | **44×44** at cell + **(26,26)**, r22 | `eq:61-63` |
| disc, dark | transparent fill; 2 px `#F15A22` ring | `eq:228-230,810-813` |
| disc, white | flat `#383838` face, **no border**, the two shadows | `eq:810-813`, `widgets.c:95-103` |
| crown | **24** dots, radius **32** from the cell centre, 15° apart | `eq:64-66,234-241` |
| dot | **4×4**, r2 | `eq:66,239-240` |
| 0 dB tick | dot **i = 0** (12 o'clock), always `#FF8A3D` | `eq:186-193` |
| stick | **4×16**, r3 (clamped to 2), at cell (46,32) | `eq:67-69,245-246` |
| stick pivot | (2,16) in the stick = cell centre (48,48) | `eq:247-248` |
| stick ink | `#FF8A3D` on both pages | `eq:246` |
| rotation | `deg = (50 − value) · 3.6`, clockwise positive; 0 dB = 12 o'clock, cut = right, boost = left | `eq:199-201` |
| default | value 50 = **0 dB** → rotation **0°** (points up) | `eq:713-716,199` |

Absolute knob discs: GAIN **(620,102)**, HI **(714,102)**, MED **(714,196)**, LOW
**(714,290)**, each 44×44. Cell centres (= crown centres): **(642,124)**, **(736,124)**,
**(736,218)**, **(736,312)**.

The stick's 16 px length ends at the cell centre, not at the disc rim: at 0° it runs from
y = cell+48 up to cell+32, i.e. **6 px inside the 22 px disc**. The tick's centre is at
cell+16; the gap between stick tip (cell+32) and tick centre is 16 px.

### 3.4 Crown dot positions

Dot `i` is at 15°·i clockwise from 12 o'clock; its 4×4 top-left in the 96×96 cell is
`(48 + round(32·sin a) − 2, 48 − round(32·cos a) − 2)`, `a = 15°·i` (`eq:234-241`). The
resulting offsets (add the cell's x/y for absolute):

```js
// i = 0 is 12 o'clock / the 0 dB tick
const DOT = [[46,14],[54,15],[62,18],[69,23],[74,30],[77,38],
             [78,46],[77,54],[74,62],[69,69],[62,74],[54,77],
             [46,78],[38,77],[30,74],[23,69],[18,62],[15,54],
             [14,46],[15,38],[18,30],[23,23],[30,18],[38,15]];
// dot i occupies DOT[i][0]..DOT[i][0]+3 × DOT[i][1]..DOT[i][1]+3, r2
```

Dot colours on both pages (`eq:182-193`, `style.c:19-21`):

| role | colour | when |
|---|---|---|
| tick | **`#FF8A3D`** | always, i = 0 only |
| lit | **`#F15A22`** | cut (`value < 50`): i = 1…span; boost (`value > 50`): i = 24−span…23 |
| dim | **`#5B220C`** | the rest |

`span = round(|value−50|/50 · 12)` (max 12). At the default `value = 50` neither lit branch
runs, so **only the tick is `#FF8A3D`, the other 23 dots are `#5B220C`**. Cutting lights the
right half (clockwise); boosting lights the left half (counter-clockwise). The lit arc
starts at 12 o'clock, not at the stick. The "0 dB marker" is exactly this tick; there is no
other tick or label.

The disc is `#383838` with no rim and no border on the white page, so the crown and stick
"float" on the darker plate; on the dark page the disc is just a 2 px ring and the cell's
`#111111` shows through.

### 3.5 M and R

M and R are the drawer's button moved into a cell: a 64×64 circle at cell **(16,16)**, r32,
one Jura 36 letter, two prebuilt states (`eq:253-305`).

| item | dark page | white page |
|---|---|---|
| circle fill | none | M: **`#111111`**; R: **`#AA2200`** |
| circle border | 2 px `#F15A22` (unpressed), **3 px** (pressed) | **4 px `#929292`** rim in both states |
| shadows | none | ambient + contact (§2.5) |
| letter | `#F15A22` | **`#EEEEEE`** both |
| cell | fill `#111111`, 2 px `#60240D` | fill `#909090`, 2 px `#60240D` |

Sources: caps `eq:718-723`, page `eq:312-330`, states `eq:277-305`, shadows via
`cassette_circle_face`. Absolute circles: M **(610,186)**, R **(610,280)**, both 64×64.
Letters:

| button | advance | box | ofs_x / ofs_y | label box | glyph box (absolute) |
|---|---:|---:|---|---|---|
| M | 28 | 22×23 | 3 / 0 | (628,199) 28×39 | **(631,207) 22×23** |
| R | 21 | 17×24 | 3 / −1 | (632,293) 21×39 | **(635,301) 17×24** |

The M/R **carcase does not thicken** on press; only the circle's ring differs, so on the
white page the two states are visually identical (rim 4 both) and the press is only
visible through the click pulse (`eq:257-260`). The click pulses the **whole cell**
(scale 256→232→256, 160 ms, pivot (48,48), `eq:565-592`); M's click also opens the Menu
(`eq:624-627`, `view:348-356`), R does nothing.

---

## 4. Header band, K7 badge, footer band

### 4.1 Header band

| property | value | source |
|---|---|---|
| rect | **(110,6) 674×71**, r8 | `header:38-46` |
| border | 2 px **`#60240D`** (`k_col_button_rect`) | `header:147-148` |
| fill, dark | `#111111` | `header:185` |
| fill, white | `#909090` | `header:185` |
| hidden pose | y = **−71** (band rows −71…−1, fully off) | `header:69` |
| slide | y `current → 6` / `current → −71`; **77 px travel** | `header:97-117` |
| duration/ease | 260 ms, ease-out | `header:112-113` |
| hide completion | band + badge hidden | `header:88-95` |
| content outside Menu | none (title empty and hidden) | `header:169,190-197` |

The band starts 2 px inside the badge (badge right boundary 112, band left 110) so the
badge's right border doubles as the divider; no second line runs beside it. The band's
right boundary is 784, the cassette's 16 px inset. Its bottom row is **76**, shared with
the first button and first DJ cell.

Note the travel: `y` goes from −71 to +6, i.e. **77 px**, not 71. The comment "its height
is the distance" is wrong (`header:8-9`). First visible when y > −71.

### 4.2 K7 badge

The badge is a **sibling** of the band (not a child), created after it, so it paints over
the band's left border, and it is animated to the same y (`header:78-84,153-155`).

| property | value | source |
|---|---|---|
| rect | **(16,6) 96×71**, r8, 2 px border | `header:51-53,153-155` |
| dark fill / border | **`#111111`** / **`#F15A22`** | `header:126-127` |
| white fill / border | **`#CC4C1C`** (`k_col_logo_back`) / **`#111111`** | `header:126-127` |
| mask | 60×32 A8, `assets/labels/k7b.png` → `header_logo.bin`, tinted | `header_logo.h:1-23`, `tools/convert_cassette_assets.py:34-40` |
| mask position | **(34,25) 60×32** — centred in the badge's content box | `header:156-157`, `ui-readme:342` |
| dark tint | **`#F15A22`** | `header:130` |
| white tint | **`#111111`** | `header:130` |

The mark's shape is the PNG's **alpha** (the asset tool keeps only the alpha plane); the
colour comes from the tint. CSS:

```css
.k7 {
  position: absolute; left: 34px; top: 25px; width: 60px; height: 32px;
  background: currentColor;                 /* dark: #F15A22; white: #111111 */
  -webkit-mask: url("assets/k7b.png") 0 0 / 60px 32px no-repeat;
          mask: url("assets/k7b.png") 0 0 / 60px 32px no-repeat;
}
```

The position is the code's actual render: the image is aligned `LV_ALIGN_CENTER` in the
96×71 badge's 92×67 content box (border 2), so `(2 + (92−60)/2, 2 + (67−32)/2)` relative to
the badge = **(34,25)** screen. (The old `75×40 at (27,21)` in `header.h:18-22` is stale.)

### 4.3 Footer band

| property | value | source |
|---|---|---|
| rect | **(16,359) 768×115**, r8 | `footer:29-32` |
| border | 2 px `#60240D` | `footer:132-134` |
| fill, dark / white | `#111111` / `#909090` | `footer:178` |
| hidden pose | y = **480** (rows 480…594, fully below) | `footer:41` |
| slide | y `current → 359` / `current → 480`; **121 px travel** | `footer:101-106` |
| duration/ease | 260 ms, ease-out | `footer:102-103` |
| hide completion | hidden | `footer:84-88` |

Its top row is **359**, shared with the last button/cell; its bottom is 473, leaving a 6 px
gap to the panel (which the page margin fills: 474…479). Content outside the Menu: none
(three labels empty and hidden, `footer:145,152,162`).

### 4.4 Menu texts (the bands' only content)

These are hidden while the Menu is closed but must be specified for the Menu mode; both
fields ride their band's slide.

| text | box | font | baseline | alignment | ink dark / white |
|---|---|---|---|---|---|
| header title | **(126,22) 648×39** | Jura 36 | **y = 53** | left, vertically centred; `LONG_DOT` at 648 px | `#F15A22` / `#111111` |
| footer song | **(40,379) 512×30** | Jura 28 | **y = 403** | top-left; `LONG_DOT` at 512 px | `#F15A22` / `#111111` |
| footer group | **(40,413) 512×30** | Jura 28 | **y = 437** | top-left; `LONG_DOT` at 512 px | `#F15A22` / `#111111` |
| footer position | **(560,401) 200×30** | Jura 28 | **y = 425** | right-aligned, right boundary **760** | `#F15A22` / `#111111` |

Computed from the style offsets and the band's 2 px border (`header:58-60,163-168`;
`footer:50-59,140-162`): title x = 110 + 2 + 14 = 126, y = 6 + 2 + (67−39)/2 = 22;
song x = 16 + 2 + 22 = 40, y = 359 + 2 + 18 = 379; group y = 359 + 2 + 52 = 413;
position x = 16 + 2 + (764 − 200 − 22) = 560, y = 359 + 2 + (111−30)/2 = 401.

Baselines: Jura 36 `line_height 39, base_line 8` → top + 31; Jura 28 `30, 6` → top + 24
(`font_jura_36.c:5241-5242`, `font_jura_28.c:3640-3641`). Content: header = the Menu list
title (`Menu`, `Tracks`, `Browse /Jazz`, …); footer = song, group, list position
(`3 / 1204`). A blank string hides the label.

**Doc drift**: `header.c:55-60` and `menu.md:61` say the title is at band x=104 (screen
214). The code's `TITLE_X` is 16 → screen **126**. Use 126.

---

## 5. The window around the cassette

| property | value | source |
|---|---|---|
| rect | **(110,76) 486×284**, r8 | `window:20-28` |
| border | 2 px **`#111111`** (`k_col_bg`) | `window:70-72` |
| fill | **`#111111`**, opaque | `window:70-73` |
| hidden | opacity **0**, hidden | `window:73-74` |
| fade in/out | opacity `current → 255` / `current → 0` | `window:46-64` |
| duration/ease | **260 ms**, ease-out | `window:59-60` |
| hide completion | hidden | `window:40-44` |
| Menu page role | fill becomes `#909090` while the Menu is open on the white page, `#111111` otherwise | `window:84-92` |
| z-order | screen child **above** the back cover, **below** the cassette container | `view:442,448,456` |

**It is invisible in the controls state, on both pages.** Its fill/border are the same
`#111111` as the 768×448 back cover that lies underneath it; its 8 px corners also reveal
that same ink. The tape (148…549 × 102…333) is drawn over it. So outside the Menu the
fade has no visible effect; it becomes visible only when the Menu recolours its fill to
the page's white (§10). Implement it (cheap) but do not draw a dark rectangle around the
tape on the white page expecting it to stand out — the dark area you see there is the
**back cover**, not the window.

Inside the window, at this pose, the tape sits with margins: left 38 (148−110), right 46
(596−550), top 26 (102−76), bottom 26 (360−334). The window's right boundary (596) is the
Pad's left edge in the Menu; the DJ block starts at 594, i.e. 2 px over the window's right
border (the block paints over it; both dark/invisible here).

---

## 6. Page flip

### 6.1 What changes, in one frame

`page_set(white)` (`view:208-227`) is a **cut**, not an animation, and runs **before** the
drawer/block/bands start their slides (`view:233-242` calls it first). A repeat request
is a no-op (`view:209-210`).

| write | dark → white | source |
|---|---|---|
| screen root bg | `#111111` → `#909090` | `view:217` |
| edge mark | reveal (delayed fade) / hide instantly | `view:218-219,180-206` |
| header fill | `#111111` → `#909090` | `header:182-188` |
| footer fill | `#111111` → `#909090` | `footer:175-180` |
| drawer buttons (both states) | rect `#909090`; disc/rim/shadows; letters `#111111` | `buttons:394-401` |
| DJ block | knob plates `#727272`/`#111111`; M/R cells `#909090`; caps/rims; knob faces/shadows | `eq:793-817` |
| header badge/mark, Menu texts | per §4.2/§4.4 | `header:123-140`, `footer:111-117` |
| track cover | hidden (any flip) | `view:213-216` |
| cassette window chips | **not repainted** — they are `#111111` on both pages | `view:224-226`, `ui-readme:423-425` |
| window | **not repainted** (still `#111111`) | `window` has no set_page |

A close always returns to the dark page (`controls_set_shown(false, …)` passes
`shown && white` = false, `view:236`), and the page flips at the start of the close, so the
chrome slides out over the dark page.

### 6.2 Back cover and the white margin

The back cover is a screen child **created before everything else**: `(16,16) 768×448`,
r0, opaque `#111111`, never hidden or recoloured (`view:442`). On the white page it is the
dark ground; the page colour shows only as the 16 px ring:

* left `x 0…15`, right `x 784…799`, top `y 0…15`, bottom `y 464…479`;
* the header (y 6…76) and badge (y 6…) cover part of the top ring; the footer (to y 473)
  covers part of the bottom ring; the drawer plates start at x=16 and the block ends at
  x=783, so the ring and those plates touch and read as one paper frame.

The bands' white fill is `#909090`, exactly the ring colour, so on the white page the
bands merge with the margin; on the dark page they merge with the ground. This is the
whole "one frame" device of the controls screen.

### 6.3 Edge mark (brief; detail in 01 §5)

Only on the white page, child of the cassette container (created after the cover, before
the tapes/meter, so it is **under the tape** and over the cover). `(136,82) 426×272`, r10,
1 px border on **top + left only** in `#8A8A8A` (`k_col_glass`), plus a linear gradient on
the box's top-left→bottom-right diagonal (the C comment calls it 135°; the box is not
square) from white at `90/255` opacity at the top-left to transparent at the bottom-right.
Position is computed from the tape centre (400,238) at the controls pose:
`(349−213, 218−136) = (136,82)` (`view:81-101,103-106,478-499`). It fades **in** after a
**260 ms delay**, over **200 ms, ease-in**, object opacity 0→255; on the dark page it is
hidden instantly, no fade-out (`view:173-206`). The exact gradient/SVG axis is in
`01-cassette-body.md` §5.1.

---

## 7. Timings, easing, reversal

All LVGL animations are 0…1024 steps; the `path_cb` names map exactly to CSS cubic
beziers (`lvgl/src/misc/lv_anim.c:287-322`):

| LVGL path | CSS |
|---|---|
| `lv_anim_path_linear` | `linear` |
| `lv_anim_path_ease_out` | `cubic-bezier(0,0,0.58,1)` |
| `lv_anim_path_ease_in` | `cubic-bezier(0.42,0,1,1)` |
| `lv_anim_path_ease_in_out` | `cubic-bezier(0.42,0,0.58,1)` |

### 7.1 The animations

| transition | property | from → to | ms | easing | completion |
|---|---|---|---:|---|---|
| cassette pose | scale / dx / dy | current → (134,−51,−20) or (256,0,0) | **260** | ease-out | — (`view:287-318`) |
| drawer | panel `x` | current → 0 / −120 | **260** | ease-out | hide on hide-done (`buttons:307-328`) |
| DJ block | every cell `x` = base + offset | current → 0 / **+208** | **260** | ease-out | hide on hide-done; raise on show-done (`eq:678-699,762-773`) |
| header + badge | `y` | current → 6 / −71 | **260** | ease-out | hide both on hide-done (`header:97-117`) |
| footer | `y` | current → 359 / 480 | **260** | ease-out | hide on hide-done (`footer:90-107`) |
| window | opacity | current → 255 / 0 | **260** | ease-out | hide on fade-done (`window:46-64`) |
| edge mark | opacity | 0 → 255, after 260 ms delay | **200** | ease-in | hide instantly on dark (`view:180-206`) |
| button/cell pulse | scale | 256 → 232 → 256 | **160** | linear triangle | — (`buttons:142-164`, `eq:571-592`) |
| page flip | all page inks | cut | **0** | — | one frame (`view:208-227`) |
| press state | hidden swap | cut | **0** | — | on PRESSED/RELEASED (`buttons:112-117`) |
| edit dim (out of scope) | `opa_layered` | 255 ↔ 0 | **0** | — | one frame (`view:326-330`) |

The pose and the drawer start in the same frame: `cassette_buttons_set_shown()` calls the
`room` callback (`buttons:367`) before it starts the panel slide (`buttons:368`), so the
cassette's 260 ms shrink and the panel's 260 ms slide are simultaneous. The block is
started immediately after the drawer in `controls_set_shown` (`view:240-241`).

### 7.2 Reversal

Every animation is "delete the old one, start a new one from the value on screen, full
duration":

```
lv_anim_delete(var, cb);
values = { current_value, target };   // never proportional to the distance
duration = 260;                       // always
```

(`buttons:316-327`, `eq:752-772`, `header:105-116`, `footer:95-106`,
`window:51-63`, `view:295-317`.) Consequences for the sim:

* a reversal mid-slide starts from the current pixel/opacity and still takes the full
  260 ms — the speed changes, the duration does not;
* a hide's completion callback never runs when the animation was deleted (so a reversed
  hide does not hide the object);
* a show always unhides the object before starting (`buttons:311`,
  `header:100-103`, `footer:93`, `eq:759`, `window:49`);
* the page flip is not reversed/animatable: it cuts to the requested page at the moment
  the new state is requested, and a close always targets dark;
* the edge mark's delayed fade is cancelled and hidden instantly when the page goes dark,
  and restarted from opacity 0 when it goes white (`view:180-206`);
* a new press cancels an in-flight pulse and resets scale to 256 before swapping state
  (`buttons:170-174`).

---

## 8. Z-order

Bottom → top, from creation order in `cassette_view_create()` (`view:435-551`) and the
modules' own creation order. "Child" means inside the same parent.

| # | layer | parent | source |
|---:|---|---|---|
| 1 | screen root (page backdrop) | — | `view:437-439` |
| 2 | back cover (16,16,768×448) | screen | `view:442` |
| 3 | window (110,76,486×284) | screen | `view:448` |
| 4 | cassette container (0,0,800×480, transparent) | screen | `view:456-464` |
| 4.1 | track cover | cassette | `view:470-472` |
| 4.2 | edge mark | cassette | `view:480-499` |
| 4.3 | tapes: frames, chips, packs/strands, rollers, run, hubs | cassette | `view:503` |
| 4.4 | VU meter | cassette | `view:504` |
| 5 | peek | screen | `view:509` |
| 6 | drawer panel + P/`<`/`>` (root, then each root's states/children) | screen | `view:514` |
| 7 | DJ cells, creation order GAIN, HI, MED, LOW, M, R | screen | `view:518,725-731` |
| 8 | 3× edit knob (hidden) | screen | `eq:732` |
| 9 | edit overlay (only while editing; created on `lv_screen_active` and moved foreground) | screen | `eq:487-502` |
| 10 | Menu page + rows + Pad (only while open) | screen | `view:523-526` |
| 11 | header band + title | screen | `view:531` |
| 12 | K7 badge + mask | screen | `header:153-157` |
| 13 | footer band + texts | screen | `view:532` |

Runtime raises that can re-order 6/7/10/11/12/13:

* the drawer moves itself to the foreground when shown (`buttons:311-313`) → above the
  block/bands/Menu at that instant;
* the DJ block raises all six cells when its show-slide completes and when the view asks
  (`eq:688-699,775-783`) → above the bands/Menu at that instant. It does not raise while
  editing or covered;
* the block is hidden/covered when the Menu is up, so its raise cannot paint over the
  Menu (`eq:841-860`, `menu.c:623-626`);
* the Menu's page/Pad are built after the block and drawer, so they are above them; the
  bands/badge are built after the Menu, so they are above it.

The 2 px overlaps in §1.4 are resolved by this order; all inks at the seams are equal, so
a simpler sim may ignore the intra-block ordering except at rounded corners.

---

## 9. Palette and fonts

### 9.1 Palette (exact)

`COL_DARKEN_HEX(c,p)` truncates each channel: `channel*p/100` (`style.h:21-24`).

| token | hex | derivation | used here by |
|---|---|---|---|
| `k_col_bg` | **`#111111`** | `COL_BLACK` | page dark, all dark-page plate fills, window, back cover, M face, knob-cell border (white) |
| `k_col_white` | **`#909090`** | `COL_WHITE` | white page, white band/rect/cell fills, Pad plate |
| `k_col_primary` | **`#F15A22`** | `COL_PRIMARY` | all orange line art, badge dark border/mark |
| `k_col_button_rect` | **`#60240D`** | 40 % of primary | every carcase/border except the badge: buttons, DJ cells, bands |
| `k_col_button_circle` | **`#F15A22`** | primary | dark-page rings (buttons, M/R, knobs) |
| `k_col_button_letter` | **`#F15A22`** | primary | dark-page letters |
| `k_col_button_r` | **`#AA2200`** | literal | R's white-page cap |
| `k_col_dome` | **`#929292`** | literal | drawer cap face + every white-page cap rim |
| `k_col_button_white` | **`#EEEEEE`** | literal | M/R letters on the white page |
| `k_col_knob_cap` | **`#383838`** | literal | four knob faces on the white page |
| `k_col_knob_rect` | **`#727272`** | literal | four knob cells on the white page |
| `k_col_eq_dim` | **`#5B220C`** | 38 % of primary | unlit crown dots |
| `k_col_eq_lit` | **`#F15A22`** | primary | lit crown dots |
| `k_col_eq_tick` | **`#FF8A3D`** | literal | 0 dB tick + stick |
| `k_col_glass` | **`#8A8A8A`** | 96 % of `#909090` | edge mark border |
| `k_col_logo_back` | **`#CC4C1C`** | 85 % of primary | badge plate on the white page |
| `k_col_frame` | `#111111` | `COL_BLACK` | chips' fill (body spec, not chrome) |

Sources: `style.c:6-39`. Note the channel math: `#F15A22` at 40 % = `(0x60,0x24,0x0D)`,
at 38 % = `(0x5B,0x22,0x0C)`, at 85 % = `(0xCC,0x4C,0x1C)`; `#909090` at 96 % =
`(0x8A,0x8A,0x8A)`.

**Misleading names**: `k_col_white` is mid-grey `#909090`; `k_col_dome` is a slightly
lighter grey `#929292`; `k_col_glass` is `#8A8A8A`.

### 9.2 Fonts

| font | file / generation | metrics | used by |
|---|---|---|---|
| **Jura 36** | `assets/fonts/Jura-VariableFont_wght.ttf`, variable `wght` 300…700, **default/used 300**; `lv_font_conv --size 36 --bpp 4` (`font_jura_36.c:1-6`) | `line_height 39`, `base_line 8` | drawer letters, M/R letters, header title |
| **Jura 28** | same face, `--size 28`, 4 bpp (`package.json:15`) | `line_height 30`, `base_line 6` (`font_jura_28.c:3640-3641`) | footer song/group/position |
| **Jura 20** | same face, `--size 20`, 4 bpp | `line_height 21`, `base_line 4` (`font_jura_20.c:2425-2426`) | Menu values (not used in chrome) |
| **xtape 36** | `assets/fonts/xtape.ttf` (icomoon), `--size 36 --bpp 4`, range `0xF000-0xF011` (`font_xtape_36.c:1-6`) | `line_height 37`, `base_line 3` | Menu-mode drawer icons |
| **xtape 24** | same, `--size 24` | `line_height 25`, `base_line 2` | Menu rows (not chrome) |

There is **no letter spacing** and no kerning in a one-glyph label. Use
`font-weight: 300` (the variable font's default instance) and **do not synthesise bold**;
Jura's 36 px raster is a 36 px em-size text, not a 36 px cap height.

```css
@font-face {
  font-family: "Jura";
  src: url("assets/jura.ttf") format("truetype");
  font-weight: 300 700;      /* variable; use 300 */
  font-style: normal;
}
.jura36 { font-family: Jura; font-weight: 300; font-size: 36px; line-height: 39px; }
.jura28 { font-family: Jura; font-weight: 300; font-size: 28px; line-height: 30px; }
```

### 9.3 Glyph metrics (for exact letter placement)

`advance = (adv_w + 8) >> 4` (`lv_font_fmt_txt.c:252-258`); glyph box is
`box_w × box_h` at `(ofs_x, ofs_y)` from the pen position; glyph top formula in §2.3.

| font | char | advance | box | ofs_x | ofs_y | source |
|---|---|---:|---:|---:|---:|---|
| Jura 36 | `P` | 20 | 16×23 | 3 | 0 | `font_jura_36.c:4683` |
| Jura 36 | `<` | 20 | 14×16 | 3 | 4 | `font_jura_36.c:4663` |
| Jura 36 | `>` | 20 | 13×16 | 3 | 4 | `font_jura_36.c:4665` |
| Jura 36 | `M` | 28 | 22×23 | 3 | 0 | `font_jura_36.c:4680` |
| Jura 36 | `R` | 21 | 17×24 | 3 | −1 | `font_jura_36.c:4685` |
| xtape 36 | U+F000 cassette-tape | 36 | 36×28 | 0 | 2 | `font_xtape_36.c:1172` |
| xtape 36 | U+F00D angle-right | 36 | 18×37 | 9 | −3 | `font_xtape_36.c:1185` |
| xtape 36 | U+F00E angle-left | 36 | 18×37 | 10 | −3 | `font_xtape_36.c:1186` |

---

## 10. Menu mode: the drawer's labels and the Pad's ink

The Menu is a mode of the Controls, not another screen (`menu.md:1-10`). It changes only
the drawer's labels/actions and covers the block with the Pad; the geometry, caps, rings,
shadows and pulse of §2 are unchanged.

### 10.1 Drawer labels

| button | Menu action | label (xtape 36) | codepoint | source |
|---|---|---|---|---|
| P | Close the Menu (any depth) | `cassette-tape` | **U+F000** | `menu.md:158-164`, `buttons:72-75,122-127` |
| `<` | Back (up one list; closes at root) | `angle-left` | **U+F00E** | same |
| `>` | Enter (same as a tap on the Pad) | `angle-right` | **U+F00D** | same |

The font swap is `xtape_36` for `jura_36`; both states are written, so the swap is
invisible whichever is up (`buttons:119-137`). Transport prev/skip is suspended while the
Menu is open (`buttons.h:105-112`). Geometry/placement of the icons (label 36×37 centred
on the 64 px circle; glyph boxes):

| button | label box (absolute) | glyph box (absolute) |
|---|---|---|
| P | (46,106) 36×37 | **(46,110) 36×28** (U+F000) |
| `<` | (46,200) 36×37 | **(56,200) 18×37** (U+F00E) |
| `>` | (46,294) 36×37 | **(55,294) 18×37** (U+F00D) |

Computed with the §2.3 formula (`line_height 37, base_line 3`, advance 36 for all three).

### 10.2 Pad ink

The Pad replaces the DJ block's place; it is a screen child above the block (created
after it) and the block is hidden while covered (`menu.c:623-626`, `eq:841-860`).

| property | value | source |
|---|---|---|
| plate | **(596,76) 186×284**, r8 | `menu.c:62-66,544-551` |
| plate fill | **`#909090`** (`k_col_white`) | `menu.c:546-547` |
| plate border | none | `menu.c:548` |
| inner | (604,84) 170×268, r8 | `menu.c:70-71,558-566` |
| inner fill | **`#111111`** (`k_col_bg`) | `menu.c:562-563` |
| guide | vertical dotted line at **x = 623**, y **150…286**, 2 px wide | `menu.c:68-79,570-583` |
| guide dash | **dash 2, gap 4**, rounded caps, **`#909090`** | `menu.c:575-580` |
| hit surface | the plate is the only surface; the inner/guide are decorative | `menu.c:552-568` |

The guide's x: inner width 170, centre 85, minus 66 = 19 → absolute 604 + 19 = **623**;
top = 84 + 66 = 150, bottom = 84 + 202 = 286. The Pad ink does not change with the page
(the plate is always `#909090`; only the Menu's page/window fill changes around it).

Menu ink table for completeness (`menu.md:170-186`, code-confirmed): Band fill
`#111111` on the white page / `#F15A22` on the dark page; Band label/icon/chevron
`#EEEEEE` on the white page / `#111111` on the dark page; unselected Row label/icon = the
page's ink (`#111111` / `#F15A22`); dimmed Row = 40 % of the label ink; the playing dot =
the label ink.

---

## Appendix A — documentation drift (read this before trusting a doc value)

| # | doc / comment | says | code actually does | spec uses |
|---:|---|---|---|---|
| 1 | `ui-readme:324-325`, root `README.md:577`, `header.h:7-8` | header band 768×71 at (16,6) | **674×71 at (110,6)**, badge fills 16…111 | code (also `menu.md:44`) |
| 2 | `ui-readme:336-342`, `header.h:13-14`, commit `397fc27` | dark badge = primary plate + light mark; white badge = white plate + dark border/mark | dark: **`#111111` plate**, `#F15A22` border + mark; white: **`#CC4C1C` plate**, `#111111` border + mark (`header:126-130`) | code |
| 3 | `ui-readme:303-310` | contact shadow 8 px at (1,2), 60 % black | **16 px at (−2,−2), 80/255 white** (`widgets.c:18-22`) | code |
| 4 | `header.c:55-60`, `menu.md:61` | Menu title at band x=104 → screen 214 | `TITLE_X 16` → screen **126** | code |
| 5 | root `README.md:583` | window 470×284 at (110,76) | **486×284 at (110,76)** (`window:20-23`) | code (also `menu.md:45` and `ui-readme:346`) |
| 6 | root `README.md:577-584`, `ui-readme:345-349` | the window is the third visible frame of the controls screen | fill/border are `#111111`, same as the back cover → **invisible outside the Menu** | code; see §5 |
| 7 | `view:66-70`, root `README.md:590` | "192 px DJ block at 592..784" | **190 px at 594..783** (first column +2; boundary 594..784) | code (also `ui-readme:429-432`) |
| 8 | `header.h:18-22` | K7 art 75×40 at (27,21) | **60×32 at (34,25)** | code (`header_logo.h:9-10`, `ui-readme:342`) |
| 9 | `header.c:8-9` | the header's "height is the distance" | travel **77 px** (y −71 → 6) | code |
| 10 | `ui-readme:317` | press shows only through the pulse on the white page | drawer rect also thickens 2→3 px; on the dark page rect+ring thicken | code |
| 11 | `.scratch/controls_white_preview.py`, `.scratch/*preview*` | page `#FFFFFF`, window 470, nudge +1, contact 8/1/2, no back cover | they are stale mock-ups; do not use for values | code |
| 12 | `widgets.c:8-11` comment | contact "8 px at 1/2 px" | constants 16 at (−2,−2) | code constants |

If the parent wants the *documented* design rather than the firmware, the only visible
differences are #1 (band width), #2 (badge colours), #3 (contact shadow), #4 (title x),
#5 (window width) and #7 (block right edge). Everything else is identical.

---

## Appendix B — acceptance checks

At 134/256, −51, −20, with the controls settled and all values at 50:

| # | check | expected |
|---:|---|---|
| 1 | cassette ink box | x 148…549, y 102…333 (402×232) |
| 2 | outer frame | x 148…549, y 102…289 (402×188) |
| 3 | header band | x 110…783, y 6…76; border 2 `#60240D` |
| 4 | K7 badge | x 16…111, y 6…76; mask x 34…93, y 25…56 |
| 5 | footer band | x 16…783, y 359…473 |
| 6 | window | x 110…595, y 76…359 (invisible: same ink as ground) |
| 7 | P rectangle / circle | 16…111, 76…171 / 32…95, 92…155 |
| 8 | P glyph (Jura 36) | x 57…72, y 113…135 |
| 9 | `<` glyph | x 57…70, y 210…225 |
| 10 | `>` glyph | x 57…69, y 304…319 |
| 11 | M glyph / circle | x 631…652, y 207…229 / circle 610…673, 186…249 |
| 12 | R glyph / circle | x 635…651, y 301…324 / circle 610…673, 280…343 |
| 13 | GAIN cell / knob | 594…689, 76…171 / 620…663, 102…145 |
| 14 | HI cell / knob | 688…783, 76…171 / 714…757, 102…145 |
| 15 | MED cell / knob | 688…783, 170…265 / 714…757, 196…239 |
| 16 | LOW cell / knob | 688…783, 264…359 / 714…757, 290…333 |
| 17 | 0 dB tick (GAIN) | x 640…643, y 90…93 (`#FF8A3D`) |
| 18 | stick (GAIN, 0°) | x 640…643, y 108…123 (`#FF8A3D`) |
| 19 | white page ring | 0…15 / 784…799 / 0…15 / 464…479 = `#909090` |
| 20 | plate fills per page | dark: header/button/cell fill `#111111`; white: `#909090`, knob cells `#727272` |
| 21 | pressed drawer button | rect 3 px, ring 3 px (dark) / rect 3 px, rim 4 px (white) |
| 22 | pulse | scale 0.90625 at 80 ms, 1.0 at 0/160 ms |
| 23 | white page is not white | 16 px `#909090` ring + light plates on the `#111111` back cover; badge plate `#CC4C1C`; knob faces `#383838` |

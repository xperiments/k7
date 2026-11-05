# 03 — The Menu mode (Root / Tracks / Browse / Settings / Station / Library / About)

Implementation spec for a pixel-for-pixel HTML/SolidJS port of the firmware's **Menu**, a
*mode of the Controls* (not a screen): the cassette area becomes a five-row list, the DJ block
becomes the **Pad**, and the transport drawer becomes **Close / Back / Enter**
(`docs/specs/menu.md:3-16`). It is reachable only from the settled controls pose
(134/256, drawer out) and is modal until closed.

Everything below is in the panel's design space: **800 × 480, origin top-left, 1 CSS px = 1
device px**. LVGL boxes are `(x, y, w, h)` with `box-sizing: border-box`; borders and strokes
are drawn **inside** the box. Where the spec doc and the C disagree, the **C wins**; every
known disagreement is listed in §12.

Read together with `04-peek-and-gestures.md`: that file owns the screen-level gesture state
machine (`S5 Menu open` is its row for this mode). This file owns the Menu's own inputs: the
Pad, the drawer's three menu buttons, and the up-swipe that closes it.

## Sources

| short | file |
|---|---|
| `menu.md` | `docs/specs/menu.md` |
| `menu.c` / `menu.h` | `components/portable/menu/menu.c` / `include/menu.h` |
| `menuview` | `components/app/screen_display/cassette_menu.c` |
| `buttons` | `components/app/screen_display/cassette_buttons.c` |
| `header` | `components/app/screen_display/cassette_header.c` |
| `footer` | `components/app/screen_display/cassette_footer.c` |
| `eq` | `components/app/screen_display/cassette_eq.c` |
| `view` | `components/app/screen_display/cassette_view.c` |
| `window` | `components/app/screen_display/cassette_window.c` |
| `widgets` | `components/app/screen_display/cassette_widgets.c` |
| `style.c` / `style.h` | `components/app/screen_display/cassette_style.c` / `.h` |
| `icons.h` | `components/app/screen_display/cassette_icons.h` |
| `font_jura_20/28/36.c` | `components/app/screen_display/font_jura_*.c` |
| `font_xtape_24/36.c` | `components/app/screen_display/font_xtape_*.c` |
| `t18` | `host/tests/t18_menu.c` |
| `lvgl` | `managed_components/lvgl__lvgl` |

## 0. Toolkit conventions to reproduce (they decide pixels)

1. **Children are placed from the parent's content box** (`lvgl/src/core/lv_obj_pos.c:780-905`),
   i.e. inside the parent's border + padding. `cassette_border_box()` removes all styles first,
   so padding is 0: an object with a 2 px border puts its children at **+2 px** from its outer
   corner (`widgets.c:38-54`). The page has a **6 px padding**, so its rows start at **+6**
   (`menuview:494-503`).
2. **A label's box is one line box**, high `line_height` with the baseline `line_height -
   base_line` below its top. `MID` alignments place it at **`content_h/2 − line_height/2`**,
   each division truncated separately (`lv_obj_pos.c:857-900`). Do **not** use
   `(content_h − line_height)/2`: the two differ by 1 px whenever the parities differ (e.g.
   content 54, line 25 → 27−12 = **15**, not 14). See §3 for the five faces and §2.6 for
   every baseline.
3. **All geometry is integer C arithmetic**: `slot_y = (i*268 + 2)/5`, `content_h/2 − h/2`,
   `54/stage`. Do not let CSS compute halves: e.g. a 53 px row's dot is at **y+23**, not 23.5
   (`menuview:147-150, 534`).
4. **Kerning and letter spacing.** The Jura faces are generated **with pair kerning**
   (`kern_dsc = &kern_classes`, `font_jura_20.c:2400`, `font_jura_28.c:3615-3616`,
   `font_jura_36.c:5216`), so keep CSS `font-kerning: normal` (the browser reads the same
   GPOS pairs) and `letter-spacing: 0`. The icon faces carry none (`font_xtape_24.c:628`,
   `font_xtape_36.c:1226`), which is irrelevant for single glyphs.
5. **Ellipsis is three ASCII dots**, not U+2026: `LV_LABEL_DOT_NUM = 3`
   (`lvgl/include/lvgl/widgets/lv_label.h:31`, `lv_label.c:1443-1472`). The label keeps whole
   characters until `text + "..."` fits its box. `text-overflow: ellipsis` is wrong (different
   glyph and advance). Menu boxes: label 400 / value-row label 340 / right column 150 /
   header title 648 / footer lines 512 / footer position 200.
6. **No shadows, no gradients, no hover, no press feedback** anywhere in the Menu itself.

---

## 1. Inventory, z-order and visibility

Screen children in creation order, bottom → top (`view:435-532`; the same list is in
`04-peek-and-gestures.md` §5):

| # | object | geometry | source | while the Menu is up |
|---|---|---|---|---|
| 1 | screen backdrop | 800×480 | `view:439, 217` | page colour: `#909090` white / `#111111` dark |
| 2 | back cover | `(16,16) 768×448`, r0, `#111111` | `view:442` | always drawn |
| 3 | window | `(110,76) 486×284`, r8, fill page / `#111111`, 2 px `#111111` border | `window:20-24, 70-72` | shown; fill becomes the page's colour |
| 4 | cassette container | 800×480 | `view:456-505` | **hidden** (tape, hubs, meter, glow, cover, edge mark) |
| 5 | peek | — | `view:509` | hidden (never coexists with the controls) |
| 6 | transport drawer + 3 buttons | panel `120×480` at `(0,0)`; buttons `(16,76/170/264) 96×96` | `buttons:43-57`, `view:514` | shown, **menu labels** |
| 7 | DJ block (6 cells) | block `(592..782, 76..360)` | `eq:48-74`, `view:518` | **hidden** (`eq:838-859`) |
| 8 | Menu page (5 rows) | `(112,78) 482×280` | `menuview:494-538`, `view:524` | shown |
| 9 | Pad | `(596,76) 186×284` | `menuview:542-584` | shown |
| 10 | header band | `(110,6) 674×71`, r8 | `header:38-41`, `view:531` | shown (controls chrome) |
| 11 | K7 badge + mark | `(16,6) 96×71` | `header:51-53` | shown |
| 12 | footer band | `(16,359) 768×115`, r8 | `footer:29-32`, `view:532` | shown |

Notes:

- The header band and badge are **created after** the Menu page and the Pad, so their 1 px
  overlap at `y=76..77` / `y=359..360` paints over the page and the Pad. The badge is created
  after the band, so the badge's 2 px border is the divider and the band's left border tucks
  under it (`header:145-157`).
- There is **no separate page rectangle**: the visible page ground is the *window's fill*
  (`window:84-94`). The page object is transparent.
- The Pad's left edge is `596`, i.e. 4 px right of the block's `592`, and flush with the
  block's right edge `782` and its `76..360` band.

**Hidden while open:** the cassette container (and therefore the cover, edge mark, glow,
tapes, hubs and VU meter), the six DJ cells, the peek, and the block's pending 550 ms
knob-open timer (cancelled by `cassette_eq_set_covered`, `eq:838-859`). The Cover's "wish" is
never written, so it returns exactly as it was (`menu.md:29-31`).

**Shown but passive:** the header/footer bands take no input (their text is set by the
Menu); the drawer's three buttons stay live and are re-aimed. The rows, the page and the
window take no touches (`menuview:494-538` sets every row and the page non-clickable,
non-scrollable).

---

## 2. Geometry

### 2.1 The page

| part | x | y | w | h | notes |
|---|---|---|---|---|---|
| window (outer) | 110 | 76 | 486 | 284 | radius 8, border 2 `#111111` |
| **page object** | **112** | **78** | **482** | **280** | the window's content box; transparent, pad 6, non-clickable |
| page content | 118 | 84 | 470 | 268 | rows live here |

`menuview:488-503`: `px=wx+2, py=wy+2, pw=ww-4, ph=wh-4`, `rw=pw-12=470`,
`content_h=ph-12=268`.

The page's fill is the **window's**: `cassette_window_set_menu_page(true, white)` turns the
window's fill `#909090` on the white page (`#111111` on the dark page) for the Menu's lifetime
and restores `#111111` on close (`window:84-94`, `menuview:623, 645`). The window's border
stays `#111111` on both pages.

### 2.2 Rows and the band

Five slots share the 268 px content height; `slot_y(i) = (i*268 + 2)/5`
(`menuview:147-150`):

| row | page-local y | absolute y | h | band y | band h |
|---|---|---|---|---|---|
| 0 | 0 | **84** | **54** | 86 | 50 |
| 1 | 54 | **138** | **53** | 140 | 49 |
| 2 | 107 | **191** | **54** | 193 | 50 |
| 3 | 161 | **245** | **53** | 247 | 49 |
| 4 | 214 | **298** | **54** | 300 | 50 |

Row x = **118** (page 112 + pad 6), w = **470**. Rows are transparent, borderless,
non-clickable, non-scrollable (`menuview:508-515`).

**Band** (`menuview:517-519`): row-local `(2, 2, 466, row_h-4)`, i.e. absolute x **120**,
radius **8**, fill = band fill (see §7), **no border**. It is the row's first child, so it is
under the icon, label, right column and dot. It is drawn only on the band slot; every other
slot's band is hidden.

### 2.3 Row internals (row-local; add 118 to x, row y to y)

| part | x | y | w | h | notes |
|---|---|---|---|---|---|
| band | 2 | 2 | 466 | row_h−4 | r8, opaque |
| icon | **16** | `row_h/2 − 25/2` = **15** (54) / **14** (53) | 24 (advance) | 25 (line box) | left-middle; `xtape_24`; hidden if `MENU_ICON_NONE` or the glyph is missing |
| label | **56** | `row_h/2 − 30/2` = 12 (54) / 11 (53) | **400**, or **340** on a `MENU_ROW_VALUE` row | 30 | `jura_28`, left, one line, three-dot truncation |
| playing dot | **364** | `(row_h−6)/2` = 24 (54) / 23 (53) | **6** | **6** | r3; hidden unless the row is the playing Track |
| right column | right edge at **454** (box 304..454) | value `row_h/2 − 21/2` = **17** (54) / **16** (53); chevron `row_h/2 − 25/2` = **15** (54) / **14** (53) | **150** | 21 (value) / 25 (chevron) | right-aligned at row-local 454 = **screen x 572** |

Screen coordinates: icon box left **134**, label left **174**, dot left **482**, right box
**422..572**, row right edge **588**. The band's right edge is 586.

- **Value rows** (`Station`, `Library`, `About`): label width 340; the right column is the
  value, `jura_20`, ASCII-uppercased, right-aligned. About's three rows also carry an icon;
  Station's and Library's do not (`menu.c:441-499`).
- **Chevron rows**: `xtape_24` `angle-right` (U+F00D), right-aligned in the same 150 px box.
  Its 24 px advance box is `548..572`; its 12 px-wide ink starts at `ofs_x = 6`, i.e.
  **x 554..565**, and its 25 px-high ink is **row_y+15 .. row_y+39** on a 54 px row
  (`row_y+14 .. row_y+38` on a 53 px row) (`font_xtape_24.c:587`).
- **Dot** is a filled circle in the row's ink (page ink or band ink); it is **fixed at x 482**
  and does **not** follow the label's end (`menuview:534-535`). A label long enough to reach it
  runs underneath; the dot is the last child and paints on top.
- The right column is created after the label, so on the (possible) overlap between a 400 px
  label and the 150 px value box the value paints over the label.
- **Label truncation** applies to the *label only*; the right column truncates separately at
  150 px, the header title at 648, the footer lines at 512 and the footer position at 200.

### 2.4 The Pad

| part | x | y | w | h | radius | paint |
|---|---|---|---|---|---|---|
| plate | **596** | **76** | **186** | **284** | 8 | `#909090`, opaque, no border |
| inner | 604 | 84 | 170 | 268 | 8 | `#111111`, opaque |
| guide | centreline **623** | 150 → 286 | 2 | 136 | — | `#909090` dotted |

Constants `menuview:62-75`: `PAD_INSET 8`, `PAD_INNER_W 170`, `PAD_INNER_H 268`,
`PAD_GUIDE_INSET 66`, `PAD_GUIDE_X = 170/2 − 66 = 19` (inner-local),
`PAD_GUIDE_TOP 66`, `PAD_GUIDE_BOT 268 − 66 = 202`.

The guide is one `lv_line` (`menuview:571-583`): **width 2, colour `#909090`, opacity cover,
rounded ends, dash width 2, gap 4** (period 6 px). In CSS: an SVG `<line>` with
`stroke-width:2; stroke:#909090; stroke-linecap:round; stroke-dasharray:2 4`, from inner-local
`(19, 66)` to `(19, 202)`; or `repeating-linear-gradient(#909090 0 2px, transparent 2px 6px)`
2 px wide. Endpoints are **inclusive** of the round caps' overshoot of 1 px, so the visible
segment is `y 149..287` if you draw caps beyond the points; the firmware's line object spans
`150..286`.

- Only the **plate** is clickable (`menuview:552-554`); the inner and the guide are
  decorative and non-clickable, so a press on them falls through to the plate.
- `lv_obj_set_gesture_bubble(s_pad, false)` (`menuview:553`): a Pad drag never reaches the
  screen, so it can neither open nor close the drawer nor act as the up-swipe.
- No visual state at all: no ripple, no press tint, no hover.
- The Pad is the **only** activator for the rows; the rows and page are non-clickable.

### 2.5 Chrome the Menu borrows (unchanged by the mode)

| part | x | y | w | h | radius | paint (white / dark page) |
|---|---|---|---|---|---|---|
| header band | 110 | 6 | 674 | 71 | 8 | fill `#909090` / `#111111`, border 2 `#60240D` |
| K7 badge | 16 | 6 | 96 | 71 | 8 | fill `#CC4C1C` / `#111111`; border 2 `#111111` / `#F15A22` |
| K7 mark | 34 | 25 | 60 | 32 | — | `#111111` / `#F15A22` (A8 mask tinted) |
| footer band | 16 | 359 | 768 | 115 | 8 | fill `#909090` / `#111111`, border 2 `#60240D` |
| drawer panel | 0 | 0 | 120 | 480 | — | transparent |
| button P | 16 | 76 | 96 | 96 | 8 | see below |
| button `<` | 16 | 170 | 96 | 96 | 8 | |
| button `>` | 16 | 264 | 96 | 96 | 8 | |

Source: `header:38-69, 145-169`; `footer:29-66, 132-166`; `buttons:43-57, 255-290`;
mark size `header_logo.h:7-8`.

- Each button: 96×96 rounded rect, fill `#909090` / `#111111`, border 2 `#60240D`; inside it
  a **64 px circle** at inset 16, on the dark page a 2 px `#F15A22` ring on a transparent face,
  on the white page a flat `#929292` disc with a 4 px `#929292` rim; one centred label, 36 px
  (`buttons:219-253, 372-392`).
- **Menu mode** swaps the label text **and font** on both the pressed and unpressed states
  (`buttons:119-138, 414-424`):

  | button | icon (36 px, `xtape_36`) | action |
  |---|---|---|
  | P | `cassette-tape` U+F000 | close the Menu from any depth |
  | `<` | `angle-left` U+F00E | Back: up one list; closes at the root |
  | `>` | `angle-right` U+F00D | Enter: the same as a tap on the Pad |

  Outside the Menu the strings are Jura 36 `P`, `<`, `>` (`buttons:72-75`). The swap is
  instant, no animation. Icon line box inside a circle: top **button_y + 30**, baseline
  **button_y + 64** (36 px `xtape_36`, line 37, base 3; circle content 60 on the dark page,
  56 on the white one → `30 − 18 = 12` or `28 − 18 = 10` below the content top; both pages
  land the P button on 106 / 140).
- Transport prev/skip is suspended for the Menu's lifetime; the buttons' click handlers are
  redirected to the Menu callbacks (`buttons:192-212`). A click is suppressed when the press
  had a gesture direction or `press_moved` (a single read moved > 20 px in x or y — the runtime
  `scroll_limit`, `touch_gt911.c:297`).
- The header title is the Menu's list title; hidden and empty when the Menu is closed
  (`header:190-197`). The footer's three lines are filled by the Menu and cleared on close
  (`footer:182-187`).

### 2.6 Text runs and baselines (all values are *screen* y)

| run | box x | line box top | baseline | notes |
|---|---|---|---|---|
| header title | 126 | 22 | **53** | Jura 36, w 648, left, dot-truncated |
| row 0 label | 174 | 96 | **120** | Jura 28, w 400 / 340 |
| row 1 label | 174 | 149 | **173** | |
| row 2 label | 174 | 203 | **227** | |
| row 3 label | 174 | 256 | **280** | |
| row 4 label | 174 | 310 | **334** | |
| row 0 icon | 134 | 99 | **122** | x per glyph (§4); line 25 |
| row 1 icon | 134 | 152 | **175** | |
| row 2 icon | 134 | 206 | **229** | |
| row 3 icon | 134 | 259 | **282** | |
| row 4 icon | 134 | 313 | **336** | |
| row 0 right — value | 422..572 | 101 | **118** | Jura 20, right-aligned |
| row 0 right — chevron | 422..572 | 99 | **122** | xtape 24, right-aligned |
| row 1 right — value | 422..572 | 154 | **171** | |
| row 1 right — chevron | 422..572 | 152 | **175** | |
| row 2 right — value | 422..572 | 208 | **225** | |
| row 2 right — chevron | 422..572 | 206 | **229** | |
| row 3 right — value | 422..572 | 261 | **278** | |
| row 3 right — chevron | 422..572 | 259 | **282** | |
| row 4 right — value | 422..572 | 315 | **332** | |
| row 4 right — chevron | 422..572 | 313 | **336** | |
| footer song | 40 | 379 | **403** | Jura 28, w 512, top-left |
| footer group | 40 | 413 | **437** | Jura 28, w 512 |
| footer position | 560..760 | 406 | **423** | Jura 28, right-aligned at 760 |

Derivation (`content_h/2 − h/2`, each truncated): header content `(112,8) 670×67`, title
`33 − 19 = 14` → top 22, baseline `+31`; footer content `(18,361) 764×111`, position
`55 − 10 = 45` → top 406, baseline `+17`; rows: label `27 − 15 = 12` (54) / `26 − 15 = 11`
(53), baseline `+24`; icon line top `27 − 12 = 15` (54) / `26 − 12 = 14` (53), baseline
`+23`; right value line top `27 − 10 = 17` / `26 − 10 = 16`, baseline `+17`; chevron like the
icon (`font_jura_20.c:2425-2426`, `font_jura_28.c:3640-3641`, `font_jura_36.c:5241-5242`,
`font_xtape_24.c:653-654`).

The dot's 6×6 box starts at `(482, row_y+24)` on a 54 px row and `(482, row_y+23)` on a 53 px
row (so its centre is `row_y+27` / `row_y+26`); it spans x `482..487`.

For CSS, either position a line box of exactly `line-height` px at the "line box top" above, or
— better, because browsers place the baseline from their own font ascent — calibrate once per
face:

```ts
// targetBaseline is the table's baseline; LH is the LVGL line height (30 / 21 / 39 / 25 / 37)
const m = ctx.measureText('Ag');            // ctx.font = `400 ${px}px Jura`
const A = m.fontBoundingBoxAscent, D = m.fontBoundingBoxDescent;
el.style.top = (baseline - ((LH - (A + D)) / 2 + A)) + 'px';
el.style.lineHeight = LH + 'px';
```

---

## 3. Fonts

| token | face | px | LVGL line height | base line | CSS |
|---|---|---|---|---|---|
| `jura_20` | Jura (variable, default instance) | 20 | 21 | 4 | `400 20px/21px Jura` |
| `jura_28` | Jura | 28 | 30 | 6 | `400 28px/30px Jura` |
| `jura_36` | Jura | 36 | 39 | 8 | `400 36px/39px Jura` |
| `xtape_24` | xtape (icomoon) | 24 | 25 | 2 | `24px/25px xtape` |
| `xtape_36` | xtape | 36 | 37 | 3 | `36px/37px xtape` |

- Source fonts: `assets/fonts/Jura-VariableFont_wght.ttf` and `assets/fonts/xtape.ttf`;
  generated by the `font:jura`, `font:jura28`, `font:jura20`, `font:xtape` scripts in
  `package.json:14-17` (`lv_font_conv --bpp 4`, no weight flag → the variable font's default
  instance, weight 400). Jura is generated **with pair kerning**; the icon font is not.
  `Jura` carries `0x20-0x7F, 0xA0-0xFF, 0x2010-0x2015,
  0x2018-0x201D, 0x2026`; `xtape` carries exactly `0xF000-0xF011`.
- **Vertical alignment is always MID** (except the footer's two top-left lines): LVGL centres
  the label's line box in the parent's content box, integer division. Baselines are in §2.6.
- **No text is ever bold, italic or underlined.** No shadows.
- Browser metrics differ from LVGL's line box: CoreText reports Jura 20 → ascent 19.30 /
  descent 4.36, 28 → 27.02 / 6.10, 36 → 34.74 / 7.85; xtape 24 → 22.5 / 1.5, 36 → 33.75 /
  2.25. Chrome reads the same `hhea` values, so a CSS line box of `line-height: LH` puts the
  baseline a couple of px below LVGL's: use the calibration snippet in §2.6 with the §2.6
  baselines rather than trusting the raw `top` values.
- The value column is uppercased by the *view*, ASCII only: bytes `a-z` minus 32
  (`menuview:152-162`); non-ASCII bytes pass through, and the em dash is untouched.

---

## 4. Icons (icomoon PUA, `xtape.ttf`)

Every glyph has `adv_w = 384` = **24 px** at 24 px. Ink box and offsets from
`font_xtape_24.c:572-591`. Screen x = **134 + `ofs_x`**; the glyph's ink top is
`line_top + (25 − 2) − box_h − ofs_y` (`lv_draw_label.c:671`), where the icon line box starts
at **row_y + 15** on a 54 px row and **row_y + 14** on a 53 px row. The ink-top column below
is for a 54 px row (subtract 1 on a 53 px row).

| cp | glyph | box w×h | ofs x,y | used by | ink x | ink top (row-relative) |
|---|---|---|---|---|---|---|
| F000 | cassette-tape | 24×19 | 0, 1 | Browse directory rows; drawer Close | 134 | 18 |
| F001 | voice-mail | 24×12 | 0, 5 | spare | 134 | 21 |
| F002 | radio-waves | 24×25 | 0, −2 | Tracks, Station | 134 | 15 |
| F003 | toggle-off | 24×17 | 0, 2 | Shuffle = off | 134 | 19 |
| F004 | toggle-on | 24×17 | 0, 2 | Shuffle = on | 134 | 19 |
| F005 | settings | 24×23 | 0, −1 | Settings | 134 | 16 |
| F006 | books | 23×23 | 1, −1 | Browse, Library | 135 | 16 |
| F007 | endless-loop | 24×22 | 0, 0 | Rescan | 134 | 16 |
| F008 | shuffle | 22×20 | 1, 1 | spare | 135 | 17 |
| F009 | disc-drive | 22×25 | 1, −2 | About / Partition | 135 | 15 |
| F00A | tablet-android | 18×25 | 3, −2 | About / Board | 137 | 15 |
| F00B | brightness | 24×25 | 0, −2 | Backlight | 134 | 15 |
| F00C | fire-shield | 21×25 | 2, −2 | spare | 136 | 15 |
| F00D | angle-right | 12×25 | 6, −2 | row chevron; drawer Enter | **554** (right column) | 15 |
| F00E | angle-left | 13×25 | 6, −2 | drawer Back (36 px) | — (left column never uses it) | — |
| F00F | k7logo | 20×11 | 2, 5 | About | 136 | 22 |
| F010 | track | 24×11 | 0, 5 | Track rows | 134 | 22 |
| F011 | micro-chip | 22×23 | 1, −1 | About / Firmware | 135 | 16 |

If you draw with the real `xtape.ttf` at 24 px (recommended), these offsets come for free; the
table is for SVG/sprite fallbacks. The **right-column chevron** is the same glyph right-aligned
in the 150 px box: its advance box ends at 572, so its 12 px-wide ink is **x 554..565** and
its 25 px-high ink is **row_y+15 .. row_y+39** on a 54 px row (`row_y+14 .. row_y+38` on a
53 px row). The icon is hidden rather than drawn as `.notdef` when the codepoint is absent
(`menuview:210-215, 249-256`).

---

## 5. Lists, rows and values

### 5.1 Tree

```
MENU (title "Menu", 4 rows)
├─ Tracks     F002 radio-waves   chevron   → the whole Playlist, one Track per row
├─ Browse     F006 books         chevron   → a directory of the Library tree
├─ Shuffle    F003/F004          OFF/ON    → toggles the broadcast's shuffle mode
└─ Settings   F005 settings      chevron   → 5 rows
   ├─ Backlight  F00B brightness  AUTO / 100% / 60% / 30%   (acts in place)
   ├─ Station    F002 radio-waves chevron
   ├─ Library    F006 books       chevron
   ├─ Rescan     F007 endless-loop RESCAN / RESCANNING...    (acts; inert while running)
   └─ About      F00F k7logo      chevron → 3 rows
      ├─ Firmware   F011 micro-chip      the app version
      ├─ Board      F00A tablet-android  the board's short name
      └─ Partition  F009 disc-drive      the app partition's size
```

`menu.c:51-64`, `menu.c:302-337`.

### 5.2 Per list

| list | count | rows (label, icon, right column) |
|---|---|---|
| **Root** | 4 | `Tracks` F002 ▸ · `Browse` F006 ▸ · `Shuffle` F003/F004 `OFF`/`ON` · `Settings` F005 ▸ |
| **Tracks** | `library_track_count` | one row per Playlist Track: cleaned name, F010, no chevron; the playing Track carries the dot |
| **Browse** | `library_child_count(dir)` | the directory's children in the library's case-insensitive name order, dirs and Tracks mixed: dir = raw name, F000, ▸; Track = cleaned name, F010, no chevron (+ dot when playing) |
| **Settings** | 5 | `Backlight` F00B value · `Station` F002 ▸ · `Library` F006 ▸ · `Rescan` F007 (no right column) · `About` F00F ▸ |
| **Station** | 3 | `Format` → `MP3` · `Rate` → `44.1 kHz` · `Bitrate` → `256 kbps`; **no icons** |
| **Library** | 5 | `State` → `ok` · `Tracks` → `1,204 tracks` · `Directories` → `38 dirs` · `Size` → `12.4 GB` · `Truncated` → `No`; **no icons** |
| **About** | 3 | `Firmware` F011 → version · `Board` F00A → `8048S050C` · `Partition` F009 → `4 MB` |

Titles: `Menu`, `Tracks`, `Settings`, `Station`, `Library`, `About`, and for Browse
`Browse` + `" /" + component` for every path component: `Browse`, `Browse /Jazz`,
`Browse /Jazz /Blue Train` (`menu.c:302-337`). The Browse path is a **relative string**
(`""` = library root), not a node.

Row kinds (`menu.h:58-65`) decide the activation:

| kind | icon | right | activation |
|---|---|---|---|
| `MENU_ROW_SUBMENU` | yes | chevron | push the list (band 0, window 0) |
| `MENU_ROW_ACTION` | yes | value or nothing | run in place (Shuffle, Backlight, Rescan) |
| `MENU_ROW_TRACK` | F010 | nothing | play `relpath`; the current Track restarts |
| `MENU_ROW_DIR` | F000 | chevron | push Browse into that directory |
| `MENU_ROW_VALUE` | none (About: yes) | value | nothing |
| `MENU_ROW_NOTE` | none | nothing | nothing; disabled |

**Name cleaning** for Track labels (`menu.c:950-953` → `cassette.c:134-197`), also used by the
footer fallback: cut at the **last `.`** (extension), drop parenthetical asides including
nested/unclosed ones, map ASCII `'` and U+2019 to a **space**, collapse runs of
space/tab/CR/LF to one space and trim. UTF-8 sequences are copied whole. Examples:
`01 - Blue Train.mp3` → `01 - Blue Train`; `02 - Moment's Notice.flac` → `02 - Moment s
Notice`; `Don't Stop (Live).wav` → `Don t Stop`; `  padded  .mp3` → `padded`.
**Directory labels are raw** — no cleaning, extension and all.

### 5.3 Value formatting (`menu.c:155-230`)

The model writes prose; the view uppercases it (`menuview:272-275`).

| field | model string | displayed |
|---|---|---|
| Shuffle | `Off` / `On` | `OFF` / `ON` |
| Backlight | `Auto` / `100%` / `60%` / `30%` | `AUTO` / `100%` / `60%` / `30%` |
| Rescan | *(no value)* | — (label becomes `Rescanning...`) |
| Station format | `MP3` / `FLAC` / `WAV` / `—` | same, uppercased (`—` unchanged) |
| Station rate | `44.1 kHz`, `48 kHz`, `—` | `44.1 KHZ`, `48 KHZ`, `—` |
| Station bitrate | `256 kbps` / `—` | `256 KBPS`, `—` |
| Library state | `ok` / `no_card` / `no_dir` / `empty` / `error` | `OK`, `NO_CARD`, … |
| Library tracks | `1,204 tracks` (`1 track` singular) | `1,204 TRACKS` |
| Library dirs | `38 dirs` (`1 dir`) | `38 DIRS` |
| Library size | `0 B`, `210 B`, `1.5 KB`, `12.4 GB` | same |
| Library truncated | `No` / `Yes` | `NO` / `YES` |
| About firmware/board/partition | `397fc27-dirty`, `8048S050C`, `4 MB` | `397FC27-DIRTY`, `8048S050C`, `4 MB` |
| any NULL fact / idle status | `—` (U+2014) | `—` |

Rules: counts are grouped with `,` every three digits from the right, plural unless exactly
1; size is binary (`/1024`) with **one decimal** for KB/MB/GB and no decimals for bytes
(`menu.c:176-188`); rate is tenths of a kHz rounded by `(hz+50)/100`, the `.0` dropped;
bitrate is `"%u kbps"`; format is `MP3`/`FLAC`/`WAV` else `—`; an unknown backlight percent
falls back to `Auto`; the backlight cycle is `Auto → 100 → 60 → 30 → Auto`
(`menu.c:212-230`).

### 5.4 Empty, dimmed and disabled rows

`enabled == false` → the icon, label and right column are drawn at **40 % opacity**
(`LV_OPA_40 = 102/255`, `menuview:243`); the band fill is still opaque, so a dimmed row on
the band shows band-ink at 40 %.

| row | disabled when |
|---|---|
| Root `Tracks`, Root `Browse` | the library lease is absent **or** its state is not `ok` (`LIB_STATE_NO_CARD/NO_DIR/EMPTY/ERROR`) — `menu.c:381-395`, `menu.c:107-110` |
| Settings `Rescan` | `facts.rescanning` (label `Rescanning...`) — `menu.c:397-412` |
| the note row | always |
| every other row | never (Library/Station/About still open with no card) |

**One-row note lists** replace Tracks and Browse when the library is unusable
(`menu.c:235-243, 245-248, 259-275`):

| condition | note label |
|---|---|
| no lease (`NULL`) | `No card` |
| state `no_card` | `No card` |
| state `empty` | `Library empty` |
| state `no_dir` / `error` | `Library error` |
| state `ok` but 0 tracks | `Library error` in the code (see §12); the library never produces this |

The note row has count 1, kind `NOTE`, no icon, no chevron, no dot, `enabled=false`; the
remaining four slots are blank. The dot is the one thing `enabled` never dims: only Track rows
carry it, and they are always enabled. An **empty directory inside a usable library is a count-0
Browse list**, not a note: five blank rows, band drawn on slot 0 (see §12).

Blank slots (index ≥ count) have empty label, hidden icon/right/dot/band; nothing paints but
the page fill.

---

## 6. Interaction

### 6.1 Opening

The Menu opens on a **tap of the M cell** of the DJ block (`eq:606-626`, `view:346-355`), and
only when **all** hold:

| gate | source |
|---|---|
| a knob is not in edit mode (`cassette_eq_is_editing()`) | `view:351` |
| the transport drawer is out (`s_buttons_want_room`) | `view:352` |
| the cassette pose has **settled** at `scale == 134` (the 260 ms shrink landed) | `view:353` |
| the Menu is not already open | `view:354` |
| the M click itself had no gesture direction and no `press_moved`, and the two-finger VU latch is clear | `eq:616-619` |

The page it opens on is the page it returns to (`cassette_menu_open(s_page_white)`,
`view:355`); white and dark are alike.

On open, **in the same frame** (`menuview:607-635`): set `open`, read the status and the
library lease, feed the model, switch the window fill, swap the drawer labels, hide the
cassette container, hide the six block cells (cancel any pending knob open), show the page and
the Pad, draw the rows (title + five rows + footer) — **no animation, no transition**.

### 6.2 Closing

| input | action |
|---|---|
| up swipe anywhere on the screen **except a press that starts on the Pad** (the Pad swallows its gestures) | `cassette_menu_close` — the only screen intent the open Menu accepts (`view:369-373`) |
| **P** (drawer, top) | close from any depth (`menuview:392-396`) |
| **`<`** (Back) at the root | `menu_back()` returns true → close (`menuview:378-384`) |
| `MENU_ACTION_CLOSE` | defined but **never returned** by the model; reserved |

On close, in one frame: hide the page and the Pad; window fill back to `#111111`; header title
cleared and hidden; footer's three lines cleared and hidden; drawer labels back to
`P`/`<`/`>`; uncover the block (its cells return according to the block's own shown state);
show the cassette container; release the library lease; zero the Pad press state. The page
and the pose are not changed (`menuview:637-659`).

### 6.3 The Pad state machine (`menuview:398-478`)

Input is sampled per pointer event; `dy` is positive **down**, `dx` positive **right**. State:
`pressed`, `axis ∈ {undecided, vertical, horizontal}`, `lastX/lastY`, `pressT/lastT`,
`dxSum`, `dySum`, `moved` (sum of `|dx| + |dy|` over samples, not net displacement).

| event | action |
|---|---|
| **press** | `pressed = true`, `axis = undecided`, `last = point`, `pressT = lastT = now`, `dx/dy/moved = 0` |
| **press with no movement** | return, nothing |
| **move** | `dx = p.x − lastX`, `dy = p.y − lastY`; if both 0 return; `dt = now − lastT`; update last/time; `dxSum += dx`, `dySum += dy`, `moved += abs(dx) + abs(dy)` |
| move, axis undecided, `moved < 12` | return |
| move, axis undecided, `moved ≥ 12` | `axis = abs(dxSum) > abs(dySum) ? horizontal : vertical` (**ties vertical**). If vertical, feed `dySum` with `dt = now − pressT` to the ratchet as **one** sample (so the undecided start is not lost); if horizontal, do nothing until release. Return. |
| move, axis horizontal | return (never ratchets) |
| move, axis vertical | feed `dy`/`dt` to the ratchet |
| **release** | if axis horizontal and `dxSum ≤ −50` → **Back**; else if `moved < 8` → **activate the band**; then reset |
| **press lost** (pointer cancel/leave) | reset, no action |

Constants: `PAD_TAP_SLOP 8`, `PAD_AXIS_SLOP 12`, `PAD_SWIPE_MIN 50`
(`menuview:86-88`). Consequences to preserve:

- A move of 8–11 px is **neither** a tap nor a decided axis: on release, nothing happens.
- The axis is decided **once** per press; a swipe can never ratchet and a drag can never go
  Back.
- `moved` accumulates absolute per-sample deltas, so jitter counts (a wiggle is not a tap).
- The left swipe counts only on **release**, at 50 px of accumulated `dxSum`; rightward
  horizontal swipes do nothing.
- The Pad does not bubble gestures, so none of this reaches the screen's up-swipe handler.

### 6.4 The ratchet (`menu.c:27-30, 780-821`)

| constant | value |
|---|---|
| `MENU_RATCHET_PX` | **54 px per row at 1×** |
| `MENU_SPEED_STAGE2` | **1200 px/s** → 2× (27 px/row) |
| `MENU_SPEED_STAGE4` | **2400 px/s** → 4× (13 px/row, `54/4` integer) |
| `MENU_STAGE_HOLD_MS` | **150 ms** before the stage drops |

```
stageFor(dy, dt):  speed = dt ? floor(|dy| * 1000 / dt) : 0
                   speed >= 2400 ? 4 : speed >= 1200 ? 2 : 1
menuDrag(dy, dt):
  target = stageFor(dy, dt)
  if target >= stage: stage = target; slowMs = 0
  else:               slowMs += dt; if slowMs >= 150: stage = target; slowMs = 0
  if dy == 0: return
  if acc != 0 && sign(dy) != sign(acc): acc = 0        // a direction change drops the partial row
  acc += dy
  perRow = floor(54 / stage)                            // 54, 27, 13
  while acc >=  perRow: step(+1); acc -= perRow
  while acc <= -perRow: step(-1); acc += perRow
```

- Stages are evaluated **per sample** and the stage is updated *before* the step, so a fast
  sample can jump straight to 4×.
- `dt = 0` never divides by zero (speed 0 → *target* 1), but the stage only drops after 150 ms of slow samples: at a held 4× a 54 px sample still advances four rows.
- The stage is held for 150 ms of accumulated slow time after the speed drops; a stutter does
  not drop the multiplier.
- On open and on close, `stage = 1`, `acc = 0`, `slowMs = 0`; the band/window survive.
- Steps are **instant**: no easing, no bounce, no partial row. The list rests only on row
  boundaries.

### 6.5 Stepping, window, clamping, virtualisation (`menu.c:277-298, 755-778`)

`step_once(dir)` (count = rows in the current list):

```
down: if band >= count-1 return
      if band == window+4: window++
      band++
up:   if band <= 0 return
      if band == window: window--
      band--
```

So inside the page the band moves; at the last visible slot the **window shifts instead** and
the band stays on that edge row; symmetric at the top. The ends **clamp**: a step past the
first/last row does nothing; the window never leaves `0 .. max(0, count-5)`.

`clamp_view()` (run after every structural change, resize of the count, Back, open):

```
if count <= 0: band = window = 0; return
band = clamp(band, 0, count-1)
if count <= 5: window = 0; return
maxW = count - 5
window = clamp(window, 0, maxW)
if band < window:        window = band
elif band > window + 4:  window = band - 4
window = clamp(window, 0, maxW)
```

The list is **virtual**: exactly five row objects exist, rebound to `window .. window+4` on
each version bump; no object per Track, no scroll container, no allocation per step. Slots
with `index >= count` are blank (short list: blank space below the last row, band cannot leave
the list, window never shifts).

The band's screen slot is `band − window` (`menu.c:697`); the view draws the band there.

### 6.6 Activating the band (`menu.c:823-872`, `menuview:346-376`)

Activation first refreshes the model (facts may have moved), then:

| list | band row | result |
|---|---|---|
| Root | 0 `Tracks` | push Tracks (band 0, window 0) |
| | 1 `Browse` | push Browse at **its root** (the path is cleared: `enter_browse_root()`) |
| | 2 `Shuffle` | `MENU_ACTION_SHUFFLE` → toggle the broadcast's mode |
| | 3 `Settings` | push Settings |
| Settings | 0 `Backlight` | `MENU_ACTION_BACKLIGHT` with the next percent; view calls `backlight_set_hold()` |
| | 1/2/4 | push Station / Library / About |
| | 3 `Rescan` | `MENU_ACTION_RESCAN` → `app_services_rescan()`; refused while already running |
| Tracks | any | `MENU_ACTION_PLAY` with the Track's **relative path** → `mini_icecast_play(path)`; activating the playing Track restarts it |
| Browse | directory | push that directory (append its raw name to the path) |
| | Track | play, exactly as above |
| Station / Library / About | any | nothing |
| any | note row / disabled row | nothing |

A push always sets `band = 0`, `window = 0`; the title follows. Nothing animates.

### 6.7 Back (`menu.c:877-943`)

| from | to | band on return |
|---|---|---|
| Root | **closes the Menu** | — |
| Settings | Root | **3** (the Settings row) |
| Tracks | Root | **0** (the Tracks row) |
| Station | Settings | **1** |
| Library | Settings | **2** |
| About | Settings | **4** |
| Browse, path empty | Root | **1** (the Browse row) |
| Browse, path non-empty | the parent directory's Browse | **the index of the row that opened it** (the directory's own index in its parent) |

Back sets the band explicitly (the one place the band is *set* rather than remembered), then
`clamp_view()`. A vanished parent pops to Browse's root. Back at the root closes.

### 6.8 Remembering (`menu.c:601-638`)

The model survives closing. `list`, `band`, `window` and the Browse `path` are remembered until
the app restarts (no NVS); the library lease is **not** — it is acquired on open and released
on close. `menu_open()` re-resolves the Browse path against the current tree and clamps the
band/window; the first open of the process is Root, band 0.

### 6.9 Data, generation and side effects

- The view holds a **library lease** while open; on a generation change it takes a new lease,
  then releases the old one, then the model re-resolves the path (`menuview:325-342`).
- A generation change while open rebuilds Tracks and the read-only lists, re-resolves Browse
  (`library_find`), pops a vanished path to Browse's root, and clamps the band
  (`menu.c:561-597, 671-685`).
- **Rescan**: the tap starts it; the row immediately reads `Rescanning...`, is dimmed and
  ignores taps. The Menu keeps its list; when the new generation lands it re-resolves and
  clamps as above (`menu.md:112-115`, `menu.c:397-412`).
- **Backlight**: cycles `Auto → 100 → 60 → 30 → Auto` in place; a non-Auto value is a hold
  (no NVS, lives until reboot). No animation on the Menu side (`menuview:363-365`).
- **Shuffle**: the value reads `mini_icecast_shuffle_enabled()` and toggles the broadcast; the
  order is the broadcast's, the Playlist order does not change (`menu.md:121-122`).
- **Tags**: the footer follows the current Track's tags; the view is given
  `(group, song)` from the tag loader and clears them on a new Track.
- The view redraws only when `menu_version` changes (title, rows, count, band slot); the
  25 Hz status poll does not repaint otherwise (`menu.c:532-557`, `menuview:288, 340`).
- Row text lives in five 96-byte buffers / 32-byte value buffers (`menu.h:37-38`); the view
  copies each into LVGL per redraw because `LONG_DOT` writes dots into the label's own copy
  (`menuview:260-263`).

### 6.10 Modal rules (screen level)

While open, the only **widgets** that take a touch are the Pad and the drawer's three buttons.
The rows, page, window and bands are non-clickable, so a touch on them falls through to the
screen, where the **only** accepted intent is UP (which closes); a down swipe and both
horizontal swipes do nothing, and the two-finger VU gesture is ignored. A leftward swipe that
starts **on the Pad** is Back. The block's cells are hidden and take nothing. Drawer-button
clicks are redirected as in §2.5. See `04-peek-and-gestures.md` §4.2 `S5`.

---

## 7. Ink

Palette: `k_col_bg #111111`, `k_col_white #909090`, `k_col_primary #F15A22`,
`k_col_button_white #EEEEEE`, `k_col_button_rect #60240D` (darken primary 40 %),
`k_col_dome #929292`, `k_col_logo_back #CC4C1C` (darken primary 85 %)
(`style.h:21-28`, `style.c:6-39`). The page's "white" is **`#909090`**, never `#FFFFFF`.

| element | white page | dark page |
|---|---|---|
| screen backdrop | `#909090` | `#111111` |
| back cover `(16,16,768,448)` | `#111111` | `#111111` |
| window fill (Menu up) | **`#909090`** | `#111111` |
| window border 2 px | `#111111` | `#111111` |
| row label / icon / right column (unselected) | `#111111` | `#F15A22` |
| **band fill** | `#111111` | `#F15A22` |
| **band label / icon / chevron / value** | `#EEEEEE` | `#111111` |
| playing dot | the row's own ink (page ink, or band ink on the band) | same |
| dimmed row's icon/label/right | row ink at **40 %** (`102/255`) | same |
| Pad plate | `#909090` | `#909090` |
| Pad inner | `#111111` | `#111111` |
| Pad guide (2 px, 2/4 dash, round) | `#909090` | `#909090` |
| header band fill / border | `#909090` / `#60240D` | `#111111` / `#60240D` |
| header title | `#111111` | `#F15A22` |
| K7 badge fill / border / mark | `#CC4C1C` / `#111111` / `#111111` | `#111111` / `#F15A22` / `#F15A22` |
| footer band fill / border | `#909090` / `#60240D` | `#111111` / `#60240D` |
| footer song / group / position | `#111111` | `#F15A22` |
| drawer button fill / border | `#909090` / `#60240D` | `#111111` / `#60240D` |
| drawer circle | face `#929292`, 4 px rim `#929292` | 2 px ring `#F15A22`, transparent face |
| drawer icon | `#111111` | `#F15A22` |

The band is the page's opposite — the same idiom as the M and R caps (`menu.md:182-186`). A
disabled (dimmed) row **on** the band is 40 % band ink over the opaque band fill. The Pad is
constant across pages: it is a white plate with a dark inner on both.

---

## 8. Motion and timing

| event | timing |
|---|---|
| Menu open | **one frame**, no animation: page + Pad unhidden, rows drawn, window recoloured, cassette + block hidden, drawer relabelled |
| Menu close | **one frame**, no animation: the inverse; the cassette returns exactly as it was |
| band move / window shift | **instant**; no scroll, no easing, no inertia |
| row rebinding | instant; five static objects |
| drawer labels | instant swap (both pressed and unpressed states) |
| header title / footer texts | instant text set |
| M tap that opens the Menu | the M cell's own 160 ms linear scale pulse, 256 → 232 → 256 (`buttons:64-68, 142-164`; `eq:578-592`). It is started **before** the Menu's open callback in the same click, then the cell is hidden with the block, so the pulse is not seen; it does not delay the frame |
| drawer slide / header+footer slide / window fade / pose (controls chrome) | 260 ms ease-out (`buttons:294-328`, `header:97-117`, `footer:90-107`, `window:46-70`, `view:295-318`). These are **already finished** when the Menu can open, and are not retriggered |
| Rescan / Backlight / Shuffle | no Menu animation; values change on the next model refresh |

There is no fade, no slide, no cross-fade, no busy indicator anywhere in the mode.

---

## 9. Demo fixture (JS)

One coherent live state for the simulator. It matches the playing Track used by
`04-peek-and-gestures.md` (M83, `04 midnight_city.mp3`), the docs' pinned library figures
(`t18:176-198` — 1,204 tracks, 38 dirs, 12.4 GB), the real board/app facts
(`board_esp32.c:120-123`, `build_sunton/project_description.json:4`, `partitions.csv`,
`appliance.c:144-145` for 44.1 kHz / 256 kbps), and the docs' footer example `3 / 1204`.

```js
// ---- board / app facts (menu_facts_t, menu.h:90-97) -------------------------
export const DEMO_FACTS = {
  shuffleOn: false,
  rescanning: false,
  backlight: 0,                 // 0 = Auto, else 30 / 60 / 100
  version: '397fc27-dirty',     // esp_app_get_description()->version of build_sunton
  board: '8048S050C',           // app_board_name()
  partition: '4 MB',            // factory partition / 1 MiB, integer
};

// ---- the broadcast (mini_icecast_status_t) ---------------------------------
export const DEMO_STATUS = {
  path: "M83/Hurry Up, We're Dreaming/04 midnight_city.mp3",
  file: '04 midnight_city.mp3',
  title: '04 midnight_city',    // set_title(file, stem): apostrophes only; unused while the tag exists
  format: 'MP3',                // MI_FMT_MP3
  sourceHz: 44100,
  outHz: 44100,                 // station rate
  bitrate: 256,                 // station bitrate (kbps)
  elapsedMs: 107000,            // shared with 04: 1:47 of 4:03
  durationMs: 243000,
  trackIndex: 3,                // 0-based Playlist position
  libraryCount: 1204,
  tags: { group: 'M83', song: 'Midnight City' },   // the footer's two lines
};

// ---- the library ------------------------------------------------------------
// Counts are the docs' fixture: 1,204 tracks / 38 dirs / 12.4 GB. Only the M83
// album's ten names are hand-written; the rest are generated to hit the counts.
const TITLES = ['slow light', 'paper moon', 'harbour', 'night bus', 'static bloom',
                'after the rain', 'low tide', 'signal', 'drift', 'the long way',
                'half asleep', 'new year'];

const ARTISTS = [
  { name: 'M83', albums: [{ name: "Hurry Up, We're Dreaming", tracks: [
      '01 intro', '02 this bright flash', '03 wait', '04 midnight_city', '05 reunion',
      '06 where the boogie started', '07 new map', '08 ok pal',
      '09 another wave from you', '10 outro',
  ]}]},
  { name: 'Massive Attack', albums: ['Blue Lines', 'Mezzanine', 'Protection'] },
  { name: 'Nils Frahm', albums: ['All Melody', 'Felt', 'Screws', 'Spaces', 'Wintermusik'] },
  { name: 'Radiohead', albums: ['A Moon Shaped Pool', 'In Rainbows', 'Kid A', 'OK Computer', 'The Bends'] },
  { name: 'Sigur Rós', albums: ['Kveikur', 'Takk...', 'Valtari', 'Ágætis byrjun'] },
  { name: 'Tame Impala', albums: ['Currents', 'Innerspeaker', 'Lonerism', 'The Slow Rush'] },
  { name: 'The xx', albums: ['Coexist', 'I See You', 'xx'] },
  { name: 'Thom Yorke', albums: ['Anima', 'Suspiria', 'The Eraser', "Tomorrow's Modern Boxes", 'Ultraviolet'] },
];

// 30 albums + 8 artist dirs = 38 dirs. M83 = 10 tracks; the other 29 albums get
// 42 or 41, summing to 1,194, so the Playlist is exactly 1,204.
const ALBUMS = ARTISTS.flatMap(a => a.albums.map(al => (typeof al === 'string'
  ? { artist: a.name, name: al }                       // generated album
  : { artist: a.name, name: al.name, tracks: al.tracks })));  // M83, hand-written
const REST = ALBUMS.slice(1);                          // 29 albums
const REMAINING = 1204 - ALBUMS[0].tracks.length;      // 1194
const BASE = Math.floor(REMAINING / REST.length);      // 41
const REM = REMAINING % REST.length;                   // 5
REST.forEach((al, i) => { al.trackCount = BASE + (i < REM ? 1 : 0); });

const SUFFIX = ['', ' II', ' III', ' IV'];          // keeps the 42 generated names unique
const albumTracks = (al) => al.tracks ?? Array.from({ length: al.trackCount }, (_, k) =>
  `${String(k + 1).padStart(2, '0')} - ${TITLES[k % TITLES.length]}${SUFFIX[Math.floor(k / TITLES.length)]}`);

export const DEMO_LIBRARY = {
  state: 'ok',                 // lib_state_t: ok / no_card / no_dir / empty / error
  generation: 7,
  truncated: false,
  tracks: 1204,
  dirs: 38,
  bytes: 13314398618,          // 12.4 GiB -> "12.4 GB"
  root: ARTISTS.map(a => ({
    name: a.name, kind: 'dir',
    children: ALBUMS.filter(al => al.artist === a.name).map(al => ({
      name: al.name, kind: 'dir',
      children: albumTracks(al).map(t => ({ name: `${t}.mp3`, kind: 'track' })),
    })),
  })),
};

// ---- initial Menu state -----------------------------------------------------
// The device boots on Root / band 0. Seed this state to reproduce the docs' screenshot:
// the band on "03 wait" (position 3 -> footer "3 / 1204") and the playing dot on
// "04 midnight_city" one row below.
export const DEMO_MENU = { list: 'tracks', band: 2, window: 0, browsePath: '' };
// Clean boot:             { list: 'root',   band: 0, window: 0, browsePath: '' }
```

Derived visible state for the demo (window 0 unless stated):

| list | title | rows (slot 0 → 4) | footer position |
|---|---|---|---|
| **Tracks** (seeded) | `Tracks` | `01 intro`, `02 this bright flash`, **`03 wait` (band)**, `04 midnight_city` (dot), `05 reunion` | `3 / 1204` |
| Root | `Menu` | `Tracks` ▸, `Browse` ▸, `Shuffle` `OFF`, `Settings` ▸, blank | `1 / 4` |
| Browse | `Browse` | `M83` ▸, `Massive Attack` ▸, `Nils Frahm` ▸, `Radiohead` ▸, `Sigur Rós` ▸ (window scrolls for `Tame Impala`, `The xx`, `Thom Yorke`) | `1 / 8` |
| Browse /M83 | `Browse /M83` | `Hurry Up, We're Dreaming` ▸, 4 blanks (short list) | `1 / 1` |
| Browse /M83 /… | `Browse /M83 /Hurry Up, We're Dreaming` | the ten M83 tracks, dot on `04 midnight_city`; the title measures **≈646 px** against the 648 px box — it just fits, 2 px to spare | `1 / 10` |
| Settings | `Settings` | `Backlight` `AUTO`, `Station` ▸, `Library` ▸, `Rescan`, `About` ▸ | `1 / 5` |
| Station | `Station` | `Format` `MP3`, `Rate` `44.1 KHZ`, `Bitrate` `256 KBPS`, 2 blanks | `1 / 3` |
| Library | `Library` | `State` `OK`, `Tracks` `1,204 TRACKS`, `Directories` `38 DIRS`, `Size` `12.4 GB`, `Truncated` `NO` | `1 / 5` |
| About | `About` | `Firmware` `397FC27-DIRTY` (chip), `Board` `8048S050C` (tablet), `Partition` `4 MB` (disc), 2 blanks | `1 / 3` |

Footer while open (Tracks seeded): song `Midnight City`, group `M83`, position `3 / 1204`.
Song = tag `song` else the cleaned `status.title`; group = tag `group` else the path's first
component; both empty with nothing playing, while the position remains
(`menuview:291-320`; `cassette.c:199-211`).

An optional **no-card** fixture for the disabled/note states:
`{ state:'no_card', generation:0, tracks:0, dirs:0, bytes:0 }` and `tags: {}`, `path: ''`,
`format: 'NONE'`; Root then dims `Tracks` and `Browse` to 40 %, and a remembered Tracks or
Browse list shows one dimmed `No card` note inside the band.

---

## 10. SolidJS / CSS notes

State shape (mirror `menu.c:66-103`):

```ts
type MenuList = 'root'|'settings'|'tracks'|'browse'|'station'|'library'|'about';
type MenuState = {
  open: boolean; white: boolean;
  list: MenuList; band: number; window: number; browsePath: string;
  stage: 1|2|4; accPx: number; slowMs: number;      // ratchet, reset on open/close
  pressed: boolean; axis: '?'|'v'|'h';               // pad gesture
};
```

Derived (never stored): `count`, the five `rows`, `title`, `position`, `canBack`. Recompute
`rows` only when `list / band / window / path / facts / status / generation` change — the
original bumps a version only when the five row models, the title, the count or the band slot
actually differ (`menu.c:532-557`).

```css
.stage { position:relative; width:800px; height:480px; overflow:hidden;
         background:var(--page); letter-spacing:0; }
.menu-page { position:absolute; left:112px; top:78px; width:482px; height:280px; }
.menu-row  { position:absolute; left:6px; width:470px; }
.menu-row:nth-child(1){ top:0 }      /* h 54 */
.menu-row:nth-child(2){ top:54px }   /* h 53 */
.menu-row:nth-child(3){ top:107px }  /* h 54 */
.menu-row:nth-child(4){ top:161px }  /* h 53 */
.menu-row:nth-child(5){ top:214px }  /* h 54 */
.row--54 { height:54px }  .row--53 { height:53px }
.band  { position:absolute; left:2px; top:2px; width:466px; height:calc(100% - 4px);
         border-radius:8px; }
.icon  { position:absolute; left:16px; width:24px; font:24px/25px xtape; }
.row--54 .icon { top:15px }  .row--53 .icon { top:14px }
.label { position:absolute; left:56px; width:400px; font:400 28px/30px Jura;
         white-space:nowrap; overflow:hidden; }
.row--54 .label { top:12px }  .row--53 .label { top:11px }
.label--value { width:340px }
.row--54 .dot { top:24px }  .row--53 .dot { top:23px }
.dot   { position:absolute; left:364px; width:6px; height:6px; border-radius:3px; }
.right { position:absolute; right:16px; width:150px; text-align:right;
         white-space:nowrap; font:400 20px/21px Jura; }
.row--54 .right { top:17px }  .row--53 .right { top:16px }
.row--54 .right--chevron { top:15px }  .row--53 .right--chevron { top:14px }
.right--chevron { font:24px/25px xtape; }
.pad   { position:absolute; left:596px; top:76px; width:186px; height:284px; border-radius:8px;
         background:#909090; touch-action:none; user-select:none; }
.pad-inner { position:absolute; left:8px; top:8px; width:170px; height:268px; border-radius:8px;
             background:#111111; pointer-events:none; }
.pad-guide { position:absolute; left:18px; top:66px; width:2px; height:136px;
             background:repeating-linear-gradient(#909090 0 2px, transparent 2px 6px);
             border-radius:1px; pointer-events:none; }   /* x=19 is the centreline; the gradient's
                                                             dashes are square, so use SVG
                                                             stroke-linecap:round for the round caps */
```

Per-row integers that CSS would otherwise round differently (use a class per height, not
`50%`, and never `(H−h)/2`): label top **12** (54 px row) / **11** (53); icon top **15** / **14**;
right value top **17** / **16**; chevron top **15** / **14**; dot top **24** / **23**; band height
**50** / **49**. Baselines are §2.6.

Three-dot truncation (same algorithm as `04` §1.4):

```ts
const fitDots = (s: string, maxW: number, measure: (t: string) => number) => {
  for (let n = s.length; n > 3; n--) {
    const t = s.slice(0, n) + '...';
    if (measure(t) <= maxW) return t;
  }
  return s.length > 3 ? '...' : s;
};
```

Measure with a canvas 2D context using the exact face, size, weight 400, `font-kerning:'normal'`
and `letter-spacing:0`; run after `document.fonts.ready`. The Menu's `maxW` values: 400 (label),
340 (value-row label), 150 (right), 648 (title), 512 (footer song/group), 200 (position).

Pad handlers (pointer events; `pointercancel`/`pointerleave` → press-lost):

```ts
onPointerDown(e) { /* §6.3 press */ }
onPointerMove(e) { /* §6.3 move; call menuDrag(dySum, t - pressT) on the deciding sample */ }
onPointerUp()    { if (axis==='h' && dxSum <= -50) back(); else if (moved < 8) activate(); reset(); }
```

Suppress the synthetic `click`, set `touch-action:none`, `user-select:none`, `draggable=false`.
Because the Pad's press is grabbed on `pointerdown` (not the screen), the up-swipe handler never
sees it.

Draw order inside `.stage` (bottom → top): backdrop, back cover, window, [cassette hidden],
[peek hidden], drawer, [block hidden], `.menu-page`, `.pad`, header band, badge, footer band.
Within a row: band, icon, label, right, dot.

---

## 11. Acceptance checklist

1. Window `(110,76) 486×284`, r8, 2 px `#111111` border; page object `(112,78) 482×280`; rows
   `(118, 84/138/191/245/298)` × `470` × `(54/53/54/53/54)`; the last row ends at 352, the page
   content at 352 and the window's inner edge at 358.
2. Band on `(row+2)` height `(h−4)` (50/49), r8, page-opposite fill.
3. Icon box left 134 (24 px advance), label left 174 (w 400 or 340), dot left 482 (6×6, top
   24/23), right column right edge 572 (box 422..572, w 150, right-aligned).
4. Header band `(110,6) 674×71`, badge `(16,6) 96×71`, title left 126 / w 648 / Jura 36 /
   baseline 53; footer `(16,359) 768×115`, song `(40,379)` / group `(40,413)` / position right
   edge 760, Jura 28, baselines 403 / 437 / 423.
5. Pad `(596,76) 186×284` r8 `#909090`; inner `(604,84) 170×268` r8 `#111111`; guide centre
   x 623, y 150..286, 2 px, dash 2/4, rounded, `#909090`; only the plate is clickable.
6. Drawer: 96×96 at `(16,76/170/264)`, 64 px circle inset 16, P/`<`/`>` become
   `F000`/`F00E`/`F00D` at 36 px, click = Close / Back / Enter; instant swap.
7. Values are ASCII-uppercased and right-aligned; labels are not uppercased; ellipsis is three
   ASCII dots in every fixed-width run.
8. Titles: `Menu`, `Tracks`, `Settings`, `Station`, `Library`, `About`,
   `Browse` + `" /" + component` per path segment.
9. Note rows and dimming: root Tracks/Browse at 40 % when the library is unusable; Rescan
   `Rescanning...` at 40 % while running; notes `No card` / `Library empty` / `Library error`;
   every other row enabled.
10. Band/window: `step_once` as §6.5, `clamp_view` as §6.5, short list leaves blanks, no
    bounce, no partial row, no scroll container.
11. Ratchet: 54 px/row at rest; ≥1200 px/s → 27 px; ≥2400 px/s → 13 px; a stage held for 150 ms
    before dropping; `dt=0` legal; direction change drops the partial row.
12. Pad: axis decided at 12 px (`|dx| > |dy|` else vertical), tap if `moved < 8`, Back if
    horizontal and `dxSum ≤ −50` on release, 8–11 px dead zone, no gesture bubbling.
13. Open only from the settled 134/256 controls with no knob edit; close by up swipe, P, or
    Back at the root; one frame in and out; cassette hidden while open and restored exactly.
14. Remembered list/band/path across close; band set on Back as §6.7; lease taken/released.
15. Ink exactly §7 on both pages; the white page is `#909090`; the Pad is constant.
16. No z-order mistakes: header/footer over the page's 1 px top/bottom; badge over the band's
    left border; dot over label/right; drawer under the page.

---

## 12. Known discrepancies and open items

Code is authoritative; these are the places where `menu.md` (or a comment) no longer matches
it, all of which must be implemented as the **code** says:

| # | doc/comment says | code does | where |
|---|---|---|---|
| 1 | header title at band `x=104` | band-local **16** → screen **126** (`TITLE_X 16`, border-adjusted 14) | `menu.md:61` vs `header:55-61, 163-169` |
| 2 | "three dotted guides" | **one** guide (the loop runs once) | `menuview:68-80, 571-583` |
| 3 | empty or OK-with-zero tracks → `Library empty` | `OK` + 0 tracks falls through to `Library error`; only `LIB_STATE_EMPTY` says `Library empty` | `menu.md:107-111` vs `menu.c:235-243` |
| 4 | the dot is "after its label" | fixed at x 364 row-local (482 screen), independent of label length | `menu.md:57` vs `menuview:56, 534-535` |
| 5 | footer position "at x=744" | 744 is **band-local** → screen **760** (content box +2, inset 22) | `menu.md:63` vs `footer:54-59` |
| 6 | `MENU_ACTION_CLOSE` closes | nothing returns it; closing comes from `menu_back()` returning true | `menu.h:85` vs `menu.c:877-943`, `menuview:378-384` |
| 7 | page fill "`k_col_bg` otherwise" | only the *window's* fill is written; on the dark page both are `#111111` anyway | `menu.md:45` vs `window:84-94` |
| 8 | Right edge "at 460" | 460 is **page-local** (page x 112) → screen **572** | `menu.md:59` vs `menuview:528-532` |
| 9 | tap/drag described only by the touch `drag limit` | tap uses **8 px** (`PAD_TAP_SLOP`), axis uses **12 px** (`PAD_AXIS_SLOP`), so 8–11 px is a dead zone | `menu.md:139-144` vs `menuview:86-88, 442-467` |
| 10 | a blank/empty page is just blank | with `count == 0`, `menu_band()` is 0 and the band **is drawn on empty slot 0** | `menu.c:277-284, 697`; `menuview:240-246` |
| 11 | About's Partition could read "1.70 MB of 4 MB" | it reads the partition **size only**, `"4 MB"` (integer MiB) | `menu.md:266-268` vs `menuview:596-602` |
| 12 | "Every right column is drawn in capitals and in the smaller Jura 20" | a **chevron** is the icon font at **24 px**, not Jura 20; only value cells are Jura 20 | `menu.md:104-106` vs `menuview:272-279` |

Also note: the Menu's own frame is not animated while the controls chrome is (260 ms), and the
Menu cannot be opened mid-slide (`view:353`); a simulator that allows it would show a state the
device cannot produce.

Open items in the firmware that the sim can ignore: no NVS for backlight hold or shuffle; no
running-image size for About's partition; acceleration thresholds are board-tuned constants.

---

## 13. Source index

| topic | lines |
|---|---|
| Menu purpose, geometry, tree, lists, Pad, drawer, ink, lifecycle | `menu.md:3-227` |
| icon table and font generation | `menu.md:229-260`; `icons.h:24-59`; `package.json:14-17` |
| model struct, lists, clamp, title | `menu.c:66-103, 259-337` |
| row builders, note, values | `menu.c:155-255, 341-529` |
| refresh / version | `menu.c:532-557` |
| browse resolve/append/pop | `menu.c:561-597, 728-742` |
| lifecycle, update, getters | `menu.c:601-709` |
| navigation, step, drag, activate, back | `menu.c:713-943` |
| name cleaning | `menu.c:950-953`; `cassette.c:134-211` |
| view constants, slot math, draw, footer | `menuview:47-162, 232-342` |
| Pad handler and lifecycle | `menuview:398-478, 482-678` |
| drawer geometry, labels, click | `buttons:43-75, 119-212, 414-424` |
| header geometry/title | `header:34-69, 163-197` |
| footer geometry/texts | `footer:29-66, 128-187` |
| block, M hook, covered | `eq:48-74, 606-626, 838-859` |
| creation order / page / open hook | `view:208-242, 344-383, 435-532` |
| window rect + menu fill | `window:18-24, 84-107` |
| palette | `style.h:21-39`; `style.c:6-39` |
| fonts / metrics | `font_jura_20.c:2425-2426`; `font_jura_28.c:3640-3641`; `font_jura_36.c:5241-5242`; `font_xtape_24.c:572-591, 653-654`; `font_xtape_36.c:1251-1252` |
| pinned behaviour | `t18:176-198, 293-340, 675-806, 882-958, 1029-1160` |

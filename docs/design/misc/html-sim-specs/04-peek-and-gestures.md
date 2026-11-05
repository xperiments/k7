# 04 — The peek overlay and the mouse-gesture state machine

Implementation spec for a pixel-for-pixel HTML/SolidJS port of the ESP32 screen's peek
overlay and of the whole touch policy, expressed as mouse rules.

Everything below is measured in the panel's design space: **800 x 480 px, origin top-left**.
All values are screen coordinates unless a row says `bar-local` / `tag-local`; those are
relative to that object's **outer** top-left corner.

## Sources

| File | Lines | What it fixes |
|---|---|---|
| `components/app/screen_display/cassette_peek.c` | 45-98 | every peek constant |
| | 156-257 | time/format, fill, idle, tag text, tether geometry |
| | 259-337 | bar slide, bob |
| | 341-456 | apply / show / hide / hide-now |
| | 460-535 | creation order, alignments, fonts, long modes |
| `components/app/screen_display/cassette_peek.h` | 1-32 | intent of the overlay, tether contract |
| `components/app/screen_display/cassette_levels.c` | 65-127 | peek toggle + frame/tag plumbing (no geometry) |
| `components/app/screen_display/screen_input.c` | 49-150 | the whole touch policy: latch, intents |
| `components/app/screen_display/screen_input.h` | 1-72 | intent set and "one touch, one intent" |
| `components/app/screen_display/touch_gt911.c` | 51-56, 80-108 | two-finger midpoint detector (30 px) |
| | 282-298 | indev overrides: min velocity 0, scroll limit 20, double-click 500 |
| `components/app/screen_display/cassette_view.c` | 66-150 | scales, room arbitration, ease |
| | 152-252 | page flip, controls set, drawer room |
| | 258-318 | hub anchors, pose animation |
| | 344-431 | the intent state machine (authoritative) |
| | 435-551 | creation order = z-order |
| `components/app/screen_display/cassette_meter.c` | 132-137 | VU starts hidden |
| `components/app/screen_display/cassette_cover.c` | 143-190, 188-304 | cover geometry, 1 s fade |
| `components/portable/cassette/cassette.c` | 261-433 | frame: time, permille, packs, hubs |
| `components/portable/cassette/include/cassette.h` | 30-93 | frame fields, rewind constants |
| `components/portable/cassette/cassette_layout.c` | 20-75 | pose arithmetic (`SC`, `SCX`, `SCY`) |
| `managed_components/lvgl__lvgl/src/indev/lv_indev.c` | 1832-1905 | gesture accumulation, axis decision |
| | 1491-1580 | release: CLICKED is still sent after a gesture |
| `managed_components/lvgl__lvgl/src/core/lv_obj_pos.c` | 780-905 | child position is relative to the parent's **content** box |
| `managed_components/lvgl__lvgl/src/misc/lv_anim.c` | 310-315, 798-810 | ease-out curve, value interpolation |
| `components/app/screen_display/README.md` | 206-277, 357-426, 513-523 | prose for the peek, the page, the reel change |

Palette used here (`components/app/screen_display/cassette_style.[ch]:34-40`,
`cassette_style.c:1-14`):

| token | hex | where |
|---|---|---|
| `k_col_bg` = `k_col_paper` | `#111111` | bar fill, tag fill, dark page |
| `k_col_primary` | `#F15A22` | bar/tag border, all peek ink, track fill |
| `k_col_meter_dim` | `#7F2C18` | the peek track's unfilled groove |
| `k_col_white` | `#909090` | the white page backdrop |
| `k_col_glass` | `#8A8A8A` | white page edge mark (`COL_DARKEN_HEX(0x909090, 96)`) |

## 0. Toolkit conventions you must reproduce

These are the three LVGL behaviours that decide pixels; ignoring any of them shifts the peek
by 2-4 px.

1. **Children are positioned from the parent's content box**, i.e. inset by the parent's
   border width plus padding (`lv_obj_pos.c:890-905`). The bar and the tags are
   `cassette_border_box(..., 2)` with `lv_obj_remove_style_all` first, so padding = 0 and
   border = 2: every child coordinate is **+2 px** from the pill's outer edge
   (`cassette_widgets.c:38-54`).
2. **A label's default size is one line box: 30 px tall** for `jura_28`
   (`font_jura_28.c:3640-3641`: `line_height = 30`, `base_line = 6`). Baseline sits
   **24 px below the label's top**. A label aligned `LEFT_MID` / `RIGHT_MID` / `CENTER`
   inside a box of content height `H` therefore has `top = border + (H - 30) / 2`
   (integer division).
3. **Animation values are integers.** `value = start + trunc((end - start) * step / 1024)`
   where `step = lv_cubic_bezier(...)` in `0..1024` (`lv_anim.c:798-810`,
   `LV_BEZIER_VAL_MAX = 1024`). Use `Math.trunc`, not `Math.round`, for the interpolant;
   the bezier itself is evaluated on an integer `t` (`lv_map(act_time, 0, duration, 0, 1024)`,
   truncating).

Fonts: **Jura, 28 px, 4 bpp, no subpixel, no letter spacing** — generated from
`assets/fonts/Jura-VariableFont_wght.ttf` at the variable font's default instance with
`lv_font_conv --size 28 --bpp 4` (see the header comment of `font_jura_28.c:1-6`). Calibrate
the CSS face by measuring a string against `font_jura_28.c`'s advances before you trust the
ellipsis cut points; the weight must be the default instance (no `--weight` was passed).

Text ink is always `#F15A22`. There is no shadow, no gradient and no opacity anywhere in the
peek.

## 1. Peek geometry

### 1.1 The bar

`s_bar = cassette_border_box(screen, 116, 424, 568, 52, 12, #111111, COVER, #F15A22, 2)`
(`cassette_peek.c:473-476`, constants `:48-53`). It is a **screen child**, so it does not
scale with the cassette pose.

| property | value |
|---|---|
| shown position | `(116, 424)` |
| hidden position | `(116, 488)` — 8 px past the 480 px panel bottom (`PEEK_BAR_HIDDEN_Y`) |
| size | `568 x 52` |
| radius | `12` |
| fill | `#111111`, opaque |
| border | `2 px` `#F15A22`, drawn **inside** (LVGL border insets) |
| padding | 0 |
| content box | `(118, 426)` … `(682, 458)` = **564 x 48** |
| hidden at build | yes (`lv_obj_set_hidden`) |

Children, `bar-local` from the bar's **outer** corner:

| part | x | y | w | h | radius | paint | notes |
|---|---|---|---|---|---|---|---|
| elapsed time | 22 | 11 | 110 | 30 | — | Jura 28, `#F15A22`, left, `CLIP` | `ALIGN_LEFT_MID, +20` (`:478-482`) |
| total time | 436 | 11 | 110 | 30 | — | Jura 28, `#F15A22`, right, `CLIP` | `ALIGN_RIGHT_MID, -20`, so its right edge is 22 px from the bar's right edge (`:484-491`) |
| track | 142 | 25 | 288 | 6 | 3 | `#7F2C18` | `cassette_box`, `:493-495` |
| fill | 142 | 25 | `w` | 6 | 3 | `#F15A22` | child of the track at `(0,0)`; **hidden when `w == 0`** (`:496-498`) |
| idle label | centred on `(284, 26)` | | auto | 30 | — | Jura 28, `#F15A22`, centred | `ALIGN_CENTER` (`:500-501`) |

Derived screen coordinates (add `(116, 424)`):

- elapsed box `138,435` → `248,465`, first glyph at **x = 138**, baseline **y = 459**
- total box `552,435` → `662,465`, last glyph's right edge at **x = 662**
- track `258,449` → `546,455`; fill `258,449` → `258+w,455`
- idle box centred at `(400, 450)`, so `y = 435..465`

Note the asymmetry the code actually produces: the 20 px insets hold on both ends
(`138-116 = 22`, `684-662 = 22`), the left gap between the elapsed box and the track is
10 px (`248 → 258`) and the right gap is 6 px (`546 → 552`), because `PEEK_TRACK_W` is
computed from the **outer** width (`:61`). Do not "fix" it.

```
# bar (bar-local, outer)
+--------------------------------------------------------------+ 52
| 2px #F15A22 border                                           |
|   [elapsed 22..132]  [track 142..430]  [total 436..546]       |  content 2..50 (48)
|      Jura28 left       6px groove        Jura28 right          |
+--------------------------------------------------------------+  r=12
```

### 1.2 Times and the idle sentence

`format_time()` (`cassette_peek.c:156-164`): `--:--` when unknown, otherwise
`"%u:%02u"` from `ms/1000` — minutes **unpadded**, seconds zero-padded to 2. Examples
`0:07`, `1:47`, `62:05`.

`update_times()` (`:166-176`):

| frame | elapsed label | total label |
|---|---|---|
| `playing == false` | hidden | hidden |
| `playing == true` | `format(elapsed_ms, true)` | `format(duration_ms, playing && duration_ms != 0)` → `--:--` when the decoder reported no length |

`update_idle()` (`:188-195`): hidden state is driven **only** by `playing`.

| `playing` | elapsed | total | track | idle |
|---|---|---|---|---|
| true | shown | shown | shown | **hidden** |
| false | hidden | hidden | hidden | shown, text `no card, or nothing to play` |

Both time labels use `LV_LABEL_LONG_CLIP` (no dots, hard clip at 110 px) — `:481`, `:490`.

### 1.3 The track and the permille fill

```
w = floor(288 * permille / 1000)        // uint64 multiply then divide, cassette_peek.c:181
fill.width = w
fill.hidden = (w == 0)
```

- Written only when `w` changes (`s_fill_w` cache, `:182-185`); `update_fill()` is also called
  on show so a freshly raised bar paints the right width immediately (`:405`).
- `permille` comes from the model: `floor(elapsed * 1000 / duration)`, clamped to 1000,
  and only re-published every `CASSETTE_DEFAULT_BAR_MS = 1000` ms
  (`cassette.c:300-316`, `cassette.h:35`).
- No animation on the fill — it steps, at most once a second.
- Fill and groove both have radius 3; the fill's right end is a semicircle.

### 1.4 The two tags

`cassette_border_box(screen, x, y, 504, 44, 22, #111111, COVER, #F15A22, 2)` (`:518-530`,
constants `:67-74`). Screen children — they do **not** scale, lift or move with the pose.

| tag | role | screen pos | size | radius | fill | border |
|---|---|---|---|---|---|---|
| 0 | song (above the hub) | `(148, 12)` | `504 x 44` | 22 | `#111111` | 2 px `#F15A22` |
| 1 | group (below the hub) | `(148, 248)` | `504 x 44` | 22 | `#111111` | 2 px `#F15A22` |

Pills span `x = 148..652`. At the peek pose the cassette occupies `x = 87..712` (origin 87,
625 wide), so each pill leaves **61 px of cassette** on its side (`148 - 87 = 61`,
`712 - 652 = 60`; `:6-11`).

Text label inside each pill (`:524-528`), `tag-local` from the pill's outer corner:

| property | value |
|---|---|
| position | `(18, 7)` — 2 px border + 16 px inset, and `2 + (40-30)/2` vertically |
| size | `472 x 30` (504 − 2·16) |
| font | Jura 28, `#F15A22` |
| align | left, `y` centred in the 40 px content box |
| baseline | `pill.y + 7 + 24` → 43 for tag 0, 279 for tag 1 |
| first glyph x | `pill.x + 18` → 166 |
| overflow | **three ASCII dots**, LVGL `LV_LABEL_LONG_DOT` (`:527`), never scrolled |

Truncation must be reproduced, not `text-overflow: ellipsis` (that uses U+2026 `…` and a
different advance): LVGL replaces the last three kept characters with `'...'`
(`LV_LABEL_DOT_NUM = 3`, `lv_label.c:1465-1472`) and shrinks by whole characters until the
string measures `<= 472 px`. Reference algorithm:

```ts
const dotCut = (s: string, maxW = 472) => {
  for (let n = s.length; n > 3; n--) {
    const t = s.slice(0, n) + '...';
    if (measure(t) <= maxW) return t;
  }
  return s.length > 3 ? '...' : s;
};
```

Empty states (`:197-208`): a label with **no text** is hidden **together with its tether**,
independently of the other one. There is no em dash, no placeholder, no empty pill.

| tag text | pill | tether |
|---|---|---|
| non-empty and `peek shown` | shown | shown |
| empty, or peek hidden | hidden | hidden |
| peek sliding out | hidden immediately (no fade) | hidden immediately |

Text sources (`:212-230`, `:341-355`, `:376-382`; `cassette.c:134-211`):

- **song** = the Track's `song` tag (TIT2 / FLAC TITLE) if non-empty, else the frame's
  cleaned ICY title (`frame.title`, the cleaned `status.title`);
- **group** = the Track's `group` tag (TPE1→TPE2 / ARTIST→ALBUMARTIST) if non-empty, else
  the **first directory** of the Track's relative path (`cassette_path_group`,
  `cassette.c:199-211`), `""` for a path with no `/`;
- a new Track clears both tags at once (`:341-348`) so the previous song's text can never be
  shown against the new one;
- buffers are 96 chars (`PEEK_TEXT_MAX`, `:98`); the label truncates at 472 px long before
  that.

### 1.5 The tethers

Two `lv_line` objects, screen children created **before** the tags so each pill covers its
own line's upper end (`:506-516`). Style: width **2 px**, colour `#F15A22`, opacity cover,
**rounded** ends.

Constants (`:84-89`):

| name | value | meaning |
|---|---|---|
| `PEEK_HUB_COL` | 251 | the left hub's column at the peek pose |
| `PEEK_LINE0_X` / `PEEK_LINE1_X` | 251 / 251 | both tethers are vertical |
| `PEEK_LINE_INSET` | 14 | how far the upper end sits inside its pill |
| `PEEK_LINE0_TOP` | `12 + 44 - 14` = **42** | line 0's upper end |
| `PEEK_LINE1_TOP` | `248 + 14` = **262** | line 1's upper end |

Endpoints; `A` is fixed, `B` is the hub anchor handed in by the view on every pose step:

| line | role | A (fixed) | B at the peek pose | B at the 256 pose | B at the 134 pose |
|---|---|---|---|---|---|
| 0 | song ↔ **top** rim | `(251, 42)` | `(251, 87)` | `(210, 122)` | `(249, 158)` |
| 1 | group ↔ **bottom** rim | `(251, 262)` | `(251, 217)` | `(210, 290)` | `(249, 244)` |

Anchor arithmetic (`cassette_view.c:258-266`, `cassette_tapes.c:394-401`, `:52-53`,
`cassette_layout.c:20-75`):

```
SC(v)  = trunc(v * scale / 256)                    // cassette_len
origin = ((800 - SC(800)) / 2 + dx, (480 - SC(480)) / 2 + dy)   // integer division
hubCx  = origin.x + SC(210)                        // k_hub[0] = (210, 206)
hubCy  = origin.y + SC(206)
r      = trunc(SC(168) / 2)                        // HUB_RING_D = 168
B0     = (hubCx, hubCy - r)                        // top rim
B1     = (hubCx, hubCy + r)                        // bottom rim
```

| pose | SC(800) | SC(480) | origin | hubCx | hubCy | r |
|---|---|---|---|---|---|---|
| 256 / 0 / 0 | 800 | 480 | `(0, 0)` | 210 | 206 | 84 |
| 200 / 0 / −60 | 625 | 375 | `(87, 52−60 = −8)` | 251 | 152 | 65 |
| 134 / −51 / −20 | 418 | 251 | `(191−51 = 140, 114−20 = 94)` | 249 | 201 | 43 |

The 251 is not a magic number: it is `origin.x + SC(210)` at 200/256, which is 20.4 % of the
504 px pill — the hub's column wins over a round fraction, so the lines stay vertical
(`:76-86`).

Bounding-box trick (`line_set`, `:236-246`) — only affects invalidation on the device; for
HTML just draw the segment:

```
ox = min(A.x, B.x) - 1;  oy = min(A.y, B.y) - 1
line element at (ox, oy); points relative: (A.x-ox, A.y-oy) .. (B.x-ox, B.y-oy)
```

`update_lines()` runs on **every pose step** during the 260 ms room animation, so the lower
end travels `(210,122)→(251,87)` while the pill stays put. The upper end never moves: the
bob does not redraw the line, the pill's opaque fill hides 14 px of it and the bob only
uncovers a little more or less (`screen_display/README.md:264-272`).

The two code comments disagree about which pill meets which rim
(`cassette_peek.h:14-15` vs `cassette_peek.c:133-135`); the wiring is authoritative:
**tag 0 (song, y=12) ↔ top rim, tag 1 (group, y=248) ↔ bottom rim**
(`cassette_view.c:262-264` + `hubs_apply`).

### 1.6 The bob

One animation for both pills, on `s_bar` as the animation variable (`:292-327`):

| property | value |
|---|---|
| duration | 5000 ms, `repeat_count = INFINITE`, `lv_anim_path_linear` |
| values | `0 → 1024` |
| amplitude | ±6 px (`PEEK_FLOAT_AMP`) |
| phase | `p = value / 1024`, `θ = p · 2π` |
| offsets | `off0 = lround(sin θ · 6)`, `off1 = lround(sin(θ + π) · 6) = −off0` |
| write rule | set `tag.y = rest + off` **only when the rounded integer differs from the last frame** (`s_bob[2]`) |
| rest | tag0 `y = 12`, tag1 `y = 248` |
| on hide | `bob_stop()` deletes the animation and writes both rest positions (`:329-337`) |
| on show | restarted from `p = 0` — the phase resets on every show (`:414`) |

At the panel's 40 ms refresh (`screen_display.c:56-58`) that is ~8 pixel writes per tag per
second. The pills therefore oscillate over `y = 6..18` and `y = 242..254`, in antiphase; the
tethers and their endpoints do not move.

```ts
const FLOAT_MS = 5000, AMP = 6;
const lround = (v: number) => Math.sign(v) * Math.round(Math.abs(v)); // lroundf, symmetric
const bob = (tMs: number) => {
  const th = ((tMs % FLOAT_MS) / FLOAT_MS) * 2 * Math.PI;
  const top = lround(Math.sin(th) * AMP);
  return { top, bottom: -top };            // sin(θ+π) = −sin θ
};
```

Use the symmetric rounding: `Math.round(-0.5)` is `-0` in JS but `lroundf(-0.5)` is `-1`.

## 2. Timing, entrance and exit

| animation | from → to | ms | path | source |
|---|---|---|---|---|
| bar slide | `y 488 → 424` (show) / `424 → 488` (hide, then hidden) | **260** | `ease_out` | `cassette_peek.c:96, 272-290` |
| cassette room → peek | `pose (200, 0, −60)` from `(256, 0, 0)` | **260** | `ease_out` | `cassette_view.c:71-75, 129-143, 287-318` |
| cassette room → controls | `pose (134, −51, −20)` | 260 | `ease_out` | same |
| drawer panel | `x −120 → 0` (or reverse, then hidden) | 260 | `ease_out` | `cassette_buttons.h:55`, `cassette_buttons.c:305-330` |
| DJ block | `x offset 208 → 0` | 260 | `ease_out` | `cassette_eq.c:93-94, 764-772` |
| header band | `y −71 → 6` | 260 | `ease_out` | `cassette_header.c:38-46, 69, 99-116` |
| footer band | `y 480 → 359` | 260 | `ease_out` | `cassette_footer.c:29-41, 100-113` |
| window frame | opacity `0 → 255` | 260 | `ease_out` | `cassette_window.c:24-65` |
| white page edge mark | opacity `0 → 255`, after a 260 ms delay | 200 | `ease_in` | `cassette_view.c:97-101, 180-197` |
| cover | layered opacity `0 → 255` / reverse | **1000** | `ease_in_out` | `cassette_cover.c:188, 209-270` |
| VU meter | layered opacity, ±1 | 220 | `ease_in_out` | `cassette_meter.c:45, 151-168` |
| peek bob | 0 → 1024 forever | 5000 | `linear` | `cassette_peek.c:92-94` |

`ease_out` = CSS `cubic-bezier(0, 0, 0.58, 1)`; `ease_in_out` =
`cubic-bezier(0.42, 0, 0.58, 1)`; `ease_in` = `cubic-bezier(0.42, 0, 1, 1)`
(`lv_anim.c:296-322`).

Exact interpolation for every 0..1024 animation (bar slide, pose, drawer, bands, EQ):

```ts
const animValue = (from: number, to: number, progress: number) => {
  const t = Math.trunc(progress * 1024);          // lv_map(act_time, 0, dur, 0, 1024)
  const step = bezierStep(t, 0, 0.58 * 1024, 1024); // returns 0..1024 integer
  return from + Math.trunc(((to - from) * step) / 1024);
};
```

### 2.1 Show sequence (`cassette_peek_show`, `:394-416`)

1. guard: already shown → return (`:396`).
2. `s_shown = true`; **ask the view for room** → `room_apply(false)` → pose animation to
   `(200, 0, −60)` over 260 ms (`:398`, `cassette_view.c:145-150`). The animation is
   *started* now but its first step runs on the next 40 ms refresh.
3. repaint current state without waiting for the poll: `update_texts()`, `update_idle()`,
   `update_times()`, `update_fill()` (`:402-405`).
4. unhide the bar; raise in this order: line 0, line 1, tag 0, tag 1, bar (`:407-412`). The
   bar is now the topmost object on the screen.
5. slide the bar `488 → 424` (`:413`).
6. start the bob at phase 0 (`:414`).

The **tags and tethers do not slide or fade**: they appear in the same frame at their rest
positions (`y = 12` / `248`, plus whatever the bob has already written — nothing, since the
phase starts at 0), with `A` as above and `B` still at the *last* handed-in anchors. Because
the pose animation has not stepped yet, that is usually the **256-pose anchors**
`(210,122)` / `(210,290)` for one frame — reproduce it by drawing the tethers from the pose
you currently hold, not from the target pose.

### 2.2 Hide sequence (`:418-431`)

1. `s_shown = false`; room request `false` → pose back to `(256, 0, 0)` (or `(134,−51,−20)`
   if the drawer is also asking) over 260 ms.
2. `bob_stop()` — animation deleted, both pills written back to `y = 12` / `248`, `s_bob` zeroed.
3. **tags and tethers hidden at once** (no fade).
4. bar slides `424 → 488`; on completion it is hidden (`bar_hidden_cb`, `:266-270`).
   If a reverse deleted the animation, the completion callback does not run.

`cassette_peek_hide_now()` (`:433-451`) is the one-frame form: room request `instant = true`
(pose set with `ms = 0`), bob stopped, slide animation deleted, bar put at 488 and hidden,
tags + tethers hidden. **Nothing in the current firmware calls it** — it exists for a Level
change that no longer happens. Present it in the sim only if you want a "settle" debug key.

`cassette_peek_is_shown()` returns the *intent*, true for the whole slide (`:453-456`).

### 2.3 The room arbitration (`cassette_view.c:129-143`)

One pose, three possibilities, decided in this order:

| drawer wants room | peek wants room | resulting pose |
|---|---|---|
| true | any | `(134, −51, −20)` — the **drawer wins**; the peek's −60 is not added |
| false | true | `(200, 0, −60)` |
| false | false | `(256, 0, 0)` |

`instant` (from `hide_now`) uses `ms = 0`; a normal swipe uses `CASSETTE_ROOM_MS = 260`.

### 2.4 What shows with no track

`playing == false` ⇒ (`cassette.c:279`, `cassette.h:89`) the bar keeps the paper strip with
the centred sentence **`no card, or nothing to play`**, the two time boxes and the track are
hidden, the fill is hidden (permille 0), the idle label is shown. Tags: an empty path gives
an empty group fallback and an empty title gives an empty song fallback, so **both pills and
both tethers disappear** and only the bar remains (`:197-208`). The cassette still shrinks and
lifts for the peek — the room request is unconditional (`:398`).

## 3. Gesture recognition: the device, then the mouse

### 3.1 What the panel produces

| parameter | value | why / source |
|---|---|---|
| read cadence | one read every **10 ms** | `touch_gt911.c:51` |
| `gesture_min_distance` ("swipe limit") | **50 px**, strict `>` per axis | `sdkconfig:4256`, `lv_conf_internal.h:1312-1317`, test at `lv_indev.c:1857-1858` |
| `gesture_min_velocity` | **0** (overrides the default 3) | `touch_gt911.c:290` — the sum must survive quiet reads |
| `scroll_limit` | **20** (runtime) | `touch_gt911.c:297` — sets `press_moved`, feeds double-click drift, no scrolling anywhere (`lv_obj_set_scrollable(scr,false)`) |
| long press | 400 ms | default; no handler on the screen |
| double click window | 500 ms | `touch_gt911.c:298`; no handler on the screen |
| two-finger midpoint threshold | **30 px** on either axis | `touch_gt911.c:54-56, 99-107`; one report per touch |
| gestures per touch | **one** (`gesture_sent` latch) | `lv_indev.c:1836, 1860` |
| direction decided | at the crossing, from the **signed sum** | `lv_indev.c:1862-1873` |
| tap | PRESSED + CLICKED, no gesture, **no intent** | `screen_input.c:124-129` |

Accumulation (`lv_indev.c:1847-1873`), with `min_velocity = 0` so the reset branch never
runs:

```
on press:   sum = (0, 0); gesture_sent = false
on move:    sum += (dx, dy)                       // per-read delta, signed, additive
            if |sum.x| > 50 or |sum.y| > 50:
                gesture_sent = true
                if |sum.x| > |sum.y|:  dir = sum.x > 0 ? RIGHT : LEFT
                else:                  dir = sum.y > 0 ? DOWN  : UP     // ties → vertical
```

Consequences to honour:

- the sum is a **signed vector**, not path length: dragging right 60 then left 60 fires
  RIGHT on the way out and then latches;
- the direction test is on the **sums**, not on per-axis thresholds: `(+60, +55)` is
  horizontal, `(+40, +60)` is vertical;
- a **45° diagonal is vertical** (strict `>` on the first branch);
- the gesture fires **mid-drag**, the moment the threshold is crossed — the finger is still
  down while the 260 ms animation starts;
- after firing, further movement is ignored until the finger lifts;
- the release of a swipe **still sends CLICKED** to the same object (`lv_indev.c:1549-1560`);
  the screen has no CLICKED handler, so it means nothing — that is why a swipe never
  double-acts. Chrome with its own CLICKED (drawer buttons, EQ cells) would fire it; the sim
  should swallow a click that follows a drag.

### 3.2 Where a gesture is allowed to start

`indev_gesture` sends the event to the pressed object, walking up while
`lv_obj_is_gesture_bubble` (`lv_indev.c:1838-1845`):

| press lands on | gesture reaches the screen? | note |
|---|---|---|
| empty screen area (`lv_obj_set_clickable(scr, true)`, `cassette_view.c:549`) | yes, directly | all decorative objects are non-clickable |
| drawer panel / its buttons | yes — both set `gesture_bubble` | `cassette_buttons.c:273-274, 349-352` |
| EQ cell | yes — cells set `event_bubble`, which bubbles `LV_EVENT_GESTURE` up to the screen | `cassette_eq.c:660` (the comment there claims otherwise; the bubble flag wins) |
| Menu Pad | **no** — `gesture_bubble = false`, no `event_bubble` | `cassette_menu.c:553` |
| Menu page and rows | yes — they are non-clickable, so the press falls through to the screen; only `UP` does anything there (closes the Menu) | `cassette_menu.c:494-538` |

### 3.3 The mouse rules (reproduce the touch feel exactly)

`mouse-down` = finger down, `mouse-move` = one 10 ms read, `mouse-up` = finger up. Use
**pointer events** with `touch-action: none` on the canvas and pointer capture; convert
client coordinates to design coordinates (divide by the letterbox scale) before summing —
the thresholds are in design pixels.

```ts
const GESTURE_LIMIT = 50;                 // strict >
type Intent = 'UP'|'DOWN'|'LEFT'|'RIGHT'|'BUTTONS_SHOW'|'BUTTONS_HIDE';

function attachGestureEngine(canvas: HTMLElement,
                             toDesign: (e: PointerEvent) => {x:number;y:number},
                             onIntent: (i: Intent) => void,
                             startedOnMenuPad: (e: EventTarget|null) => boolean) {
  let down = false, fired = false, sx = 0, sy = 0, lx = 0, ly = 0;

  canvas.addEventListener('pointerdown', e => {
    if (e.button !== 0) return;              // primary button = one finger
    if (startedOnMenuPad(e.target)) return;  // the Pad swallows its own drags
    down = true; fired = false;              // a new press clears the two-finger latch
    const p = toDesign(e); sx = sy = 0; lx = p.x; ly = p.y;
    canvas.setPointerCapture(e.pointerId);
  });

  canvas.addEventListener('pointermove', e => {
    if (!down || fired) return;
    const p = toDesign(e);
    sx += p.x - lx; sy += p.y - ly;          // signed sum, not distance
    lx = p.x; ly = p.y;
    if (Math.abs(sx) <= GESTURE_LIMIT && Math.abs(sy) <= GESTURE_LIMIT) return;
    fired = true;                             // one intent per press
    onIntent(Math.abs(sx) > Math.abs(sy)
      ? (sx > 0 ? 'RIGHT' : 'LEFT')
      : (sy > 0 ? 'DOWN' : 'UP'));
  });

  const end = () => { down = false; };        // release adds nothing; a tap is nothing
  canvas.addEventListener('pointerup', end);
  canvas.addEventListener('pointercancel', end);
  canvas.addEventListener('pointerleave', end); // only with capture this is unreachable
}
```

Rules, spelled out:

| rule | value |
|---|---|
| drag threshold | `abs(sumX) > 50` **or** `abs(sumY) > 50`; exactly 50 does not fire |
| axis | `abs(sumX) > abs(sumY)` → horizontal; otherwise vertical (ties and 45° → vertical) |
| direction sign | screen y grows **down**: positive `sumY` = DOWN, negative = UP; positive `sumX` = RIGHT |
| when it fires | during the drag, not on release |
| after it fires | all movement ignored until pointerup |
| release | no intent, ever; never synthesise a click on the canvas |
| tap (press + release, sums ≤ 50) | **no intent** (the screen has no click action) |
| click after a drag | suppressed on the canvas and on chrome (`pointerup` inside a drag does not click) |
| second intent in one press | impossible (latch) |
| time limits | **none** — a very slow 51 px drag still fires; no velocity gate, no long-press gate |
| repeated presses | each press starts a fresh sum; nothing carries over |
| two-finger swipe | not reproducible with one mouse button → **ignore** `VU_SHOW`/`VU_HIDE`; keep the intent enum and the latch flag so the state machine is complete, and optionally bind them to a modifier + horizontal drag if a demo needs the meter |
| backlight wake | the device's first press on a dimmed screen is swallowed (`touch_gt911.c:145-168`); do not model it |

## 4. The state machine

The screen has **one** Level — Bare — plus overlays. The authoritative decision order is
`screen_intent_cb` (`cassette_view.c:359-431`):

```
1. if (editing a knob) return;                       // every intent ignored, incl. UP
2. if (menu open) { if (UP) close menu; return; }    // every other intent ignored
3. switch (intent) { ... }
```

### 4.1 States

| id | name | pose (scale/dx/dy) | peek | drawer+block | page | cover | menu | cassette drawn |
|---|---|---|---|---|---|---|---|---|
| S0 | **Bare** | 256 / 0 / 0 | hidden | hidden | dark `#111111` | off | closed | yes |
| S1 | **Peek** | 200 / 0 / −60 | **shown** | hidden | dark | off | closed | yes |
| S2 | **Controls dark** | 134 / −51 / −20 | hidden | **shown** | dark | off | closed | yes |
| S3 | **Controls white** | 134 / −51 / −20 | hidden | shown | **white `#909090`** | off | closed | yes |
| S4 | **Cover on** | 134 / −51 / −20 | hidden | shown | white | **shown** | closed | yes |
| S5 | **Menu open** | 134 / −51 / −20 | hidden | shown (menu mode) | as it was opened | off | **open** | **hidden** |
| S6 | *knob edit* | 134 | — | — | — | — | — | **omit**: all intents ignored (`cassette_eq_is_editing()`, `cassette_view.c:351, 365`) |
| S7 | *animating* | between poses | — | — | — | — | — | see §4.3 |

Supporting flags, because they gate intents before the pose lands:

- `s_peek_wants_room` — true from the instant the peek is asked to show, until it is asked to
  hide (not until the animation ends);
- `s_buttons_want_room` — same for the drawer;
- `s_pose.scale` — the **interpolated** scale, so `== 256` only when the shrink/lift has
  actually landed;
- `s_page_white` — the page face;
- cover `shown` — the *wanted* flag, cleared at the start of its 1 s fade out.

### 4.2 Transition matrix (state × gesture → action)

`⌀` = accepted and does nothing visible. `IGN` = ignored by a guard. "→ Sx" includes the
260 ms pose animation unless noted.

| state | UP | DOWN | LEFT / RIGHT | TAP |
|---|---|---|---|---|
| **S0 Bare** | → **S1** raise peek — only if `pose.scale == 256`; else IGN | → **S3** (`controls_set_shown(true, true)`: drawer + block + **white page**, one gesture, no dark intermediate) | both → **S2** controls on the dark page | ⌀ (no handler) |
| **S1 Peek** | ⌀ (peek already wanted) | → **S0**: hide peek only; the same swipe does **not** open the controls (`cassette_view.c:393`) | both → **S2**: peek hidden first, then drawer + block on dark (`controls_set_shown(true,false)` → `toggle_peek`, `:235`) | ⌀ |
| **S2 Controls dark** | → **S0**: close controls (`controls_set_shown(false,false)`), page stays dark, cover off | → **S3**: page flips white, controls stay (drawer not re-slid) | both → **S0** close, page forced dark | ⌀ |
| **S3 Controls white** | → **S0** close controls (cover not shown, so this branch), page back to dark | → **S4** cover on — **only if `pose.scale == 134`**; during the shrink it is IGN and the next swipe shows it (`:397-399`) | both → **S0** close, page back to dark, cover cut with it (1 s fade) | ⌀ |
| **S4 Cover on** | → **S3**: cover off (1 s fade), controls stay | ⌀ (`cover_set_shown(true)` again is a no-op) | both → **S0** close; `page_set(false)` takes the cover with it | ⌀ |
| **S5 Menu open** | → close the menu, back to the page it opened on (`cassette_menu_close`, `:370-373`) — an UP anywhere except the Pad (the page and rows do not take clicks, so their drags reach the screen) | IGN | IGN | ⌀ — the Pad decides its own taps; nothing on the screen reacts to a click |
| **S6 Knob edit** | IGN | IGN | IGN | IGN |
| **S7 Animating** | see §4.3 | see §4.3 | accepted — the drawer toggles immediately, reversing the pose in flight | ⌀ |

Notes that fall out of the table:

- **LEFT and RIGHT are synonyms.** `BUTTONS_HIDE` and `BUTTONS_SHOW` are separate intents
  (`screen_input.h:47-57`) but the view runs the same toggle: `controls_set_shown(!shown)`
  (`cassette_view.c:377-384`). Either opens from S0 and closes from S2/S3/S4.
- **The dark controls page is only reachable with a horizontal swipe.** A down swipe always
  paints the white page.
- **The peek and the controls never coexist.** Every path into the controls closes the peek
  first (`:235`), and a down swipe with the peek up only closes it.
- **The peek can only be raised from exactly 256/0/0** (`:418`), with neither the drawer nor
  the peek itself asking for room.

### 4.3 During animation

`room_apply` and the peek/bar/drawer slides all start together and all take 260 ms, but the
guards read the **flags**, not the animations:

| situation | UP | DOWN | LEFT / RIGHT |
|---|---|---|---|
| peek showing (`s_peek_wants_room = true`, pose 256→200, bar 488→424, tags already up) | IGN (`s_peek_wants_room`) | closes the peek immediately | opens the controls: peek hidden, drawer slides in from whatever the peek left |
| peek hiding (flag false, pose 200→256, bar 424→488 then hidden, tags already hidden) | **IGN** (`pose.scale != 256`) until the pose lands | opens the white controls from the current pose | opens the dark controls |
| controls opening (flag true, pose →134, chrome sliding) | closes the controls (reverses the pose) | page → white, or cover if the scale is already 134 | closes |
| controls closing (flag false, pose →256, chrome sliding out) | **IGN** (`pose.scale != 256`) | opens the white controls from the current pose | opens the dark controls |
| cover fading (1 s) | wanted flag already false, so a second UP closes the controls | no-op | closes, cover fades out with the page |
| white page edge mark (260 ms delay + 200 ms fade) | as S3 | as S3 | as S3 |

A reversed animation always restarts **from the pose currently on screen**
(`cassette_view.c:291-297`): `lv_anim_delete` + `s_pose_from = s_pose`. Never lerp from a
remembered start.

## 5. The cover overlay

Shown only in **S4**, i.e. all of:

1. the drawer and the block are out and `s_page_white` is true,
2. the pose is settled at **exactly 134** (`cassette_view.c:397`),
3. a decoded cover exists (`cassette_cover_set_image` with a non-NULL descriptor;
   `cassette_levels.c:52-61` clears it for a Track with no picture or no format),
4. the down swipe asked for it (`cassette_cover_set_shown(true)`).

Geometry (controls pose) — `cassette_cover.c:143-190`, `cassette_tapes.c:99-103, 353-357`:

| property | value |
|---|---|
| box | frame 0 = screen `(148, 102)` size `402 x 188` (design `16,16,768,360` at 134/256) |
| box radius | `SC(16) + 2 = 10` — the print sits 2 px inside the shell's border |
| picture | stretched to the frame's width 402, height `402 · srcH / srcW`, **top-aligned**, clipped by the box (`LV_IMAGE_ALIGN_STRETCH` on the image, not on the box) |
| hole | frame 1 = design `(122,122,556,168)` → controls `(203,157,291,87)`, i.e. `cover-local (55, 55)` size `291 x 87`, radius `min(291,87)/2 = 43` (a pill), punched out of an A8 mask so the page's glow and the tape show through |
| frame 1's own border | takes the same 43 px pill radius while the label is up (`cassette_tapes_set_cover_print`) |
| z-order *inside* the cassette | `cinta < glow < label`; the two frames are raised above the label so their borders stay crisp |

Show/hide: `1000 ms` `ease_in_out` on the container's **layered opacity** (`COVER_FADE_MS`,
`:188, 209-270`). A **cut** (no fade) happens on a Track change — the loader reuses its single
buffer — and a page flip starts a fade because it calls `cover_set_shown(false)`
(`cassette_view.c:215`). Calling show twice just re-raises.

### Z-order, bottom to top (creation order, `cassette_view.c:435-551`)

| # | object | source |
|---|---|---|
| 1 | screen backdrop — `#111111` (dark) / `#909090` (white page) | `:217` |
| 2 | back-cover box `(16,16,768,448)`, `#111111`, square corners — never flips, so the white page reads as a 16 px ring | `:442` |
| 3 | window frame `(110,76,486,284)`, opacity-faded with the controls | `:448` |
| 4 | cassette container (full 800×480, transparent, non-clickable); internal stack bottom→top: page edge mark, tapes, VU meter, cover (raised only while shown), then the two frames raised above the cover | `:456-505` |
| 5 | peek tethers, then peek tags, then the peek bar (raised above everything on show) | `:506-530`, `:408-412` |
| 6 | transport drawer panel (raised on show) | `:514` |
| 7 | DJ block cells | `:518` |
| 8 | Menu page and Pad | `:524` |
| 9 | header band, then footer band | `:531-532` |

The peek sits above the cassette but below the drawer, the block, the Menu and the bands.
Because they never coexist, the only observable overlap is the 260 ms in which a horizontal
swipe hides the peek and slides the drawer in over the shrinking cassette.

## 6. Demo status fixtures

Use one playing Track and one idle state.

```jsonc
// playing — status (mini_icecast_status_t, mini_icecast.h:95-110)
{
  "title": "Midnight City",                                  // ICY title, cleaned by cassette.c:269-277
  "file": "04 midnight_city.mp3",
  "path": "M83/Hurry Up, We're Dreaming/04 midnight_city.mp3",
  "format": "MP3",
  "elapsed_ms": 107000,
  "duration_ms": 243000,
  "level_l": 0.50,
  "level_r": 0.22
}
```

Derived frame (`cassette.c:261-433`) and peek rendering:

| quantity | value | how |
|---|---|---|
| `playing` | `true` | `file != "" && format != NONE` (`cassette.c:279`) |
| peek song label | `Midnight City` | tag, else the cleaned title |
| peek group label | `M83` | tag, else the first path segment (`cassette_path_group`) |
| elapsed text | **`1:47`** | `107000 ms` |
| total text | **`4:03`** | `243000 ms` |
| `permille` | **440** | `trunc(107000·1000/243000)` |
| fill width | **126 px** | `trunc(288·440/1000)` |
| `pack_fill` | **`[595, 484]`** | `80 + 920·(1000−q)/1000`, `80 + 920·q/1000` (`cassette.c:420-422`) |
| hub revolution rates | supply **0.383 rev/s** (2.61 s/rev), take-up **0.410 rev/s** (2.44 s/rev) | `47.625 / (2π·r)`, `r = sqrt(11² + 455·fill/1000)` mm (`cassette.c:112-118, 355-372`) |
| `hub_frame` | **`[11, 28]`** | `floor(frac·36) % 36`; equivalent phases `0.31` / `0.78`; 36 frames/rev (`cassette_tapes.h:35`) |
| `vu_l`, `vu_r` | `≈0.50`, `≈0.22` | peak-hold of the RMS, `peak = max(level, peak·0.90)` per 40 ms (`cassette.c:287-290`) |
| VU lit bars | **6** and **5** of 7 | `round((20·log10(vu) + 48)/48·7)` clamped (`cassette_meter.c:64-70`) |
| `rewinding` | `false`, `rewind_permille 0`, `tape_ink 1.0` | no boundary |
| tags at rest | tag0 `y 12`, tag1 `y 248` | plus the bob |

Idle fixture:

```jsonc
{ "title": "", "file": "", "path": "", "format": "NONE",
  "elapsed_ms": 0, "duration_ms": 0, "level_l": 0, "level_r": 0 }
```

→ bar visible with the centred sentence `no card, or nothing to play`; elapsed/total/track
hidden; `permille 0` → fill hidden; both pills and both tethers hidden. The peek still raises
and the cassette still goes to `(200, 0, −60)`.

## 7. Mouse-specific UX notes for a faithful feel

1. **Threshold in design pixels, not CSS pixels.** Convert the pointer position into the
   800×480 space first; otherwise the letterbox scale changes how far a swipe must travel.
2. **Sum signed deltas, do not measure distance from the press point.** The device
   integrates per-read vectors (`lv_indev.c:1854-1855`), so a wiggle in and out does not add
   up. This also makes a circular drag much harder to trigger than a straight one — the
   signature of the original feel.
3. **Act mid-drag, not on release.** The picture starts moving while the button is still
   held; the user's next action is a fresh press. Do not defer to `pointerup`, and do not
   cancel or reverse on release.
4. **Release adds nothing.** No intent, no click. Swallow the synthetic `click` that follows
   a drag (and set `user-select: none`, `draggable=false`, `touch-action: none`) so an
   accidental press-and-drag over a drawer button does not also fire the button.
5. **One intent per press.** After it fires, ignore movement until pointerup even if the
   mouse reverses direction. This is what stops a long drag from opening and closing the
   drawer twice.
6. **A tap does nothing at all.** Do not map a click on the cassette to the peek or to the
   controls — only drags act. A click on a drawer button or an EQ cell is that widget's.
7. **No time gate.** A slow 51 px drag must fire, and holding still for 2 s then dragging must
   also fire (there is no long-press swallow on the screen).
8. **No velocity or inertia.** The gesture is a threshold, not a flick; the animations are
   fixed-duration eases, not physics.
9. **Modifier for the two-finger gesture** (optional): if the demo needs the VU meter, bind
   `Alt` + horizontal drag to `VU_SHOW` / `VU_HIDE` (left shows, right hides,
   `screen_input.c:76-87`); label it in the UI as a substituted gesture.
10. **Cursor**: `default` everywhere on the canvas; no open/closed hand, no resize arrows —
    the panel has no hover state and no cursor object.
11. **Reversal is free.** If the user swipes again while a 260 ms pose animation runs, start
    the new animation from the interpolated pose on screen (`cassette_view.c:291-297`) — the
    sim should read its current animated values, not its targets, or the cassette will jump.
12. **Do not debounce.** Two deliberate swipes 150 ms apart are two intents (the device has
    no cooldown; only the per-press latch).

## 8. Acceptance checklist

1. Bar at `(116,424)`, `568×52`, radius 12, 2 px `#F15A22` border on `#111111`.
2. Elapsed glyphs start at x=138 and the box ends at 248; total box `552..662`, right-aligned;
   both Jura 28 at baseline y=459.
3. Track `258..546` at `y 449..455`, groove `#7F2C18`, fill `#F15A22`, width
   `floor(288·permille/1000)`, hidden at 0.
4. Idle sentence centred at `(400, 450)` exactly when `playing` is false; times and track
   hidden then.
5. Times use `m:ss` with unpadded minutes; total is `--:--` when the duration is 0.
6. Pills at `(148,12)` and `(148,248)`, `504×44`, radius 22, text at `+18, +7`, width 472,
   ellipsis rendered as three ASCII dots.
7. An empty label hides its pill **and** its tether, independently.
8. Tethers from `(251,42)`→hub-top and `(251,262)`→hub-bottom, 2 px, rounded, `#F15A22`;
   at the settled peek pose the ends are `(251,87)` and `(251,217)`.
9. Tether lower ends follow the pose at every animation step; upper ends never move.
10. Bob: ±6 px, 5000 ms, antiphase, integer offsets written only on change, reset to rest on
    hide and restarted at phase 0 on show.
11. Show: bar slides 488→424 in 260 ms `cubic-bezier(0,0,0.58,1)`; pills and tethers appear
    in the first frame with no slide or fade; the pose goes to 200/−60 in the same 260 ms.
12. Hide: pills and tethers vanish at once; the bar slides to 488 and is hidden at the end;
    the pose returns to 256/0/0.
13. Drag threshold 50 px on either axis of the signed sum, axis by larger magnitude, ties
    vertical, fires once per press, mid-drag.
14. Every cell of §4.2 matches, including: DOWN from Bare goes straight to the **white**
    controls; LEFT/RIGHT are synonyms; an UP during any `pose.scale != 256` is dropped; a
    second DOWN on the white page is refused until the pose is settled at 134.
15. Menu open swallows every intent but UP; knob edit swallows everything.
16. Cover appears only from S3 with a settled 134 pose and an image; 1 s `ease-in-out` fade;
    the up swipe takes it away before it closes the controls.
17. Z-order: peek above the cassette, below the drawer/block/Menu/bands; the cover inside the
    cassette with the frames over it.

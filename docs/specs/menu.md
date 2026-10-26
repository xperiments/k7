# The Menu

The Menu is the screen's way into everything that is not the cassette: the Playlist, the card's
directories, the shuffle mode and the settings. It is a **mode of the Controls**, not a Level or
a screen: the cassette area becomes a five-row list, the DJ block becomes the **Pad**, and the
drawer's three buttons become Back, Enter and Close.
[ADR 0022](../adr/0022-the-menu-is-a-controls-mode.md) records why;
[ADR 0023](../adr/0023-shuffle-is-a-broadcast-mode.md) records the shuffle mode the root row
toggles. The vocabulary is in [CONTEXT.md](./CONTEXT.md): **Menu**, **Menu list**, **Row**,
**Band**, **Pad**, **Page**, **Controls**.

This document is the operational spec: geometry, the tree, the interaction rules, the data the
lists read, and the split between the portable model and the LVGL view. It describes what is
built - `components/portable/menu` with `t18_menu`, `cassette_menu.c`, the chrome hooks in the
five modules, and the shuffle mode of ADR 0023. The board pass is still owed: the Pad's feel,
the acceleration thresholds, and the five icon glyphs that are not drawn yet.

## What it is

- Opened by a tap on the **M cell** of the DJ block, only while the Controls are out and the
  cassette has settled at the 134/256 pose, and not while a knob is in edit mode. On the white
  page and on the dark one alike.
- **Modal**: while it is up, the only live inputs are the Pad and the drawer's three buttons; the
  rows and the page take no touches. A down swipe or either horizontal swipe does nothing; the
  two-finger VU gesture is ignored; the block's cells take no touches.
- Closed by an **up swipe**, by **P** at any depth, or by **`<`** from the root. Its **Menu list**
  and **Band** are remembered until the app restarts, so the next M returns where it left.
- Appears and disappears in **one frame**. The cassette container - tape, hubs, meter, glow and
  the Cover - is hidden for the Menu's lifetime and shown again on close; the Cover's wish is
  never changed, so it returns exactly as it was.
- Follows the **Page**: every ink below has a white-page and a dark-page form, like the drawer,
  the block and the bands.

## Geometry

The Menu draws inside the controls' existing frame; it adds no rectangle.

| Part | x, y | size | notes |
|---|---|---|---|
| Screen | 0, 0 | 800x480 | the panel |
| Back cover | 16, 16 | 768x448 | the dark ground under the white page's margin |
| Header band | 16, 6 | 768x71 | carries the K7 mark and the list's title |
| **Menu page** | 110, 76 | 486x284 | the existing window; its right edge meets the Pad at 596; 2 px border, 8 px corner; inner 482x280; its fill is the Page's colour while the Menu is up and `k_col_bg` otherwise |
| **Rows** | 112, 78 | 482x56 each | five of them, exactly filling the inner page |
| Footer band | 16, 359 | 768x115 | now playing on the left, the band's position on the right |
| Drawer buttons | 16, 76/170/264 | 96x96 each | P, `<`, `>` |
| **Pad** | 596, 76 | 186x284 | a white plate with a dark inner inset 8 px, and two dotted vertical guides, inside the DJ block's place |

Row internals, relative to the row:

| Part | x | notes |
|---|---|---|
| Band | 2 | inset 2 px on every side, 478x52, radius 8, the selected row's own fill |
| Icon | 16 | 24x24 from the icon font, vertically centred |
| Label | 56 | Jura 28, vertically centred, at most 300 px (240 on a value row), ellipsized |
| Playing dot | 364 | 6 px, centred vertically, the row's own ink |
| Chevron or value | right, inset 16 | Jura 28; `>` for a list, the value for a setting (capped at 150 px), nothing for an action; the right edge is at 466 |

The header title is Jura 36 at x=104 (18 px clear of the 75 px K7 mark) and vertically centred.
The footer's first line (the song) is Jura 28 at (24, 20), the second (the group) at (24, 54);
the band's position, `3 / 1204`, is Jura 28 right-aligned at x=744 and vertically centred. With
nothing playing both footer lines are empty.

## The tree

```
MENU
├─ Tracks        radio-waves    chevron   the Playlist, one Track per Row
├─ Browse        folder         chevron   a directory of the Library tree
├─ Shuffle       shuffle        Off / On  toggles Shuffle mode (ADR 0023)
└─ Settings      gear           chevron
   ├─ Backlight   sun           Auto / 100% / 60% / 30%
   ├─ Station     radio-waves   chevron
   ├─ Library     SD            chevron
   ├─ Rescan      endless-loop  Rescan / Rescanning... (inert while running)
   └─ About       info          chevron
```

| List | Rows | Source | Title |
|---|---|---|---|
| Root | 4 | fixed | `Menu` |
| Tracks | the whole Playlist, Track rows | `library_track_count()` / `library_track_at()` | `Tracks` |
| Browse | the directory's children in the library's name order, directories and Tracks mixed | `library_child_count()` / `library_child()` / `library_kind()` | `Browse`, then `Browse /Jazz`, ... |
| Settings | 5 | fixed | `Settings` |
| Station | Format, Rate, Bitrate | `mini_icecast_status_t` | `Station` |
| Library | State, Tracks, Directories, Size, Truncated | `library_*` | `Library` |
| About | Firmware, Board, Partition | `esp_app_get_description()`, the board adapter, the partition table | `About` |

- A **directory Row** wears `cassette-tape-` and opens the next Browse list; a **Track Row** wears
  `radio-waves` and plays. Inside Browse, the title is the path from the root, ellipsized.
- The **playing Track's** Row carries a 6 px dot after its label, in the Row's own label ink (a
  shape, so it reads on both pages and on the band); activating the Row restarts the Track.
- A **Track Row**'s label is the cleaned name: the library name without its extension,
  apostrophes (and the typographic U+2019) as spaces, parenthetical asides dropped, whitespace
  collapsed - the same string the Peek falls back to.
- Station's values: `MP3`, `44.1 kHz`, `192 kbps`, an em dash when the status is idle.
  Library's: the state's name, `1,204 tracks` (`1 track` in the singular), `38 dirs`, `12.4 GB`
  (binary units, one decimal), `No`/`Yes`. About's: the app's version, the board's short name
  (`8048S050C`), the app partition's size (`4 MB`). A value row has no icon and no chevron.
- **No card or no library**: Tracks and Browse are dimmed (40% opacity) and inert, and their
  lists hold one non-selectable Row that repeats the state - `No card` (no card, no library
  lease), `Library empty` (empty, or OK with zero Tracks), `Library error` (no directory or a
  read error). Every other list works, so Rescan and Library stay reachable. An empty directory
  inside a usable library is a blank Browse list, not a note.
- **Rescan**: a tap starts `app_services_rescan()`; the Row reads `Rescanning...`, is dimmed and
  ignores taps until the new generation lands. The Menu keeps its list; when the generation changes it
  re-resolves the Browse path against the new tree and pops to Browse's root if the directory has
  vanished. The Band clamps to the new count.
- **Backlight**: a tap cycles `Auto`, `100%`, `60%`, `30%` in place. Auto is the idle ladder the
  screen already runs; any other value pins the panel with a hold so the ladder does not
  overwrite it, and the choice lives until reboot. No NVS. A hold also keeps the panel lit: the
  idle dim and the screen-off step are skipped until Auto is chosen again, which is the cost of
  a brightness the user chose.
- **Shuffle**: the Row reads `mini_icecast_shuffle_enabled()` and toggles it; the order itself is
  the broadcast's (ADR 0023).

## The Pad

A white plate (`k_col_white`, 8 px corner) over the block's place, with a dark inner
(`k_col_bg`) inset 8 px and rounded by the same 8 px, and two dotted vertical guides inside the
inner, 20 px either side of its centre, inset 20 px from its top and bottom. The guides are
`lv_line` objects: 2 px wide, a 2 px dash and a 4 px gap, rounded, in `k_col_white`. The whole plate is the one
surface - the inner and the guides are decorative, so a press on them falls through to it - and
the six carcases it replaces are not drawn in the mode: the view draws the plate and hides the
DJ block, so the Pad owns no part of the block's object tree.

- A **vertical drag** ratchets the list. At rest, **56 px of travel is one row**; a step is
  instant and the list rests only on row boundaries. A fast drag multiplies the rows per 56 px in
  **three stages, 1x / 2x / 4x**: 2x from 1200 px/s and 4x from 2400 px/s, holding a stage for
  150 ms before dropping back to 1x; a fast sample can jump straight to 4x. The constants live in
  the model and are tuned with a finger on the board.
- A **tap** that never became a drag (no move beyond the touch's drag limit) activates the
  **Band**.
- The **Band** starts on the first visible Row of a list (or the remembered Row) and moves one
  Row per step in the drag's direction. At the last visible Row, the next down step shifts the
  window one Row and the Band stays on that edge Row; symmetric at the top.
- The ends **clamp**: a step past the first or last Row does nothing, and the window never leaves
  the list. There is no bounce and no partial Row.
- The list is **virtual**: five Row slots are rebound to the window's indices on each step; no
  object exists per Track and nothing is allocated per step. There is no LVGL scroll container,
  no spacer and no snap call.
- A **short list** (fewer Rows than the page) leaves blank space below its last Row; the Band
  cannot leave the list and the window never shifts.

## The drawer

While the Menu is up the three buttons change meaning and label:

| Button | Action | Label |
|---|---|---|
| P | Close the Menu, from any depth | `cassette-tape` |
| `<` | Back: up one Menu list; closes the Menu at the root | `angle-circle-left` |
| `>` | Enter: the same as a tap on the Pad | `angle-circle-right` |

Transport prev/skip is suspended until the Menu closes. Back puts the Band on the Row that opened
the list it returns to (the Settings Row, the directory Row), which is the one place the Band is
set rather than remembered.

## Ink

| | White page | Dark page |
|---|---|---|
| Page fill | `k_col_white` (0x909090) | `k_col_bg` (0x111111) |
| Row label | `k_col_bg` | `k_col_primary` |
| Row icon | `k_col_bg` | `k_col_primary` |
| Band fill | `k_col_bg` | `k_col_primary` |
| Band label, icon, chevron | `k_col_button_white` (0xEEEEEE) | `k_col_bg` |
| Dimmed Row | 40% of the label ink | 40% of the label ink |
| Playing Track's dot | the label ink | the label ink |

The band is the page's opposite, the same idiom as the M and R caps; unselected Rows take the
page's own ink. The Menu's page fill is the window's own fill recoloured for the Menu's lifetime:
the window is `k_col_bg` on both pages outside the Menu, because the cassette stands on it, and
while the Menu is up it takes the page's fill so the Rows have the page's ground. The Menu never
changes the page itself: the up swipe that closes it leaves the white or dark page as it found it.

## Data and lifecycle

- The view takes a **library lease** when the Menu opens and releases it when the Menu closes;
  the portable model reads the tree through the pointer it is given and never touches the card.
- The model owns the state: the list kind, the Browse path (a relative path string, not a node),
  the Band, the window's first index, the ratchet's accumulator and stage, and the remembered
  list. It survives closing; the lease does not.
- On a **generation change** while open, the model re-resolves its Browse path with
  `library_find()`; a path that no longer exists pops to Browse's root. Tracks and the read-only
  lists rebuild from the new generation; the Band clamps.
- A **Track request** is a relative path: the view calls `mini_icecast_play(path)` for the
  broadcast to resolve, exactly as a Browse selection does today.
- Label text lives in five fixed buffers owned by the model (96 bytes each). The view copies a
  row's text into LVGL with `lv_label_set_text()` on every redraw rather than handing the buffer
  over as static text: LVGL 9's `LV_LABEL_LONG_DOT` writes its ellipsis into the label's copy, so
  static text cannot be ellipsized. The copies happen per redraw (a step, a list change), not per
  frame, and the library's strings are valid for as long as the lease is held.

## The model and the view

- **`components/portable/menu`** - the model: the tree, the navigation, the window, the Band, the
  ratchet, the row models and the name cleaning - `cassette_clean_name()`, the one function the
  Row label and the Peek's fallback share (the Menu passes `stem = true` to drop the file
  extension; the Peek's title keeps its dots). It returns actions (`play this relative path`, `rescan`,
  `next backlight`, `toggle shuffle`, `close`) and needs no LVGL. Host-tested in `host/tests/t18_menu.c`
  through a small Library fixture, as `cassette` and `cassette_layout` are.
- **`screen_display/cassette_menu.c`** - the view: five Row slots, the band, the Pad, the header
  and footer texts, the drawer's menu labels, and the executor for the model's actions. It calls
  `cassette_eq`'s M hook, hides the block and the cassette, and gates `screen_input` while open.
- **`cassette_eq`** gains the M click hook and the hidden state the Pad covers; **`cassette_buttons`**
  gains the menu mode (the three actions and their icon labels); **`cassette_header`** and
  **`cassette_footer`** gain their Menu texts.
- **`backlight`** gains the hold: a manual percent is kept and the idle ladder is skipped while it
  is set.
- **`mini_icecast`** gains the shuffle mode and its order (ADR 0023).

Repaint budget: a step that moves only the Band repaints two Rows - 2 x 482 x 56 x 2 = 107,968 B
painted, 215,936 B with the direct-mode sync copy; a step that shifts the window repaints the page
- 482 x 280 x 2 = 269,920 B painted. The cassette is hidden, so the 40 ms reel tick adds nothing
while the Menu is up.

## Icons and fonts

`xtape.ttf` carries nine glyphs today; five more are needed. The mapping:

| Codepoint | Glyph | Used by |
|---|---|---|
| U+F000 | `cassette-tape-` | Browse directories; the drawer's Close |
| U+F001 | `voicemail` | spare |
| U+F002 | `radio-waves` | Tracks, Track Rows, Station |
| U+F003 | `angle-circle-left` | the drawer's Back |
| U+F004 | `angle-circle-down` | spare |
| U+F005 | `angle-circle-up` | spare |
| U+F006 | `angle-circle-right` | the drawer's Enter |
| U+F007 | `endless-loop` | Rescan |
| U+F008 | `shuffle` | Shuffle |
| U+F009 | `folder` | Browse |
| U+F00A | `gear` | Settings |
| U+F00B | `sun` | Backlight |
| U+F00C | `info` | About |
| U+F00D | `SD` | Library |

The five new glyphs are drawn in the same icomoon project and exported to
`assets/fonts/xtape.ttf`; `package.json` grows a `font:xtape` script beside `font:jura` and
`font:jura28`. It generates the same range at two sizes because one font is one pixel size:
`font_xtape_24.c` (`--range 0xF000-0xF00D --size 24 --bpp 4`) for the Rows, where a 24 px glyph
sits in a 56 px Row, and `font_xtape_36.c` at 36 px for the drawer's 64 px circles, where 24 px
would read small against the Jura 36 letters they replace. Text is Jura 28 in the Rows and the
footer and Jura 36 in the header; the submenu chevron is Jura's `>` at 28 px.

## Open items

- Whether the shuffle mode is shown anywhere outside the Menu (the Peek, the footer); this cut
  says nowhere.
- About's Partition shows the app partition's size. The running image's size has no runtime
  field in `esp_app_get_description()`; until a source is added the row cannot read
  "1.70 MB of 4 MB".
- The acceleration thresholds, the hysteresis and the touch's drag limit: constants, tuned with a
  finger on the board.
- NVS for the backlight hold and the shuffle mode: deferred; both live until reboot.

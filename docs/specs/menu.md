# The Menu

The Menu is the screen's way into everything that is not the cassette: the Library tree, the card's
Playlists, the shuffle mode and the settings. It is a **mode of the Controls**, not a Level or
a screen: the cassette area becomes a five-row list, the right bar's group gives way to the
**Pad**, and the mode bar's cassette button is the way in and out.
[ADR 0022](../adr/0022-the-menu-is-a-controls-mode.md) records why;
[ADR 0023](../adr/0023-shuffle-is-a-broadcast-mode.md) records the shuffle mode the root row
toggles; [ADR 0096](../adr/0096-the-menu-browses-the-library-and-walks-its-lists.md) records this
tree. The vocabulary is in [CONTEXT.md](./CONTEXT.md): **Menu**, **Menu list**, **Row**,
**Band**, **Pad**, **Page**, **Controls**.

This document is the operational spec: geometry, the tree, the interaction rules, the data the
lists read, and the split between the portable model and the LVGL view. It describes the contract
the firmware builds to: `components/portable/menu` with `t18_menu`, `cassette_menu.c`, the chrome
hooks in the five modules, and the shuffle mode of ADR 0023. The board pass is still owed: the
Pad's feel and the acceleration thresholds.

## What it is

- Opened by a tap on the **cassette button** of the mode bar, only while the Controls are out and
  the cassette has settled at the 134/256 pose, and not while a knob is in edit mode. On **Art**
  (the dark page) and on **Real** (the white one, with the Cover) alike.
- **Modal**: while it is up, the only live inputs are the Pad and the rows; the mode bar's buttons
  are inert. A down swipe on the screen does nothing and the up swipe closes the Menu rather than
  leaving the Level - a leftward swipe on the Pad is its Back; the group's cells are gone,
  replaced by the plate.
- Closed by an **up swipe**, by **the cassette button** again at any depth, or by the Pad's
  leftward swipe from the root. Its **Menu list** and **Band** are remembered until the app
  restarts, so the next tap returns where it left; the group it covered is remembered across the
  mode and reinstalled on close.
- Appears and disappears in **one frame**. The cassette container - tape, hubs, meter, glow and
  the Cover - is hidden for the Menu's lifetime and shown again on close; the Cover's wish is
  never changed, so it returns exactly as it was.
- Follows the **Page**: the Menu's own inks - the rows, the band, the page fill - have a
  white-page and a dark-page form, like the drawer, the block and the bands. The Pad's plate is
  the exception: white on both pages.

## Geometry

The Menu draws inside the controls' existing frame; it adds no rectangle.

| Part | x, y | size | notes |
|---|---|---|---|
| Screen | 0, 0 | 800x480 | the panel |
| Back cover | 16, 16 | 768x448 | the dark ground under the white page's margin |
| **K7 badge** | 16, 6 | 96x72 | a rounded plate holding the K7 mark; its right border is the divider, so the band's left border tucks under it |
| Header band | 110, 6 | 674x72 | the list's title |
| **Menu page** | 110, 76 | 486x284 | the existing window; its right edge meets the Pad at 596; 2 px border, 8 px corner; inner 482x280 with a 6 px inset for the rows; its fill is the Page's colour while the Menu is up and `k_col_bg` otherwise |
| **Rows** | 118, 84 | 470x53-54 each | five of them, inside the page's 6 px inset; they share its 268 px equally, so their edges round to whole pixels (54, 53, 54, 53, 54) |
| Footer band | 16, 358 | 768x116 | now playing on the left, the band's position on the right |
| Drawer buttons | 16, 76/170/264 | 96x96 each | P, `<`, `>` |
| **Pad** | 596, 76 | 186x284 | a white plate with a dark inner inset 8 px, and one dotted vertical guide, inside the DJ block's place |

Row internals, relative to the row:

| Part | x | notes |
|---|---|---|
| Band | 2 | inset 2 px on every side, 466x50 (49 on a 53 px row), radius 8, the selected row's own fill |
| Icon | 16 | 24x24 from the icon font, vertically centred |
| Label | 56 | Jura 28, vertically centred, at most 400 px (340 when the row carries a value), ellipsized |
| Current dot | 364 | 6 px, centred vertically, the row's own ink; the playing Track, or the walking Playlist |
| Chevron or value | right, inset 16 | the `angle-right` glyph in the icon font's 24 px for a list, or a value - a setting's, or a Playlist's declared count - in Jura 20 capitals (capped at 150 px); nothing for an action; the right edge is at 460 |

The header title is Jura 36 at screen x=126 and vertically centred.
The footer's first line (the song) is Jura 28 at (24, 21), the second (the group) at (24, 55);
the band's position, `3 / 1204`, is Jura 28 right-aligned at x=744 and vertically centred. With
nothing playing both footer lines are empty.

## The tree

```
MENU
├─ Browse        books           chevron   a directory of the Library tree
├─ Playlists     radio-waves     chevron   the card's Playlists
│  ├─ Library order  books                 the broadcast's own order
│  └─ <a list>       radio-waves  12 tracks   the walking one carries the dot
├─ Settings      settings        chevron
│  ├─ Backlight  brightness      AUTO / 100% / 60% / 30%
│  ├─ Rescan     endless-loop    RESCAN / RESCANNING... (inert while running)
│  ├─ Wi-Fi      radio-waves     chevron
│  └─ About      k7logo          chevron
│     ├─ Firmware   micro-chip       the app's version
│     ├─ Board      tablet-android   the board's short name
│     ├─ Partition  disc             the app partition's size
│     ├─ Format     radio-waves      the broadcast's container
│     ├─ Rate       radio-waves      its sample rate
│     ├─ Bitrate    radio-waves      its bit rate
│     ├─ State      books            the Library's state
│     ├─ Tracks     books            how many
│     ├─ Directories books           how many
│     ├─ Size       books            the total
│     └─ Truncated  books            whether the scan dropped rows
└─ Shuffle       toggle-off/on   OFF / ON  toggles Shuffle mode (ADR 0023)
```

| List | Rows | Source | Title |
|---|---|---|---|
| Root | 4 | fixed | `Menu` |
| Browse | the directory's children in the library's name order, directories and Tracks mixed | `library_child_count()` / `library_child()` / `library_kind()` | `Browse`, then `Browse /Jazz`, ... |
| Playlists | `Library order`, then the card's lists, or one note | `playlist_list()` read by the view; `playlist_select_current()` | `Playlists` |
| Settings | 4 | fixed | `Settings` |
| Wi-Fi | Setup, Share, Network, Password, Reset password, Address | fixed, `setup_status()` and the share window's deadline | `Wi-Fi` |
| About | 11 | `esp_app_get_description()`, the board adapter, the partition table, `mini_icecast_status_t`, `library_*` | `About` |

- A **directory Row** wears `cassette-tape` and opens the next Browse list; a **Track Row** wears
  `track` and plays. Inside Browse, the title is the path from the root, ellipsized.
- The **broadcast's current Row** carries a 6 px dot after its label, in the Row's own label ink
  (a shape, so it reads on both pages and on the band): a Track Row inside Browse, or the walking
  Playlist's Row. Activating a Track Row restarts the Track; activating a Playlist Row walks it.
- A **Track Row**'s label is the cleaned name: the library name without its extension,
  apostrophes (and the typographic U+2019) as spaces, parenthetical asides dropped, whitespace
  collapsed - the same string the Peek falls back to.
- **Playlists**: `Library order` is first and returns the broadcast to the library tree's own
  order; it wears `books` and carries the dot when no list walks. The card's lists follow in the
  file's name order (A-Z, byte order); each Row's right column is the list's declared row count,
  `12 tracks` / `1 track`, and a list with no rows is dimmed and inert
  ([playlist-format.md](./playlist-format.md), ADR 0095). The list being walked carries the dot;
  a Row is labelled with the file's name verbatim, ellipsized. A tap calls
  `playlist_select_set(card_root, lib, id)` - the command behind `POST /api/playlists/select` -
  with 0 for `Library order`; the choice is written to `k7.select` and survives a reboot
  ([front-door.md](./front-door.md), U3). A refusal, or a list whose rows the Library cannot
  resolve, leaves the walk as it was: the Row does not move and the panel has no message for it.
- **Playlists with no card or no lists**: with no card or no library, the root's Playlists Row is
  dimmed and inert. A card pulled while its list is open leaves one `No card` note; a usable card
  with no `k7.playlists/` directory leaves `Library order` and one `No playlists` note; a
  directory over the 64-list cap leaves `Library order` and one `Too many lists` note. The index
  is read when the Menu opens and again on a new generation; the model reads that index from its
  facts and never touches the card.
- **Settings** changes things only: Backlight, Rescan, Wi-Fi, About and Player. The Station and
  Library destinations are gone; their facts live in About. **Player** is how the player's audio
  is sent — `ESP-NOW` or `HTTP` — and it is the same choice the micro switch on the player will
  make (ADR 0104, `espnow-stream.md` E9): one transport at a time, remembered across a power cut.
  Choosing applies at once: the station stops and starts at that transport's format — 128 kbps
  for `ESP-NOW`, 256 for `HTTP` — so About's Bitrate follows the row without a reboot
  ([ADR 0106](../adr/0106-the-station-stops-and-starts-so-the-format-can-follow-the-transport.md)).
- **About** is the player's diagnostics page, eleven value Rows in three icon groups: Firmware,
  Board, Partition (the player); Format, Rate, Bitrate (the broadcast); State, Tracks,
  Directories, Size, Truncated (the library). Its values: the app's version, the board's short
  name (`8048S050C`), the app partition's size (`4 MB`); `MP3`, `44.1 kHz`, `128 kbps` (an em
  dash when the status is idle); the library's state name, `1,204 tracks` (`1 track`), `38 dirs`,
  `12.4 GB` (binary units, one decimal), `No`/`Yes`. The groups wear `micro-chip`,
  `tablet-android` and `disc`; `radio-waves`; and `books`. Eleven Rows do not fit the page, so
  About scrolls. Every right column is drawn in capitals and in the smaller Jura 20, whatever the
  model writes.
- **No card or no library**: Browse and Playlists are dimmed (40% opacity) and inert, and their
  lists hold one non-selectable Row that repeats the state - `No card` (no card, no library
  lease), `Library empty` (empty, or OK with zero Tracks), `Library error` (no directory or a
  read error) for Browse, and the Playlists notes above. Every other list works, so Rescan,
  Wi-Fi and About stay reachable. An empty directory inside a usable library is a blank Browse
  list, not a note.
- **Rescan**: a tap starts `app_services_rescan()`; the Row reads `Rescanning...`, is dimmed and
  ignores taps until the new generation lands. The Menu keeps its list; when the generation
  changes it re-resolves the Browse path against the new tree, pops to Browse's root if the
  directory has vanished, and re-reads the Playlists index; a selection whose list has gone falls
  back to the library's order. The Band clamps to the new count.
- **Backlight**: a tap cycles `Auto`, `100%`, `60%`, `30%` in place. Auto is the idle ladder the
  screen already runs; any other value pins the panel with a hold so the ladder does not
  overwrite it, and the choice lives until reboot. No NVS. A hold also keeps the panel lit: the
  idle dim and the screen-off step are skipped until Auto is chosen again, which is the cost of
  a brightness the user chose.
- **Shuffle**: the last root Row reads `mini_icecast_shuffle_enabled()` and toggles it; the order
  itself is the broadcast's (ADR 0023). The mode starts **On** at boot, so the first pass is
  shuffled without a Menu trip, and a toggle lasts until the next boot — the mode is not stored
  ([ADR 0091](../adr/0091-the-player-boots-shuffled.md)).

## The Pad

A white plate (`k_col_white`, 8 px corner) over the block's place, with a dark inner
(`k_col_bg`) inset 8 px and rounded by the same 8 px, and one dotted vertical guide inside the
inner, 66 px left of its centre and inset 66 px from its top and bottom. The guide is an
`lv_line` object: 2 px wide, a 2 px dash and a 4 px gap, rounded, in `k_col_white`. The whole plate is the one
surface - the inner and the guides are decorative, so a press on them falls through to it - and
it is white on both pages, with no page ink of its own. It is the right bar's panel value, not
a screen child of the Menu's: the bar builds the plate's carcase at the Pad's rect and deletes
the group's six cells when `open()` installs it. `close()` calls the view's restore hook, which
reinstalls the active group's panel (ADR 0026); the group's knob values, its meter and its edit
layer survive the swap, and an open edit ends with the panel, as any hidden cell's does.

- A **vertical drag** ratchets the list. At rest, **54 px of travel is one row** (the rows are
  53-54 px); a step is instant and the list rests only on row boundaries. A fast drag multiplies
  the rows per 54 px in **three stages, 1x / 2x / 4x**: 2x from 1200 px/s and 4x from
  2400 px/s, holding a stage for 150 ms before dropping back to 1x; a fast sample can jump
  straight to 4x. The constants live in the model and are tuned with a finger on the board.
- A **tap** that never became a drag (no move beyond the touch's drag limit) activates the
  **Band**.
- A **leftward swipe** is the Menu's Back: up one Menu list, and out of the Menu at the root.
  The press takes one axis, decided by its first 12 px of travel, so a swipe never ratchets and
  a drag never goes Back; the swipe counts once it has travelled 50 px, the screen's own gesture
  limit. The plate still bubbles no gesture, so the screen behind it never sees the swipe.
- The **Band** starts on the first visible Row of a list (or the remembered Row) and moves one
  Row per step in the drag's direction. At the last visible Row, the next down step shifts the
  window one Row and the Band stays on that edge Row; symmetric at the top.
- The ends **clamp**: a step past the first or last Row does nothing, and the window never leaves
  the list. There is no bounce and no partial Row.
- The list is **virtual**: five Row slots are rebound to the window's indices on each step; no
  object exists per Row and nothing is allocated per step. There is no LVGL scroll container,
  no spacer and no snap call.
- A **short list** (fewer Rows than the page) leaves blank space below its last Row; the Band
  cannot leave the list and the window never shifts.

## The door, and the mode bar

The Menu opens from the mode bar's **cassette** button, the left bar's top cell, and that button
**toggles**: a second tap closes the Menu from any depth, exactly as the up swipe does. Its disc is
lit while the Menu is open.

| Input | Action |
|---|---|
| the cassette button | open the Menu; tapped again, close it |
| a leftward swipe on the Pad | Back: up one Menu list; closes the Menu at the root |
| a tap on a Row, or a tap on the Pad's band | Enter |
| an up swipe | close the Menu |

The mode bar keeps its group meanings while the Menu is up and its group buttons are inert for the
mode's lifetime: the left bar never changes mode (ADR 0026). Back puts the Band on the Row that
opened the list it returns to (the Playlists Row, the Settings Row, the directory Row), which is
the one place the Band is set rather than remembered.

## Ink

| | White page | Dark page |
|---|---|---|
| Page fill | `k_col_white` (0x909090) | `k_col_bg` (0x111111) |
| Row label | `k_col_bg` | `k_col_primary` |
| Row icon | `k_col_bg` | `k_col_primary` |
| Band fill | `k_col_bg` | `k_col_primary` |
| Band label, icon, chevron | `k_col_button_white` (0xEEEEEE) | `k_col_bg` |
| Dimmed Row | 40% of the label ink | 40% of the label ink |
| Current Row's dot | the label ink | the label ink |

The band is the page's opposite, the same idiom as the M and R caps; unselected Rows take the
page's own ink. The Menu's page fill is the window's own fill recoloured for the Menu's lifetime:
the window is `k_col_bg` on both pages outside the Menu, because the cassette stands on it, and
while the Menu is up it takes the page's fill so the Rows have the page's ground. The Menu never
changes the page itself: the up swipe that closes it leaves the white or dark page as it found it.

## Data and lifecycle

- The view takes a **library lease** when the Menu opens and releases it when the Menu closes,
  and reads `k7.playlists/` into the index it hands the model. The portable model reads the tree
  through the pointer it is given and never touches the card.
- The model owns the state: the list kind, the Browse path (a relative path string, not a node),
  the Band, the window's first index, the ratchet's accumulator and stage, and the remembered
  list. It survives closing; the lease and the index do not.
- On a **generation change** while open, the model re-resolves its Browse path with
  `library_find()`; a path that no longer exists pops to Browse's root. Browse and the Playlists
  list rebuild from the new generation; a selection the card no longer holds has already fallen
  back to the library's order; the Band clamps.
- A **Track request** is a relative path: the view calls `mini_icecast_play(path)` for the
  broadcast to resolve, exactly as a Browse selection does today.
- A **Playlist request** is an id, 0 for the library's own order: the view calls
  `playlist_select_set(card_root, lib, id)`; a refusal leaves the walk as it was. The selection
  module stores the id in `k7.select` and applies the resolved order to the broadcast.
- Label text lives in five fixed buffers owned by the model (96 bytes each). The view copies a
  row's text into LVGL with `lv_label_set_text()` on every redraw rather than handing the buffer
  over as static text: LVGL 9's `LV_LABEL_LONG_DOT` writes its ellipsis into the label's copy, so
  static text cannot be ellipsized. The copies happen per redraw (a step, a list change), not per
  frame, and the library's strings are valid for as long as the lease is held.

## The model and the view

- **`components/portable/menu`** - the model: the tree, the navigation, the window, the Band, the
  ratchet, the row models and the name cleaning - `cassette_clean_name()`, the one function the
  Row label and the Peek's fallback share (the Menu passes `stem = true` to drop the file
  extension; the Peek's title keeps its dots). The Playlists list reads the index and the current
  id from the facts. It returns actions (`play this relative path`, `walk this Playlist`,
  `rescan`, `next backlight`, `toggle shuffle`, `close`) and needs no LVGL. Host-tested in
  `host/tests/t18_menu.c` through a small Library fixture and a Playlist index, as `cassette`
  and `cassette_layout` are.
- **`screen_display/cassette_menu.c`** - the view: five Row slots, the band, the Pad's panel
  value (its plate, inner and guide), the header and footer texts, and the executor for the
  model's actions. It reads the card's Playlist index when the Menu opens and on a new
  generation, executes a walk through `playlist_select_set()`, installs the Pad on the right bar
  and hides the cassette, and gates `screen_input` while open. On close it calls the view's
  restore hook, which reinstalls the active group's panel (ADR 0026).
- **`app_services`** exposes the mounted card's root - `app_services_card_root()` - so the view
  can call `playlist_list()` and `playlist_select_set()` without knowing how the card is mounted.
- **`cassette_bars`** carries the panel swap: `cassette_bars_set_panel()` is what puts the Pad up
  and a group back, and there is no covered state any more. **`cassette_buttons`** is the mode
  bar (the Menu's toggle and the two group selectors); **`cassette_header`** and
  **`cassette_footer`** gain their Menu texts.
- **`backlight`** gains the hold: a manual percent is kept and the idle ladder is skipped while it
  is set.
- **`mini_icecast`** gains the shuffle mode and its order (ADR 0023).

Repaint budget: a step that moves only the Band repaints two Rows - 2 x 470 x 54 x 2 = 101,520 B
painted, 203,040 B with the direct-mode sync copy; a step that shifts the window repaints the page
- 482 x 280 x 2 = 269,920 B painted. The cassette is hidden, so the 40 ms reel tick adds nothing
while the Menu is up.

## Icons and fonts

`xtape.ttf` carries twenty-five glyphs. The mapping:

| Codepoint | Glyph | Used by |
|---|---|---|
| U+F000 | `cassette-tape` | Browse directories; the mode bar's Menu door |
| U+F001 | `voice-mail` | spare |
| U+F002 | `radio-waves` | Playlists and Wi-Fi; About's Format, Rate and Bitrate |
| U+F003 | `toggle-off` | the Shuffle Row while shuffle is off |
| U+F004 | `toggle-on` | the Shuffle Row while shuffle is on |
| U+F005 | `settings` | Settings |
| U+F006 | `books` | Browse, `Library order`; About's library facts |
| U+F007 | `endless-loop` | Rescan |
| U+F008 | `shuffle` | spare |
| U+F009 | `disc-drive` | About's Partition |
| U+F00A | `tablet-android` | About's Board |
| U+F00B | `brightness` | Backlight |
| U+F00C | `fire-shield` | spare |
| U+F00D | `angle-right` | the Row chevron |
| U+F00E | `angle-left` | spare (the groups have no Back cell) |
| U+F00F | `k7logo` | About |
| U+F010 | `track` | Track Rows |
| U+F011 | `micro-chip` | About's Firmware |
| U+F012 | `sliders-v` | the mode bar's EQ group |
| U+F013 | `led` | the mode bar's RGB group |
| U+F014 | `prev` | the Home group's key 2 |
| U+F015 | `next` | the Home group's key 3 |
| U+F016 | `play` | the Home group's key 1 |
| U+F017 | `mic` | the Home group's key 4 |
| U+F018 | `pause` | spare: in the font, in no cut |

The Playlists list adds no glyph: `radio-waves` and `books` are already in the font.

`assets/fonts/xtape.ttf` is drawn in the icomoon project; `package.json` has a `font:xtape`
script beside `font:jura`, `font:jura28` and `font:jura20`. It generates each cut at its own
range, because one font is one pixel size and flash is what a glyph costs: `font_xtape_24.c`
(`--range 0xF000-0xF013 --size 24 --bpp 4`) for the Rows and the chevron, where a 24 px glyph
sits in a 53-54 px Row, and `font_xtape_36.c` (`--range 0xF000-0xF017 --size 36 --bpp 4`) for
the mode bar's and the groups' 64 px circles - the Home group's four among them - where 24 px
would read small against the Jura 36 letters they replace. A third cut, `font_xtape_logo_128.c`,
carries U+F00F alone, sized so the ink is 128 px tall, for the boot screen's mark (ADR 0045);
no Menu Row uses it. Row labels are Jura 28 and their right column is Jura 20 in capitals; the
footer is Jura 28 and the header Jura 36.

## Open items

- Whether the shuffle mode is shown anywhere outside the Menu (the Peek, the footer); this cut
  says nowhere.
- Whether the panel may create, rename or delete a Playlist. This cut says no (ADR 0096): the App
  authors the files and the panel only chooses which one walks.
- A refused walk has no on-panel message: the Row stays and the broadcast keeps what it had. The
  App reports the missing rows a list carries; the panel does not.
- About's Partition shows the app partition's size. The running image's size has no runtime
  field in `esp_app_get_description()`; until a source is added the row cannot read
  "1.70 MB of 4 MB".
- The acceleration thresholds, the hysteresis and the touch's drag limit: constants, tuned with a
  finger on the board.
- NVS for the backlight hold and the shuffle mode: deferred; both live until reboot. The
  shuffle mode's boot default is On (ADR 0091), so a reboot is shuffled again rather than
  remembering the last choice.

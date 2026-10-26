# An iPod-style menu for xtape: what the iPod actually does, and how to put it on a touch-only 800x480 panel — verified from primary sources (2026-10-22)

Axis: **the owner wants an "iPod-style menu" on a device that has no wheel — an 800x480 RGB565
panel, a GT911 pointer polled every 10 ms, one BOOT button, and a screen whose only input language
today is tap/swipe on the cassette. This note establishes what the iPod menu is from Apple's own user
guides and patents, compares three ways to map it onto this panel, checks the LVGL 9.6 widgets that
are actually vendored here, and gives one recommendation with layout, object tree, menu tree, memory
arithmetic and a phased plan.**

Everything local is read out of this repository and the vendored `managed_components/lvgl__lvgl/`
tree on this machine. Apple claims are cited to the model/year and page of the guide that owns them,
or to the patent number and claim. No board run was made while writing this; where a number can only
come from the panel it is listed in §4.4 as something still to measure.

Five kinds of statement appear below and they are never mixed:

- **MEASURED (board)** — printed by this firmware's telemetry on hardware, with the source note that
  recorded it. I did not re-run any of these.
- **MEASURED (link/source)** — read by me out of an artefact on this machine: the linker map, the
  built `.bin`, the generated font C file, the vendored LVGL source. The artefact is named.
- **DOCUMENTED** — a line of source, a Kconfig `help`, a header comment, or an Apple manual/patent
  passage, always cited by `path:line` or by document + page/claim.
- **ARITHMETIC** — a value computed here from DOCUMENTED or MEASURED inputs cited beside it. The
  multiplication is mine.
- **UNVERIFIED** — a claim that is widely repeated but that I could not trace to an Apple primary
  source in this pass. It is named as such and is not used to justify a decision.

Two files are rewritten by every build (`sdkconfig.sunton`, `build_sunton/config/sdkconfig.h`); they
are cited by the `CONFIG_…` symbol they carry, not by line number. Hand-written sources and the
vendored LVGL tree are cited by `file:line`.

**Working-tree note (2026-10-23).** Since this note was written, `cassette_view.c` has **three** Levels,
not four: Bare, Label cover and Artwork; the small cream label is an overlay on Bare, not a Level
(`components/app/screen_display/README.md:57-72`, `cassette_view.c:184-195`). The menu is therefore a
**fourth** Level, not a fifth, and the enum in §5.4 must be read with `LEVEL_LABEL` removed. Line
numbers in `cassette_view.c` have drifted by about 120 lines; the symbol names are authoritative, and
the follow-up note `docs/research/player-menu-scroll-and-coverflow.md` cites the current lines. The
chip section in §5.2 has been updated for the current tree: the label bar is now an overlay at
(96,324) 608x52, so the only remaining chip collision is the K7 cover title (the `COVER_TITLE_W`
720→664 fix still applies).

## Bottom line

1. **The iPod menu is four visual elements and two input rules.** The elements: a title bar with
   status icons; rows with one highlighted row (the "select bar"); an arrow on rows that open another
   menu; and a now-playing indicator. The rules: the wheel turns angular motion into linear selection
   movement at a speed-mapped rate, and Menu goes back one level (hold it for the root). Apple's
   guides document all of it except the scrollbar and the wrap behaviour (§1).
2. **Keep the input *model*, not the wheel.** The transferable idea is angular→linear with
   speed-mapped acceleration in stages, plus one button that means "select" and one that means
   "back". A drawn wheel big enough to target (a 280 px ring) spends ~20 % of the screen on a
   control a finger already does better by touching the row it wants, and squeezes the list into
   the rest (§2).
3. **Recommendation: direct-touch list, 6 rows of 56 px (7.6 mm), tap to select, vertical drag with
   snap-to-row, a 56 px back target in the title bar, and a now-playing strip that stays visible
   on every menu page.** Enter it from a 56 px "MENU" chip plus a 400 ms long-press; make it a fourth Level with a
   remembered return, not a coexisting overlay (§2.3, §5.4).
4. **Do not use `lv_list`:** in LVGL 9.6 it is deprecated as a whole
   (`include/lvgl/widgets/lv_list.h:25-30`) and its convenience row sets
   `LV_LABEL_LONG_MODE_SCROLL_CIRCULAR` (`src/widgets/list/lv_list.c:110`), which animates and
   invalidates every visible row forever. `lv_roller` is one label holding every option, and
   `lv_table` allocates a cell per row and scans from row 0 on every repaint (§3).
5. **Build it from `lv_obj` + absolute rows over a scroll container, with ~8 recycled row objects and
   a spacer for content height.** Object count is constant in the library size (10 vs 20 000 tracks
   cost the same), per-row cost is ~300–400 B of PSRAM by structure arithmetic, and the whole menu is
   ~25–30 objects / ~8–13 KB, against the **504,300 B** of PSRAM left after the artwork cover
   (MEASURED board, `README.md:601-603`).
6. **The expensive thing is not the objects, it is the invalidation.** A scrolling list repaints
   800x336 px = 537,600 B, and direct-mode double buffering copies every dirty area once more, so one
   scrolling frame is ~1.08 MB of PSRAM traffic — ~29x the cassette's current per-cycle invalidation
   (the 439x42 title) and ~80 % of the panel's own scan-out bandwidth at the programmed 18 MHz pclk.
   Prefer page snaps over free pixel scrolling until the board says otherwise (§4.2, §4.4).
7. **No new font and no new asset are needed for P0.** Oxanium 36 is already 39,861 B of glyph bitmap
   (MEASURED link) with a 42 px line height, and Montserrat 18/22/48 are already enabled. The image
   has **680,624 B** free in the 4 MB partition (MEASURED link), so a menu is not a partition
   problem; a second Oxanium size would be ~18 KB of glyph data and is optional (§4.3).
8. **The menu tree is mostly "exists today".** Playlist order, track names, resolving a relative path
   to a Track and playing it are all public API (`library.h`, `mini_icecast.h`); folder browsing,
   shuffle, rescan and backlight are small adapters; repeat, tag-based Artist/Album and BOOT-button
   navigation are new capability. Phase it P0/P1/P2 (§6).

---

## 1. The iPod menu, deconstructed

Two Apple user guides are used, both hosted by Apple, both fetched in full for this note:

- **iPod classic User Guide**, 58 pp., Apple, 2009 (the guide shipped with the 160 GB Late 2009
  model; Apple's product page is `https://support.apple.com/en-us/docs/ipod/132673`, the PDF is
  `https://cdsassets.apple.com/live/6GJYWVAV/user/ma1195_ipod_classic_160gb_user_guide.pdf`). Cited
  below as **classic 2009**.
- **iPod nano User Guide**, 104 pp., Apple, 2009 (5th generation, with FM radio, video camera and
  Cover Flow; product page `https://support.apple.com/en-us/docs/ipod/132643`, PDF
  `https://cdsassets.apple.com/live/6GJYWVAV/user/ma1194_ipod_nano_5th_gen_userguide.pdf`). Cited as
  **nano 2009**.

They disagree in places, and the disagreements are model facts, not errors: the classic puts
`Settings > Main Menu` and `Settings > Backlight` at the top of Settings, while the nano nests both
under `Settings > General` (classic 2009 p.8-9; nano 2009 p.10-11). The nano's menu anatomy also has
a **preview panel** the classic does not (nano 2009 p.9), and the nano's main menu has Radio and Video
Camera entries the classic cannot have (nano 2009 p.52, p.58).

### 1.1 The controls and the click-wheel contract

The classic's control surface is the **Click Wheel** with its **Center button**, plus **Menu**,
**Previous/Rewind**, **Next/Fast-forward** and **Play/Pause** (classic 2009 p.5; same set in nano 2009
p.5). The contract, in Apple's words:

> "Move your thumb lightly around the Click Wheel to select a menu item. To choose the item, press
> the Center button. To go back to the previous menu, press Menu on the Click Wheel."
> — classic 2009 p.5; the same instruction is in nano 2009 p.5.

The control table on the next page makes the rest explicit (classic 2009 p.6):

| Action | Control |
|---|---|
| Choose a menu item | "Use the Click wheel to scroll to the item and press the Center button to choose it." |
| Go back to the previous menu | "Press Menu." |
| Go directly to the main menu | "Press and hold Menu." |
| Access additional options | "Press and hold the Center button until a menu appears." |
| Browse for a song | "From the main menu, choose Music." |
| Play all songs in random order | "From the main menu, choose Shuffle Songs. You can also shuffle songs from the Now Playing screen." |

Three things follow that are easy to get wrong when copying the iPod:

1. **There is no "hold to scroll".** Acceleration is a function of *wheel speed*, not of holding a
   direction. Apple's own description: "You can scroll quickly through a long list of songs, videos,
   or other items by moving your thumb quickly on the Click Wheel", and the steps are "Move your
   thumb quickly … to display a letter of the alphabet on the screen … Lift your thumb momentarily
   to return to normal scrolling" (classic 2009 p.9; nano 2009 p.11-12). The mechanism is spelled out
   in the accelerated-scrolling patent (§1.5).
2. **Menu is a stack, and hold-Menu is a jump to the root.** Back is one level; the root is
   reachable only by holding the same button. That is a cheap two-target model that a touch UI can
   copy exactly: a back chip plus a "hold back = home" behaviour, or a back chip and a separate home
   row.
3. **Play/Pause is a hardware button, not a menu function.** The menu never owns transport; the
   wheel and the center button navigate, and the ring buttons keep working from any screen. On xtape
   the analogue is that the menu must not steal the transport gestures (last track/next track) that
   the cassette already has — see §2.4.

### 1.2 Screen anatomy

Apple's own display-item table (classic 2009 p.8; nano 2009 p.9 adds the preview panel):

| Display item | Apple's function text | Notes |
|---|---|---|
| Menu title | "Displays the title of the current menu." | classic 2009 p.8; on the nano the Lock icon "replaces the menu title" (nano 2009 p.9) |
| Play icon | "The Play (▸) icon appears when a song, video, or other item is playing. The Pause (❙❙) icon appears when the item is paused." | classic 2009 p.8 |
| Battery icon | "Shows the approximate remaining battery charge." | classic 2009 p.8; it is "in the top-right corner" (classic 2009 p.13) |
| Lock icon | Appears when the Hold switch is set to HOLD; "indicates that the iPod classic controls are disabled" | classic 2009 p.8 |
| Menu items | "Use the Click Wheel to scroll through menu items. Press the Center button to choose an item. An arrow next to a menu item indicates that it leads to another menu or screen." | classic 2009 p.8; nano 2009 p.9 |
| Preview panel | Album art/photos/storage for the selected row | nano 2009 only, p.9-10 |

The **selection band** is not named in either manual's table, but it is in the patent that owns the
interaction: the display presents "a visual indicator that is capable of linearly traversing through
the list of songs in order to designate a specific song", and the method claims "one of the items on
the list of items is distinguishly displayed from the other items in the list of items based on the
rotational user input, thereby providing the visual feedback" (US 7,348,967 B2, claim 1;
US 7,312,785 B2, claims 22-23). The patents call it a **select bar** (US 7,312,785 B2, FIG. 11 and
its description). So: a band is documented, its colour and gradient are not.

**The scrollbar is UNVERIFIED.** Neither guide's anatomy table lists one, and neither patent I read
claims one. Real iPods draw a thin vertical thumb at the right edge, but I could not trace that to an
Apple primary source in this pass, so this note does not justify any design decision by it. Treat
"iPod has a scrollbar" as product memory, not evidence.

### 1.3 Navigation rules

What is documented, in one place:

| Rule | Evidence |
|---|---|
| The main menu is the root; selecting a row either performs a function or opens another menu. | classic 2009 p.8: "When you turn on iPod classic, you see the main menu. Choose menu items to perform functions or go to other menus." |
| A row with an arrow opens a submenu or screen. | classic 2009 p.8; nano 2009 p.9 |
| Back is one level; hold-back is the root. | classic 2009 p.6 |
| The center button selects; holding it opens a context menu on the selected item. | classic 2009 p.6 |
| Long lists accelerate with wheel speed and show an alphabet letter while scrubbing; lifting the thumb returns to normal scrolling. | classic 2009 p.9 |
| Now Playing is a screen that appears when a song plays; center-button presses cycle through scrubber, Genius, shuffle, rating and lyrics. | classic 2009 p.24-25 |
| Playback continues while menus are navigated; the menu is not modal over audio. | classic 2009 p.7 (Hold switch section: "the song … continues to play"), and the play icon appears in every menu (p.8) |

**"Return to Now Playing" is not a documented navigation rule.** The guides say the Now Playing screen
*appears* when a song plays (classic 2009 p.24) and that the play icon is visible in every menu
(p.8), but neither says how to get back to it from a deep menu. That is the gap xtape's now-playing
strip fills deliberately: it is always on screen, so there is nothing to "return" to (§5.5).

What is **not** documented, and must be an explicit xtape choice rather than a copied "iPod rule":

- **Wrap vs clamp at list ends: UNVERIFIED.** Neither guide says whether the highlight wraps from
  the last row to the first. The wheel itself can rotate forever (that is its physical nature, and
  US 7,348,967 B2 claim 1 only claims "linearly traversing"), but whether the *selection* wraps is
  not stated. The existing xtape Level machine clamps and does not wrap
  (`components/app/screen_display/cassette_view.c:847-848`), so clamping is the consistent choice.
- **"Now Playing" as a main-menu row: UNVERIFIED.** Both guides describe the Now Playing *screen*
  and its elements, but neither lists it in the main menu. The documented main-menu entries are
  Music, Videos, Photos, Extras, Settings and Shuffle Songs (classic 2009 p.6, p.8-9, p.36, p.41).
- **Exact per-model main-menu contents: partly UNVERIFIED.** Both guides document that the user can
  add and remove main-menu items (`Settings > Main Menu`, classic 2009 p.8; `Settings > General >
  Main Menu`, nano 2009 p.10), so the list is a user setting and not a fixed anatomy. Say "the
  documented entries are …", not "the iPod menu is …".

### 1.4 Hierarchy, per model

The tree that can actually be cited from classic 2009:

```
Main menu (documented entries)
├── Music                                  classic 2009 p.6
│   ├── Cover Flow                         classic 2009 p.26-27, "Browsing music using Cover Flow"
│   ├── Playlists                          classic 2009 p.27-28, "Choose Music > Playlists"
│   ├── Genius Mixes                       classic 2009 p.7, "From the Music menu, choose Genius Mixes"
│   └── Songs                              classic 2009 p.8, "add a Songs item to the main menu,
│                                          so you don't have to choose Music before you choose Songs"
├── Videos                                 classic 2009 p.6
├── Photos                                 classic 2009 ch.5 (p.36)
├── Extras                                 classic 2009 ch.6 (p.41): Clocks, Alarms, Stopwatch, Games,
│                                          Screen Lock, Contacts, Calendars, Notes, Voice Memos
├── Settings                               classic 2009 p.8-9: Main Menu, Language, Backlight,
│                                          Brightness, Clicker, About, Reset Settings, …
├── Shuffle Songs                          classic 2009 p.6
└── (Now Playing)                          UNVERIFIED as a main-menu row; the screen itself is
                                           documented at p.24-25
```

The **Music submenu's full five-way list (Playlists / Artists / Albums / Songs / Genres) is
UNVERIFIED** in these two guides: only Cover Flow, Playlists, Genius Mixes and Songs are explicitly
documented. Artist and album browsing is documented from the *Now Playing* context ("Browse Artist",
"Browse Album", classic 2009 ch.3), not from the Music menu. This matters for xtape because its
Library has no tag index at all (§6), so the iPod's Artist/Album rows are the part that does *not*
transfer.

The nano 2009 adds model-specific entries that show how the structure flexes: Radio, Video Camera,
Genius Mixes, and a preview panel on the main menu (nano 2009 p.5, p.9). Structure survives model
changes; the leaves do not.

### 1.5 What the patents add

The two patents are the mechanism behind the manual's one-line descriptions, and they are the part
worth re-implementing on a touch panel.

**US 7,348,967 B2, "Touch pad for handheld device"** (Zadesky and Tan, Apple, granted 2008-03-25;
priority 2001-10-22). Claim 1 is a handheld music player with:

- "a display screen … configured to present a list of songs … and to present a visual indicator that
  is capable of linearly traversing through the list of songs";
- "a touch pad assembly … configured to track the arcuate motion of a finger sliding thereon";
- a controller that moves the indicator, where "the linear direction and speed of the movement of
  the visual indicator [are] based at least in part on the arcuate direction and speed of the finger
  sliding across the planar touch surface";
- "a button … configured to select the specific song designated by the visual indicator";
- "an audio feedback device … configured to provide audio feedback in conjunction with movement of
  the visual indicator".

Read the claims rather than the title: the invention is **not** "a wheel". It is
(a) angular input mapped to a linear list, (b) speed-preserving, (c) a separate select button,
(d) per-step audible feedback. Claim 9 claims transforming angular motion to linear motion; claim 13
claims the touchpad itself can select; claim 20 puts the button at the centre of the pad. Claims
18-19 claim "angular input areas … configured to receive input from a swirling finger motion".

**US 7,312,785 B2, "Method and apparatus for accelerated scrolling"** (Tsuk and Robbin, Apple,
granted 2007-12-25; priority 2001-10-22). The abstract states the model precisely:

> "A rotational user action … can provide accelerated scrolling. … The amount of acceleration
> provided can be performed in successive stages, and/or performed based on the speed of the
> rotational user action."

The claims that pin the behaviour down:

- **Claim 1**: receive a number of units from the rotational input, "determining an acceleration
  factor", "modifying the number of units by the acceleration factor", determine the next portion of
  the data set, present it — with visual feedback and audio feedback as the interaction happens.
- **Claim 7**: "the acceleration factor is dependent upon a rate of speed for the rotational user
  input".
- **Claim 9**: "the acceleration factor can successively increase to provided successively greater
  levels of acceleration". The drawing (FIG. 5) labels the stages as no acceleration / slow (2x) /
  4x / fast (8x).
- **Claim 13(b)-(d)**: determine the rotation speed, "applying acceleration when the speed of
  rotation is greater than a speed threshold", and "removing any acceleration being applied when the
  speed of rotation is less then the speed threshold".
- **Claim 16**: the increased acceleration is "an integer multiple of the current acceleration
  amount".
- **Claim 17**: a duration threshold before the acceleration stage changes again — i.e. the stages
  cannot ratchet up faster than a minimum time.
- **Claims 22-23**: one item is "distinguishly displayed" from the others as feedback, which is the
  selection band in patent language.

The transferable algorithm is therefore: **count input units, multiply by a stage factor that
depends on input speed (with hysteresis in time), then move the selection by that many rows.** On a
touch panel the "units" are pixels of vertical drag (or detents of a drawn wheel), and the same
multiplier/hysteresis can be reused unchanged. This is the one piece of iPod behaviour that is
cheap on this hardware and genuinely improves a 20 000-track list.

### 1.6 Claim → source table

| Claim | Source | Status |
|---|---|---|
| Ring + center + Menu/Prev/Next/Play layout | classic 2009 p.5; nano 2009 p.5 | DOCUMENTED |
| Wheel moves selection, center selects | classic 2009 p.5; nano 2009 p.5 | DOCUMENTED |
| Menu = back one level | classic 2009 p.5-6 | DOCUMENTED |
| Hold Menu = main menu | classic 2009 p.6; nano 2009 p.6 | DOCUMENTED |
| Hold center = extra options | classic 2009 p.6 | DOCUMENTED |
| Title bar, status icons, arrows on submenu rows | classic 2009 p.8; nano 2009 p.9 | DOCUMENTED |
| Selection band / "select bar" | US 7,312,785 B2 claims 22-23, FIG. 11 | DOCUMENTED (concept) |
| Speed-mapped acceleration, successive stages, time hysteresis | US 7,312,785 B2 claims 1, 7, 9, 13, 16, 17 | DOCUMENTED |
| Angular→linear mapping with speed preserved | US 7,348,967 B2 claims 1, 9 | DOCUMENTED |
| Center button selects the highlighted item | US 7,348,967 B2 claims 1, 13, 20 | DOCUMENTED |
| Audible click per movement | US 7,348,967 B2 claim 1; US 7,312,785 B2 claim 1 | DOCUMENTED |
| Alphabet scrubber while scrolling fast | classic 2009 p.9 | DOCUMENTED |
| Scrollbar on the right edge | — | UNVERIFIED |
| Wrap vs clamp at list ends | — | UNVERIFIED |
| Full Music menu (Artists/Albums/Genres) | — | UNVERIFIED |
| Now Playing as a main-menu row | — | UNVERIFIED |

---

## 2. Transfer to a touch-only 800x480 device

### 2.1 What the input surface actually is

This is the part that decides the mapping, so it is worth stating exactly what the hardware reports:

- **A GT911 on I2C0, polled every 10 ms** (`TOUCH_READ_MS 10`,
  `components/app/screen_display/touch_gt911.c:50-51`; README.md:554-558). The interrupt line is not
  used because it does not carry the touch on this board (`touch_gt911.c:1-26`).
- **At most two contacts per read** (`esp_lcd_touch_get_data(..., 2)`, `touch_gt911.c:133-137`), and
  LVGL is fed only contact 0 as the pointer (`touch_gt911.c:170-177`). The second contact is used
  only by the hand-written two-finger swipe detector: the midpoint of the first two points must move
  30 px, once per touch (`touch_gt911.c:53-56`, `:80-108`).
- **Sparse reporting while a finger is down**: "the controller reports sparsely while a finger is
  down, and leaves the last point in the driver between reports" (`touch_gt911.c:1-26`). This is why
  the read latches the last point and why the gesture velocity gate is set to zero
  (`touch_gt911.c:284-290`).
- **The first press after a dim only wakes the screen** and is withheld from LVGL entirely
  (`touch_gt911.c:145-168`).
- **LVGL's pointer gesture** recognizes one-finger direction once the summed movement exceeds 50 px
  (`LV_INDEV_DEF_GESTURE_LIMIT`, `include/lvgl/config/lv_conf_internal.h:1312-1316`), with the
  velocity gate disabled here. The two-finger *recognizer* is off
  (`# CONFIG_LV_USE_GESTURE_RECOGNITION is not set`, `sdkconfig.sunton`), which is why the adapter
  detects the two-finger swipe itself.
- **A drag suppresses the click.** On release, LVGL sends `LV_EVENT_CLICKED` only when no object was
  being scrolled; otherwise it sends `LV_EVENT_SCROLL_THROW_BEGIN`
  (`src/indev/lv_indev.c:1569-1581`). This is the single most useful fact for a direct-touch list: a
  tap selects, a drag scrolls, and the two do not fight.
- **Gestures bubble to the parent** when `LV_OBJ_FLAG_GESTURE_BUBBLE` is set, which it is by default
  for any object with a parent (`src/core/lv_obj.c:1503-1508`;
  `src/indev/lv_indev.c:1839-1844`). A menu row's vertical drag therefore still reaches the screen's
  gesture handler unless it is explicitly gated (§2.4).
- **Long-press is 400 ms by default** (`LV_INDEV_DEF_LONG_PRESS_TIME`,
  `Kconfig:1216-1219`; `lv_conf_internal.h:1288-1292`), and a long press suppresses the click
  (`src/indev/lv_indev.c:967-971`).

Physical size: the part is a "050" 5-inch-class 800x480 panel. No panel dimension is recorded in this
repository, so: at 5.0" diagonal, 800x480 gives ≈186.6 ppi, i.e. **1 px ≈ 0.136 mm** (ARITHMETIC from
the part number; the diagonal is not in the repo and must be confirmed against the panel datasheet).
That makes a 48 px row 6.5 mm, below the usual 7 mm minimum for a finger, and a 56 px row 7.6 mm.
This is why the spec in §5 uses 56 px rows.

### 2.2 Three mappings

**Option A — direct-touch list.** Rows are real objects; tap selects; vertical drag scrolls with
momentum and snap; the title bar carries a back target, and the left edge can carry an edge-swipe
back.

**Option B — on-screen click wheel.** Draw a ring (say 280 px diameter) plus a center button, either
as a dedicated half of the screen or as a floating control; rotational drag around the center emits
detents; center = select; a "Menu" target beside it = back.

**Option C — circular drag on the content / hybrid.** No drawn wheel; a circular drag anywhere over
the list is interpreted as a wheel turn (angle around the list's center), or the list is direct-touch
but a two-finger twist drives acceleration.

| Criterion | A: direct touch | B: drawn wheel | C: circular drag / hybrid |
|---|---|---|---|
| Discoverability with a finger | High — rows look like rows; tap is the first thing a finger tries | High — the ring is a picture of a wheel, but its mapping (drag *around* the ring, not on the list) has to be learned | Low — nothing on screen says "drag in a circle"; users will drag vertically and get nothing or a Level change |
| Hit target | The whole 800x56 row (≈109x7.6 mm) | Ring band ~56 px wide (7.6 mm) plus a center button; a rotational gesture needs the finger to stay on a 280 px circle | The list area, but the gesture is angular, so effective target is the circle's radius |
| Conflict with existing tap/swipe | Tap-to-select conflicts with tap-anywhere-skips; must be gated while the menu is open (§2.4) | No conflict inside its own region, but the vertical Level swipe and the horizontal track swipe still exist everywhere else; the wheel region steals a large rectangle from them | Direct conflict with the vertical Level swipe and with horizontal next/previous; needs a modal menu anyway |
| One-handed use on a landscape panel | Good — thumb reaches the near half of an 800 px row; the far end of a row is still selectable because the *row* is the target, not a small icon | Poor — the wheel must sit at one end (bottom or right), forcing the other hand to hold the device; the list is off to the side | Poor — same as B plus no visual anchor |
| GT911 / LVGL event model | Native: pointer press/move/release, row `LV_EVENT_CLICKED`, container scroll, snap, momentum, scrollbar all built in | Needs custom angular math from raw points (available), but LVGL gives no rotation gesture and no detents; all feedback (selection movement, click) is manual | Same custom math as B, on top of an object that also scrolls vertically; ambiguous at the start of a drag |
| Screen cost | Full 800x480 for content | ~250x300 px for the ring, i.e. ~20 % of the panel, plus the list must shrink to ~500 px wide | Full screen, but the gesture surface overlaps the list |
| Fits xtape's visual language | High — the list can be cream paper and tape red | Medium — a literal iPod wheel is the least "xtape" object possible | Low — invisible affordance |
| Implementation cost | Low: rows + scroll + snap + two callbacks | High: angular accumulation, dead zone, detent mapping, center hit test, drawn ring at 60/120 Hz | High, and the result is worse than A |

The patent's own claim construction is a warning about B: what Apple claimed is angular→linear with
speed, not the wheel as an ornament (US 7,348,967 B2 claim 1; US 7,312,785 B2 claim 1). Option A can
implement that claim's *behaviour* — speed-mapped movement — on a vertical drag (flick velocity →
rows per frame), and it can implement the click feedback (a short tick per row crossed) if the
hardware ever has a buzzer. The wheel shape adds nothing a finger needs.

Option C is rejected outright: the hardest part of a touch UI is teaching the gesture, and a circular
drag over a list has no visual affordance and directly collides with the existing vertical Level
swipe.

### 2.3 Recommendation

**Option A: direct-touch list.** Concretely:

- 6 rows visible, 56 px each, full width minus 8 px insets (784 px), in a 336 px viewport.
- Tap a row = activate it (open submenu / play track / toggle setting).
- Vertical drag = scroll, with `LV_OBJ_FLAG_SCROLL_MOMENTUM` (on by default,
  `src/core/lv_obj.c:1496-1502`) and `lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_START)`
  (`include/lvgl/core/lv_obj_scroll.h:38-44`, `:78-80`), so a flick lands on a row boundary.
- A 56 px back target at the left of the title bar; at the root it returns to the cassette Level it
  came from.
- A 56 px "MENU" chip at the top-right of the screen (above every cassette sheet, hidden while the
  menu itself is open) as the visible way in, plus a 400 ms long-press anywhere on the cassette as
  the shortcut.
- The now-playing strip stays at the bottom of every menu page and is itself a target that opens the
  Artwork Level (the existing Now Playing screen).
- Acceleration is the vertical-drag analogue of the patent: rows moved = drag pixels / row height,
  multiplied by a speed-dependent stage factor with a time hysteresis (2x, 4x, 8x; US 7,312,785 B2
  claims 7, 9, 13, 16, 17). This is optional in P0 — with 6 rows a page and a scrollbar, a plain
  flick is enough — but it is the cheapest way to make a 200-row folder usable without a letter
  index.

**What keeping the cassette gestures costs.** The cassette's input lives in three callbacks plus one
adapter hook, and every one of them has to be gated while the menu is open:

| Existing behaviour | Code | Cost to keep it |
|---|---|---|
| Tap anywhere = next track | `screen_clicked_cb`, `cassette_view.c:960-973` | One `if (s_level == LEVEL_MENU) return;` at the top. Without it, tapping a menu background row or the sheet would also skip a track. |
| One-finger left/right = next/prev | `screen_gesture_cb`, `cassette_view.c:911-958` | Same guard. Without it, a horizontal drag inside the list (which LVGL may classify as a gesture after 50 px) would change tracks. |
| One-finger up/down = Level | `screen_gesture_cb`, `cassette_view.c:924-956` | Same guard. Without it, a vertical list drag would change Levels *and* scroll. The list's own scroll does not stop the gesture from bubbling (`src/indev/lv_indev.c:1839-1844`). |
| Two-finger swipe = reserved for the menu | `screen_two_finger_cb`, `cassette_view.c:894-901`; adapter `touch_gt911.c:80-108` | The current callback only latches the gesture and logs "reserved for the menu"; take it as the global menu toggle (open outside, close inside). It is the only gesture that is unambiguous and already plumbed, so this replaces a no-op rather than changing the touch adapter. |
| BOOT button = next track | `components/app/appliance/appliance.c:193-207` | Nothing to do in P0 — but note it keeps skipping tracks while the menu is open, because it is polled outside LVGL. Routing it to the menu is P2 new code. |

The menu sheet itself must **swallow** background taps: make the sheet `clickable` with a no-op
`LV_EVENT_CLICKED` handler, so a tap that misses a row does not fall through to the screen. The rows
are clickable and the list is scrollable, so taps on rows and drags on the list never reach the
screen's handlers anyway — except via gesture bubbling, which is why the guard is still needed.

One ordering detail: `make_decorative()` (`cassette_view.c:242-249`) walks the tree at creation time
and makes every existing object non-clickable so the screen is the single hit target. Its own comment
says "A future control is created after this walk and stays clickable, so it takes its own press"
(`cassette_view.c:237-241`). The menu objects must therefore be created *after* `make_decorative()`
(or simply be exempted), or every row will be dead.

### 2.4 Event model as a diagram

```
GT911 (10 ms poll)
  └─ touch_read() ── contact 0 ──────────────► LVGL pointer
                     └─ two contacts ───────► s_two_finger_cb (§2.3 guard)

LVGL pointer
  ├─ press on a row ──► row PRESSED/RELEASED/CLICKED ──► activate(index)
  │                       (CLICKED is suppressed if the list scrolled, lv_indev.c:1569-1581)
  ├─ drag on the list ─► LV_EVENT_SCROLL on the list ──► rebind visible rows
  ├─ drag ≥ 50 px ────► LV_EVENT_GESTURE on the row ──► bubbles to screen ──► gated when menu open
  └─ press ≥ 400 ms ──► LV_EVENT_LONG_PRESSED on the screen ──► open menu (gated outside LEVEL_MENU)

Screen (cassette_view.c)
  ├─ screen_clicked_cb  ── guarded: skip track only outside LEVEL_MENU
  ├─ screen_gesture_cb  ── guarded: transport/Level only outside LEVEL_MENU
  └─ screen_two_finger_cb ── inside the menu: return to the cassette; outside: K7 toggle
```

---

## 3. LVGL 9.6 implementation options

The vendored tree is 9.6.0 (`dependencies.lock`); all line numbers below are from
`managed_components/lvgl__lvgl/`.

### 3.1 `lv_list` — deprecated, and a trap for this use

Three independent problems, each proved from source:

1. **It is deprecated as a whole.** The public header says: "The `lv_list` widget is deprecated and
   kept only for backward compatibility. A list is just a flex container with a column flow, so build
   one directly from `lv_obj` + a `LV_FLEX_FLOW_COLUMN` layout instead."
   (`include/lvgl/widgets/lv_list.h:25-30`). Every API carries `LV_DEPRECATED` and logs
   `LV_LOG_DEPRECATED` at runtime (`src/widgets/list/lv_list.c:69`, `:81`, `:94`, …).
2. **Its rows animate forever.** `lv_list_add_button()` sets the row label to
   `LV_LABEL_LONG_MODE_SCROLL_CIRCULAR` (`src/widgets/list/lv_list.c:110`). On this panel that is a
   per-frame invalidation for any title wider than the row — exactly the behaviour the cassette's
   scrolling title already costs (§4.2), multiplied by every visible row.
3. **It has no selection model and no virtualization.** It is a flex column of
   `lv_button`+`lv_image`+`lv_label` (`:90-113`); every item is an object, and the "selection" is
   whatever the theme's button pressed state happens to look like. For 20 000 tracks it allocates
   20 000 buttons.

Verdict: **do not use it.** Building a flex column directly costs the same code and keeps the label
mode under our control.

### 3.2 `lv_roller` — one label, one string, no per-row hit test

A roller is a single `lv_label` whose text is all options separated by `\n`
(`src/widgets/roller/lv_roller.c:365-368`, `:276-280`), sized to the visible row count
(`:241`), with the selection drawn as an overlay band (the widget's `draw_main`). Infinite mode
duplicates the option list until the label is at least 1000 px tall
(`EXTRA_INF_SIZE 1000`, `:31`; mode handling `:181-198`, `:253-258`).

For xtape:

- 20 000 tracks × ~40 B per line is ~800 KB of text in one label, plus LVGL's line layout over the
  whole string; the 504 KB of free PSRAM after artwork does not hold it.
- A tap cannot select a row: the roller maps drag to an index, and a tap on a visible option does
  nothing (the whole object is the spinner). That is the opposite of the direct-touch requirement.
- It could be used for a small Settings value picker (e.g. backlight presets, 3 options) — that is a
  legitimate, small use, not for the library.

### 3.3 `lv_table` — O(rows) memory and an O(rows) draw scan

`lv_table` stores a `row_h` entry per row and a `cell_data` pointer per cell
(`src/widgets/table/lv_table.c:232`, `:130-140`), so a 20 000-row, 1-column table is ~120 KB of
metadata (40 KB of row heights at 2 B each plus 80 KB of cell pointers at 4 B each) plus the text of
every cell. Worse, `draw_main()` starts at row 0 and walks rows until the
clip area is passed:

```c
for(row = 0; row < table->row_cnt; row++) {
    ...
    if(cell_area.y1 > clip_area.y2) break;
```
(`src/widgets/table/lv_table.c:729-735`; the same walk exists in `refr_size_form_row()`, `:283`).

So a repaint of a list scrolled to row 19 000 does 19 000 iterations before drawing 6 rows, and
changing one cell can re-measure every row after it. It is the wrong primitive for a large library.

### 3.4 `lv_obj` + absolute rows over a scroll container — the chosen primitive

The plain object gives everything the menu needs, all of it already on in this build:

| Capability | Where | Note |
|---|---|---|
| Absolute placement | `lv_obj_set_pos(row, 0, i * 56)`; if the list ever gets a layout, every row and the spacer need `LV_OBJ_FLAG_IGNORE_LAYOUT` (`include/lvgl/core/lv_obj.h:70`) | the list itself stays layout-free: a flex column would lay the recycled rows out in order and override the `index × 56` positions, which is exactly wrong for a windowed list |
| Scrolling, momentum, elastic | on by default: `scrollable`, `scroll_elastic`, `scroll_momentum` all set in the constructor (`src/core/lv_obj.c:1496-1502`) | flags documented in `include/lvgl/core/lv_obj.h:57-62` |
| Scroll direction | `lv_obj_set_scroll_dir(list, LV_DIR_VER)` | a diagonal finger must not pan the list sideways; the title bar and strip are `lv_obj_set_scrollable(..., false)`, so only the list moves |
| Snap to a row | `lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_START)` (`include/lvgl/core/lv_obj_scroll.h:38-44`, `:78-80`); children are snappable by default (`snappable = 1`, `lv_obj.c:1497`) | gives the iPod's row-at-a-time landing without custom physics |
| Scrollbar | `lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO)`; the bar is drawn as `LV_PART_SCROLLBAR` of the same object, no extra object | style it with `bg_color`, `width`, `radius` |
| Text truncation | `lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS)` (`include/lvgl/widgets/lv_label.h:49-55`) | no animation; `CLIP` is the alternative; `SCROLL_CIRCULAR` is what `lv_list` forces |
| Text without a copy | `lv_label_set_text_static()` (`include/lvgl/widgets/lv_label.h:132`) | useful only if the source string outlives the row; for the library it does **while a lease is held** (the arena is immutable within a generation), so hold `library_acquire()` for as long as the menu shows those strings and release on exit |
| Focus/keypad later | `lv_group` + `LV_KEY_UP/DOWN/ENTER/ESC` (`include/lvgl/core/lv_group.h:24-34`) | see §3.7 |

**Virtualization.** LVGL computes scroll limits from children extents, so a windowed list needs one
invisible spacer child whose height is `N × 56`; rows are positioned at `index × 56` in content
coordinates and only the ~8 visible ones exist. On `LV_EVENT_SCROLL`, compute the first visible index
from `lv_obj_get_scroll_y(list)` and rebind the row labels (and their row indices) to that window.
This is the standard "recycle rows" pattern and it makes object count independent of `N`.

Do **not** put a flex layout on the list itself. Flex lays children out in order and overrides the
`index × 56` positions; if a layout is wanted for another reason, every row and the spacer must carry
`LV_OBJ_FLAG_IGNORE_LAYOUT` (`include/lvgl/core/lv_obj.h:70`). The simpler rule is: the list is
layout-free; flex (or fixed `x` positions) is used *inside* a row for the label and the chevron.

**Wrap does not exist here.** None of the primitives wraps a scrollable list: the scroll offset is
clamped to the content extents (`lv_obj_scroll_by_bounded`, `include/lvgl/core/lv_obj_scroll.h`), so
the list stops at its first and last row. A wrap would mean jumping the offset from the end to the
start, which is a design decision and not a widget option. Clamping matches the cassette Level
machine (`cassette_view.c:847-848`) and the iPhone-era list; the iPod's own behaviour is UNVERIFIED
(§1.3), so clamp is the defensible default.

The alternative — create all rows and let LVGL scroll them — is simpler and is fine up to a few
hundred rows (a large album), but at `LIB_DEFAULT_MAX_TRACKS 20000`
(`components/portable/library/include/library.h:31`) it is 20 000 objects × ~300 B = ~6 MB, which
does not exist. Use the windowed form from the start; the code is only ~30 lines more.

### 3.5 Custom draw — the fallback, and this repo's own idiom

If profiling shows the row objects themselves are too expensive, the same list can be one object with
an `LV_EVENT_DRAW_MAIN` callback that draws rects and labels for the visible rows only. This repo
already uses exactly that pattern for the tape line and the VU arcs
(`cassette_view.c:427-436`, `:460-485`), so it is a known quantity here. The costs of moving to it:
no per-row hit testing (compute the index from `y`), no automatic group focus, and the scroll
handling must be hand-written or kept on a transparent scroll container. It is the right choice only
if the measured frame cost of real rows is the bottleneck; the object arithmetic in §3.6 says it
probably is not.

### 3.6 Per-row cost, arithmetic

Sizes are computed from the vendored structures for a 32-bit ESP32-S3 with this configuration
(`CONFIG_LV_OBJ_STYLE_CACHE=y`, `CONFIG_LV_USE_OBJ_ID` off, `CONFIG_LV_USE_EXT_DATA` off):

| Allocation | Bytes | Source |
|---|---|---|
| `lv_obj_t` (class, parent, spec_attr, styles, 2 style-cache words, user_data, coords, flags, state) | 52 | `src/core/lv_obj_private.h:58-160`; `CONFIG_LV_OBJ_STYLE_CACHE` adds 8 B (`Kconfig:1307-1311`) |
| `lv_obj_spec_attr_t` (children, group, event list, scroll, padding, flags) | 56 | `src/core/lv_obj_private.h:32-58`; event list `src/misc/lv_event_private.h:53-62` + `lv_array_t` `src/misc/lv_array.h:34-41` |
| children array (capacity 4) | 16 | `LV_ARRAY_DEFAULT_CAPACITY 4`, `src/misc/lv_array.h:21-22` |
| `lv_label_t` (obj + text pointer + dot buffer + hint + selection + size cache + offset + long mode) | ~120 | `src/widgets/label/lv_label_private.h:31-60`; hint is 12 B (`Kconfig:2214-2218`), selection 8 B (`Kconfig:2210-2212`) |
| label text copy (typical row 20–40 chars) | 20–40 | `lv_label_set_text()` copies; `lv_label_set_text_static()` does not |
| 2–4 local styles: `lv_obj_style_t` entry + `lv_style_t` + property pairs | 60–100 | `src/core/lv_obj_style_private.h:27-33` (8 B/entry), `include/lvgl/core/lv_style.h:370-383` (12–16 B + heap array of prop/value pairs, 8 B each) |
| **per row, total** | **~300–400** | ARITHMETIC from the rows above |

Cross-check against the only board measurement of LVGL object cost in this repo: a 320x240 screen of
**123 objects held 54,276 B in 956 blocks**, i.e. **441 B/object on average** (MEASURED board,
`docs/research/radio-player-memory-and-lvgl.md:743-749`, also `docs/adr/0004-ui-memory-is-external.md:39-44`).
The arithmetic per-row figure is in the same range. For a menu of ~25–30 objects the total is
therefore **~8–13 KB**, all PSRAM.

### 3.7 `lv_group`, keypad and encoder — for a future BOOT-driven menu

Groups exist regardless of a Kconfig switch (`lv_group.c` is core). What matters:

- `lv_group_add_obj()` / `lv_group_focus_obj()`; `lv_group_set_wrap(group, true/false)` with the
  default **wrap on** (`src/core/lv_group.c:67`, `:353-356`).
- `LV_KEY_UP/DOWN` move focus and `LV_KEY_ENTER` activates
  (`include/lvgl/core/lv_group.h:24-34`); the group sends the key to the focused object's event
  callback, so a row's `LV_EVENT_KEY` can implement select, and `LV_KEY_ESC` can mean back there.
- An encoder input device (`LV_INDEV_TYPE_ENCODER`) also drives a group; a keypad
  (`LV_INDEV_TYPE_KEYPAD`) drives it with arrow keys. The BOOT button is a single GPIO polled in
  `appliance.c:193-207`, not an LVGL keypad, so using it would mean either creating an
  `LV_INDEV_TYPE_KEYPAD` whose read callback returns one key (`LV_KEY_NEXT`/`LV_KEY_ENTER`) or
  calling a view function directly.

For the recommended windowed list, **do not put the recycled rows in a group**: focus would move
onto an object that is about to be rebound to a different track. Keep a logical `sel` index in the
menu model and let a key handler move it and scroll it into view
(`lv_obj_scroll_to_view(row, LV_ANIM_ON)`, `include/lvgl/core/lv_obj_scroll.h:241`). That is a
smaller change than fighting LVGL focus with recycled objects, and it works identically for a
keypad, an encoder, or the BOOT button later.

### 3.8 Object tree and events for the recommended approach

```
screen (lv_screen_active; clickable; handlers at cassette_view.c:1008-1016)
├── cassette sheets/objects (existing; hidden while LEVEL_MENU is settled)
├── s_menu_button            56x56  "MENU" chip, top-right, hidden while LEVEL_MENU is settled
└── s_menu_sheet             800x480, cream, clickable (no-op CLICKED swallows background taps)
    ├── s_menu_titlebar      800x56  cream; 2 px tape-red bottom rule
    │   ├── s_menu_back      56x56   "<" (Oxanium 36), left
    │   ├── s_menu_title     label    Oxanium 36 ink; "MUSIC", "TRACKS", …
    │   └── s_menu_count     label    Montserrat 22, right; "12 / 64" or the menu path
    ├── s_menu_list          800x336 scrollable, snap_y START, scrollbar AUTO, clip
    │   ├── s_menu_spacer    1x(N*56), transparent  (content height for scroll limits)
    │   ├── s_menu_row[0..7] 784x56 clickable; index in user_data
    │   │   └── s_menu_row_label  Oxanium 36, LONG_MODE_DOTS, x=16
    │   └── (LV_PART_SCROLLBAR is a part of s_menu_list, not a child object)
    └── s_menu_strip         800x88  tape black; clickable → LEVEL_ART (Now Playing)
        ├── s_menu_strip_title   Montserrat 22 cream
        ├── s_menu_strip_meta    Montserrat 18 muted; "Track 12 of 64 · 04:12 / 06:30"
        └── s_menu_strip_progress  lv_bar or 4 px box, tape red, width = elapsed/duration
```

Events it needs:

| Object | Event | Action |
|---|---|---|
| `screen` | `LV_EVENT_LONG_PRESSED` | open the menu (store the current Level as the return Level); ignore when already `LEVEL_MENU`, or a long press on the menu background re-enters it |
| `s_menu_button` | `LV_EVENT_CLICKED` | open the menu |
| `s_menu_back` | `LV_EVENT_CLICKED` | pop one menu level; at the root, `set_level(return_level)` |
| `s_menu_sheet` | `LV_EVENT_CLICKED` | swallow (no-op) |
| `s_menu_sheet`, title bar, strip | — | `lv_obj_set_scrollable(..., false)`; only `s_menu_list` scrolls, so a drag on the bar or the strip cannot pan the sheet |
| `s_menu_row[i]` | `LV_EVENT_CLICKED` | activate the row's node (open dir / play track / toggle) |
| `s_menu_list` | `LV_EVENT_SCROLL` | rebind the window (`first = scroll_y / 56`) |
| `s_menu_list` | `LV_EVENT_SCROLL_END` | snap is automatic; optional click tick per row crossed |
| `s_menu_strip` | `LV_EVENT_CLICKED` | `set_level(LEVEL_ART)` |
| `screen` | `LV_EVENT_GESTURE` | gated: ignored while the menu is open |
| `screen` | `LV_EVENT_CLICKED` | gated: ignored while the menu is open |
| `screen` | two-finger callback | toggle the menu: open it from the cassette, close it from the menu |

---

## 4. Rendering and memory budget

### 4.1 The baseline, with one correction to an earlier note

| Item | Value | Source |
|---|---|---|
| Panel | 800x480 RGB565, **pclk 18 MHz**, 2 framebuffers in PSRAM, direct mode, `avoid_tearing` | `components/app/screen_display/panel_rgb.c:32`, `:73-80`, `:107-122` |
| Panel frame | 410,000 pclk/frame ÷ 18 MHz = **22.78 ms**, 43.9 fps; scan-out 768,000 B / 22.78 ms = **33.72 MB/s** | ARITHMETIC from `panel_rgb.c:32` and the porch numbers in `docs/research/panel-rgb-lvgl-performance.md:209-212` |
| LVGL refresh | the code sets the refresh timer to `UI_FRAME_MS` = **40 ms** on both paths | `components/app/screen_display/screen_display.c:61`, `:342-343`, `:356-357` |
| Screen task | core 0, priority 3, 16 KB stack, rest = max(40 ms, 3x frame), capped at 500 ms | `screen_display.c:280-300`, `:411-430` |
| Last board FPS observation | **15 FPS**, 2026-10-17, at the then-current 33 ms period | MEASURED (board), `docs/research/panel-rgb-lvgl-performance.md:73` |
| Colours | 16-bit, `CONFIG_LV_COLOR_DEPTH_16=y` | `sdkconfig.sunton` |
| LVGL heap | custom allocator, PSRAM first | `components/app/screen_display/lvgl_mem.c:35-40`; `docs/adr/0004-ui-memory-is-external.md` |
| PSRAM after artwork | **504,300 B free** (971,248 B before the 464,310 B cover buffer) | MEASURED (board), `README.md:601-603` |
| Encoder | 44.6 % of one core | MEASURED (board), `docs/adr/0004-ui-memory-is-external.md:42` |
| Image / partition | 3,513,680 B in a 4,194,304 B factory partition | MEASURED (link/source): `ls -l build_sunton/sd_stream_server.bin`; `partitions.csv:8` |

**Correction:** `docs/research/panel-rgb-lvgl-performance.md:64` and `:222` say the panel runs at
16 MHz and cite `panel_rgb.c:31`. The current file says `PCLK_HZ 18000000` at line 32, and
`CONFIG_LV_DEF_REFR_PERIOD` is 33 in `sdkconfig.sunton` while `screen_display.c` overrides the timer
to 40. The old note's 25.625 ms / 39.02 fps figure is therefore stale; this note uses 22.78 ms /
43.9 fps. Everything else in that note about direct-mode copies, vsync and invalidation still holds.

### 4.2 Invalidation arithmetic

In direct double-buffered mode LVGL renders each invalidated area in place and then copies every
dirty area into the other framebuffer, so a changed pixel is written once and copied once
(`src/core/lv_refr.c:677-684`, `:782-784`; the mechanism is set out in
`panel-rgb-lvgl-performance.md` §4d). The menu's areas:

| Operation | Pixels | Bytes painted | Bytes with the direct-mode copy | Compared with |
|---|---|---|---|---|
| Selection band moves (2 rows) | 2 x 784x56 = 87,808 | 175,616 | **351,232** | the whole cassette title box per cycle is 439x42 = 36,876 B |
| One page of 6 rows (page turn, or entering the menu's list) | 800x336 = 268,800 | 537,600 | **1,075,200** | 29x the cassette's per-cycle title invalidation |
| Full-screen sheet swap (cassette ↔ menu) | 800x480 = 384,000 | 768,000 | **1,536,000** | the perf note's full-screen figure (`panel-rgb-lvgl-performance.md:993-997`) |
| Free scrolling at 25 Hz (if held) | — | — | **26.9 MB/s** | scan-out is 33.72 MB/s; the menu would add 80 % more PSRAM traffic |

(All ARITHMETIC from `panel_rgb.c:32`, `screen_display.c:61`, the direct-mode copy in `lv_refr.c`,
and the geometry in §5.2. The 25 Hz figure is the nominal `UI_FRAME_MS = 40`; the board's actual
cadence was last observed at 15 FPS.)

Four consequences:

1. **The latency floor is one refresh period, not one touch period.** The GT911 is read every 10 ms,
   but the panel only redraws on the 40 ms refresh timer, so a tap is visible one read plus one
   refresh later (10–50 ms nominal, 10–76 ms at the last observed 15 FPS). A drag is sampled four
   times per rendered frame; there is no point designing a 120 Hz interaction on this pipeline.
2. **A selection move is cheap** (0.35 MB, only on tap) and **a page turn is one expensive frame**
   (1.08 MB). That is affordable at 25 Hz because it happens once per gesture, not continuously.
3. **Free pixel-follow scrolling is the expensive mode**: 1.08 MB per frame, every frame, while the
   finger moves. On a panel that last reported 15 FPS and whose scan-out already owns 34 MB/s, this
   is the thing to avoid until measured. The recommended fallback is **page snapping**: let LVGL
   scroll with snap while the drag is short, but design the interaction so the common gesture is a
   6-row page flip, not a long continuous scrub.
4. **Entering the menu is a full-screen change** (1.54 MB), the same class of operation that made the
   300 ms sheet slide stutter (`components/app/screen_display/README.md:70-72`;
   `docs/adr/0013-artwork-level-and-case-window.md:93-94`).
   It must be a one-frame swap, exactly like the existing Level changes
   (`cassette_view.c:666-668`), not an animation.

### 4.3 Objects, fonts, flash, internal RAM

| Resource | Cost of the menu | Basis |
|---|---|---|
| LVGL objects | ~25–30 (title bar 3, back 1, list 1, spacer 1, 8 rows x 2, strip 3, chip 1) | object tree §3.8 |
| LVGL heap | ~8–13 KB PSRAM (441 B/object measured average; ~300–400 B/row arithmetic) | §3.6; 504,300 B free after artwork |
| New fonts | **none required**: Oxanium 36 (rows, title), Montserrat 22 (strip, counts), Montserrat 18/48 already enabled | `sdkconfig.sunton` `CONFIG_LV_FONT_MONTSERRAT_14/18/22/48`; `cassette_view.c:58`, `:916`, `:944` |
| Optional second Oxanium size (24 px) | ~17,700 B glyph data + ~3 KB glyph descriptors + the `lv_font_t`; at 4 bpp from the 36 px bitmap | ARITHMETIC from `font_oxanium_36.c:1-4` and the 39,861 B glyph bitmap (`build_sunton/sd_stream_server.map:91670`); area scales as (24/36)^2 |
| Font placement | the Oxanium 36 bitmap sits at `0x3c14c2e8`, inside the external-RAM data window: rodata is in PSRAM, so a new font costs PSRAM as well as flash | MEASURED (link), map `:91670`; `README.md:604-606` |
| New assets | **none**: the chevron and play glyphs are ASCII text (`<`, `>`, `\|\|`) or `lv_line`/`lv_draw_rect`; no bitmap is needed, and no symbol-font glyph is used (see the note under §5.2) | — |
| Flash | the image has **680,624 B free** (16.2 %); a menu is code, not data, and a second font (~25 KB) still fits | MEASURED (link) `build_sunton/sd_stream_server.bin` 3,513,680 B; `partitions.csv:8` |
| Internal RAM | **no change**: every LVGL allocation is PSRAM-first; the menu adds no static buffers and no task | `lvgl_mem.c:35-40`; `docs/adr/0004-ui-memory-is-external.md` |
| Encoder | no direct cost — the menu runs on core 0 (LVGL port at priority 4, screen task at 3), not on the encoder's core 1 | `panel_rgb.c:46-52`; `screen_display.c:411-430`; `mini_icecast.c` task placement in `panel-rgb-lvgl-performance.md:707-711` |
| What could force a partition change | nothing in P0/P1. Two more Oxanium sizes plus a large prepared asset could approach the 680 KB, which would then be a flash decision | ARITHMETIC |

The one asset-like question is the **selection band look**. It can be a rounded rect (LVGL draw,
already used everywhere in the cassette), a gradient (`lv_obj_set_style_bg_grad_color`), or a bitmap.
Use a rounded rect: no asset, no alpha, one style.

### 4.4 What must be measured on the board, not asserted

1. **A page turn and a drag-scroll, with the perf monitor on.** `CONFIG_LV_USE_SYSMON` is currently
   **off** (`sdkconfig.sunton`), so the label is not available in this build; enable
   `CONFIG_LV_USE_SYSMON` + `CONFIG_LV_USE_PERF_MONITOR` for the run and read `render | flush` and
   `refr_cnt` vs `redraw_cnt` (`lv_sysmon.c:358-375`, `:390-397`). What matters is whether one page
   turn stays under ~40 ms and whether a drag holds the cadence.
2. **Whether drag-scroll is usable at the 25 Hz request.** If a page turn already costs more than a
   frame on the board (the last observation was 15 FPS), drop drag-follow and use discrete page flips
   driven by a swipe. That is a one-evening change and it is the fallback this note recommends
   designing for.
3. **Tap latency and false gestures on 56 px rows.** The GT911 reports sparsely and the gesture
   threshold is 50 px; a finger that rolls 50 px while tapping a row could be classified as a swipe.
   Verify with 20 taps per row and a log of `LV_EVENT_CLICKED` vs `LV_EVENT_GESTURE`.
4. **Whether entering/leaving the menu glitches** like the old full-screen slide. It should be a
   one-frame hidden/shown swap; confirm visually.
5. **PSRAM free/largest after the menu is built.** `screen_display_lvgl_held()` is already logged
   (`screen_display.c:404-409`); add the menu and compare against 504,300 B.
6. **Internal free and largest unchanged** (the whole point of ADR 0004). Sample with the menu open.
7. **Encoder % and `starvation rebases` with the menu open.** The menu's cost is PSRAM bandwidth, not
   core 1 CPU, but the two share the bus; the soak script already reports both (`README.md:674-682`).
8. **Whether the user's real card has more than ~100 tracks.** The design (windowed list + scrollbar)
   is size-independent, but the P1 folder browse and any letter index depend on what the card holds.

---

## 5. Design integration

### 5.1 Three visual directions

All three borrow the iPod's *structure* — title bar, rows, one selection band, an arrow on rows that
open something, a now-playing indicator — and none borrows its chrome.

**A. Cream paper (recommended).** The menu is a full-bleed sheet of the same cream as the cassette's
label, with a thin tape-red rule under the title bar, black Oxanium rows, and a tape-red selection
band with white text. The now-playing strip is tape-black with cream text, so the bottom of the
screen reads like the cassette's own band. This is the closest to the physical object the device
already is; it is also the cheapest to draw (solid fills, one rounded rect for the band, text).

**B. K7 label sheet.** The menu floats as a cream label sticker inset 24 px on the cassette, with a
red header stripe and rounded 16 px corners; the cassette's reels and packs stay visible around the
edges. Strong identity, but the 24 px inset leaves a 752x432 sheet, about 320 px of list (5 rows),
and it still repaints nearly the whole screen when it changes; it also fights the cassette's own
label Level for the same metaphor.

**C. Tape black.** A dark `#2E1C19` sheet with cream text and a cream selection band with black text,
title bar in tape red. Reads as "the device's system menu" rather than "paper", and its contrast is
higher in daylight. Good as a second skin for
**Settings** (system) while Music stays cream (paper), which gives the hierarchy a visible cue
without a second font or asset.

**Recommendation: A for music/library screens, C for Settings.** B is the prettiest and the least
practical; keep it as a possible "album" header treatment later, not as the menu container.

### 5.2 Layout spec, 800x480 (direction A)

| Element | x | y | w | h | Fill / style | Type |
|---|---|---|---|---|---|---|
| Title bar | 0 | 0 | 800 | 56 | `#F4EABB`; 2 px `#E54820` bottom rule | — |
| Back target | 8 | 8 | 56 | 56 | transparent; `<` `#E54820` | Oxanium 36 |
| Menu title | 80 | 7 | 560 | 42 | `#444444` | Oxanium 36 |
| Count / path | 640 | 17 | 120 | 26 | `#8A7A62`, right-aligned | Montserrat 22 |
| List viewport | 0 | 56 | 800 | 336 | `#F4EABB`; clip; scrollbar `LV_PART_SCROLLBAR` | — |
| Row slot (i = 0..5) | 8 | 56 + i*56 + 2 | 784 | 52 | unselected: transparent | — |
| Selection band | 8 | 56 + i*56 + 2 | 784 | 52 | `#E54820`, radius 8 | — |
| Row label | 24 | row + 5 | 640 | 42 | `#444444`; selected `#FFFFFF` | Oxanium 36, `LONG_MODE_DOTS` |
| Chevron (submenu) | 752 | row + 5 | 32 | 42 | `#E54820`; selected `#FFFFFF` | Oxanium 36, `>` |
| Row count (right) | 680 | row + 15 | 64 | 26 | `#8A7A62`; selected `#FFFFFF` | Montserrat 22, right |
| Scrollbar | 792 | 60 | 4 | 328 | track `#D9C7A8` 40 %, thumb `#E54820`; `AUTO` | — |
| Now-playing strip | 0 | 392 | 800 | 88 | `#2E1C19` | — |
| Play glyph | 24 | 412 | 20 | 24 | `#E54820` (`>` playing, `\|\|` idle) | Montserrat 22 |
| Strip title | 56 | 402 | 600 | 26 | `#F4EABB` | Montserrat 22 |
| Strip meta | 56 | 430 | 600 | 22 | `#B9A88F` | Montserrat 18 |
| Progress line | 24 | 470 | 752 | 4 | track `#573534`; fill `#E54820`, width = (elapsed/duration) x 752 | `lv_bar` |

The entry chip is not part of the sheet: 56x56 at (736, 8), `#F4EABB` fill, 2 px `#E54820`
border, radius 8, the word `MENU` in Montserrat 18 `#E54820`. It is visible on the three cassette
Levels plus the label overlay on Bare and **hidden while `LEVEL_MENU` is open**, where the title bar's back target is the exit and
the count/path label owns the right end of the bar (x=640..760). It is the only always-visible menu
affordance on the cassette; the long-press is the shortcut.

**Collision check (the one existing layout this changes).** The K7 cover's title sits at (40, 8)
with width 720, so it ends at x=760 and would run 24 px under the chip
(`COVER_TITLE_X/Y/W`, `cassette_view.c:108-110`, applied at `:1081-1084`). `COVER_TITLE_W` must shrink
from 720 to **664**, which ends the scrolling title at x=704 and leaves a 32 px gap to the chip. The
alternative — an invisible 56x56 hit area with a small mark drawn over the title's tail — keeps the
full scroll width but puts the mark on top of moving text, which is worse. The small label is now an
overlay at (96,324) 608x52, not a Level (`cassette_view.c:82-85`), so the chip does not touch it; the
Artwork title (y=424, `cassette_view.c:121-122`) does not either. The only remaining overlap is the
chip over the Artwork Case's top-right corner, over the image, not over any text. The menu's own count
label never shares the bar with it because the chip is hidden there.

Checks: `56 + 336 + 88 = 480`; `6 x 56 = 336`; a 52 px band inside a 56 px slot leaves 2 px above
and below, so bands never touch; Oxanium 36 has a 42 px line height
(`components/app/screen_display/font_oxanium_36.c:5900-5901`), which fits 42 px inside the 52 px band.
The 56 px row is 7.6 mm at the assumed 186.6 ppi (§2.1); 48 px would be 6.5 mm and is rejected.

The band in the table is not a separate object: it is the selected row's own background style
(fill, radius and text colour), switched from the old row to the new one. That is why §4.2 counts two
rows of invalidation when the selection moves, and why the object tree stays at ~28 objects.

The row label width leaves room for a 64 px right-hand count; long names are dotted, not scrolled
(`LV_LABEL_LONG_MODE_DOTS`), because a scrolling row would invalidate itself every frame exactly like
the current title does. **This is a deliberate difference from the cassette**: the cassette's
scrolling title is acceptable because there is one of it; a list of six is not.

**Glyph note, because this is a trap.** None of the shapes above is a symbol-font glyph:

- the vendored `lv_font_montserrat_22` carries 62 codepoints in its sparse symbol range, and they
  do **not** include
  `LV_SYMBOL_PLAY` (0xF04B), `LV_SYMBOL_PAUSE` (0xF04C), `LV_SYMBOL_LEFT` (0xF053),
  `LV_SYMBOL_RIGHT` (0xF054) or `LV_SYMBOL_LIST` (0xF00B) — measured from its `unicode_list_1`
  (`managed_components/lvgl__lvgl/src/font/lv_font_montserrat_22.c:3147-3158`; the defines are
  `include/lvgl/font/lv_symbol_def.h:48`, `:108`, `:112`, `:128`, `:132`);
- Oxanium 36 is generated for `0x20-0x7F, 0xA0-0xFF, 0x2010-0x2015, 0x2018-0x201D, 0x2026`
  (`components/app/screen_display/font_oxanium_36.c:1-4`), which excludes `‹`, `›`, `≡` and `▸`.

The spec therefore uses ASCII `<`, `>`, `||` and the word `MENU`, all inside both ranges. A naive
`LV_SYMBOL_PLAY` or `›` would draw nothing and the bug would look like a layout problem.

### 5.3 Palette and type, with provenance

| Token | Hex | Where it already is |
|---|---|---|
| Paper | `#F4EABB` | `ui-player-800x480.html:13` (the cream UI text colour); the cassette label itself is `#FFFFFF` (`cassette_view.c:135`) |
| Screen backdrop | `#3C2B2A` | `cassette_view.c:131` |
| Tape black | `#2E1C19` | `cassette_view.c:133` |
| Tape red | `#E54820` | `cassette_view.c:136`, `:140` |
| Ink | `#444444` | `cassette_view.c:137` |
| Hub cream | `#F3F2EE` | `cassette_view.c:139` |
| Paper divider | `#D9C7A8` | **new constant**; the only new colour proposed (a 15 % shade between paper and ink) |
| Muted ink | `#8A7A62` | **new constant**; secondary counts only |
| Progress track | `#573534` | already used for the cassette's three lines (`cassette_view.c:993`) |

No new font: Oxanium 36 for rows/title/chevrons (existing), Montserrat 22 for the strip and counts
(existing), Montserrat 18 for meta (enabled in `sdkconfig.sunton`, currently unused by the view).

### 5.4 The menu as a fourth Level, and entry/exit

The screen already has a Level machine with exactly the property the menu needs: one sheet visible,
the others hidden, no animation (`cassette_view.c:184-195`, `:709-782`, `:845-...`). Add
`LEVEL_MENU` after `LEVEL_ART`:

```c
enum {
    LEVEL_BARE = 0,
    LEVEL_COVER,
    LEVEL_ART,
    LEVEL_MENU,          /* fourth Level */
};
#define LEVEL_MAX LEVEL_MENU
```

and a sheet (`s_menu_sheet`) with rest at (0,0) and hidden at y = -488, exactly like `LEVEL_ART`
(`cassette_view.c:709-731`). `level_settled()` then hides the cassette when the menu is up, and the
existing one-frame swap applies.

**Entry:**

1. A 56x56 "MENU" chip at (736, 8), created after `make_decorative()`, moved to the foreground above
   every cassette sheet and hidden while `LEVEL_MENU` is settled (the back target is the exit; the
   menu's count label owns the right end of its bar). It is the discoverable way in on the cassette.
2. A 400 ms long-press anywhere on the cassette (`LV_EVENT_LONG_PRESSED`, default 400 ms,
   `Kconfig:1216-1219`). This is the shortcut, and it works from any Level including Artwork. It does
   not collide with a tap (a long press suppresses `CLICKED`, `lv_indev.c:967-971`) or with a swipe
   (a movement past the 50 px gesture threshold is classified as a gesture, not a press).
3. Remember `s_return_level = s_level` before switching, so leaving the menu returns to the sheet the
   user was looking at.

**Exit:**

1. The back target in the title bar pops one menu level; at the root it calls
   `set_level(s_return_level)`.
2. A two-finger swipe toggles the menu: outside it opens the menu, inside it returns to the
   cassette. The current callback already reserves the gesture and only latches it
   (`cassette_view.c:894-901`), so this replaces a no-op rather than changing the touch adapter.
3. A right-swipe from the left 24 px edge is a P1 addition (it needs an edge test in the sheet's
   gesture handler, not the screen's).

**Why a Level and not an overlay.** An overlay has to solve the same problems (one visible surface,
input arbitration, a remembered return) outside the machine that already solves them, and it risks
two interactive trees at once. The Level is this repo's idiom — ADR 0013 added Artwork as a Level
for exactly this reason — and it gives the "return to where you were" behaviour for free. The
cost is that the menu must be a full-screen sheet (so entering is a 1.54 MB frame), which the
measurements in §4.2 say is acceptable once per gesture.

### 5.5 The now-playing strip

The strip is the iPod's "play icon + Now Playing is always reachable" idea, made concrete. It is a
child of the menu sheet, so it is visible on every menu page, not on the cassette: the cassette
already has its own title and band, and putting the strip above the cassette would be an overlay,
which §5.4 rejects.

- Left: `>` if `status.elapsed_ms` advanced since the previous update (a delta; the RMS
  `level_l/level_r` is zero during a silent passage and must not be used as a liveness test), `||`
  otherwise. The station has no pause (`README.md:961-962`), so the idle glyph is
  only for "no card / nothing playing".
- Middle: the **cleaned** title, which `cassette_step()` already computes for the cassette label
  (`components/portable/cassette/cassette.c:101-125`, `:176-184`), Montserrat 22, dotted. Do not use
  `status.title` directly: that is the raw ICY title, parenthetical asides included
  (`mini_icecast.h:93`).
- Right: `elapsed / duration` as `mm:ss`, from `status.elapsed_ms` / `status.duration_ms`
  (`mini_icecast.h:100-101`); when duration is 0, show only elapsed.
- Second line: `Track {track_index+1} of {library_count}` from
  `mini_icecast.h:102-104`, plus the format (`MP3 44.1k`) from `status.format` / `out_hz`.
- Bottom 4 px: progress from `status.elapsed_ms / status.duration_ms` (NOT the cassette model's
  permille, which is the tape's own progress and is smoothed differently).
- Tap: opens the Artwork Level (the existing Now Playing screen with the cover). That makes the strip
  the "return to Now Playing" affordance the iPod's center-button cycle provides.

The strip is updated by the same 25 Hz `cassette_view_update()` pass, but it must only touch its
labels when the values change (the existing `apply_frame` pattern, `cassette_view.c:530-573`), or it
becomes a per-frame invalidation of its own.

---

## 6. The proposed xtape menu tree

Ground truth: the Library is an immutable tree of directories and Tracks, sorted by name, built once
at boot; the Playlist is the depth-first track index; a Track is played by its relative path
(`mini_icecast.h:144`), and the path is resolved by the Library
(`components/portable/library/include/library.h:1-15`, `:107-132`). There is **no artist,
album, genre or tag index** — the tree is whatever directories the card holds (`library.h:1-15`),
and the status struct carries title/file/path/format, not tags (`mini_icecast.h:92-110`). There is no
pause or seek — the output is a live broadcast (`README.md:961-962`) — and the whole playback
control surface is play/skip/prev (`mini_icecast.h:141-147`), so there is no repeat either.

```
MENU (root)
├── Now Playing                    → the Artwork Level (cover + title)         exists today
├── Tracks                         → the Playlist order, windowed list         exists today
│   └── <track>                    → mini_icecast_play(relpath)                exists today
├── Browse                         → directories under Music                   small adapter
│   └── <dir>                      → children; recurse; Tracks at the leaves   small adapter
├── Shuffle                        → random Track, play it                     small adapter (semantics TBD)
├── Rescan                         → app_services_rescan()                     small adapter
├── Settings                       → (dark variant, direction C)
│   ├── Backlight                  → 100 / 60 / 30 %                           small adapter + ladder override
│   ├── Station                    → format, bitrate, library count, generation exists today (display)
│   ├── Library                    → state, tracks, dirs, bytes, truncated     exists today (display)
│   └── About                      → firmware/board/partition                 small adapter
└── Back to cassette               → set_level(return_level)                   exists today
```

Status of each leaf, with the source that decides it:

| Leaf | Status | Evidence / what is missing |
|---|---|---|
| Now Playing (screen) | **exists today** | the Artwork Level already shows the cover and title (`cassette_view.c:962-990`); `status.title` is in `mini_icecast.h:93` |
| Tracks (Playlist) | **exists today** | `library_track_count` (`library.h:102`), `library_track_at` (`:131`), `library_name` (`:111`), `library_relpath` (`:118`); play with `mini_icecast_play` (`mini_icecast.h:144`) |
| Play a Track | **exists today** | `mini_icecast_play()` resolves the path, checks it is a Track, queues it at the next chunk boundary (`mini_icecast.c:1046-1085`) |
| Current position | **exists today** | `status.track_index`, `library_count`, `generation` (`mini_icecast.h:102-104`) |
| Browse directories | **small adapter** | `library_root/parent/child_count/child/name/kind` (`library.h:107-113`), `library_relpath` (`:118`); the adapter is the menu's nav stack + row mapping. No new library code. |
| Shuffle | **small adapter** | there is no shuffle in `mini_icecast` (`mini_icecast.h:146-147` only skip/prev); a random `library_track_at` + `mini_icecast_play` is a few lines, but decide what "shuffle" means when the next Track auto-advances (it would leave shuffle mode) |
| Rescan | **small adapter** | `app_services_rescan()` (`app_services.h:65-70`); it starts a background scan that parks the broadcast, so the row needs a "working…" state |
| Backlight | **small adapter + override** | `backlight_set_percent()` (`backlight.h:26-31`) works, but `screen_task()` recomputes the idle target every pass (`screen_display.c:237-245`), so a manual setting is overwritten within 40 ms; either add an offset/override in `backlight.c` or a "hold" flag in the view |
| Station / Library | **exists today** (display) | `status.bitrate`, `out_hz`, `format`, `library_count`, `generation` (`mini_icecast.h:96-104`); `library_state`, `library_track_count`, `library_dir_count`, `library_total_bytes`, `library_truncated` (`library.h:95-105`) |
| About | **small adapter** | firmware/board/partition are not in `status` or `library`; they need `esp_app_get_description()` and the partition API (a few lines, no new state) |
| Repeat | **new capability** | the control surface is play/skip/prev only (`mini_icecast.h:141-147`); the broadcast consumes the Playlist linearly, so a repeat mode needs new playback state |
| Artist / Album (tag-based) | **new capability** | the Library has no tags; it would need ID3/FLAC tag parsing at scan and a secondary index, which is a much larger change than a menu |
| Search | **new capability** | no tag index and no text-entry font; a letter index over the Playlist is the cheap variant, not full search |
| Pause / seek / volume | **not applicable** | the stream is a live broadcast, cannot pause or seek (`README.md:961-962`); volume is client-side |
| BOOT button in the menu | **new capability** | the button is polled in `appliance.c:193-207` outside LVGL; routing it needs a view hook or an LVGL keypad device |

### 6.1 Phases

**P0 — smallest useful menu (target: one day of work plus board time).**
Fourth Level + title bar + back + 6-row windowed list + now-playing strip; root = Now Playing,
Tracks, Back. The Tracks list scrolls through the Playlist with a scrollbar; tap plays; strip updates from
`mini_icecast_status()`. Everything in P0 is "exists today" plus the view code and the gesture gates.

Risks: the gesture gates (one missed gate means a tap in the menu skips a track); the full-screen
swap stutter (must be one frame); stack/heap in the view (all PSRAM); **the Library lease** — the menu
is the first UI consumer of the Library, so it must hold `library_acquire()` for as long as it shows
`library_name()` pointers (or re-acquire per update) and release on exit. A rescan or card change can
publish a new generation and free the old arena; with `lv_label_set_text_static()` that dangles.
This applies to P0's Tracks list, not only to P1's Browse.

**P1 — library browsing and controls (3–5 days).**
Browse directories with a nav stack and the current path in the title bar; Shuffle; Rescan; Backlight
with a ladder override; Settings (dark variant); scrollbar + optional letter index for >100 tracks;
left-edge swipe back.

Risks: the nav stack must not keep raw `library_t` pointers across a rescan. Either hold a lease for
the menu's lifetime and release on exit, or re-acquire per render; a rescan publishes a new generation
and frees the old arena when its last lease drops (`library_acquire`/`release`, `library.h:85-90`). A
rescan while a directory is open must return to the root or re-resolve the path; a play from the
Browse list must use `library_relpath`, not a string built by hand.

**P2 — nice to have.**
Speed-mapped acceleration on the drag (the patent's stage multiplier); click feedback if a buzzer is
ever fitted; BOOT-button navigation via a group/keypad; repeat/shuffle modes (new `mini_icecast`
capability); tag index (new scan capability); an optional on-screen wheel skin for the owner's
nostalgia, behind a setting, not as the primary input.

Risks: every P2 item except acceleration touches a component outside the screen, and the menu itself
must not become the place where playback semantics are invented.

---

## 7. Open decisions for the owner

| # | Decision | Options | Recommended default | Why |
|---|---|---|---|---|
| 1 | Input model | direct touch / drawn wheel / circular gesture | **Direct touch** (§2.3) | finger-sized rows, no wasted screen, no new gesture to teach; the iPod's transferable part is the acceleration curve, not the ring |
| 2 | Menu container | fourth Level / overlay / separate screen | **Fourth Level with remembered return** (§5.4) | reuses the existing one-sheet machine and ADR 0013's precedent; overlay duplicates input arbitration |
| 3 | How much library to expose | flat Playlist only / folders / tags | **Folders + flat Playlist in P1**; tags are out of scope | the Library has dirs, not tags (`library.h:1-15`); tags are a scan-time project |
| 4 | Settings scope | none / backlight+info / full | **Backlight + Station + Library + About in P1**; repeat/shuffle later | backlight needs a small override; the info leaves are already in `status`/`library` |
| 5 | Entry gesture | MENU chip / long-press / two-finger / swipe from Bare | **MENU chip + 400 ms long-press, with two-finger as the global toggle** | chip is discoverable, long-press is conflict-free and works from any Level; the current code reserves two-finger for the menu, so it is free to take |
| 6 | Back affordance | title-bar chip / edge swipe / both | **chip in P0, edge swipe in P1** | the chip is the visible one; edge swipe is a convenience that costs a hit test |
| 7 | Where Shuffle leads | play now / set a mode | **play a random Track now** in P1, and say so in the row | the broadcast has no queue or mode; pretending it does would need new playback semantics |
| 8 | Menu look | cream paper / K7 label / tape black | **cream paper for music, tape black for Settings** (§5.1) | strongest xtape identity, cheapest draw; the dark variant separates system from music without a second font |
| 9 | Row density | 6 x 56 px / 7 x 48 px / 5 x 64 px | **6 x 56 px** | 7.6 mm target; 48 px is 6.5 mm and too small for a finger on this panel |
| 10 | BOOT button in the menu | ignore / route to select+next | **ignore in P0/P1, route in P2** | it is polled outside LVGL; a hook is a small but separate change |

---

## 8. What is proved, and what only the board can settle

### Proved, with the proof

| Claim | How it is proved |
|---|---|
| The iPod menu's controls and navigation are as described in §1.1-1.3 | classic 2009 p.5-9, 24-25; nano 2009 p.5-11, 33-34 |
| The selection band and the acceleration model are claimed in Apple's patents, not folklore | US 7,348,967 B2 claims 1, 9, 13, 20; US 7,312,785 B2 claims 1, 7, 9, 13, 16, 17, 22-23 |
| There is no hold-to-scroll in the iPod model; acceleration is speed-based with stages | classic 2009 p.9; US 7,312,785 B2 claims 7, 9, 13, 16, 17 |
| `lv_list` is deprecated in 9.6 and forces scroll-circular labels | `include/lvgl/widgets/lv_list.h:25-30`; `src/widgets/list/lv_list.c:110` |
| `lv_roller` holds all options in one label and grows it to 1000 px in infinite mode | `src/widgets/roller/lv_roller.c:31`, `:241`, `:276-280`, `:365-368` |
| `lv_table` allocates per-cell data and scans rows from 0 when drawing | `src/widgets/table/lv_table.c:130-140`, `:232`, `:729-735` |
| A drag suppresses the click, so tap-select and drag-scroll can coexist | `src/indev/lv_indev.c:1569-1581` |
| Gestures bubble from a row to the screen unless gated | `src/core/lv_obj.c:1503-1508`; `src/indev/lv_indev.c:1839-1844` |
| Scrolling/momentum/elastic are on by default on `lv_obj` | `src/core/lv_obj.c:1496-1502` |
| Snap-to-row and a scrollbar need no new object | `include/lvgl/core/lv_obj_scroll.h:38-44`, `:78-80`; `LV_PART_SCROLLBAR` |
| Per-row cost is ~300-400 B and the menu ~8-13 KB | §3.6 arithmetic; cross-checked against the measured 441 B/object (`radio-player-memory-and-lvgl.md:743-749`) |
| A scrolling list is ~1.08 MB of PSRAM traffic per frame | §4.2 arithmetic from direct-mode copies and 800x336x2 B |
| 504,300 B of PSRAM is free after the artwork, and LVGL is PSRAM-first | MEASURED (board) `README.md:601-603`; `lvgl_mem.c:35-40` |
| The Oxanium 36 bitmap is 39,861 B and lives in PSRAM | MEASURED (link) `build_sunton/sd_stream_server.map:91670` (`0x3c14c2e8`) |
| The image has 680,624 B free in its 4 MB partition | MEASURED (link) `.bin` 3,513,680 B; `partitions.csv:8` |
| The panel is programmed at 18 MHz, not 16 | `panel_rgb.c:32` |
| The current code asks for 25 Hz, not 30 | `screen_display.c:61`, `:342-343`, `:356-357` |

### Only the board can settle

1. The real frame cost of a page turn and of a drag-scroll, and whether drag is usable at all at this
   cadence (§4.4 items 1-2).
2. Tap latency and false gesture classification on 56 px rows with this GT911 (§4.4 item 3).
3. Whether entering/leaving the menu glitches like the old full-screen slide (§4.4 item 4).
4. The actual PSRAM headroom with the menu built and a cover decoded (§4.4 item 5).
5. Whether the menu's PSRAM traffic perturbs the broadcast/scan-out (§4.4 item 7).
6. Whether a manual backlight setting can survive the idle ladder without a `backlight.c` change
   (§6, "Backlight").
7. The physical panel diagonal, hence the mm figures for hit targets (§2.1); the part number says
   5-inch class, the repo does not record it.

### What this note could not verify

- Whether the iPod's selection wraps at the ends of a list (UNVERIFIED; this note assumes clamp, to
  match the existing Level machine).
- That the iPod draws a scrollbar (UNVERIFIED; not in the guides' anatomy tables, not in the patents
  read).
- The full Music submenu (Artists/Albums/Genres) and "Now Playing" as a main-menu row (UNVERIFIED in
  the two guides read; both are model/software-version facts, and the guides show the main menu is
  user-editable).
- The exact look of the selection band (only its existence and feedback role are claimed).
- Any click-wheel detent count or haptic behaviour (not claimed in the documents read).

---

## 9. The artefacts cited

### Local, by path

- This repository: `components/app/screen_display/cassette_view.c`, `screen_display.c`,
  `touch_gt911.c`, `panel_rgb.c`, `backlight.h`, `lvgl_mem.c`,
  `components/portable/library/include/library.h`, `components/portable/mini_icecast/include/mini_icecast.h`,
  `components/portable/mini_icecast/mini_icecast.c`, `components/portable/app_services/include/app_services.h`,
  `components/app/appliance/appliance.c`, `partitions.csv`, `sdkconfig.sunton`,
  `build_sunton/sd_stream_server.bin`, `build_sunton/sd_stream_server.map`,
  `components/app/screen_display/font_oxanium_36.c`, `ui-player-800x480.html`,
  `README.md`, `docs/adr/0004-ui-memory-is-external.md`,
  `docs/adr/0013-artwork-level-and-case-window.md`,
  `docs/research/panel-rgb-lvgl-performance.md`,
  `docs/research/radio-player-memory-and-lvgl.md`.
- LVGL 9.6.0, vendored: `managed_components/lvgl__lvgl/` — `Kconfig`,
  `include/lvgl/widgets/lv_list.h`, `include/lvgl/widgets/lv_label.h`,
  `include/lvgl/core/lv_obj.h`, `include/lvgl/core/lv_obj_scroll.h`,
  `include/lvgl/core/lv_group.h`, `include/lvgl/core/lv_style.h`,
  `include/lvgl/config/lv_conf_internal.h`, `src/widgets/list/lv_list.c`,
  `src/widgets/roller/lv_roller.c`, `src/widgets/table/lv_table.c`,
  `src/core/lv_obj.c`, `src/core/lv_obj_private.h`, `src/core/lv_obj_style_private.h`,
  `src/core/lv_group.c`, `src/core/lv_refr.c`, `src/indev/lv_indev.c`,
  `src/misc/lv_array.h`, `src/misc/lv_event_private.h`,
  `src/widgets/label/lv_label_private.h`.

### Apple, by document

- **iPod classic User Guide**, Apple, 2009 (160 GB Late 2009 model), 58 pp.
  Product page: <https://support.apple.com/en-us/docs/ipod/132673>. PDF:
  <https://cdsassets.apple.com/live/6GJYWVAV/user/ma1195_ipod_classic_160gb_user_guide.pdf>.
  Cited pages: 5 (controls), 6 (control table, back/hold-Menu/Shuffle Songs), 8 (menu anatomy,
  main-menu editing), 9 (backlight, brightness, scrolling quickly), 24-25 (Now Playing elements).
- **iPod nano User Guide**, Apple, 2009 (5th generation), 104 pp.
  Product page: <https://support.apple.com/en-us/docs/ipod/132643>. PDF:
  <https://cdsassets.apple.com/live/6GJYWVAV/user/ma1194_ipod_nano_5th_gen_userguide.pdf>.
  Cited pages: 5 (controls), 6 (back/hold-Menu), 9 (menu anatomy with preview panel), 10-11
  (main menu, font size, language, backlight), 11-12 (scrolling quickly), 33-34 (Now Playing).

### Patents, by number

- **US 7,348,967 B2**, "Touch pad for handheld device", S. P. Zadesky and T. Y. Tan, Apple Inc.,
  granted 2008-03-25, priority 2001-10-22. Claims 1, 9, 13, 20 (and 18-19).
  <https://patents.google.com/patent/US7348967B2/en>;
  PDF <https://patentimages.storage.googleapis.com/26/3c/8d/e78362d6412dc2/US7348967.pdf>.
- **US 7,312,785 B2**, "Method and apparatus for accelerated scrolling", R. W. Tsuk and
  J. L. Robbin, Apple Inc., granted 2007-12-25, priority 2001-10-22. Claims 1, 7, 9, 13, 16, 17,
  22-23; FIG. 5 (acceleration stages).
  <https://patents.google.com/patent/US7312785B2/en>;
  PDF <https://patentimages.storage.googleapis.com/d4/94/f0/85cfb8f4f5b495/US7312785.pdf>.

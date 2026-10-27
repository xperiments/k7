# screen_display

The screen: the panel (or the headless framebuffer), LVGL's PSRAM allocator, the
cassette screen and, on the panel board, its touch controller and the card-side artwork
loader. The public interface is `include/screen_display.h` — `screen_display_start()`
and nothing else. Everything else is private:

- `screen_display.c` — start-up, the draw buffer, the serial dump and the task that keeps
  the cassette current.
- `lvgl_mem.c` — the `LV_USE_CUSTOM_MALLOC` backend, PSRAM first (ADR 0004).
- `cassette_view.c` — the composition: the container under the screen root, the pose and
  its animation, and the applier of the Controls model's changed-mask (the page, the two
  bars, the peek, the cover, the Menu, the meter and the room). The page is the screen's
  backdrop - dark at rest, white while the controls are out - and the model's PAGE bit is
  what flips it. It owns no geometry and no peek state; the picture is built from the
  modules below. The cassette's drawing hangs off one full-bleed, unpainted container at
  0,0, so every position in the parts stays a screen coordinate, and the peek and the bars'
  cells are the container's siblings.
- `cassette_bars.h` / `cassette_bars.c` — the two edge bars: their cells, their entrance and
  their page ink. The bar builds one 96x96 carcase per cell at the layout's rect and hands
  each to the panel that is up; `set_panel(side, panel)` installs a panel value (a grid of
  cells, or one plate), `set_shown()` slides the cells over 260 ms, reverses from wherever
  they are and hides them at the end, and `set_page()` paints the Page. Swapping the panel is
  a cut, which is how the Menu's Pad takes the DJ block's place and gives it back with
  `cassette_eq_panel()`. The right bar's M is chrome the module builds itself, so the Menu's
  door outlives every panel; the Left bar's Transport panel and the right bar's EQ panel fill
  the other cells and own none of the strip, and the right bar hosts the Pad's one plate while
  the Menu is up. No bar keeps a slide of its own, and there is no container: the cells are
  the strip.
- `cassette_button.c` / `.h` — the shared button, built five times: the drawer's P, `<` and
  `>` and the right bar's M and R. It builds the two circles and the letter inside a carcase
  its host made and owns the one pressed state (the carcase's ring for the drawer, the disc's
  for M and R), the 160 ms pulse and the tap guard. The cap is spec data and the click is a
  callback, so the module knows nothing about the transport or the Menu; the spec's internal
  name (P/PREV/SKIP/MENU/REC) is carried but never drawn by the module.
- `cassette_layout` (`components/portable/cassette`) — the design grid the screen is
  written on: the 800x480 grid, the scale (256 = pixel for pixel), the centring, the
  offset, the even-diameter rounding and the animation's interpolation; and the Controls'
  fixed frame - the badge, the two bands, the window, the Pad and the block - with the two
  bars' 96 px cells on their 94 px pitch, the hit rule, the Menu's page and its five slots,
  and the three named poses. It has no LVGL include, so the host tier compiles it and
  `host/tests/t16_layout.c` pins the lot. The view keeps the current pose and passes it to
  every part on `place()`; the chrome never scales, so it asks for its rect once at create.
- `controls` (`components/portable/controls`) — the Controls as a value: the rung of the
  ladder the screen is on (Peek, Initial, Art or Real), the Menu's open state and the room the
  cassette asks for. The Page, the Cover, the Peek and the chrome are readings of the rung, not
  fields. One `controls_handle(intent, facts)` returns a changed-mask; the view applies it in
  the model's order. `t24_controls` pins the ladder.
- `knob_model` (`components/portable/knob_model`) — the knob's arithmetic: value 0..100, dB,
  angle, click-to-value, the crown's lit span and the edit ordering, with the dB ranges read
  from `tone_eq`. `t22_knob` pins it; the view keeps the timer, the overlay and the blink.
- `pad.c` (`components/portable/menu`) — the Pad's press classification: samples to STEP,
  TAP or BACK; the ratchet itself stays the Menu model's. `t23_pad` pins the edges.
- `cassette_button_rule` (`components/portable/cassette`) — the one tap guard, the ring
  widths and the pulse curve the button module uses. `t21_button` pins it.
- `cassette_style.h` / `cassette_style.c` — the palette, one definition per colour, read
  by the view, the tapes, the meter, the peek, the transport drawer and the DJ block.
- `cassette_widgets.h` / `cassette_widgets.c` — the LVGL object helpers (`box`,
  `border_box`, `text`, `image`) and `cassette_circle_face()`, which gives a circle the
  white page's dome - its face the colour the caller passes - or the dark page's line-art
  face; the drawer's buttons and the DJ block's knobs both use it. The face is a pair of
  objects - a transparent wrapper for the broad shadow and the disc for the contact one -
  because LVGL draws one shadow per object. `cassette_cap_t` is the pair of colours a button's
  white-page cap carries (see the drawer below).
  Every object they build is decorative, so a press
  reaches the screen rather than a painted box. The bars opt the cell carcases into
  hit-testing and set each panel's bubbling with it: the Transport drawer's cells bubble
  gestures back to the screen, the EQ panel's do not.
- `cassette_tapes.c` — the tape body: the two packs, the two strands, the rollers, the
  run, the two hubs and, on the panel board, the two border frames. Its interface is
  `create(parent)`, `place(pose)`, `set(frame)`. A pack is one object whose draw callback
  paints every ring its fill puts there, so a fill change invalidates two boxes rather
  than one per circle; each hub is a rim painted once plus two opposite spokes, so an
  invalidation stays in one quadrant; the run is its own line, so a fill change repaints
  two narrow sides. The tape leaves each pack on the outer tangent, wraps a 30 px roller
  whose bottom sits 16 px above the screen edge, and joins the run between the two
  rollers. The two panel-board frames are filled with `k_col_frame` (FF00FF for the
  current design) and drawn before every other part of the body. Its interface is
  `create(parent)`, `place(pose)` and `set(frame)`: the chips along the bottom are windows,
  filled with the background's `k_col_bg`, which is what is behind them on both pages.
- `cassette_meter.c` — the VU meter, `create(parent)`, `place(pose)`, `set(frame)`,
  `show()` and `hide()`. Two
  100x24 rows centred on the right hub, L on top and R below; each row is a rounded
  rectangle with an L/R badge, a 6 px gap and seven dim bars. One rounded lit fill grows
  from the left and ends in a semicircle. The panel is transparent, so the hub shows
  through.
- `cassette_cover.c` — the white page's Track picture. `create(parent)` builds the clip
  box, the A8 mask and the fallback hole; `place()` follows the frames' current pixels;
  `set_image(dsc)` takes the loader's one buffer; `set_shown()` / `is_shown()` are the
  page's wish. The label is the top layer: the stack is cinta < glow < label. The clip box
  is frame 0 with its radius plus 2 px and `clip_corner`; the window is a real transparent
  hole cut by an A8 bitmap mask (`min(width, height) / 2`, a pill whose ends are the hubs'
  circles), so the glow and the cinta below show through. The frame[1] border takes the same
  pill while the label is up (`cassette_tapes_set_cover_print()`), and the two frames are
  raised over it (`cassette_tapes_raise_frames()`). The picture is scaled to frame 0's width
  and top-aligned, so its height follows from its aspect and the frame clips the rest. A
  pose that is not the controls pose does not show it: the mask is built at 402x188.
- `cassette_menu.c` / `.h` — the Menu's view. `create(screen, cassette)` builds five row
  slots inside the window and the Pad's panel value; `open(white)`, `close()`,
  `update(status)` and `set_tags(group, song)` are the view's calls, and the right bar's M
  cell (through `cassette_bars_set_menu_cb()`) opens it. `open()` installs the Pad on the
  right bar and hides the cassette container; `close()` puts the EQ panel back through
  `cassette_eq_panel()`. The model is `components/portable/menu`;
  `menu.h` is the seam and `t18_menu` pins the tree, band, window and ratchet. The page is
  the window's inner 482x280, a 6 px inset framing rows of 53-54 px, the band inset 2 px, the Pad
  `(596,76,186,284)`. `cassette_icons.h` maps the row icons to the icon font; `xtape_24` is
  the rows' and the chevron's and `xtape_36` the drawer's, both generated by the `font:xtape`
  script from `assets/fonts/xtape.ttf`. A glyph the font does not carry is hidden, not drawn as
  .notdef. Row labels are Jura 28; the right column is the value in Jura 20 capitals or the
  `angle-right` glyph in `xtape_24`.
- `cassette_levels.c` — the peek's host and the Track request. Its interface is
  `create(screen)`, `apply(frame)`, `update(status)`, `set_peek(shown)`, `set_tags()` and
  `set_hubs()`. The Label cover and Artwork screens and the old Level machine are gone; the
  peek is the bottom rung of the four (ADR 0025). There is no room callback: the Controls
  model owns the room and the view applies its ROOM bit. The peek still needs room from the cassette (the view shrinks and
  lifts it so the bar clears the tape) and its tethers need the hub anchors, which the view
  hands in on every pose step. On a new Track it hides the old picture and asks
  `artwork_loader` for the new tags and cover in one request slot.
- `cassette_peek.c` — the up swipe's overlay: the timeline bar and the two floating tags.
  Its interface is `create(screen)`, `apply(frame)`, `set_path()`, `set_tags()`,
  `set_hubs()`, `show()` / `hide()` and `is_shown()`. The bar carries the
  Track's elapsed, a rounded fill and its total; the tags carry the song above the hub and
  the group below it, each tethered to it. The texts come from ID3/FLAC tags read
  by the loader, with the artist directory and the filename as the fallback. One
  animation bobs both tags; each tether is a static line whose lower end follows the hub.
- `cassette_buttons.c` — the Transport panel of the left bar: the panel value the bar fills
  (one column of three), and the table of the buttons' labels, caps and actions. The bar
  builds the three 96x96 carcases and slides them; the buttons themselves are the shared
  `cassette_button.c` - the same module as the EQ panel's R and the bar's M - which owns the
  one pressed state, the 160 ms pulse and the tap guard. The panel's own interface is
  `create()` (which hands the value to the bar) and `set_menu_mode()`. `<`/`>` call
  `mini_icecast_prev()` / `mini_icecast_skip()` and P is still a placeholder; the Menu swaps
  in the three icons and Close / Back / Enter.
- `cassette_eq.c` — the EQ panel of the right bar: five of the bar's six cells, in two
  touching columns (the first one's middle cell is the bar's chrome, M). The panel builds the
  knobs and R into the carcases the bar hands it and owns only its edit state; it creates no
  carcase and no root. The panel is GAIN, HI, MED, LOW and R; R is the Transport panel's
  button moved here - the same 64 px circle, Jura 36 letter, one pressed state, click pulse
  and four inks, with no action yet. Each knob is a 44 px circle with a 24-dot crown outside
  it and a line from the centre towards the rim; a tap opens a 3x virtual knob and a click on
  its ring sets the target angle. The next/previous controls are the Transport panel's `<`
  and `>`. Its interface is `create(screen)`, which installs the panel value in the bar and
  builds the edit layer, plus `is_editing()` and `cassette_eq_panel()` - the value the Menu
  installs again after its Pad. The knob values and the edit layer outlive the swap; an open
  edit ends with the panel, as it does whenever the panel goes. The Menu is its only caller.
- `cassette_window.c` — the window around the cassette on the controls screen: 486x284 at
  (110,76), its top and bottom edges on the bands' lines and its sides in the gaps either
  side of the scaled tape. Same 2 px border, 8 px corner and carcase ink as the bands. It
  does not slide - it is a window, not a band, and sweeping it over the tape would read as a
  mistake - so its interface `create(screen)` / `set_shown()` fades it 0..255 over the
  drawer's 260 ms instead. It is built as the screen's first child, before the cassette's
  container, so the tape draws over it and reveals the border as the pose shrinks the
  cassette into the window.
- `cassette_header.c` / `cassette_footer.c` — the two bands that close the controls screen
  across the top and the bottom: the header's composite frame is 768x72 at (16,6), its 2 px
  border landing on the drawer's and the DJ block's top line (76..77); the footer is 768 px
  wide at (16,358) and 116 px tall, 6 px short of the panel's bottom, its 2 px border landing
  on their bottom line. They are line art like the carcases they join - a 2 px border rounded by 8 px in
  `k_col_button_rect` - over an opaque fill in the page's colour: `k_col_bg` on the dark
  screen, `k_col_white` on the white page, repainted by `set_page()` as the view's page
  flips, so a band covers what it slides over instead of letting the picture show through
  it. Each interface is `create(screen)`, `set_shown()` and `set_page()`. They move with the
  rest of the chrome: a 260 ms ease-out slide, down from above the panel and up from below
  it, reversed from wherever it is if the controls change their mind, and hidden at the end
  of the slide out. The header also carries the K7 mark from `assets/labels/k7b.png` in a
  badge of its own: a 96x72 rounded plate at (16,6) that is the palette's primary on the
  dark page and the page's white with a `k_col_bg` border on the white one, with the 60x32
  A8 mask centred in it and tinted to suit - the cap's light ink on the primary plate,
  `k_col_bg` on the white one - so the mark sits at (34,26) on the screen.
- `screen_input.c` — the touch policy: the screen's one-finger vertical gesture becomes one
  `screen_intent_t`, DOWN or UP, and nothing else does. It names no screen and calls nothing
  above itself; the view's sink maps the two into the Controls model, which owns the rungs.
  The horizontal swipes are retired (ADR 0025): the drawer is Art, reached by a down swipe,
  and the only left swipe left is the Menu's Back, whose gesture the Pad keeps local. A
  two-finger swipe is nothing at all; there is no latch any more.
  A tap is not an intent: next and previous are the DJ block's own cells, and the view
  drops every intent while a knob is in edit mode.
- `artwork_loader.c` — the card side of the peek's tags and the white page's cover. It
  locates the Track's picture (an ID3 `APIC` or a FLAC `PICTURE`), decodes the JPEG with
  TJpgDec on its own task, and fits it cover-style into one 603x385 RGB565 buffer; the
  same task reads the text tags (TPE1/TIT2, ARTIST/TITLE). Results are handed to the view
  under the LVGL lock. The request is one slot, replaced rather than queued, and the read
  backs off while the broadcast is behind. Compiled and started only on the panel board.
- `panel_rgb.c` — the Sunton panel adapter, compiled only for that board.
- `backlight.c` — the Sunton backlight on GPIO 2, now a 400 Hz, 8-bit LEDC duty rather than
  an on/off pin. It starts at `APP_UI_BACKLIGHT_AWAKE_PCT` (100% by default), then
  fades to 60% after 2 minutes, 30% after 5, and 0% after 10 with no touch, each step
  set by its own Kconfig pair. A touch restores the awake level in the input read
  itself, from wherever the light currently is. The ladder is measured on LVGL's
  inactivity clock, so any press resets it, and the fade is stepped by the screen
  task (`APP_UI_BACKLIGHT_DIM_FADE_MS`, 2500 ms; wake
  `APP_UI_BACKLIGHT_WAKE_FADE_MS`, 300 ms) so the steps do not snap. The first press
  after a dim only wakes the screen; the view sees no click or gesture from it. If
  LEDC refuses the pin the module falls back to holding it high and only dimming is
  lost. `APP_UI_BACKLIGHT_TEST` replaces the cassette with a white screen and steps
  the backlight through 100/80/60/40/20/0 every five seconds, printing the level, so
  the PWM range can be checked without waiting for the sleep timer.
- `touch_gt911.c` — the Sunton touch adapter: a GT911 on I2C0, polled every 10 ms and
  bound to LVGL as the display's pointer. It reports the first point and nothing else: the
  two-finger swipe and its midpoint detector are gone with the VU meter's old home
  (ADR 0025). Next and previous are the left drawer's `<` and `>`, never a screen gesture or
  a tap.
- `assets/labels/k7b.png` — the K7 mark in the controls header. It is greyscale with an
  alpha plane and only the alpha is exported, as `assets/header_logo.bin` (A8, 60x32),
  by `tools/convert_cassette_assets.py`; CMake embeds that blob and `cassette_header.c`
  tints it inside the header's badge.
- `assets/labels/cover.jpg` — the old Artwork Case, parked with the Label cover and
  Artwork screens and not embedded. The white page's cover does not use it; the cassette's
  frames are the frame. `tools/convert_cassette_assets.py` is still the way the old
  label-cover blob is made.

**The Menu.** A tap on the DJ block's M opens a mode of the controls: the cassette area
becomes the window's five-row page, the right bar swaps its EQ panel for the Pad, the drawer
becomes Back/Enter/Close, and the header and footer carry the list's title and what is playing.
The model is portable - `components/portable/menu`, host test `t18_menu` - and owns the tree,
the band, the window and the ratchet; `cassette_menu.c` draws five row slots, executes its
actions, installs the Pad on the right bar and hides the cassette container for the mode's
lifetime. The Pad is a white plate with a dark inner (inset 8 px) and a dotted vertical guide
inside it, over the block's place and white on both pages: a vertical drag ratchets one row per
54 px (1x/2x/4x by speed), a tap activates the band, a leftward swipe is the drawer's Back, and
the plate does not bubble gestures so a drag cannot close the Menu. Closing the Menu puts the
EQ panel back with its knob values and its edit layer as they were.
The root reaches Tracks, Browse, Shuffle (a mode of the broadcast, ADR 0023) and Settings
(Backlight, Station, Library, Rescan, About); Backlight pins the panel with a hold until
reboot. The icon font carries the whole set now - `books` for Browse and Library, `settings`,
the two `toggle` glyphs for Shuffle, `brightness`, `k7logo` for About, `track` for Track rows,
`micro-chip`/`tablet-android`/`disc` for About's rows and the `angle-right` chevron - and the
view hides a glyph the font does not carry. The tree and rules are in
[`docs/specs/menu.md`](../../../docs/specs/menu.md), with ADR
[0022](../../../docs/adr/0022-the-menu-is-a-controls-mode.md) and
[0023](../../../docs/adr/0023-shuffle-is-a-broadcast-mode.md).

The panel and the headless path are two adapters behind the same interface, selected by
`CONFIG_APP_BOARD_SUNTON_8048S050C` in this component's CMake. The touch adapter and the
artwork loader are a second, input-side pair under the same selection: a board without a
controller simply has no pointer and no peek. A third panel promotes that selection to a
`display_port` seam without changing `screen_display_start()`.

## The four rungs and the peek

The screen is a stack of four, top to bottom: **Real** (the chrome on the white page, with the
Track's picture), **Art** (the chrome on the dark page), **Initial** (the cassette alone) and
**Peek** (the timeline bar and the two tags). It boots at Initial. A down swipe takes one rung
towards Real and an up swipe one towards the Peek; the ends stop. That is ADR 0025, and it is
the only way between them - the horizontal swipes and the two-finger VU are gone.

At the entry 100% pose (256/256) the up swipe raises the peek and a down swipe with the peek up
puts it away; an up swipe while it is up does nothing. The peek can only be raised from that
100% pose: while the cassette holds the 134/256 pose (or the peek's own 200/256 is still
settling) the up swipe does nothing. The down from Initial is **Art**: the drawer and the DJ
block slide in on the dark page. The next down is **Real**: the page turns white and the Track's
picture prints on the cassette, as one step; the up swipe undoes each rung in turn. The Label
cover and Artwork screens that used to sit below the cassette are gone, with their two down
gestures and the old Level machine.

The peek's own 260 ms slide is kept: it moves a 568x52 bar and two 504x44 tags, not the
panel. Its cassette shrink re-places every part in the same 260 ms — geometry only, no
bitmap and no re-sampled pixels — so what it costs is a redraw of the cassette, not of
the panel's picture. `cassette_view_set_scale(scale, 0, dy, 0)` is the instant form if
that ever reads as a stutter.

The Track's picture and text tags come from `artwork_loader.c`, which the panel board
starts after the view. A Track with no usable picture simply shows no cover; a Track with
no tags keeps the peek's filename fallback.

## The peek

The peek is the bottom rung of the four (ADR 0025), owned by `cassette_peek.c`. It has three
things: the bar, the tags and the tethers.

The bar is the old paper strip, 568x52 at (116,424) with a 12 px radius. It no
longer carries the title. Its left box is the Track's elapsed time, its right box the
total, and between them a 288x6 rounded track whose lit fill grows with the model's
`permille`. The model reports whole-second changes (`time_changed`) and permille changes
(`permille_changed`), so the times are rewritten once a second and the fill only when its
pixel width moves. With nothing playing the times and the track are hidden and the bar
shows the screen's idle sentence instead.

The tags are two 504x44 pills rounded by 22 px, black with the playing red's 2 px border
and 28 px Jura text, centred on the cassette at x=148..651 (61 px of cassette on each
side): the song at (148,12) above the left hub, the group at (148,248) below it, their
centres 118 px from the hub's centre on either side.
Sixteen px of inset leaves 472 px of text, enough for a long title; a longer one is cut
with dots, not scrolled. The peek uses a second Jura face at 28 px (`font_jura_28.c`);
there is no 36 px band left on the screen. Both tethers leave on the left hub's column
(251 px; 20.4 % of the centred 504 px pill rather than a round quarter, so the lines stay
vertical): the upper label's from its bottom to the top rim, the lower one's from its top
to the bottom rim.
The texts are the Track's ID3/FLAC tags, read by `artwork_loader`: TPE1 (artist, with TPE2
as its fallback), TIT2 in an MP3; ARTIST (with ALBUMARTIST as its fallback), TITLE in a
FLAC Vorbis comment. Until they land - and for a Track that carries none - the group is
the first directory of the Track's relative path and the song is the cleaned filename. A
label wider than its pill is truncated with dots, not scrolled: a marquee would animate
every frame the peek is up. A label with no text is hidden with its tether on its own - no
em dash stands in for it - so a Track with only one of the two shows only that pill.

Each tether is a two-point `lv_line` from 14 px inside its pill to the left hub's near
rim: the upper label from its bottom to the top rim, the lower one from its top to the
bottom rim.
The line's upper end is under the pill's opaque fill, so the tag's bob uncovers a little
more or less of it without a line redraw; the lower end is the hub anchor the view hands
in from `cassette_tapes_hub_anchor()` on every pose step, so the tethers follow the
cassette while it shrinks and lifts. The line object is placed on its own bounding box,
so an invalidation is the line and not the screen from 0,0.

The movement is one `lv_anim` over both tags: a five-second sine, +/-6 px, the two tags
in opposite phase. Its callback rounds the offset to a whole pixel and writes `y` only
when that pixel changes, which is around eight writes per tag per second at 25 Hz; each
write invalidates one 504x44 pill, not the cassette and not the screen. The tethers are
static between pose changes. The peek's room request is the view's 200/256 pose with the
-60 px lift; the tags appear with the first frame, the bar runs its 260 ms slide, and the
next up swipe puts everything away.

## The transport drawer

The three buttons at the left edge are the shared `cassette_button.c`; `cassette_buttons.c`
is their table and the panel value the bar fills. The drawer is three 96 px cells with no panel
object around them, hidden off-screen at rest; the left bar (`cassette_bars.c`) builds and
slides them over 260 ms and the Controls model decides when from the ladder. A down swipe from
Initial slides it in as Art; an up swipe slides it out. There is no sideways route to it any
more (ADR 0025). The entry view is the untouched full-scale cassette; while the
drawer is out the model puts the cassette at 134/256 (`CASSETTE_SCALE_CONTROLS`), which
brings the tape's box to 402x232 px at x=148..550: the drawer's 96 px column at x=16 is clear
on the left and the 190 px DJ block at 594..784 has room on the right. The DJ block is shown
and hidden with the drawer by the same model bit, and the pose runs over the same 260 ms. The
three buttons start 76 px from the top, so the middle button `<` is on y=218; the -20 px dy
puts the cassette's centre near that line.
The drawer wins over the peek while it is out; when it slides away the peek's 200/256 and
-60 px lift come back.

Each button is 96x96, rounded by 8 px, with a 64 px circle at its centre and P, `<` or `>`
centred in the circle in Jura 36. The carcase is the left bar's - the same 96x96 box as the
DJ block's - filled opaque with the page's colour and bordered `k_col_button_rect`. Each
button carries **its own cap** - `cassette_cap_t`, the spec data the DJ block's M and R carry
too, and the white page's alone: the face of the disc there and the letter drawn on it. The
drawer's three start in `k_col_dome`'s neutral grey with the letter `k_col_bg` (black); M's
cap is `k_col_bg` and R's `k_col_button_r`, both lettered `k_col_button_white` (see the DJ
block below). The cap is spec data at create: the panel never re-inks one afterwards. The
dark page is not the caps': it is the palette's line art,
the playing red ring and letter (`k_col_button_circle`, `k_col_button_letter`), the same for
every button, whatever a cap says.

On the white page the circle becomes a **dome**: a flat disc (one colour - no gradient) with
**two** outer shadows, because LVGL draws one per object. A transparent circle exactly under
the disc carries the broad ambient one - 24 px of blur at a (2, 5) offset, 60 % black - and
the disc itself carries the tight contact one - 8 px at (1, 2), 60 % black - so the edge is
dark and the shade falls off into the bezel, with no ring. Both are outer-only; the dome is
not inset anywhere. The contact one's whole blur sits inside the 96 px bezel (x=85, y=86);
the broad one reaches x=94, and its faintest tail a couple of px past the bezel's bottom.
Each has its own colour constant, so a tinted second shadow is a one-line change.
The dome's face - one flat colour, no gradient - is the button's own `face`, so a button whose
disc is not the playing red can carry that colour on this page too, and the letter on it is its
`letter`, `k_col_bg` (black) by default. The rim just inside the edge is always `k_col_dome`'s
original grey, `CASSETTE_CAP_RIM_W` (4 px), so a coloured cap reads as a disc let into the
original one; it is the disc's own border, which LVGL draws inward - an inset, not an outline -
and on a cap whose face is already that grey it is invisible.
The dome is the pressed state's too: on this page the disc is the cap, and its
`CASSETTE_CAP_RIM_W` rim is the same width pressed or not, so a press reads through the
click pulse - and, in the drawer, through the carcase's ring widening from 2 to 3 px.
`LV_EVENT_PRESSED` writes that ring and release or `LV_EVENT_PRESS_LOST` writes it back. A
click runs the 160 ms scale pulse (256 -> 232 -> 256) from the portable
`cassette_button_rule`; `<` and `>` also call `mini_icecast_prev()` /
`mini_icecast_skip()`. P is still a placeholder; the transport's next/previous are these
two arrows now.

All five buttons are that one module, and it is where the press, the pulse and the guard
live: it builds the disc, the letter and the page ink inside a carcase its host made, and
hands a click to the host's callback - the drawer maps its three to the transport or the
Menu, M opens the Menu and R has none. The tap guard is one rule for all five:
`!moved && !gesturing`. A click that carries a gesture direction or has moved past LVGL's
press limit is not a tap and does nothing at all - no pulse and no action. The guard used to
carry a third term for the two-finger gesture; that gesture is gone (ADR 0025).

The bands across the top and the bottom of the controls screen live in `cassette_header.c`
and `cassette_footer.c`: 768x72 at (16,6) and 768x116 at (16,358), a 2 px border rounded by
the buttons' 8 px in the drawer's carcase ink. The header's 2 px border lands on the first
button's and the first DJ cell's top line, the footer's on their bottom line, and both
bands' sides sit on the cassette frame's 16 px inset, so the drawer, the block and the two
bands read as one frame. Their fill is opaque and follows the page - `k_col_bg` on the dark
screen, `k_col_white` on the white one, which is what makes them read as part of the page
they close and lets them cover the tape while they slide over it. They arrive with the rest
of the chrome: the header drops from just above the panel and the footer rises from just
below it, both over the drawer's 260 ms ease-out, and both are hidden when their slide out
completes. A reversal starts from wherever the band is, like the drawer's own slide.

The header carries the K7 mark in a badge at its left. `assets/labels/k7b.png` is greyscale
art with transparency; the asset tool keeps only its alpha plane (A8, 60x32, 60 px stride)
and the firmware tints that mask with `lv_obj_set_style_image_recolor()`. The badge is a
96x72 rounded plate at (16,6), a screen child above the band so its border is the divider:
`k_col_bg` fill with a `k_col_primary` border and mask on the dark page; `k_col_logo_back`
with a `k_col_bg` border and mask on the white one. The mask is centred on the plate, so the 60x32 art sits at (34,26) on the
screen, and the badge rides the band's slide.

The third rectangle of the frame is the window around the picture, `cassette_window.c`:
486x284 at (110,76), its top and bottom edges on the bands' lines and its sides in the gaps
either side of the scaled tape. It has no edge to come in from - sweeping a window over the
tape would read as a mistake - so it fades in and out over the same 260 ms, from whatever
opacity it holds if it is reversed, and hides at the end of the fade out.

The drawer is chrome above the peek. The bars are created after `cassette_levels`; the left
bar's three carcases and the buttons inside them bubble events, and the cells bubble gestures
where a panel asks them to, so a swipe that starts on the chrome still reaches
`screen_input`. A button press cannot fall through as a tap: a click on a child is the
child's, and the screen no longer gives a click any meaning.

## The white page (Real)

The white page is the **Real** rung, and it carries the Track's picture. A down swipe from
**Art** paints the screen's backdrop `k_col_white` (0x909090) instead of `k_col_bg`
(0x111111), so the orange line art reads as ink on paper, and prints the cover in the same
step; the up swipe flips the page back to dark and fades the picture out over it. The view
owns the flip (`page_set()`, held in a flag so a repeat request costs nothing), and it runs in
one frame as the step is taken - a colour animation would repaint the whole 800x480 panel on
every step. The flip writes the screen's backdrop, the opaque fills of the two bands, the
drawer's three buttons and the DJ block's six cells, the tape's window chips, and each
button's circle (line art or dome) and letter. The model's PAGE bit carries both halves, and
the applier flips the page before it shows the picture.

The white is a margin: the flip also reveals `s_screen_back_cover`, a square-cornered
768x448 `k_col_bg` box at (16,16) that is the screen's first child, so on the white page the
picture keeps its dark ground and the white reads as the 16 px ring around everything.

The same page carries the **cover** when the Track has one; a Track without a picture still
has Real, just a plain white page. Every flip back to dark hides the picture with it.
The picture is the loader's one 603x385 buffer, drawn by `cassette_cover.c` into the
cassette's outer frame: its width is frame 0's, it is top-aligned, and it is clipped to the
frame's radius plus 2 px, so the print's corners sit a little inside the shell's border. The
label window - frame 1 - is a real transparent hole, cut by an A8 bitmap mask on the label's
container, so the glow and the cinta below show through. The hole takes the largest radius
the rectangle allows - `min(width, height) / 2`, a pill whose ends are the hubs' circles - and
the label window's border follows the same pill while the label is up
(`cassette_tapes_set_cover_print()`).

The label is a child of the cassette container, created after the tape body and after the
glow, so the stack is **cinta < glow < label**. It covers the body inside the outer frame;
the tape exposed below the frame stays visible. The two frames are lifted over the label
while it is up (`cassette_tapes_raise_frames()`) and put back at the bottom when it goes, so
their borders stay crisp without moving the cinta. The label is re-placed on every pose step,
and its picture is scaled at draw time, so the buffer stays the loader's one size; the
controls pose scales it down, not up. The mask is built once at that pose (402x188), which is
why the model refuses the down from Art until the pose has landed - the same two downs can
arrive faster than the shrink.

The down fades the label in and the up fades it out over 1 s
(`lv_anim_path_ease_in_out`), on the container's `opa_layered`, so the mask and the picture
go together. A page flip or a Track change cuts it instead: the loader reuses the one cover
buffer and the pose leaves the mask's size, so there is nothing to fade over.

One more thing belongs to that page, and only to it: the **edge mark**, a child of the
cassette created after the meter and before the label, so it sits between the cinta and the
label. It is a 426x272 rectangle centred on the tape at the controls pose (via `cassette_x/y`),
and it has two parts. Its border is an L - top and left lit in `k_col_glass`, right and bottom
transparent (`EDGE_SIDES`). Its interior is a **linear gradient at 135 degrees**, white at the
top-left corner fading to transparent at the bottom-right, with the two stops carrying the
opacity. That gradient needs `CONFIG_LV_USE_DRAW_SW_COMPLEX_GRADIENTS`, which `sdkconfig.defaults`
now turns on: LVGL ignores `LV_GRAD_DIR_LINEAR` without it. All of the mark's numbers are
`EDGE_*` and `EDGE_GLOW_*` at the top of `cassette_view.c`.

Being the middle layer, the mark lights the cinta (including the tape exposed below the
frame) and is covered by the label inside the outer frame; the label's window lets it show
through. The peek and the chrome are screen children above the cassette.

The mark fades in over `EDGE_GLOW_FADE_MS` from nothing - its object opacity multiplying the
gradient's stops and the border together - so it never pops. It used to wait for the chrome's
260 ms entrance first; since ADR 0025 the white page is only reached from Art, where that
entrance has long landed, so only the fade is left. A page flip back to dark cancels it and
hides the mark at once.

The chips along the tape's bottom are windows on whatever is behind the cassette. That is
`k_col_bg` on both pages - the dark screen at rest, and the back cover on the white page - so
they are filled with `k_col_bg` and the page flip does not touch them.

## The DJ block

The six cells at the right edge are the right bar's (`cassette_bars.c`), which builds them,
slides them and paints them; `cassette_eq.c` is the EQ panel that fills five of them, and M
is the bar's chrome. The Controls model shows them with the Transport panel. While the Menu is
up the same bar hosts the Pad instead - one white 186x284 plate over the block's place - and
the EQ panel comes back on close. Two touching 96 px columns at x=594..784 (the first column
nudged 2 px right; the second stays at 688), three rows at y=76/170/264, so the block is 190x284
and its right edge leaves 16 px to the screen. The 16 px is the inset of the 64 px circle
inside each carcase, not a gap: the carcases share their borders like the Transport panel's.
The first column is GAIN above M and R; the second is HI, MED and LOW. All six cells are
carcases over the page's opaque fill, so the block covers the tape it slides across: the bar's
page fill, then the EQ panel's own ink - the four knobs' cells are the knob plate on the white
page only - `k_col_knob_rect` filling them and `k_col_bg` for their border, so no carcase line
runs round a knob. M's and R's cells keep the page's fill and the drawer's `k_col_button_rect`
border, and so does every cell on the dark page. M and R carry their own caps, like the
drawer's buttons, and those are the white page's alone: on the dark page both are the
palette's playing red line art like
everything else there; on the white page M's cap is `k_col_bg` (black) and R's
`k_col_button_r`, and both letters on them are
`k_col_button_white` (`0xEEEEEE`) - they are the only two buttons whose letter is not the
page's black, because they are the only two whose cap is not the page's grey. On the white page
the four knobs' 44 px circles and M's and R's 64 px circles take the cap face - the same two
shadows: the knobs in `k_col_knob_cap` (much darker than the buttons' grey so a knob does not
read as a button, and with no rim), M in black, R in its own red. M's and R's 4 px
rim is `k_col_dome`'s grey, the drawer buttons' cap rim, so each cap reads as a disc let into
it, and the knobs' own plate - `k_col_knob_rect` inside a `k_col_bg` border - is what makes
them read as knobs rather than buttons. The knobs' crown and pointer keep the EQ inks. The
block is hidden at rest, so the entry view is the full-scale cassette; the model shows it with
the transport drawer, when the cassette is already at the 134/256 control pose. The right bar
slides the cells in from the right edge over the same 260 ms as the drawer and back out when
the drawer leaves.

Each knob is a 44 px circle - 30% smaller than M's and R's 64 px one - with a 24-dot
crown at r=32 outside it and a line from the centre towards the rim (the owner's tuned
4 px width). 0 dB is at twelve
o'clock; the right side cuts and the left side boosts, and the arc lights from twelve to
the line on the side it points.

A tap on a knob opens edit mode after a 260 ms delay: a 3x virtual knob (256 px at x=220,
90% of the 96+94+94 length, its top at y=90, a 2 px circle stroke and a centre line, with no
outer square - only the circle, the crown and the line) appears over the cassette, and under
the dial a rounded `k_col_primary` pillow carries the knob's internal name (GAIN, HI, MED or
LOW) in the light cap ink, while a transparent 800x480 overlay owns the touch. The cassette cuts out and
the knob appears; on close the knob disappears and the cassette cuts back in. The delay is
there so a second tap is read as the reset double click instead of opening the knob. Only
the big knob's centre circle closes. A click on the ring - the 256 px area around that
circle - is the target point: the knob jumps to that angle, 0 dB at twelve, clockwise
cutting and counter-clockwise boosting. A click outside does nothing; there is no drag. A
double tap on any small knob resets it to 0 dB and cancels the pending open, so the virtual
knob never appears. While the overlay is up nothing else responds - the placeholders, the
drawer and the peek are all behind it - and the view's sink drops every screen intent too,
because editing is a fact of the Controls model. Both the small
active circle and the big one blink their border 100% -> 27% -> 100%, 600 ms each way.
The left drawer's `<` and `>` are the next/previous controls and call
`mini_icecast_prev()` / `mini_icecast_skip()`; the screen's tap and the BOOT button no
longer do either.

The audio side is `components/portable/tone_eq`: a 200 Hz low shelf, a 1 kHz bell (Q 0.7)
and a 4 kHz high shelf, plus a trim applied before them. The knobs map to the DJ ranges:
GAIN -12..+12 dB (default 0) and the bands -26..+6 dB (default 0). The targets are
published by `mini_icecast_eq_set()` / `mini_icecast_gain_set()`, read by the broadcast
task once per 1024-frame chunk, and slewed at 2 dB per chunk before the coefficients are
rebuilt, so a moved knob glides rather than steps. The EQ runs in `emit_chunk()` and on the
partial tail in `flush_pipeline()` before `measure_level()`, so the VU is the level that
goes out and a track's last 23 ms is shaped too. The int16 clamp is the last stage; a hot
trim and a boosted band can reach it, like a mixer.

## The VU meter

The status carries the RMS of the 1024-frame chunk the encoder is about to take
(0..1, full scale), measured by `measure_level()` in `components/portable/mini_icecast`
after the tone EQ has run, so it is the level that goes on the wire.
`components/portable/cassette` turns it into a peak-hold: `vu = max(level, vu * 0.90)`
per 40 ms update, about -23 dB/s, so the peak follows a real drop instead of hanging.

`cassette_meter.c` draws it inside the right hub: two 100x24 rows, 8 px apart, L on top
and R below. Each row is a rounded rectangle (radius 12, half its height) with a 24 px L/R
badge, a 6 px gap and seven 10 px dim bars; the bars touch each other and the row's top and
bottom. The lit level is one rounded fill over the first lit bars, one step dimmer than the
badge, so its right end is a semicircle; the row's clip_corner rounds the track's right end
too. The fill is mapped over -48..0 dB so the row moves like a meter rather than a linear
amplitude. There is no panel behind it - the rows, badges, track and fill are the only
things painted, so the hub shows through. A level change only resizes the fill, and a row
whose lit count has not changed is not touched.

The meter starts hidden and stays hidden. It is **parked** (ADR 0025): the view still
creates it, places it with the pose and feeds it every frame, and nothing calls `show()`.
The two-finger gesture that used to fade it in and out is gone, and so is the latch it used
to set; the 220 ms layered fade is kept with the module, for its new home.

## The reel change

Eight seconds after a track boundary the model returns the rewind window: the packs
migrate back and the hubs turn in reverse (ADR 0011). The model also reports the window's
ink as `cassette_frame_t.tape_ink` (0 = the tape's own colour, 1 = the playing red, 1 when
not rewinding); `cassette_tapes.c` mixes each hub ring's border from `k_col_tape_dark` to
`k_col_tape` with it, and only writes the style when the value changes. Nothing else
fades: the meter, the title and the cassette are information, not tape. The model still
reports `rewind_permille`, and `t10_cassette` tests both the reel arithmetic and the
`tape_ink` ramp.

## The Track picture and the Case

The Track's picture is back, but not the old Artwork screen: the white page's cover uses
it inside the cassette's frames. The locator and fit (`components/portable/artwork`'s
`artwork_locate()`, `id3.c`, `flac.c`) and the TJpgDec decode in `artwork_loader.c` are
built on the panel board. The loader owns one 603x385 RGB565 cover buffer (464,310 B) and
starts it with the screen; the fit is still cover-style, centre-cropped, and the white
page scales the result to frame 0's width at draw time.

The old Case backdrop is still parked: `assets/labels/cover.jpg` (768,000 B decoded) and
the Label cover's source (`assets/labels/k7logo.png`) stay in the tree unembedded, with the
Label cover and Artwork screens. The cover needs neither: the cassette frames are the
frame. The host tier still builds and tests the locators and the fit
(`host/tests/t15_artwork.c`).

## Board-only properties

A host pass cannot establish the JPEG decode time or the card contention; those belong
to the artwork loader, and its board measurements are in the git history. The host tier
covers the locators and the fit (`host/tests/t15_artwork.c`) and the design
grid (`host/tests/t16_layout.c`).

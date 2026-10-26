# screen_display

The screen: the panel (or the headless framebuffer), LVGL's PSRAM allocator, the
cassette screen and, on the panel board, its touch controller and the card-side artwork
loader. The public interface is `include/screen_display.h` — `screen_display_start()`
and nothing else. Everything else is private:

- `screen_display.c` — start-up, the draw buffer, the serial dump and the task that keeps
  the cassette current.
- `lvgl_mem.c` — the `LV_USE_CUSTOM_MALLOC` backend, PSRAM first (ADR 0004).
- `cassette_view.c` — the composition: the container under the screen root, the pose and
  its animation, the page, the model's step and the intent sink. The page is the screen's
  backdrop - dark at rest, white while the controls are out - and this file is the only
  thing that flips it. It owns no geometry and no peek
  state; the picture is built from the modules below. The cassette's drawing hangs off
  one full-bleed, unpainted container at 0,0, so every position in the parts stays a
  screen coordinate, and the peek and the transport drawer are the container's siblings.
- `cassette_layout` (`components/portable/cassette`) — the design grid the screen is
  written on: the 800x480 grid, the scale (256 = pixel for pixel), the centring, the
  offset, the even-diameter rounding and the animation's interpolation. It has no LVGL
  include, so the host tier compiles it and `host/tests/t16_layout.c` pins it. The view
  keeps the current pose and passes it to every part on `place()`.
- `cassette_style.h` / `cassette_style.c` — the palette, one definition per colour, read
  by the view, the tapes, the meter, the peek, the transport drawer and the DJ block.
- `cassette_widgets.h` / `cassette_widgets.c` — the LVGL object helpers (`box`,
  `border_box`, `text`, `image`) and `cassette_circle_face()`, which gives a circle the
  white page's dome - its face the colour the caller passes - or the dark page's line-art
  face; the drawer's buttons and the DJ block's knobs both use it. The face is a pair of
  objects - a transparent wrapper for the broad shadow and the disc for the contact one -
  because LVGL draws one shadow per object. `cassette_ink_t` is the pair of colours a button's
  white-page cap carries (see the drawer below).
  Every object they build is decorative, so a press
  reaches the screen rather than a painted box. The transport drawer and the DJ block's
  six cells are the parts that opt their own roots into hit-testing; the drawer also
  bubbles gestures back to the screen.
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
  slots inside the window, the Pad and the mode's lifecycle; `open(white)`, `close()`,
  `update(status)` and `set_tags(group, song)` are the view's calls, and the M cell's tap
  (through `cassette_eq_set_menu_cb()`) opens it. The model is `components/portable/menu`;
  `menu.h` is the seam and `t18_menu` pins the tree, band, window and ratchet. The page is
  the window's inner 482x280, the rows 56 px, the band inset 2 px, the Pad
  `(596,76,186,284)`. `cassette_icons.h` maps the row icons to the icon font; `xtape_24` is
  the rows' and `xtape_36` the drawer's, both generated by the `font:xtape` script from
  `assets/fonts/xtape.ttf`. A glyph the font does not carry is hidden, not drawn as
  .notdef - folder, gear, sun, info and SD are still to be drawn.
- `cassette_levels.c` — the peek's host and the Track request. Its interface is
  `create(screen, room)`, `apply(frame)`, `update(status)`, `toggle_peek()`, `set_tags()`
  and `set_hubs()`. The Label cover and Artwork screens and the Level machine are gone:
  Bare is the only screen. The peek needs room from the cassette (the view shrinks and
  lifts it so the bar clears the tape) and its tethers need the hub anchors; this module
  passes the view's room callback through and the view hands the anchors in on every pose
  step. On a new Track it hides the old picture and asks `artwork_loader` for the new tags
  and cover in one request slot.
- `cassette_peek.c` — the up swipe's overlay: the timeline bar and the two floating tags.
  Its interface is `create(screen, room)`, `apply(frame)`, `set_path()`, `set_tags()`,
  `set_hubs()`, `show()` / `hide()` / `hide_now()` and `is_shown()`. The bar carries the
  Track's elapsed, a rounded fill and its total; the tags carry the song above the hub and
  the group below it, each tethered to it. The texts come from ID3/FLAC tags read
  by the loader, with the artist directory and the filename as the fallback. One
  animation bobs both tags; each tether is a static line whose lower end follows the hub.
- `cassette_buttons.c` — the transport drawer: the three line-art buttons P, `<` and `>`,
  each with a prebuilt pressed and an unpressed state and its own cap,
  and the left-edge panel that slides in and out. Its interface is `create(screen, room)`,
  `set_shown()`, `is_shown()`, `set_cap()` and `raise()`. A press swaps states, a click
  pulses the button's scale, and `<`/`>` call `mini_icecast_prev()` / `mini_icecast_skip()`;
  P is still a placeholder. The drawer reports its visibility through the `room` callback;
  the view turns that into the 134/256 control pose and shows the DJ block with it.
- `cassette_eq.c` — the DJ block at the right edge: six 96 px carcases in two touching
  columns, the same carcase as the left drawer's. The first column is GAIN above M and R;
  the second is HI, MED and LOW. M and R are the drawer's buttons moved to the block - the
  same 64 px circle, Jura 36 letter, pressed and unpressed rings, click pulse and four
  inks; M opens the Menu and R has no action yet. R is the only one that is not the playing
  red and the neutral
  grey (its own red on the white page). Each
  knob is a 44 px circle with a 24-dot crown outside it and a line from the centre towards the
  rim; a tap opens a 3x virtual knob and a click on its ring sets the target angle. The
  next/previous controls are the left drawer's `<` and `>`. Its interface is `create(screen)`,
  `raise()`, `set_page()`, `set_cap()` and `is_editing()`.
- `cassette_window.c` — the window around the cassette on the controls screen: 486x284 at
  (110,76), its top and bottom edges on the bands' lines and its sides in the gaps either
  side of the scaled tape. Same 2 px border, 8 px corner and carcase ink as the bands. It
  does not slide - it is a window, not a band, and sweeping it over the tape would read as a
  mistake - so its interface `create(screen)` / `set_shown()` fades it 0..255 over the
  drawer's 260 ms instead. It is built as the screen's first child, before the cassette's
  container, so the tape draws over it and reveals the border as the pose shrinks the
  cassette into the window.
- `cassette_header.c` / `cassette_footer.c` — the two bands that close the controls screen
  across the top and the bottom: `cassette_header` is 768x71 at (16,6), its bottom edge on
  the drawer's and the DJ block's top line (y=76); `cassette_footer` is 768 px wide at
  (16,359) and 115 px tall, 6 px short of the panel's bottom, its top edge on their bottom
  line. They are line art like the carcases they join - a 2 px border rounded by 8 px in
  `k_col_button_rect` - over an opaque fill in the page's colour: `k_col_bg` on the dark
  screen, `k_col_white` on the white page, repainted by `set_page()` as the view's page
  flips, so a band covers what it slides over instead of letting the picture show through
  it. Each interface is `create(screen)`, `set_shown()` and `set_page()`. They move with the
  rest of the chrome: a 260 ms ease-out slide, down from above the panel and up from below
  it, reversed from wherever it is if the controls change their mind, and hidden at the end
  of the slide out. The header also carries the K7 mark from `assets/labels/k7b.png`, a
  75x40 A8 mask tinted with the page's ink: `k_col_primary` on the dark page and `k_col_bg`
  on the white one. It is centred on the drawer's button column (screen x=64) and on the
  band's own middle, so it sits at (27,21) on the screen and reads as the drawer's mark.
- `screen_input.c` — the touch policy: a press and a one-finger gesture (plus the touch
  adapter's two-finger swipe) become one `screen_intent_t`, with a latch that keeps one
  touch from being two intents. It names no screen and calls nothing above itself; the
  view's sink maps the intents: the horizontal swipes toggle the transport drawer on the
  dark page, a down swipe shows it on the white page, an up swipe takes it away (or raises
  the peek), and two fingers are the menu.
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
  bound to LVGL as the display's pointer. It feeds LVGL's multi-touch recognizer, so
  `screen_input.c` can tell a two-finger swipe from a one-finger one: a one-finger
  right/left swipe slides the transport drawer in/out, and the vertical pair is the
  controls' ladder and the peek's - down shows the controls (or puts an open peek away),
  then turns the white page's cover on, and up turns the cover off before it hides the
  controls (or raises the peek). A two-finger swipe is the VU meter's toggle;
  it is
  still reported so it cannot fall through to the one-finger handler. Next and previous
  are the left drawer's `<` and `>`, never a screen gesture or a tap.
- `assets/labels/k7b.png` — the K7 mark in the controls header. It is greyscale with an
  alpha plane and only the alpha is exported, as `assets/header_logo.bin` (A8, 75x40),
  by `tools/convert_cassette_assets.py`; CMake embeds that blob and `cassette_header.c`
  tints it with the page's ink.
- `assets/labels/cover.jpg` — the old Artwork Case, parked with the Label cover and
  Artwork screens and not embedded. The white page's cover does not use it; the cassette's
  frames are the frame. `tools/convert_cassette_assets.py` is still the way the old
  label-cover blob is made.

**The Menu.** A tap on the DJ block's M opens a mode of the controls: the cassette area
becomes the window's five-row page, the block becomes one Pad, the drawer becomes
Back/Enter/Close, and the header and footer carry the list's title and what is playing. The
model is portable - `components/portable/menu`, host test `t18_menu` - and owns the tree,
the band, the window and the ratchet; `cassette_menu.c` draws five row slots, executes its
actions and hides the cassette container for the mode's lifetime. The Pad is a white
plate with a dark inner (inset 8 px) and three dotted guides inside it, over the block's
place: a vertical drag ratchets one row per 56 px
(1x/2x/4x by speed), a tap activates the band, and the plate does not bubble gestures so a
drag cannot close the Menu.
The root reaches Tracks, Browse, Shuffle (a mode of the broadcast, ADR 0023) and Settings
(Backlight, Station, Library, Rescan, About); Backlight pins the panel with a hold until
reboot. The five new icon glyphs are not in `xtape.ttf` yet: the view hides a glyph the font
does not carry. The tree and rules are in
[`docs/specs/menu.md`](../../../docs/specs/menu.md), with ADR
[0022](../../../docs/adr/0022-the-menu-is-a-controls-mode.md) and
[0023](../../../docs/adr/0023-shuffle-is-a-broadcast-mode.md).

The panel and the headless path are two adapters behind the same interface, selected by
`CONFIG_APP_BOARD_SUNTON_8048S050C` in this component's CMake. The touch adapter and the
artwork loader are a second, input-side pair under the same selection: a board without a
controller simply has no pointer and no peek. A third panel promotes that selection to a
`display_port` seam without changing `screen_display_start()`.

## Bare and the peek

The cassette screen has one screen: Bare, the cassette alone. At the entry 100% pose
(256/256) the up swipe raises the peek - the timeline bar and the two floating tags - and a
down swipe puts it away; an up swipe while it is up does nothing. The peek can only be
raised from that 100% pose: while the transport drawer holds the 134/256 control pose (or
the peek's own 200/256 is still settling) the up swipe does nothing. The down swipe's other
job, when no peek is up, is the controls: it shows the drawer and the DJ block on the white
page, and an up swipe with the controls out takes them away again. Once that page is white,
a further down swipe turns the Track's cover on and the up swipe turns it off before it
closes the controls; every way off the page takes the cover with them. The Label cover and
Artwork screens that used to sit below Bare are gone, with their two down gestures and the
Level machine.

The peek's own 260 ms slide is kept: it moves a 568x52 bar and two 504x44 tags, not the
panel. Its cassette shrink re-places every part in the same 260 ms — geometry only, no
bitmap and no re-sampled pixels — so what it costs is a redraw of the cassette, not of
the panel's picture. `cassette_view_set_scale(scale, 0, dy, 0)` is the instant form if
that ever reads as a stutter.

The Track's picture and text tags come from `artwork_loader.c`, which the panel board
starts after the view. A Track with no usable picture simply shows no cover; a Track with
no tags keeps the peek's filename fallback.

## The peek

The peek is the one overlay on Bare, owned by `cassette_peek.c`. It has three things:
the bar, the tags and the tethers.

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

The three buttons at the left edge live in `cassette_buttons.c`. The drawer is a 120 px
panel, hidden off-screen at rest; either horizontal swipe slides it in from the entry; while
it is out, either slides it out, both over 260 ms. The vertical swipes do the same from the
ladder: a down swipe slides it in (on the white page), an up swipe slides it out. The entry view is the untouched
full-scale cassette; while
the drawer is out the view puts it at 134/256 (`CASSETTE_SCALE_BUTTONS`), which brings the
tape's box to 402x232 px at x=148..550: the 120 px drawer is clear on the left and the
190 px DJ block at 594..784 has room on the right. The DJ block is shown and hidden with the
drawer, and the pose runs over the same 260 ms. The three buttons start 76 px from the top,
so the middle button `<` is on y=218; the -20 px dy puts the cassette's centre near that line.
The drawer wins over the peek while it is out; when it slides away the peek's 200/256 and
-60 px lift come back.

Each button is 96x96, rounded by 8 px, with a 64 px circle at its centre and P, `<` or `>`
centred in the circle in Jura 36. The rectangle uses `k_col_button_rect`, filled opaque
with the page's colour. Each button carries **its own cap** - `cassette_ink_t`, shared with
the DJ block's M and R, and the white page's alone: the face of the disc there and the letter
drawn on it. All of them start in `k_col_dome`'s neutral grey with the letter `k_col_bg`
(black), and `cassette_buttons_set_cap()` changes one button at a time (both of its states,
repainted for the page it is on). The dark page is not the caps': it is the palette's line art,
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
Both states share the dome; the press still shows through the click pulse, not through a
style change. `LV_EVENT_PRESSED` shows the
pressed state; release or `LV_EVENT_PRESS_LOST` restores the other. A click runs a 160 ms
scale pulse (256 -> 232 -> 256); `<` and `>` also call `mini_icecast_prev()` /
`mini_icecast_skip()`. P is still a placeholder; the transport's next/previous are these
two arrows now.

The bands across the top and the bottom of the controls screen live in `cassette_header.c`
and `cassette_footer.c`: 768x71 at (16,6) and 768x115 at (16,359), a 2 px border rounded by
the buttons' 8 px in the drawer's carcase ink. The header's bottom edge meets the first
button's and the first DJ cell's top line, the footer's top edge their bottom line, and both
bands' sides sit on the cassette frame's 16 px inset, so the drawer, the block and the two
bands read as one frame. Their fill is opaque and follows the page - `k_col_bg` on the dark
screen, `k_col_white` on the white one, which is what makes them read as part of the page
they close and lets them cover the tape while they slide over it. They arrive with the rest
of the chrome: the header drops from just above the panel and the footer rises from just
below it, both over the drawer's 260 ms ease-out, and both are hidden when their slide out
completes. A reversal starts from wherever the band is, like the drawer's own slide.

The header carries the K7 mark on the left. `assets/labels/k7b.png` is greyscale art with
transparency; the asset tool keeps only its alpha plane (A8, 75x40, 76 px stride) and the
firmware tints that mask with `lv_obj_set_style_image_recolor()`: `k_col_primary` on the
dark page and `k_col_bg` on the white one, flipped by the same `set_page()` call as the
band's fill. Its horizontal centre is the drawer button column's (screen x=64) and its
vertical centre is the band's (y=41.5), so the 75x40 mask sits at (11,15) in the band and
(27,21) on the screen - above the P button, inside the frame.

The third rectangle of the frame is the window around the picture, `cassette_window.c`:
486x284 at (110,76), its top and bottom edges on the bands' lines and its sides in the gaps
either side of the scaled tape. It has no edge to come in from - sweeping a window over the
tape would read as a mistake - so it fades in and out over the same 260 ms, from whatever
opacity it holds if it is reversed, and hides at the end of the fade out.

The drawer is chrome above the peek. It is created after `cassette_levels`, and its panel
and button roots bubble gestures, so a swipe that starts on the drawer still reaches
`screen_input`, and they bubble events, so a press clears the two-finger latch. A button
press cannot fall through as a tap: a click on a child is the child's, and the screen no
longer gives a click any meaning.

## The white page

The white page belongs to the down swipe, not to the controls. A down swipe that brings the
drawer and the DJ block in paints the screen's backdrop `k_col_white` (0x909090) instead of
`k_col_bg` (0x111111), so the orange line art and the cassette's window chips read as ink on
paper; the horizontal swipes keep showing the same controls on the dark screen, and every way
out - the up swipe or either horizontal one - returns the page to dark. A down swipe while the
controls are already out (from a side swipe) leaves them where they are and only turns the
page; once it is white, the next down swipe turns the cover on. The view owns the flip
(`page_set()`, held in a flag so a repeat request costs
nothing), and it runs in one frame as the drawer's slide starts - a colour animation would
repaint the whole 800x480 panel on every step. The flip writes the screen's backdrop, the
opaque fills of the two bands, the drawer's three buttons and the DJ block's six cells, the
tape's window chips, and each button's circle (line art or dome) and letter. Both ways in and out go through
`controls_set_shown(shown, white)`, and only the down swipe passes `white`. Opening the
controls closes an open peek, so neither controls page carries the peek's black bar and tags.

The white is a margin: the flip also reveals `s_screen_back_cover`, a square-cornered
768x448 `k_col_bg` box at (16,16) that is the screen's first child, so on the white page the
picture keeps its dark ground and the white reads as the 16 px ring around everything.

The same page carries the **cover** when the Track has one. The down swipe turns it on and
the up swipe turns it off before it closes the controls; every flip back to dark hides it.
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
controls pose scales it down, not up. The mask is built once at that pose (402x188) and the
down swipe only shows the label when the pose has landed.

The down fades the label in and the up fades it out over 1 s
(`lv_anim_path_ease_in_out`), on the container's `opa_layered`, so the mask and the picture
go together. A page flip or a Track change cuts it instead: the loader reuses the one cover
buffer and the pose leaves the mask's size, so there is nothing to fade over.

One more thing belongs to that page, and only to it: the **edge mark**, a child of the
cassette created after the meter and before the label, so it sits between the cinta and the
label. It is a 466x272 rectangle centred on the tape at the controls pose (via `cassette_x/y`),
and it has two parts. Its border is an L - top and left lit in `k_col_glass`, right and bottom
transparent (`EDGE_SIDES`). Its interior is a **linear gradient at 135 degrees**, white at the
top-left corner fading to transparent at the bottom-right, with the two stops carrying the
opacity. That gradient needs `CONFIG_LV_USE_DRAW_SW_COMPLEX_GRADIENTS`, which `sdkconfig.defaults`
now turns on: LVGL ignores `LV_GRAD_DIR_LINEAR` without it. All of the mark's numbers are
`EDGE_*` and `EDGE_GLOW_*` at the top of `cassette_view.c`.

Being the middle layer, the mark lights the cinta (including the tape exposed below the
frame) and is covered by the label inside the outer frame; the label's window lets it show
through. The peek and the chrome are screen children above the cassette.

The mark does not arrive with the chrome: `page_set()` starts a delayed fade, so it waits
`EDGE_GLOW_DELAY_MS` (260 ms - the drawer's, the block's, the bands' and the window's
entrance) and then fades in over `EDGE_GLOW_FADE_MS`, its object opacity multiplying the
gradient's stops and the border together. Closing the controls, or a page flip back to dark,
cancels the fade and hides the mark at once.

The chips along the tape's bottom are windows on whatever is behind the cassette. That is
`k_col_bg` on both pages - the dark screen at rest, and the back cover on the white page - so
they are filled with `k_col_bg` and the page flip does not touch them.

## The DJ block

The six cells at the right edge live in `cassette_eq.c`. Two touching 96 px columns at
x=594..784 (the first column nudged 2 px right; the second stays at 688), three rows at
y=76/170/264, so the block is 190x284 and its right edge leaves
16 px to the screen. The 16 px is the inset of the 64 px circle inside each carcase, not a
gap: the carcases share their borders like the left drawer's. The first column is GAIN
above M and R; the second is HI, MED and LOW. All six cells are carcases over the page's
opaque fill (`cassette_eq_set_page()`), so the block covers the tape it slides across: the
page's own fill, except the four knobs' cells, which are the knob plate on the white page only -
`k_col_knob_rect` filling them and `k_col_bg` for their border, so no carcase line runs round a
knob. M's and R's cells keep the page's fill and the drawer's `k_col_button_rect` border, and
so does every cell on the dark page. M and R carry their own caps, like the drawer's buttons, and those are the
white page's alone: on the dark page both are the palette's playing red line art like
everything else there; on the white page M's cap is `k_col_bg` (black) and R's
`k_col_button_r`, and both letters on them are
`k_col_button_white` (`0xEEEEEE`) - they are the only two buttons whose letter is not the
page's black, because they are the only two whose cap is not the page's grey. On the white page
the four knobs' 44 px circles and M's and R's 64 px circles take the cap face - the same two
shadows: the knobs in `k_col_knob_cap` (much darker than the buttons' grey so a knob does not
read as a button, and with no rim), M in black, R in its own red. M's and R's 4 px
rim is `k_col_dome`'s grey, the drawer buttons' cap rim, so each cap reads as a disc let into
it, and the knobs' own plate - `k_col_knob_rect` inside a `k_col_bg` border - is what makes
them read as knobs rather than buttons. The knobs' crown and pointer keep the EQ inks. The block is hidden at rest, so the entry
view is the full-scale cassette; the view shows it with the transport drawer, when the cassette
is already at the 134/256 control pose. It slides in
from the right edge over the same 260 ms as the drawer and slides back out when the drawer
leaves.

Each knob is a 44 px circle - 30% smaller than M's and R's 64 px one - with a 24-dot
crown at r=32 outside it and a line from the centre towards the rim (the owner's tuned
4 px width). 0 dB is at twelve
o'clock; the right side cuts and the left side boosts, and the arc lights from twelve to
the line on the side it points.

A tap on a knob opens edit mode after a 550 ms delay: a 3x virtual knob (284 px at x=206,
the 96+94+94 length, its top on the GAIN button's top, a 2 px circle stroke and a centre
line, with no outer square - only the circle, the crown and the line) appears over
the cassette, while a transparent 800x480 overlay owns the touch. The cassette cuts out and
the knob appears; on close the knob disappears and the cassette cuts back in. The delay is
there so a second tap is read as the reset double click instead of opening the knob. Only
the big knob's centre circle closes. A click on the ring - the 284 px area around that
circle - is the target point: the knob jumps to that angle, 0 dB at twelve, clockwise
cutting and counter-clockwise boosting. A click outside does nothing; there is no drag. A
double tap on any small knob resets it to 0 dB and cancels the pending open, so the virtual
knob never appears. While the overlay is up nothing else responds - the placeholders, the
drawer and the peek are all behind it - and the view's sink drops every screen intent
too, because the two-finger menu arrives from the touch adapter directly. Both the small
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

The meter starts hidden. A two-finger swipe left fades it in and a two-finger right fades
it out and hides it, over 220 ms; the fade is the whole meter panel's layered opacity, so
the two rows, badges and bars go together. A vertical two-finger swipe is neither and only
sets the one-finger latch.

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

# screen_display

The screen: the panel (or the headless framebuffer), LVGL's PSRAM allocator, the
cassette screen and, on the panel board, its touch controller and the card-side tags
loader. The public interface is `include/screen_display.h` — `screen_display_start()`
and nothing else. Everything else is private:

- `screen_display.c` — start-up, the draw buffer, the serial dump and the task that keeps
  the cassette current.
- `lvgl_mem.c` — the `LV_USE_CUSTOM_MALLOC` backend, PSRAM first (ADR 0004).
- `cassette_view.c` — the composition: the container under the screen root, the pose and
  its animation, the model's step and the intent sink. It owns no geometry and no peek
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
  `border_box`, `text`, `image`). Every object they build is decorative, so a press
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
  current design) and drawn before every other part of the body.
- `cassette_meter.c` — the VU meter, `create(parent)`, `place(pose)`, `set(frame)`. Two
  100x24 rows centred on the right hub, L on top and R below; each row is a rounded
  rectangle with an L/R badge, a 6 px gap and seven dim bars. One rounded lit fill grows
  from the left and ends in a semicircle. The panel is transparent, so the hub shows
  through.
- `cassette_levels.c` — the peek's host. Its interface is `create(screen, room)`,
  `apply(frame)`, `update(status)`, `toggle_peek()`, `set_tags()` and `set_hubs()`. The
  Label cover and Artwork screens and the Level machine are gone: Bare is the only screen.
  The peek needs room from the cassette (the view shrinks and lifts it so the bar clears
  the tape) and its tethers need the hub anchors; this module passes the view's room
  callback through and the view hands the anchors in on every pose step.
- `cassette_peek.c` — the up swipe's overlay: the timeline bar and the two floating tags.
  Its interface is `create(screen, room)`, `apply(frame)`, `set_path()`, `set_tags()`,
  `set_hubs()`, `show()` / `hide()` / `hide_now()` and `is_shown()`. The bar carries the
  Track's elapsed, a rounded fill and its total; the tags carry the group on the left and
  the song on the right, each tethered to its hub. The texts come from ID3/FLAC tags read
  by the loader, with the artist directory and the filename as the fallback. One
  animation bobs both tags; each tether is a static line whose lower end follows the hub.
- `cassette_buttons.c` — the transport drawer: the three line-art buttons P, R, W, each
  with a prebuilt pressed and an unpressed state, and the left-edge panel that slides in
  and out. Its interface is `create(screen, room)`, `set_shown()`, `is_shown()` and
  `raise()`. A press swaps states and a click pulses the button's scale. The buttons are
  placeholders: they wire no transport action. The drawer reports its visibility through
  the `room` callback; the view turns that into the 134/256 control pose and shows the DJ
  block with it.
- `cassette_eq.c` — the DJ block at the right edge: six 96 px carcases in two touching
  columns, the same carcase as P/R/W. The first column is GAIN above the < and > arrows;
  the second is HI, MED and LOW. Each knob is a 44 px circle with a 24-dot crown outside
  it and a line from the centre towards the rim; a tap opens a 3x virtual knob and a click on
  its ring sets the target angle. The arrows are the only next/previous controls and call
  `mini_icecast_prev()` / `mini_icecast_skip()`. Its interface is `create(screen)`,
  `raise()` and `is_editing()`.
- `screen_input.c` — the touch policy: a press and a one-finger gesture (plus the touch
  adapter's two-finger swipe) become one `screen_intent_t`, with a latch that keeps one
  touch from being two intents. It names no screen and calls nothing above itself; the
  view's sink maps the intents: the horizontal swipes show and hide the transport drawer,
  an up swipe toggles the peek, a down swipe has no meaning, and two fingers are the menu.
  A tap is not an intent: next and previous are the DJ block's own cells, and the view
  drops every intent while a knob is in edit mode.
- `tags_loader.c` — the card side of the peek's tags: reads the Track's text tags
  (TPE1/TIT2 in an MP3, ARTIST/TITLE in a FLAC Vorbis comment) on its own task and hands
  them to the view under the LVGL lock. The cover pipeline that used to share this loader
  is parked in `artwork_loader.c` and is not built.
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
  right/left swipe slides the transport drawer in/out, an up swipe toggles Bare's peek,
  and a down swipe has no meaning. A two-finger swipe is reserved for the menu; it is
  still reported so it cannot fall through to the one-finger handler. Next and previous
  are the DJ block's arrows, never a screen gesture or a tap.
- `assets/label_cover.bin`, `assets/cover.jpg` — parked with the Label cover and Artwork
  screens and not embedded in the firmware. They stay for when those screens come back;
  `tools/convert_cassette_assets.py` is still the way they are made.

The panel and the headless path are two adapters behind the same interface, selected by
`CONFIG_APP_BOARD_SUNTON_8048S050C` in this component's CMake. The touch adapter and the
tags loader are a second, input-side pair under the same selection: a board without a
controller simply has no pointer and no peek. A third panel promotes that selection to a
`display_port` seam without changing `screen_display_start()`.

## Bare and the peek

The cassette screen has one screen: Bare, the cassette alone. The up swipe raises the
peek - the timeline bar and the two floating tags - and the next up swipe puts it away.
A down swipe does nothing; the Label cover and Artwork screens that used to sit below
Bare are gone, with their two down gestures and the Level machine.

The peek's own 260 ms slide is kept: it moves a 568x52 bar and two 504x44 tags, not the
panel. Its cassette shrink re-places every part in the same 260 ms — geometry only, no
bitmap and no re-sampled pixels — so what it costs is a redraw of the cassette, not of
the panel's picture. `cassette_view_set_scale(scale, 0, dy, 0)` is the instant form if
that ever reads as a stutter.

The cover pipeline (the Track picture, the Case backdrop and their buffers) is parked in
`artwork_loader.c`; the file and the assets are kept, but nothing in this build uses
them. The peek's tags still come from the Track's container through `tags_loader.c`.

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
side): the group at (148,12) above the left hub, the song at (148,248) below it, their
centres 118 px from the hub's centre on either side.
Sixteen px of inset leaves 472 px of text, enough for a long title; a longer one is cut
with dots, not scrolled. The peek uses a second Jura face at 28 px (`font_jura_28.c`);
there is no 36 px band left on the screen. Both tethers leave on the left hub's column
(251 px; 20.4 % of the centred 504 px pill rather than a round quarter, so the lines stay
vertical): the group's from its bottom to the top rim, the song's from its top to the
bottom rim.
The texts are the Track's ID3/FLAC tags, read by `tags_loader`: TPE1 (artist, with TPE2
as its fallback), TIT2 in an MP3; ARTIST (with ALBUMARTIST as its fallback), TITLE in a
FLAC Vorbis comment. Until they land - and for a Track that carries none - the group is
the first directory of the Track's relative path and the song is the cleaned filename. A
label wider than its pill is truncated with dots, not scrolled: a marquee would animate
every frame the peek is up. A `—` stands in for a missing half; when both are missing the
tags and tethers are hidden rather than floating empty.

Each tether is a two-point `lv_line` from 14 px inside its pill to the left hub's near
rim: the group from its bottom to the top rim, the song from its top to the bottom rim.
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
panel, hidden off-screen at rest; a one-finger right swipe slides it in and a left swipe
slides it out, both over 260 ms. The entry view is the untouched full-scale cassette; while
the drawer is out the view puts it at 134/256 (`CASSETTE_SCALE_BUTTONS`), which brings the
tape's box to 402x232 px at x=148..550: the 120 px drawer is clear on the left and the
192 px DJ block at 592..784 has room on the right. The DJ block is shown and hidden with the
drawer, and the pose runs over the same 260 ms. The three buttons start 76 px from the top,
so the middle button R is on y=218; the -20 px dy puts the cassette's centre near that line.
The drawer wins over the peek while it is out; when it slides away the peek's 200/256 and
-60 px lift come back.

Each button is 96x96, rounded by 8 px, with a 64 px circle at its centre and P, R or W
centred in the circle in Jura 36. Both states are built once: the unpressed state is the
playing red at 2 px, the pressed one the cream ink at 3 px. `LV_EVENT_PRESSED` shows the
pressed state; release or `LV_EVENT_PRESS_LOST` restores the other. A click runs a 160 ms
scale pulse (256 -> 232 -> 256) and does nothing else: the buttons are placeholders, and
the transport is not wired.

The drawer is chrome above the peek. It is created after `cassette_levels`, and its panel
and button roots bubble gestures, so a swipe that starts on the drawer still reaches
`screen_input`, and they bubble events, so a press clears the two-finger latch. A button
press cannot fall through as a tap: a click on a child is the child's, and the screen no
longer gives a click any meaning.

## The DJ block

The six cells at the right edge live in `cassette_eq.c`. Two touching 96 px columns at
x=592..784, three rows at y=76/170/264, so the block is 192x284 and its right edge leaves
16 px to the screen. The 16 px is the inset of the 64 px circle inside each carcase, not a
gap: the carcases share their borders like the P/R/W drawer. The first column is GAIN above
the `<` and `>` arrows; the second is HI, MED and LOW. The block is hidden at rest, so the
entry view is the full-scale cassette; the view shows it with the transport drawer, when the
cassette is already at the 134/256 control pose. It slides in from the right edge over the
same 260 ms as the drawer and slides back out when the drawer leaves.

Each knob is a 44 px circle - 30% smaller than the arrows' 64 px one - with a 24-dot
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
knob never appears. While the overlay is up nothing else responds - the arrows, P/R/W, the
drawer and the peek are all behind it - and the view's sink drops every screen intent
too, because the two-finger menu arrives from the touch adapter directly. Both the small
active circle and the big one blink their border 100% -> 27% -> 100%, 600 ms each way. The
arrows are the only next/previous controls and call `mini_icecast_prev()` /
`mini_icecast_skip()`; the screen's tap and the BOOT button no longer do either.

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

## The reel change

Eight seconds after a track boundary the model returns the rewind window: the packs
migrate back and the hubs turn in reverse (ADR 0011). The model also reports the window's
ink as `cassette_frame_t.tape_ink` (0 = the tape's own colour, 1 = the playing red, 1 when
not rewinding); `cassette_tapes.c` mixes each hub ring's border from `k_col_tape_dark` to
`k_col_tape` with it, and only writes the style when the value changes. Nothing else
fades: the meter, the title and the cassette are information, not tape. The model still
reports `rewind_permille`, and `t10_cassette` tests both the reel arithmetic and the
`tape_ink` ramp.

## Parked: the Track picture and the Case

The Artwork screen and its Label cover are removed. Their cover pipeline - the Track
picture locator (`components/portable/artwork`'s `artwork_locate()`, `id3.c`, `flac.c`),
the JPEG decode and fit, the Case backdrop and its buffers - is parked in
`artwork_loader.c` and is not built. The cover sources stay in
`components/portable/artwork` and the host test `t15_artwork` still builds and tests
them. `assets/label_cover.bin` and `assets/cover.jpg` stay in the tree unembedded for
when the screen comes back. The firmware no longer carries the TJpgDec include, the
45 KB Case JPEG or the 464 KB cover buffer: the app image dropped by about 1.2 MB.

The peek keeps its text tags through `tags_loader.c`, which uses only
`components/portable/artwork`'s `tags.c`; that is the part of the old loader still in the
build.

## Board-only properties

A host pass cannot establish the JPEG decode time or the card contention; those belong
to the parked Artwork pipeline, and its board measurements are in the git history. The
host tier covers the locators and the fit (`host/tests/t15_artwork.c`) and the design
grid (`host/tests/t16_layout.c`).

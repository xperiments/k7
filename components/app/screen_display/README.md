# screen_display

The screen: the panel (or the headless framebuffer), LVGL's PSRAM allocator, the
cassette screen and, on the panel board, its touch controller and the card-side artwork
loader. The public interface is `include/screen_display.h` — `screen_display_start()`
and nothing else. Everything else is private:

- `screen_display.c` — start-up, the draw buffer, the serial dump and the task that keeps
  the cassette current.
- `lvgl_mem.c` — the `LV_USE_CUSTOM_MALLOC` backend, PSRAM first (ADR 0004).
- `cassette_view.c` — the composition: the container under the screen root, the pose and
  its animation, the model's step and the intent sink. It owns no geometry and no Level
  state; the picture is built from the modules below. The cassette's drawing hangs off
  one full-bleed, unpainted container at 0,0, so every position in the parts stays a
  screen coordinate, and the Level sheets and the transport drawer are the container's
  siblings.
- `cassette_layout` (`components/portable/cassette`) — the design grid the screen is
  written on: the 800x480 grid, the scale (256 = pixel for pixel), the centring, the
  offset, the even-diameter rounding and the animation's interpolation. It has no LVGL
  include, so the host tier compiles it and `host/tests/t16_layout.c` pins it. The view
  keeps the current pose and passes it to every part on `place()`.
- `cassette_style.h` / `cassette_style.c` — the palette, one definition per colour, read
  by the view, the tapes, the meter, the Levels, the transport drawer and the DJ block.
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
- `cassette_levels.c` — the Level machine, the Label cover, the Artwork sheet and the
  screen backdrop. Its interface is `create(screen, room)`, `apply(frame)`,
  `update(status)`, `swipe_down()` / `swipe_up()`, `set_artwork()` / `set_case()`,
  `set_tags()` and `set_hubs()`. The peek needs room from the cassette (the view shrinks
  and lifts it so the bar clears the tape) and its tethers need the hub anchors; the
  Levels pass the room callback through and the view hands the anchors in on every pose
  step. The peek is the only room request; the resting pose already clears the drawer and
  the DJ block.
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
  the second is HI, MED and LOW. The four knobs are a 24-dot crown and a stick (0 dB at
  twelve o'clock, right cuts, left boosts); a tap enters a modal edit overlay, the drag is
  horizontal, and the active circle blinks. The arrows are the only next/previous controls
  and call `mini_icecast_prev()` / `mini_icecast_skip()`. Its interface is
  `create(screen)`, `raise()` and `is_editing()`.
- `screen_input.c` — the touch policy: a press and a one-finger gesture (plus the touch
  adapter's two-finger swipe) become one `screen_intent_t`, with a latch that keeps one
  touch from being two intents. It names no Level and calls nothing above itself; the
  view's sink maps the intents: the horizontal swipes show and hide the transport drawer,
  the vertical ones move the Levels, and two fingers are the menu. A tap is not an intent:
  next and previous are the DJ block's own cells, and the view drops every intent while a
  knob is in edit mode.
- `artwork_loader.c` — the card side of the Artwork level and of the peek's tags:
  locates a Track's own picture (`components/portable/artwork`), reads it, decodes the
  JPEG with TJpgDec and fits it to the Case's 603x385 window, on its own task. It also
  reads the Track's text tags (TPE1/TIT2 in an MP3, ARTIST/TITLE in a FLAC Vorbis
  comment) there, and hands them to the view under the LVGL lock. It decodes the
  embedded Case JPEG once at start. The Levels never touch the card or a decoder.
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
  right/left swipe slides the transport drawer in/out, a one-finger down/up swipe moves
  one Level through Bare, Label cover and Artwork, and at Bare an up swipe raises the peek.
  A two-finger swipe is reserved for the menu: it moves no Level today, but it is still
  reported so it cannot fall through to the one-finger handler. Next and previous are the
  DJ block's arrows, never a screen gesture or a tap.
- `assets/label_cover.bin`, `assets/cover.jpg` — both placed by
  `tools/convert_cassette_assets.py`. The label cover is generated and is RGB565A8 so its
  transparent shapes show the reels. `cover.jpg` is the Case, copied unchanged and
  embedded as the original 45 KB JPEG: the loader decodes it once at start instead of
  carrying a 768 KB raw RGB565 copy in flash. The hubs are not an asset: the tapes draw
  them, a static rim with a divided circle turning inside it, so there is no hub blob to
  embed (the measurements behind that are in `docs/research/hub-drawn-vs-blitted.md`).

The panel and the headless path are two adapters behind the same interface, selected by
`CONFIG_APP_BOARD_SUNTON_8048S050C` in this component's CMake. The touch adapter and the
artwork loader are a second, input-side pair under the same selection: a board without a
controller simply has no pointer and no Artwork level. A third panel promotes that
selection to a `display_port` seam without changing `screen_display_start()`.

## The three Levels

The cassette screen has three Levels, and exactly one sheet is shown at a time:

| Level | Sheet | What is on screen |
| --- | --- | --- |
| Bare | none | the cassette alone; an up swipe raises the peek (timeline and tags) |
| Label cover | `k7logo` full-bleed | the K7 sticker; its tape windows show the reels |
| Artwork | the Case + the Track picture | `cover.jpg` full-bleed; the picture fills its window, 603x385; the title below |

`cassette_levels.c` owns the machine, and a table indexed by Level carries each sheet's
rest and hidden position, whether reaching it asks for the Track's picture, and what the
backdrop does on arrival. A one-finger down/up swipe moves one Level - down
from Bare to the Label cover to Artwork, up the other way - and the ends do not wrap. At
Bare the up swipe has no Level above to move to, so it raises the peek: the timeline bar
slides up and the cassette shrinks to `CASSETTE_SCALE_LABEL` so the bar clears the tape,
both over the same 260 ms. The two tags appear with it - the group above the left hub,
the song below it - and bob out of phase. While the peek is up, either vertical
swipe slides it out; a down swipe only moves to the Label cover once the peek is away.
Any Level change puts it away at once. The peek is an overlay on Bare, not a fourth Level.

The two-finger swipe is reserved for the menu: it moves no Level today, and because
`screen_input.c` still latches it, the same touch cannot also trip the one-finger gesture
or the click. The Level changes on release and it changes in one frame: the target sheet
is placed at rest and every other sheet is hidden before LVGL draws. There is no slide.
The 300 ms slide was measured as a stutter: each frame of it moved a full-screen sheet, a
1.15 MB read and a 0.77 MB write, and the RGB panel's scanout wants the same PSRAM. The
peek's own 260 ms slide is kept: it moves a 568x52 bar and two 504x44 tags, not the
panel. Its cassette shrink re-places every part in the same 260 ms — geometry only, no
bitmap and no re-sampled pixels — so what it costs is a redraw of the cassette, not of
the panel's picture. `cassette_view_set_scale(scale, 0, dy, 0)` is the instant form if
that ever reads as a stutter.

## The peek

The peek is the one overlay on Bare, owned by `cassette_peek.c`. It has three things:
the bar, the tags and the tethers.

The bar is the old Label's paper strip, 568x52 at (116,424) with a 12 px radius. It no
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
with dots, not scrolled. The peek uses a second Jura face at 28 px (`font_jura_28.c`); the
cover and Artwork bands keep 36. Both tethers leave on the left hub's column (251 px;
20.4 % of the centred 504 px pill rather than a round quarter, so the lines stay
vertical): the group's from its bottom to the top rim, the song's from its top to the
bottom rim.
The cover and Artwork band titles use the same dot truncation, so no marquee runs anywhere
on the screen. The texts are the Track's ID3/FLAC tags, read by the loader:
TPE1 (artist, with TPE2 as its fallback), TIT2 in an MP3; ARTIST (with ALBUMARTIST as its
fallback), TITLE in a FLAC Vorbis comment. Until they land - and for a Track that carries
none - the group is the first directory of the Track's relative path and the song is the
cleaned filename. A label wider than its pill is truncated with dots, not scrolled: a
marquee would animate every frame the peek is up. A `—` stands in for a missing half;
when both are missing the tags and tethers are hidden rather than floating empty.

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
-60 px lift; the tags appear with the first frame, the bar runs its 260 ms slide, and a
Level change puts everything away in one frame.

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

The drawer is chrome above every sheet. It is created after the Levels, and the view raises
it (together with the DJ block) after a vertical swipe because the Artwork sheet raises
itself when it settles. Its panel and button roots bubble gestures, so a swipe that starts on
the drawer still reaches `screen_input`, and they bubble events, so a press clears the
two-finger latch. A button press cannot fall through as a tap: a click on a child is the
child's, and the screen no longer gives a click any meaning.

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
crown at r=32 outside it and a 6x22 line from the centre to the rim. 0 dB is at twelve
o'clock; the right side cuts and the left side boosts, and the arc lights from twelve to
the line on the side it points.

A tap on a knob opens edit mode after a 550 ms delay: a 3x virtual knob (284 px at x=206,
the 96+94+94 length, its top on the GAIN button's top, a 2 px circle stroke and a 6 px
centre line, with no outer square - only the circle, the crown and the line) appears over
the cassette, while a transparent 800x480 overlay owns the touch. The cassette cuts out and
the knob appears; on close the knob disappears and the cassette cuts back in. The delay is
there so a second tap is read as the reset double click instead of opening the knob. Only
the big knob's centre circle closes. A click on the ring - the 284 px area around that
circle - is the target point: the knob jumps to that angle, 0 dB at twelve, clockwise
cutting and counter-clockwise boosting. A click outside does nothing; there is no drag. A
double tap on any small knob resets it to 0 dB and cancels the pending open, so the virtual
knob never appears. While the overlay is up nothing else responds - the arrows, P/R/W, the
drawer and the Levels are all behind it - and the view's sink drops every screen intent
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

## The Track picture and the Case

`artwork_loader.c` asks `components/portable/artwork` for the picture's offset and
length — an ID3v2.3/2.4 `APIC` frame in an MP3, or a native FLAC `PICTURE` block,
preferring the front-cover type — then reads it, decodes it and fits it to the Case's
window: (101,23) to (704,408), 603x385. The fit is cover, not stretch: a non-square
picture is centre-cropped, never squeezed. The window is the cyan rectangle of
`assets/labels/cover.jpg`, so the Track picture replaces it and the case is the backdrop.

The Case is one opaque 800x480 JPEG, embedded in the image. The loader decodes it once
at start into one 768 000 B PSRAM buffer and hands the descriptor to the view's
`cassette_view_set_case()`, which forwards it to the Levels; if the decode fails, the
Artwork screen keeps its flat `k_col_art_bg` backdrop. The title sits on the free band under
the case, at y=424.

Only JPEG is decoded. TJpgDec is called directly on the encoded bytes rather than through
LVGL's image decoder, which wants a file or the memfs driver and cannot hand back the
single fitted buffer the screen needs. A PNG picture is located and refused with a log
line: it is rare in an MP3, and the window does not justify LodePNG's full-size buffer.

Memory is the project's scarce resource, so:

- The cover is one 603x385 RGB565 buffer (464 310 B of PSRAM), allocated once and reused
  for every Track. `cassette_levels.c` hides the picture when a new Track is requested, so
  the decode writes it while the panel is not drawing it; a second buffer would not fit.
- The Case is one 800x480 RGB565 buffer (768 000 B) in PSRAM, decoded once at start.
  Embedding the 45 KB JPEG instead of a raw RGB565 blob is what keeps the app inside its
  4 MB partition.
- The Track's JPEG is read from the card **on demand**: TJpgDec asks for a few hundred
  bytes at a time, so the encoded picture is never in PSRAM. The only workspace is a band
  (width x 16 rows of RGB888) for a streaming fit, or a crop buffer no larger than the
  window for an upscale; both are freed after the decode.
- A picture whose crop is at least the window's size is box-filtered from TJpgDec's MCU
  bands into that buffer (`artwork_cover_*`): no full-size RGB buffer exists even for a
  4000x4000 JPEG. A smaller picture is decoded into the crop buffer and upscaled with
  the portable bilinear fit.
- The decode runs on core 0 at priority 2, below the screen and the network. The file
  reads yield every 16 KB and back off while `mini_icecast_lag_ms()` says the broadcast
  is behind: the VFS holds the volume lock for one `fread`.
- A newer Track abandons the old read and decode instead of making the new picture wait.

A Track with no usable picture leaves the Case's cyan window showing, with the title still
on screen. The pictures live in PSRAM; the descriptors are handed to the view under the
LVGL lock, so the Levels stay free of stdio, FATFS and the decoder.

## Board-only properties

A host pass cannot establish the JPEG decode time, the PSRAM peak while a cover is read
and decoded, or the card contention while the stream is running. Those are measured on
the panel, and the numbers belong in the README's board tier. The panel pass has recorded
the decode: the first card track's 239x236 `APIC` (22,080 B) became the 603x385 window in
691 ms and showed, with 504,300 B of PSRAM left after the cover buffer. The host tier
covers the locators and the fit (`host/tests/t15_artwork.c`) and the design grid
(`host/tests/t16_layout.c`); the card contention and the PSRAM peak are the remaining
board-only properties to watch.

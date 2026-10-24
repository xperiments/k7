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
  screen coordinate, and the Level sheets are the container's siblings.
- `cassette_layout` (`components/portable/cassette`) — the design grid the screen is
  written on: the 800x480 grid, the scale (256 = pixel for pixel), the centring, the
  offset, the even-diameter rounding and the animation's interpolation. It has no LVGL
  include, so the host tier compiles it and `host/tests/t16_layout.c` pins it. The view
  keeps the current pose and passes it to every part on `place()`.
- `cassette_style.h` / `cassette_style.c` — the palette, one definition per colour, read
  by the view, the tapes, the meter and the Levels.
- `cassette_widgets.h` / `cassette_widgets.c` — the LVGL object helpers (`box`,
  `border_box`, `text`, `image`). Every object is decorative at construction, so the
  screen is the one hit-test target and there is no finished-tree walk.
- `cassette_tapes.c` — the tape body: the two packs, the two strands, the rollers, the
  run, the two hubs and, on the panel board, the two border frames. Its interface is
  `create(parent)`, `place(pose)`, `set(frame)`. A pack is one object whose draw callback
  paints every ring its fill puts there, so a fill change invalidates two boxes rather
  than one per circle; each hub is a rim painted once plus two opposite spokes, so an
  invalidation stays in one quadrant; the run is its own line, so a fill change repaints
  two narrow sides. The tape leaves each pack on the outer tangent, wraps a 30 px roller
  whose bottom sits 16 px above the screen edge, and joins the run between the two
  rollers.
- `cassette_meter.c` — the VU meter, `create(parent)`, `place(pose)`, `set(frame)`. An
  8-cell lattice of 10 px steps and one column per channel, twelve segments to a column
  filling from the bottom; the panel is transparent, so the cassette shows through.
- `cassette_levels.c` — the Level machine, the Label, the Label cover, the Artwork sheet
  and the screen backdrop. Its interface is `create(screen, room)`, `apply(frame)`,
  `update(status)`, `swipe_down()` / `swipe_up()`, and `set_artwork()` / `set_case()`.
  The Label needs room from the cassette (the view shrinks and lifts it so the bar clears
  the tape), and it asks through the one `room` callback rather than touching the pose.
- `screen_input.c` — the touch policy: a press, a click and a one-finger gesture (plus the
  touch adapter's two-finger swipe) become one `screen_intent_t`, with a latch that keeps
  one touch from being two intents. It names no Level and calls nothing above itself; the
  view's sink maps the intents to the transport and the Level swipes.
- `artwork_loader.c` — the card side of the Artwork level: locates a Track's own picture
  (`components/portable/artwork`), reads it, decodes the JPEG with TJpgDec and fits it to
  the Case's 603x385 window, on its own task. It also decodes the embedded Case JPEG once
  at start. The Levels never touch the card or a decoder.
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
  `screen_input.c` can tell a two-finger swipe from a one-finger one: a tap is next
  track, a one-finger left/right swipe is next/previous, a one-finger down/up swipe moves
  one Level through Bare, Label cover and Artwork, and at Bare an up swipe peeks the
  small title bar. A two-finger swipe is reserved for the menu: it moves no Level today,
  but it is still reported so it cannot fall through to the one-finger handler.
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
| Bare | none | the cassette alone, with no label; an up swipe peeks the small title bar |
| Label cover | `k7logo` full-bleed | the K7 sticker; its tape windows show the reels |
| Artwork | the Case + the Track picture | `cover.jpg` full-bleed; the picture fills its window, 603x385; the title below |

`cassette_levels.c` owns the machine, and a table indexed by Level carries each sheet's
rest and hidden position, whether reaching it asks for the Track's picture, and what the
backdrop does on arrival. A one-finger down/up swipe moves one Level - down
from Bare to the Label cover to Artwork, up the other way - and the ends do not wrap. At
Bare the up swipe has no Level above to move to, so it peeks the small title bar: the
first up swipe slides the cream bar in and shrinks the cassette to
`CASSETTE_SCALE_LABEL` so the bar clears the tape, both over the same 260 ms. While it is
up, either vertical swipe slides it out; a down swipe only moves to the Label cover once
the bar is away. Any Level change puts it away at once. The bar is an overlay on Bare, not
a fourth Level: it carries the same cleaned title as the K7 cover and the Artwork band.

The two-finger swipe is reserved for the menu: it moves no Level today, and because
`screen_input.c` still latches it, the same touch cannot also trip the one-finger gesture
or the click. The Level changes on release and it changes in one frame: the target sheet
is placed at rest and every other sheet is hidden before LVGL draws. There is no slide.
The 300 ms slide was measured as a stutter: each frame of it moved a full-screen sheet, a
1.15 MB read and a 0.77 MB write, and the RGB panel's scanout wants the same PSRAM. The
small title bar's own 260 ms slide is kept: it moves a 735x60 bar, not the panel. Its
cassette shrink re-places every part in the same 260 ms — geometry only, no bitmap and no
re-sampled pixels — so what it costs is a redraw of the cassette, not of the panel's
picture. `cassette_view_set_scale(scale, 0)` is the instant form if that ever reads as a
stutter.

## The VU meter

The status carries the RMS of the same 1024-frame chunk the encoder is about to take
(0..1, full scale), measured by `measure_level()` in `components/portable/mini_icecast`.
`components/portable/cassette` turns it into a peak-hold: `vu = max(level, vu * 0.90)`
per 40 ms update, about -23 dB/s, so the peak follows a real drop instead of hanging.

`cassette_meter.c` draws it as a segmented meter on the right, centred vertically in the
outer frame: an 8-cell lattice of 10 px steps and one column per channel, twelve segments
to a column filling from the bottom. The meter is 300 px tall, the mockup's 149 px box
scaled up, so a segment is 18 x 8 px. There is no panel behind it - the lattice and the
segments are the only things painted, so the cassette shows through. Lit segments are the
tape red, unlit the dim red the mockup uses, and the level is mapped over -48..0 dB so the
bars move like a meter rather than a linear amplitude. Only the segments are objects: a
level change recolours the cells that flipped and invalidates those few pixels, never the
lattice, and a column whose count has not changed is not touched at all.

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

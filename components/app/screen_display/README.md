# screen_display

The screen: the panel (or the headless framebuffer), LVGL's PSRAM allocator, the
cassette view and, on the panel board, its touch controller and the card-side artwork
loader. The public interface is `include/screen_display.h` — `screen_display_start()`
and nothing else. Everything else is private:

- `screen_display.c` — start-up, the draw buffer, the serial dump and the task that keeps
  the cassette current.
- `lvgl_mem.c` — the `LV_USE_CUSTOM_MALLOC` backend, PSRAM first (ADR 0004).
- `cassette_view.c` — the LVGL objects, layout, Level machine and touch actions; the tape
  arithmetic is `components/portable/cassette`. The Label cover is
  `assets/labels/k7logo.png`, an RGB565A8 800x480 image, full-bleed at 0,0. Its black
  triangle and circle are transparent, so the reels show through them. While it is up the
  wave view is hidden behind it with the reels. The Artwork level is the cassette Case, `assets/labels/cover.jpg`, full-bleed at
  0,0, with the Track picture in its window at (101,23), 603x385, and the title on the
  band below.
- `artwork_loader.c` — the card side of the Artwork level: locates a Track's own picture
  (`components/portable/artwork`), reads it, decodes the JPEG with TJpgDec and fits it to
  the Case's 603x385 window, on its own task. It also decodes the embedded Case JPEG once
  at start. The view never touches the card or a decoder.
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
  bound to LVGL as the display's pointer. It feeds LVGL's multi-touch recognizer, so the
  view can tell a two-finger swipe from a one-finger one: a tap is next track, a
  one-finger left/right swipe is next/previous, a one-finger down/up swipe moves one
  Level through Bare, Label cover and Artwork, and at Bare an up swipe peeks the small
  title bar. A two-finger swipe is reserved for the menu: it moves no Level today, but it
  is still reported so it cannot fall through to the one-finger handler.
- `assets/label_cover.bin`, `assets/cover.jpg` — both placed by
  `tools/convert_cassette_assets.py`. The label cover is generated and is RGB565A8 so its
  transparent shapes show the reels. `cover.jpg` is the Case, copied unchanged and
  embedded as the original 45 KB JPEG: the loader decodes it once at start instead of
  carrying a 768 KB raw RGB565 copy in flash. The hubs are not an asset: the view draws them, a
  static rim with a divided circle turning inside it, so there is no hub blob to embed
  (the measurements behind that are in `docs/research/hub-drawn-vs-blitted.md`).

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

A one-finger down/up swipe moves one Level - down from Bare to the Label cover to
Artwork, up the other way - and the ends do not wrap. At Bare the up swipe has no Level
above to move to, so it peeks the small title bar: the first up swipe slides the cream
bar in over the cassette. While it is up, either vertical swipe slides it out; a down
swipe only moves to the Label cover once the bar is away. Any Level change puts it away
at once. The bar is an overlay on Bare, not a fourth Level: it carries the same cleaned
title as the K7 cover and the Artwork band.

The two-finger swipe is reserved for the menu: it moves no Level today, and because the
view still latches it, the same touch cannot also trip the one-finger gesture or the
click. The Level changes on release and it changes in one frame: the target sheet is
placed at rest and every other sheet is hidden before LVGL draws. There is no slide. The
300 ms slide was measured as a stutter: each frame of it moved a full-screen sheet, a
1.15 MB read and a 0.77 MB write, and the RGB panel's scanout wants the same PSRAM. The
small title bar's own 260 ms slide is kept: it moves a 735x60 bar, not the panel.

## The VU meter

The status carries the RMS of the same 1024-frame chunk the encoder is about to take
(0..1, full scale), measured by `measure_level()` in `components/portable/mini_icecast`.
`components/portable/cassette` turns it into a peak-hold: `vu = max(level, vu * 0.90)`
per 40 ms update, about -23 dB/s, so the peak follows a real drop instead of hanging.

The view draws it as a segmented meter in the top right corner: an 8-cell lattice of 5 px
steps and one column per channel, twelve segments to a column filling from the bottom.
There is no panel behind it - the lattice and the segments are the only things painted, so
the cassette shows through. Lit segments are the tape red, unlit the dim red the mockup
uses, and the level is mapped over -48..0 dB so the bars move like a meter rather than a
linear amplitude. Only the segments are objects: a level change recolours the cells that
flipped and invalidates those few pixels, never the lattice, and a column whose count has
not changed is not touched at all.

## The reel change

Eight seconds after a track boundary the model returns the rewind window: the packs
migrate back and the hubs turn in reverse (ADR 0011). The view treats the tape's red edge
as a playing indication, so for the window it cross-fades the tape's own lines
from `COL_TAPE_DARK` to `COL_TAPE` and fades the VU out; both return over the window's
last second. `rewind_tape_opa()` turns the model's `rewind_permille` into that envelope,
and `tape_edge_color()` uses it to mix the edge colour. `tape_opa_apply()` sets the strand
lines and invalidates the packs, whose draw callback paints the solid arc. The progress bar and the titles do not fade:
they are chrome and information, not tape. The model is untouched: it still reports `rewind_permille`, and the host tier still tests the reel
arithmetic.

## The Track picture and the Case

`artwork_loader.c` asks `components/portable/artwork` for the picture's offset and
length — an ID3v2.3/2.4 `APIC` frame in an MP3, or a native FLAC `PICTURE` block,
preferring the front-cover type — then reads it, decodes it and fits it to the Case's
window: (101,23) to (704,408), 603x385. The fit is cover, not stretch: a non-square
picture is centre-cropped, never squeezed. The window is the cyan rectangle of
`assets/labels/cover.jpg`, so the Track picture replaces it and the case is the backdrop.

The Case is one opaque 800x480 JPEG, embedded in the image. The loader decodes it once
at start into one 768 000 B PSRAM buffer and hands the descriptor to the view; if the
decode fails, the Artwork screen keeps its flat `COL_ART_BG` backdrop. The title sits on
the free band under the case, at y=424.

Only JPEG is decoded. TJpgDec is called directly on the encoded bytes rather than through
LVGL's image decoder, which wants a file or the memfs driver and cannot hand back the
single fitted buffer the screen needs. A PNG picture is located and refused with a log
line: it is rare in an MP3, and the window does not justify LodePNG's full-size buffer.

Memory is the project's scarce resource, so:

- The cover is one 603x385 RGB565 buffer (464 310 B of PSRAM), allocated once and reused
  for every Track. The view hides the picture when a new Track is requested, so the
  decode writes it while the panel is not drawing it; a second buffer would not fit.
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
LVGL lock, so the view stays free of stdio, FATFS and the decoder.

## Board-only properties

A host pass cannot establish the JPEG decode time, the PSRAM peak while a cover is read
and decoded, or the card contention while the stream is running. Those are measured on
the panel, and the numbers belong in the README's board tier. The panel pass has recorded
the decode: the first card track's 239x236 `APIC` (22,080 B) became the 603x385 window in
691 ms and showed, with 504,300 B of PSRAM left after the cover buffer. The host tier
covers the locators and the fit (`host/tests/t15_artwork.c`); the card contention and the
PSRAM peak are the remaining board-only properties to watch.

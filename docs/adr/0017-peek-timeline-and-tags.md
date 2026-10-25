# The peek's bar becomes the timeline, and the names float on the cassette

The Bare screen's up swipe put one cream bar on the screen and the bar carried the
**Now playing** title. The product wants that bar to carry the track's clock instead -
position, elapsed, total - and wants the group and the song as two separate rounded
labels floating over the cassette, one per hub, each joined to its hub by a line. The two
labels move a little, and the movement has to stay cheap on a 800x480 panel whose screen
task already shares its core with the network.

## Decision

- **The bar is the timeline.** The same 568x52 strip at (116,424) now holds the elapsed
  time on the left, a 288x6 rounded track with a lit fill in the middle and the total on
  the right. With nothing playing it shows the screen's idle sentence. Its title is gone.
- **Two tags carry the names.** Two 504x44 pills, rounded by 22 px, black with the
  playing red's 2 px border and Jura 28, centred on the cassette at x=148..651 (61 px
  of cassette on each side): the group at (148,12) above the left hub, the song at
  (148,248) below it, their centres 118 px from the hub's centre on either side.
  Sixteen px of inset leaves 472 px of text, enough for a
  long title; a longer one is truncated with dots, not scrolled. The 28 px face is a
  second Jura generated for the peek; it is the only text size left on the screen.
- **Each tag hangs from the same hub.** A vertical two-point `lv_line` joins the pill to
  the left hub's near rim: both leave on the hub's column (251 px), which is 20.4 % of
  the centred 504 px pill rather than a round quarter. The hub's column wins over the
  fraction, so the tethers stay vertical. The group meets the top rim from its bottom,
  the song the bottom rim from its top. The pill end is under the pill's opaque fill, so the bob
  uncovers more or less of it; the hub end is the anchor the view asks
  `cassette_tapes_hub_anchor()` for on every pose step, so the tethers follow the cassette
  while it shrinks and lifts.
- **The texts are the Track's own tags.** The loader reads ID3v2.3/2.4 `TPE1` (artist,
  `TPE2` as the fallback) and `TIT2` in an MP3, and `ARTIST` (with `ALBUMARTIST`) and
  `TITLE` in a FLAC Vorbis comment, on its own task, from the same card side that reads
  the picture. Until they land, and for a Track that carries none, the group is the first
  directory of the Track's path and the song is the cleaned filename. A missing half shows
  an em dash; both missing hides the tags and their tethers. The broadcast never reads a
  file for the display.
- **The movement is one animation.** A five-second sine drives both tags +/-6 px in
  opposite phase. Its callback writes a tag's `y` only when the rounded pixel changes -
  about eight writes per tag per second at 25 Hz - and each write invalidates one 504x44
  pill. The tethers are static between pose changes and sit on their own bounding boxes,
  so a line update is a line, not the screen from 0,0. There is no second timer and no
  full-screen invalidation.

## Considered options

- **Keep the title in the bar and add the tags.** Redundant, and the bar has room for
  only one of the two jobs. The product asked for the bar's room to go to the clock.
- **Put the tags in the free band between the cassette and the bar.** Cheaper to compose
  and no overlap with the reels, but the product chose the tags over the cassette, and the
  band is only 57 px tall at the peek pose.
- **Read the tags in the broadcast's `set_track()`.** The file is about to be opened
  anyway, but the broadcast task is the real-time one and the project keeps its card
  reads short and out of the UI's path. The loader already owns the card side behind the
  broadcast's lag signal.
- **Read the tags while the Library is scanned at boot.** Correct for every Track at
  once, but it adds a read per file to the boot scan, a tag cache sized by the library,
  and a place for a malformed tag to fail the scan. Only the playing Track is needed.
- **Marquee the long labels.** `LV_LABEL_LONG_SCROLL` animates for as long as the text
  overflows; that is a continuous invalidation the product's "minimum performance" rule
  rules out. Dots are one layout on the text change.
- **Animate each tag separately, and the tether with it.** Two timers and two line
  invalidations per frame. One timer with quantized writes, and a tether whose upper end
  hides under the pill, is the cheapest shape that still reads as floating.
- **Draw the tag and the tether as one object.** LVGL has no rounded rectangle with a
  tail; the line is its own object and stays static, which is the point.

## Consequences

- `components/portable/artwork` gains `tags.c` and `artwork_tags()`, with the same
  "container through a read callback" shape as the picture locator. `t15_artwork` pins
  Latin-1, UTF-8 and UTF-16 ID3 frames, the TPE2/ALBUMARTIST fallbacks, the Vorbis
  comments and the malformed cases.
- `components/portable/cassette` gains `elapsed_ms`, `duration_ms` and `time_changed` on
  the frame. The times are the status's own, clamped to the duration and reported when
  their whole seconds differ, so the bar is rewritten at the same one hertz the position
  is. `t10_cassette` pins the clock.
- `screen_display` gains `cassette_peek.c` (the bar, the tags, the tethers and the bob)
  and `font_jura_28.c` for the peek's text. `cassette_levels.c` is now the peek's host:
  it creates the peek and passes the view's room callback through. The Label objects are
  gone; the Label cover and Artwork sheets are removed by ADR 0019.
- `tags_loader` carries the one request: a path, a format and a sequence, with a stale
  result dropped rather than shown against the wrong song. The tag read is one open per
  Track, on the loader task, behind the broadcast's lag wait. (ADR 0019 split this out of
  `artwork_loader`; the cover half is parked.)
- The board pass is owed for the visual result, the bob's read at 25 fps and the tag
  read's latency on a real card. The host tier cannot compile the LVGL view; it covers
  the parsers and the clock.
- `docs/specs/CONTEXT.md` calls the overlay the **Peek**, the strip the **Timeline bar**,
  the labels **Now playing tags** and the lines **Tethers**; the old **Label** term is
  retired with the title it carried.

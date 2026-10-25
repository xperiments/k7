# Remove the Label cover and Artwork screens; park the cover pipeline

The screen had three Levels - Bare, the K7 label cover and the Artwork Case with the
Track's picture - reached by swiping down twice, and an up-swipe peek on Bare. The owner
wants one screen: the cassette, its drawer, the DJ block and the peek. The two down
gestures and the two full-screen sheets are removed. The MP3 cover extraction will be
needed again when the Artwork screen returns, so it is parked in the tree rather than
deleted; the peek's text tags are not part of the cover and stay.

## Decision

- **Bare is the only Level.** The up swipe at Bare toggles the peek; the down swipe has no
  meaning. The Level machine, the Label cover, the Artwork sheet, the Case backdrop, the
  sheet titles, the backdrop mix and the cover request are removed from
  `cassette_levels.c`, which is now the peek's host.
- **`screen_input` names the intent, not the screen.** `SCREEN_INTENT_LEVEL_DOWN` and
  `SCREEN_INTENT_LEVEL_UP` become one `SCREEN_INTENT_PEEK_TOGGLE`; `LV_DIR_BOTTOM` is not
  reported. The two-finger menu latch and the drawer's left/right intents are unchanged.
- **The peek's tags stay.** `tags_loader.c` is the tag half of the old artwork loader: one
  worker task, one path/format/sequence request, `components/portable/artwork`'s `tags.c`,
  and the result handed to the view under the LVGL lock. The path/filename fallback is
  unchanged.
- **The cover pipeline is parked, not deleted.** `artwork_loader.c/.h`, the cover
  locator/fit (`artwork.c`, `id3.c`, `flac.c`) and the `label_cover.bin`/`cover.jpg`
  assets stay in the tree. They are out of the firmware build: the screen CMake no longer
  compiles the loader or embeds the assets, and `components/portable/artwork` compiles
  only `tags.c`. The host test `t15_artwork` still builds and tests the locator and the
  fit. Restoring the Artwork screen means restoring, together: the loader in the CMake,
  the two asset embeds, the cover sources in the component, the view's
  `set_artwork()`/`set_case()`, and the Artwork sheet in `cassette_levels`.
- **The parked headers say so.** `artwork_loader.h` and the top of `artwork_loader.c`
  carry the restore list, and the `artwork` component's CMake says which files are
  parked.

## Consequences

- A down swipe no longer changes anything; the peek is the only vertical gesture.
- The firmware drops the TJpgDec include, the 45 KB Case JPEG and the 464 KB cover
  buffer. The app image is **1.65 MB** with **60 %** of the 4 MB partition free, down from
  2.86 MB / 32 %.
- The peek's tags still work; a Track with none falls back to the path and filename.
- The parked cover code does not compile against the current view: it includes the
  removed `cassette_view_set_artwork()`/`set_case()` and `cover_jpg.h`. That is deliberate
  and the restore list in its header is the way back.
- ADR 0013 (the Artwork level) and ADR 0014 (the three-Level order) are superseded for the
  screens and gestures they describe. ADR 0017's peek, bar, tags and tether decisions
  still hold; its loader paragraph now names `tags_loader.c`.

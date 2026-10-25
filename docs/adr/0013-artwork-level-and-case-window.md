# The Artwork level puts the Track's picture in the cassette Case's window

**Superseded by ADR 0019 for the Artwork screen and its cover.** The screen is removed and
the cover pipeline is parked; the picture, the Case and the window below are kept for when
it returns.

The cassette screen had one piece of art: the K7 label cover, a static 800x480 sticker
opened by a two-finger swipe. An MP3 usually carries its own picture in an ID3 `APIC`
frame, and the product requirement is that the device show it. The first cut was a free
300x300 square on a plain screen; the supplied design replaces that with a **Case**:
`assets/labels/cover.jpg`, an 800x480 cassette shell with a cyan window. The requirement
is now that the Track's picture fill that window — **603x385 at (101,23)** — fitted
cover-style, centre-cropped, never squeezed.

## Decision

Add a fourth **Level** to the cassette screen - Bare, Label, Label cover, Artwork - and
give the Artwork level the Case: `cover.jpg` full-bleed at 0,0, the Track's picture in
the Case's window at (101,23), 603x385, and the cleaned **Now playing** title on the free
band under the case. The one-finger down/up swipe, which used to slide the small label,
moves one Level; the ends do not wrap. The two-finger swipe stays as the Label cover's
shortcut, so the K7 sticker is still one gesture away.

The picture is the Track's own: an ID3v2.3/2.4 `APIC` frame in an MP3 or a native FLAC
`PICTURE` block, preferring the front cover. `components/portable/artwork` locates it by
reading headers and returns its offset and length; `artwork_loader.c` reads the picture,
decodes the JPEG with the TJpgDec that LVGL already vendors, and fits it to the window.

The fit is **cover**, not stretch: the source is scaled until it covers 603x385 and the
overflow is centre-cropped. For an 800x480 picture that keeps the full height and crops
24 px from each side; for a square picture it scales to 603x603 and crops to 385 high; a
small picture is upscaled the same way. The window is exactly the cyan rectangle of the
Case, so the picture replaces the cyan and the case becomes the frame.

The Case is embedded as the original **45 KB JPEG** and decoded once at start into one
768 000 B PSRAM buffer, then handed to the view. A raw RGB565 blob would be 768 KB of
flash and would not fit the 4 MB app partition (the image is 3.51 MB with 16 % free);
TJpgDec is already linked, so the 45 KB JPEG is the cheap path. If the Case decode
fails, the Artwork screen keeps its flat `COL_ART_BG` background.

Only JPEG is decoded. A PNG `APIC`/`PICTURE` is located and refused with a log line: it
is rare in an MP3, and the window does not justify LodePNG's full-size buffer.

The loader runs on core 0 at priority 2, below the screen and the network, and the view
never touches the card or a decoder. TJpgDec reads its input a few hundred bytes at a time,
so the Track's JPEG is read from the card on demand; there is no encoded copy in PSRAM.
The read yields every 16 KB and backs off while `mini_icecast_lag_ms()` says the broadcast
is behind: the VFS holds the volume lock for one `fread`. A newer Track abandons the old
read and decode instead of making the new picture wait.

No full-size RGB buffer exists for the Track picture. A picture at least the window's
size is box-filtered from TJpgDec's MCU bands into the 603x385 buffer
(`artwork_cover_*`); a smaller one is decoded into a crop buffer no larger than the window
and upscaled with the portable bilinear fit. The band or crop buffer is per decode. The
cover itself is one buffer: the view hides the picture when a new Track is requested, so
the decode writes it while it is not drawn. A second 464 KB buffer does not fit: the app's
rodata and instructions must stay in PSRAM (moving them to flash brought the panel's
glitches back), which leaves only about 1.7 MB free for the artwork at start.

## Considered options

- **Keep the first cut, a free 300x300 square on a plain screen.** It was built and
  tested, but the supplied Case makes the screen an object rather than a gallery panel;
  the picture now belongs in the window, and the window is 603x385, not square.
- **Embed the Case as a raw RGB565 or RGB565A8 blob.** 768 KB or 1.15 MB of flash on top
  of the 3.51 MB image: it does not fit the 4 MB partition. The 45 KB JPEG plus a 768 KB
  PSRAM buffer does, and the decoder is already there.
- **Move the app's rodata and instructions out of PSRAM.** It frees about 1.8 MB, enough
  for two cover buffers, but it brought the panel's glitches back on this board. The
  artwork pipeline was fitted around the PSRAM instead: one buffer, the JPEG read on
  demand, a per-decode band.
- **Draw the Case scaled into the window.** The Case is the frame, not the picture; the
  numbers in the design are the window of the native 800x480 asset.
- **Replace the K7 sticker with the Track's cover.** Loses the designed label and makes
  the big screen empty for a Track with no picture. The sticker and the Case are
  different Levels.
- **Draw the picture on the cassette body (`Bare`/`Label`).** A 603x385 rectangle lands
  over the packs and the reels, which are the screen's whole animation.
- **PNG as well as JPEG.** LodePNG decodes at full size, so a cap and an extra buffer are
  needed for a case that is rare in MP3s. JPEG only for now.
- **LVGL's own image decoder.** It wants a file (an FS driver) or the memfs driver, and
  its band-at-a-time interface does not produce the single fitted buffer the screen
  needs. Calling TJpgDec directly is 30 lines and keeps the fit in the portable module,
  where the host tier tests it.
- **Decode the Track picture at full size and scale with `artwork_scale_cover`.** Simple,
  but a 2000x2000 cover needs 8 MB of RGB565 before the fit. The streaming fit makes the
  memory independent of the picture's size.

## Consequences

- The app image is 3.51 MB with 16 % of the 4 MB partition free (the 45 KB Case JPEG
  included).
- The Artwork path adds one 464 310 B PSRAM cover buffer (the view hides the picture while
  it is written), a 768 000 B Case buffer, a per-decode band or crop buffer, and the
  loader's 16 KB PSRAM-first stack. The Case JPEG is read from the embedded 45 KB blob;
  the Track's JPEG is read from the card on demand, so neither needs an encoded copy.
- The Level changes in one frame. A 300 ms slide of a full-screen sheet was measured as
  a stutter on this panel; the cassette's own label and K7 no longer animate either.
- The one-finger down/up gesture changes meaning: it moves Levels instead of sliding the
  small label. The two-finger swipe keeps the Label cover.
- `host/tests/t15_artwork.c` pins the locators (ID3v2.3/2.4, the v2.4 plain-size quirk,
  UTF-16 descriptions, FLAC PICTURE, the caps), the fit (box filter, crop, raster order,
  the upscale refusal) and the exact 603x385 window crop. The panel pass is recorded: the
  first card track's 239x236 APIC decoded to the window in 691 ms and showed, with
  504,300 B of PSRAM left after the cover buffer. The card contention and the PSRAM peak
  are the remaining board-only properties to watch.
- A Track with no usable picture leaves the Case's window empty and the title on screen.
  There is no fallback image: the K7 Level is still there.
- Superseded in part by
  [ADR 0014](./0014-three-level-swipe-order-and-menu-gesture.md): the screen has three
  Levels, not four, and the two-finger swipe is reserved for the menu instead of opening
  the Label cover.

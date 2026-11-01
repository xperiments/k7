# The Shell is one image, and it is resident

The front door serves the page and the screen tears. Not every time, and not always in the
same place: a band of the picture shifts sideways for a frame, and after a page load the whole
image can sit a few pixels off what it was. It is worse the more the page is used — reloading
is the reliably bad case — and the audio is perfect throughout, which is what made it look
like a screen problem and sent three attempts at the panel.

It is not the panel. It is the memory the panel reads, and what the rest of the board does to
it while it is reading.

## What the failure actually is

The panel's scan-out is two 800x480 framebuffers in PSRAM, read by GDMA in direct mode
(`panel_rgb.c`: `num_fbs = 2`, `bounce_buffer_size_px = 0`, `fb_in_psram`, `avoid_tearing`).
That DMA is continuous: 800 × 480 × 2 B at 39 fps, so ~32 MB/s has to come out of PSRAM every
second, and the LCD FIFO holds a few tens of microseconds of it.

Any flash access takes that memory away, in one of two ways:

- **a partition read** (`esp_partition_read`, which is what a SPIFFS file read is) disables the
  cache, and with it the PSRAM window, for the whole transaction — milliseconds per call. ADR
  0054 found this the hard way;
- **a mapped read** is a cache line fetch, ~1.6 µs on SPI0/1 — but the RGB DMA is on the same
  controller, so every line is a stall it cannot fetch through.

The IDF guide's own words for the result, quoted in
`docs/research/panel-rgb-lvgl-performance.md`, are "the driver to fail in switching the bounce
buffer promptly, resulting in a **screen shift**". This project had already met the mechanism
twice and fixed it twice, both times on the *code* path: instructions and rodata were moved
into PSRAM because "moving either to flash brought the panel's glitches back"
(`sdkconfig.defaults.sunton`), and the bounce-buffer mode was reported worse than none — which
is what that mode does, because in it the refill ISR reads PSRAM too.

The front door then put flash back, on the *data* path, and it did it in the worst possible
way: `serve_file` streamed the page out of SPIFFS in 2 KB chunks, one `esp_partition_read`
each. Measured from the host against the board, a 187 KB bundle took **598 ms in 92 chunks —
6.5 ms each**, and a whole 492 KB reload was about 1.2 s of near-continuous blackout, around
fifty panel frames. The status route was innocent: `/api/status` stayed under 60 ms through
3.8 MB of load, and the broadcast's own clock tracked wall time to 0.1 %, so the screen was
being fed correct numbers the whole time. It was short of pixels.

## The first fix, and what it taught

The page became one image — a table of contents and the files' bytes, packed by
`tools/pack_shell.py` and flashed to the `frontdoor` partition — served from a *mapping*
instead of a filesystem. It was five times faster (187 KB in **108 ms**) and it still tore.
That measurement is the useful part: the difference between 598 ms of cache-disabled reads and
108 ms of mapped ones is a difference of degree, and the panel needs the bus to itself, not
less of it.

So the rule this board already lived by — **nothing at runtime touches flash while the panel
scans** — turns out to apply to the page too, and the only way to honour it is to keep the
page's bytes in RAM.

## Decision

- **The Shell is one image, not a filesystem.** `tools/pack_shell.py` writes `web/dist-shell`
  as a header, a table of contents, the names and the files' bytes exactly as the build left
  them (text already gzipped, the rest plain). `esptool_py_flash_to_partition` flashes it to
  the `frontdoor` partition, so one `idf.py flash` still writes firmware and page together. The
  partition's subtype stops claiming to be SPIFFS; it is opaque data now.
- **It is read once, into PSRAM, before the panel exists.** `app_board_shell_image()` maps the
  partition, validates the image, copies its declared length into a PSRAM buffer, unmaps, and
  hands that buffer over. A page request is then a table lookup and one send out of RAM: no
  flash read, no mapping, no stall. The copy itself is the one burst of flash reads in the
  process, and it happens while nothing is scanning — the appliance calls it before
  `screen_display_boot()`.
- **The page path no longer uses the flash writer.** A resident file is not a flash read, so
  `front_door` drops `flash_job`. That also removes ADR 0054's accepted cost: a slow client no
  longer holds the one flash-capable task, and a Snapshot upload no longer waits behind a page
  load. ADR 0054's rule stands for everything that does touch flash.
- **The format is portable and validated once.** `components/portable/shell_image` is plain C
  with no ESP-IDF and no LVGL. `shell_image_open()` checks the header and the whole table at
  boot, including the image's own length — the partition is bigger than the image, so "the
  bytes I was handed" is not a bound a lookup may trust. A bad image is treated exactly like a
  firmware that carries no page. `host/tests/t31_shell_image.c` builds its fixture **by hand,
  byte by byte** — a test that assembles its input with the packer proves only that the two
  agree with each other — and pins every malformation the reader must refuse.
- **The host serves the same image from a buffer.** `XTAPE_SHELL_IMAGE` points at a packed
  image, and `front_door_tests.py` packs its fixture tree with the same tool the build uses,
  so the tier that runs in CI exercises the reader the board runs.

## Why not the alternatives

- **Serve from the mapping.** Tried, measured, and it still tears — the section above. Cheaper
  on PSRAM and not good enough.
- **Keep SPIFFS and hold the Shell in PSRAM.** This is the decision, with the filesystem
  removed: the filesystem's 2 KB reads were the worst version of the same fault.
- **Pace the reads:** send in 32-byte slices with a gap, so no bus burst outlives the FIFO. It
  could be made to work, and it spends a tuning constant, a slower load and a busy CPU on
  buying back what 390 KB of PSRAM buys outright.
- **A bounce buffer.** Tried on this board in October and reported worse; the refill ISR reads
  PSRAM, so a flash read starves it too. Its documented purpose is a higher pixel clock, not
  immunity to the bus being taken away.
- **A partial-draw arrangement over one PSRAM framebuffer.** It changes where LVGL draws and
  removes the vsync wait (research note, lever 10), and it does nothing about flash reads.

## What this costs

- **390 KB of PSRAM, permanently.** ADR 0047 counted ~504 KB free with the cover path live;
  this spends most of it, leaving ~114 KB. The page is always wanted and the cover is only
  wanted for a Track that has a picture, so the page wins — but if the artwork loader ever
  starts failing to allocate, this is the line to look at, and the boot log prints PSRAM free
  before and after the copy.
- **No degraded fallback.** If the PSRAM allocation fails there is no page; `/` answers 503
  and says why, in the log and in the response. The alternative was a page that tears, which
  is the bug this exists to remove.
- **A build step and a format.** Two hundred lines of packer and reader, one ADR, and a table
  of contents that has to stay in step with the directory — which the build enforces by
  repacking whenever anything under `web/dist-shell` changes.
- **The page is fixed at flash time.** It always was: nothing in the firmware wrote that
  partition. What is lost is the ability to add a file to a mounted filesystem at runtime,
  which no code did.

## What it does not change

The routes, the cache rules (`immutable` under `/assets/`, `no-cache` elsewhere), the
compressed-only rule and its 406, the 404 for an unknown path, the 503 for a page this
firmware does not carry, and `/api/status`. The image is a different way to reach the same
bytes; the page's contract is untouched.

## The offset, and the rule it establishes

With the page no longer touching flash, reloads stopped tearing — and the picture was still
sometimes shifted sideways by a band, differently on different boots, from the boot mark onward.
Nine suspects were cleared by measurement before the tenth was found, and the list is worth
keeping because each was a plausible fix that would have been wrong:

**cleared** — the page (it predates the front door), the SD card (identical with no card in, and
with no card there is no SD traffic at all), the pixel clock (reverted to the profile's measured
16 MHz), the drawing (the boot mark's glyph has `adv_w` 314, `ofs_x` 38, `box_w` 238, so
`LV_ALIGN_CENTER` lands its ink on exactly 157), the boot mark itself (a build with it bypassed
shifted the same way), the framebuffers' address (page-aligning them changed nothing), the
FIFO priming race (starting the panel at a quarter clock changed nothing), the driver's restart
(its link skips 17 px into framebuffer zero, so it trades one offset for another), and the
panel's power-on wait (250 → 1500 ms changed nothing).

**What found it** was a pattern with no renderer behind it: both framebuffers filled with equal
bars 20 px in from each edge, painted straight into the buffers and never touched by LVGL. On
the glass it is *correct for the first frames of the boot* and then, 100-200 ms in, the picture
**shifts 40 px right and wraps** — the right-hand bar reappears at the left edge.

Forty pixels at 16 MHz is **2.5 µs of scan-out**, which is one flash transaction. And 100-200 ms
after the panel starts is when `app_services_start()` runs and Wi-Fi reads NVS out of flash.

That is the rule this repository already applies to its own code and rodata, arriving from
another direction: **the scan-out cannot survive any flash access.** A partition read disables
the cache and the PSRAM window with it (ADR 0054); the LCD engine then consumes pixels that are
not in the FIFO, and from that instant the DMA and the engine are permanently out of step — the
picture sits a band to one side until the next boot, and no re-sync that keeps the skip-free
start can undo it. It also explains why a bisect landed on a commit that added a font: the fault
is *when the firmware first touches flash*, so anything that moves boot timing moves the odds.

The fix is where the boot's flash work ends, not where the panel begins: at the mark-to-cassette
handover, `panel_rgb_reinit()` blacks both buffers and re-runs the driver's init — stop the LCD
engine, reset the FIFO and the channel, start again — so the cassette's frame is established
from a quiet bus. On the board the cassette is then correct on every reboot.

**What it does not fix is the boot mark**, and that is honest rather than incidental: the mark is
on screen *during* those flash reads, so it can sit off for the second or two it is up. The
re-init catches the scan before the cassette, which is why the cassette is clean and the mark
sometimes is not. Fixing the mark would mean either doing the network's flash work before the
panel exists — a boot-order change ADR 0004 argues against, because the panel's 16 KB internal
stack must be taken before Wi-Fi asks for its contiguous DMA buffers — or re-initialising the
scan periodically while the mark is up, which flickers.

## The measurements

From the host, against the board over the LAN, one bundle of 187 324 bytes:

| The page comes from | Median | What the panel sees |
|---|---:|---|
| a SPIFFS file, 2 KB chunks | 598 ms | 92 cache-disabled reads, ~6.5 ms each |
| the image, mapped | 108 ms | 187 KB of cache-line fetches on the shared bus |
| the image, resident | one `write` loop | nothing |

And during a reload, `elapsed_ms` from `/api/status` advanced 23.09 s over 23.07 s of wall
time: the screen was never short of the truth.

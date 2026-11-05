# The Shell is one image

> **Superseded by [ADR 0067](./0067-chrome-only-direct-to-the-device.md).** There is no Shell:
> the device serves no page and the `frontdoor` partition is gone.

> **Amended again: the image is the Launcher, and it is resident.** The App is the hosted
> origin's — the device never carried a second copy that anyone opened, and the 390 KB this
> document weighed was that copy, not the page. The Launcher is `index.html` (5 KB),
> `launcher.js` (33 KB, 9 KB gzipped) and `launcher-origins.js`: **14 613 bytes**, copied into
> PSRAM at boot and served from there. No flash read is behind a page load, so the slide is
> not hidden — it does not happen — and the black hold, its re-establish and the picture that
> sometimes came back a band to one side all go with it. Shape 1 below was right about the
> mechanism and wrong about the price: it was pricing the App, not the Launcher.
>
> **Amended twice the same day. The image is mapped, not resident, and the screen blacks out
> while a page is served.**
>
> Three shapes were tried against the rule this document establishes:
>
> 1. **Resident** — copy the image into PSRAM at boot. It works, and it is what the sections
>    below were written around; it costs 390 KB of the PSRAM the artwork budget counts on, and
>    on the board that lost the cover. Rejected on cost.
> 2. **Bounce buffer** — leave the image in flash and give the panel a ten-line bounce buffer,
>    so the LCD's DMA reads internal SRAM and a flash transaction is absorbed (0.5 ms of slack
>    against 2.5 µs). No PSRAM, no blank screen. Rejected before it was measured: its price is a
>    CPU copy of the whole frame every frame (~30 MB/s, and ~13 % of a core), and this board
>    already spends its cores on the encoder. It stays the first thing to retry if the chosen
>    shape ever proves too ugly.
> 3. **Chosen: the screen blacks out for a page load.** The image is mapped, so it costs no
>    PSRAM and no CPU. `screen_display_page_service()` blacks both framebuffers when a page file
>    is about to be read out of flash, and a second after the last request the screen task
>    re-establishes the scan (`panel_rgb_reinit()`) and repaints. The slip that flash reads cause
>    happens behind a black frame, where there is nothing to see, and the frame boundary is put
>    back before the picture returns. It costs a black screen for the length of a page load,
>    which is the one moment nobody is looking at the cassette.
>
>    Two variants of the hold were built and measured on the board before this one was settled:
>    **freeze only** (renderer stopped, picture left up) — the cassette stays and visibly jumps
>    sideways during a load, then snaps back at the re-establish; and **freeze + slow the scan to
>    2 MHz** (to make the FIFO eight microseconds deep against a 2.5 us transaction) — which on
>    this panel blanks anyway, because the clock change itself takes it out of lock. A black
>    that happens on purpose is worth more than a black that happens by accident.
>
> **Amended again: the first clock now waits for the boot's flash work.** The chosen shape
> stands; what changes is when the panel starts. `esp_lcd_panel_init()` — the call that starts
> the pixel clock and the scan — now runs after `app_services_start()`, so the boot's flash work
> happens on a dark panel and the mark is never on the glass while flash is touched. The boot
> mark gets its own 2 s dwell and the cassette follows. See "The late clock" below.

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
- **~~It is read once, into PSRAM, before the panel exists.~~ Amended: it is mapped, and the
  screen is taken off the glass while it is read.** `app_board_shell_image()` maps the partition,
  validates the image and hands the mapping over; nothing is copied and no PSRAM is spent.
  Serving runs on the flash writer, because a mapped window and a Snapshot write must not overlap
  (ADR 0054), and each page file is preceded by `screen_display_page_service()` — see the
  amendment at the top for why a page load blacks the screen.
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

- **~~390 KB of PSRAM, permanently.~~ Amended: a black screen for the length of a page load.**
  The chosen shape costs no memory at all, and the price is paid in the one currency left: the
  screen is black from the first page file until a second after the last one (PAGE_HOLD_MS), and
  comes back only once the scan has been re-established. A slow client that keeps requesting
  keeps it black; nothing else does.
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

**What the re-init does not fix is the boot mark**, and that is honest rather than incidental:
the mark is on screen *during* those flash reads, so it can sit off for the second or two it is
up. The re-init catches the scan before the cassette, which is why the cassette is clean and the
mark sometimes is not. Nothing in the shape as first chosen could fix that: the boot's own flash
reads have no black frame to hide behind, because the mark *is* what is on the glass. The
amendment below fixes it by moving the clock, and with it the mark's window - see "The late
clock".

## The late clock

The mark's window was the boot's own flash work, and a picture cannot hide a slip it is the
subject of. The mark is decorative, so the answer was to move the window rather than hide it:
do not start the panel's clock until that flash work is finished.

**What moved.** `esp_lcd_new_rgb_panel()` and `lvgl_port_init()` stay where they are, before
`app_services_start()`. That is the internal-RAM ordering constraint, and it is why route (2) -
moving the whole screen after the services - was not taken: the port's 16 KB internal stack has
to be taken before `esp_wifi_init()` asks for its contiguous DMA buffers (ADR 0004), and moving
the screen would have put it after Wi-Fi's allocation instead. One call moved:
`esp_lcd_panel_init()`, the driver's init that applies the timings, starts the pixel clock and
starts the scan. It is now `panel_rgb_scan_start()`, called from `screen_display_mark()` after
`app_services_start()` has returned. Two consequences fell out of that:

- **The refresh timer waits with the clock.** `lv_display_create()` invalidates the screen and
  marks the refresh timer ready, and an RGB flush in direct mode waits on the panel's vsync,
  which a stopped engine never sends. `panel_rgb_start()` therefore holds the port's recursive
  lock across the display's creation and pauses the refresh timer before releasing it.
  `panel_rgb_scan_start()` resumes it, and so does `panel_rgb_reinit()` - the belt, and the
  recovery path if the first clock ever failed.
- **The mark is painted after the services, not before them.** `screen_display_boot()` creates
  the panel, the two framebuffers and the port, and leaves the buffers unpainted and the light
  off. `screen_display_mark()` fills both framebuffers black, starts the first clock, paints the
  K7 mark, lights the backlight over it and holds it for `APP_UI_MARK_HOLD_MS` (2 s).
  `screen_display_start()` still calls `panel_rgb_reinit()` at the handover, unchanged.

`esp_lcd_panel_init()` was already the routine that restarts the scan after a page load, so the
late start reuses it rather than adding a second mechanism.

**Measured on the board, warm boot.** The order is in the log, and so is the fact that the boot's
flash work now precedes the first clock:

    I (1592) panel: panel up through esp_lvgl_port: ... pclk 16000000 Hz, first clock deferred
    I (1607) screen: screen memory: internal free 227519 B (largest 131072 B) before,
                      205519 B (largest 118784 B) after bringing the panel up
    I (1641) wifi:config NVS flash: enabled
    I (2061) app: library: state=ok tracks=93 ... scan=167 ms
    I (2431) http: listening on :80 ...
    I (2522) panel: the panel's first clock starts with the boot's flash work behind it
    I (2625) backlight: backlight on GPIO 2 ...
    I (2629) screen: boot mark 800x480 on the glass, held 2000 ms before the cassette
    I (4854) screen: cassette 2 of 7: (no track)

The mark is up from 2629 to 4854: the 2 s dwell plus the cassette's own build. Before the change
the same board logged the mark at 1847 and Wi-Fi's NVS read at 1890, so the mark was on the glass
while flash was being touched; now every known flash user in the boot - NVS, the Snapshot, the
mapped Shell - has finished before 2522. What moved is the scan (from ~1.65 s, inside the old
panel bring-up, to ~2.52 s), the mark (~0.8 s later) and the cassette (~2.0 s later, the dwell).

**The internal-RAM line is unchanged.** It still covers exactly the panel bring-up - new panel +
port + display. The mark, the splash and the light are PSRAM/LEDC work and allocate no internal
RAM, and nothing else in the bring-up moved. Measured 227 519 B free (largest 131 072 B) before
and 205 519 B (largest 118 784 B) after, against HEAD's 227 483/205 479 and 227 615/205 611 on
the same board: the same cluster and the same largest blocks. Route (2) would have moved the
port's 16 KB after Wi-Fi's allocation, so it was not needed and was not taken.

**What this does not change.** A flash access while the panel scans still displaces the picture
until the engine is restarted. The late clock removes the boot's own flash work from the mark's
window; it does not make the scan able to share the bus. A page load is still hidden behind the
black frame, and the handover still re-establishes the scan with `panel_rgb_reinit()`.

**Cold boots are the acceptance, and they passed.** The run was 8-10 power pulls - power off,
the rail drained, power on - with the K7 mark centred and up for its ~2 s on every boot, and the
cassette centred on every boot. A warm reset is not the test: it does not re-run the panel's
power-on state, and a flash or a reset leaves the rail up, so a shifted picture outlives the
reboot that was meant to clear it. The log above is a warm boot and proves the order; the cold
run is what proves the picture.

## The measurements

From the host, against the board over the LAN, one bundle of 187 324 bytes:

| The page comes from | Median | What the panel sees |
|---|---:|---|
| a SPIFFS file, 2 KB chunks | 598 ms | 92 cache-disabled reads, ~6.5 ms each |
| the image, mapped | 108 ms | 187 KB of cache-line fetches on the shared bus |
| the image, resident | one `write` loop | nothing |

And during a reload, `elapsed_ms` from `/api/status` advanced 23.09 s over 23.07 s of wall
time: the screen was never short of the truth.

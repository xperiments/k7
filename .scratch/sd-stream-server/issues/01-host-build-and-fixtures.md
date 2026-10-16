# 01 — Host build and fixture corpus for the broadcast path

**What to build:** A developer can build and run the real server on macOS with no board
attached, point it at a generated corpus of test tracks, and pull a live stream from it with an
ordinary HTTP client. A second client can connect while a first one sits silent and idle, and
still gets served.

This is the first vertical slice because the two-tier acceptance rule makes the host build the
test layer every later ticket depends on — and because it cannot be built at all today: the
decoder's vendored headers are missing from the workspace, and the include directories the
build registers do not exist.

**Blocked by:** None — can start immediately.

**Status:** done (host tier). The board tier of a later ticket has not been run: no ESP32-S3 was attached.

- [ ] Vendor the three missing decoder headers (public domain / MIT-0) so the decode layer compiles, and repair the include-directory registration so the build no longer references directories that do not exist.
- [ ] The host build target compiles with one command and produces a runnable server.
- [ ] The host build compiles the **same production sources** as the firmware. No forked copy of any component source, and no target conditional compilation anywhere in a production component — host-only behaviour is supplied by the shim.
- [ ] The server takes its music directory and port as runtime values, binds to the loopback interface on a non-privileged port, and needs no elevation.
- [ ] `GET /stream` returns continuous audio at the station rate.
- [ ] A client that connects and sends no request cannot delay or block a second client. Measured: the second client receives response headers after it connects, not several seconds later.
- [ ] A fixture generator regenerates the corpus with a single command. The corpus covers all three source formats and includes at least one input rate different from the station rate.
- [ ] A measurement harness reports encoding cost per frame and free heap in both memory classes, runnable on the host; this is the harness later tickets reuse.

Spec: `docs/specs/sd-stream-server.md` §10 (H1–H4, H6), §5 D1, §9 T4.

## Comments

- [x] Vendored `dr_mp3.h`, `dr_flac.h`, `dr_wav.h` (public domain / MIT-0) as their own `components/dr_libs` component; the component registrations no longer name directories that do not exist (`include/` now exists, `third_party/` became its own component).
- [x] `make -C host` builds `host/build/bin/icecast_host` with one command.
- [x] The host build compiles the same production sources. `make -C host layering` fails the build if a component source is missing from the host Makefile or if a `*_host.c` fork exists. H2 is enforced too: no `CONFIG_IDF_TARGET` anywhere in `components/`.
- [x] Music directory and port are runtime arguments; default port 18000, no elevation.
- [x] `GET /stream` streams continuously: `curl -s --max-time 5 .../stream | ffprobe -` reports MPEG audio, 44100 Hz, 128 kb/s, stereo, and `ffmpeg -v error -f null -` reports zero errors over the whole capture.
- [x] A silent client cannot delay a second one: with a silent socket open, `/` answers in 0.0008 s and `/stream` headers in 0.0004 s (T4 measures it in `host/tests/http_tests.sh`).
- [x] `make -C host fixtures` regenerates the corpus (ffmpeg, 44100/48000/32000 in all three formats, plus a deliberately non-multiple-of-1152 track and a sub-frame track). Committed so host and board see identical bytes.
- [x] The measurement harness is `host/build/bin/mi_bench` (encoder cost per frame) plus the periodic telemetry in `mini_icecast.c` (free heap in both classes); both are host-runnable and are what later tickets reuse.

Evidence: `make -C host check` -> unit tests, layering checks and 88 HTTP assertions, all passing.

**Board run (partial).** `rm -rf build sdkconfig && idf.py build` succeeds from a clean
checkout against ESP-IDF v5.4.4 (972 KB app, 37 % of a 1.5 MB partition free), and the
firmware boots on the ESP32-S3 N16R8 devkit and stays up. The startup telemetry runs on
the device: 8 MB PSRAM in octal mode at 80 MHz with a passing memory test, 305 571 B of
free internal heap and 8 386 148 B of free external heap, the station format, and which
memory each large buffer took. The HTTP listener reports `listening on :8000 (backlog 16,
max 4 at once)`.

Two things the firmware build needed that the host build could not reveal: `esp_flash`
is not a component in IDF 5.4 (`spi_flash` is), and the flash geometry has to be set
explicitly - the default assumed 2 MB, which left a 950 KB image in a 1 MB partition.
Both are fixed in `main/CMakeLists.txt` and `sdkconfig.defaults`.

One bug was found only on the board: with the services started before the network stack
was up, the first `socket()` call tripped `assert failed: tcpip_send_msg_wait_sem ...
(Invalid mbox)` and rebooted the device. The host shim has no lwIP, so no host test could
catch it. `net_start()` and `wifi_wait_connected()` are now separate steps; the whole
startup telemetry now appears before, and independently of, an association.

Outstanding on this ticket's board tier: the HTTP tests, which need the device on a
network (see the README's board status section).

**Specification deviation found on hardware: the card is on SPI, not 1-bit SDMMC.**
§2.3's pin table (CLK 39 / CMD 38 / D0 40) was [read] off the old `resources/main.c`,
not off the board, and the board has nothing on those pins: the SDMMC mount returned
`sdmmc_init_ocr: send_op_cond (1) returned 0x107` (a timeout, correctly reported by the
degradation path from ticket 09). The card is actually wired as SPI - CS 10, CLK 12,
MOSI 11, MISO 13 - and mounts as soon as `sd_mount()` uses `esp_vfs_fat_sdspi_mount()`
instead: `Name: SL32G`, `Type: SDHC`, `card: 30420 MB total, 30418 MB free`.

R2's capability (the card mounted at `/sdcard`) is met; R2's *mechanism* is not, and this
ticket's pin bullet is superseded by the README's card-bus table. Two knock-on effects:
the specification's GPIO39/40 cost (USB-JTAG disabled) does not apply to this board -
those pins are free and on-chip debugging survives - and the 10 kΩ pull-up warning on
CMD/D0 is about a bus that is not in use here.

Also added while diagnosing: the mount now logs the filesystem's size and whether
`music_dir` exists, because "mounted but empty" and "mounted but no such directory" look
identical from outside; and the broadcast logs `cannot open /sdcard/music: No such file
or directory` once, distinguishing a missing directory from a directory with nothing
playable in it.

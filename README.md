# SD stream server

An ESP32-S3 devkit with a microSD card in it joins your Wi-Fi and serves the card's
music over HTTP:

* **one continuous live MP3 stream** at `http://<device>:8000/stream` — every
  client hears the same track at the same moment, and any player works, because
  the stream format never changes;
* **a browsable index** at `/`, listing the tracks on the card with their sizes
  and what is playing now;
* **byte-exact downloads** at `/music/<relative path>` — the original `.mp3`, `.flac` or
  `.wav` exactly as it sits on the card, not a transcode.

Anything the card holds at any sample rate is decoded, resampled to one station
rate and encoded to MP3 for the stream. The stream is the universal-compatibility
path; downloads are the fidelity path.

> ## Read this before you deploy it
>
> **This firmware serves a live stream, the file index, the library API and a
> read-only MCP endpoint to anyone who can reach port 8000, and its Wi-Fi
> credentials are compiled into the binary.** There is no authentication
> anywhere: not on the index, not on the downloads, not on the stream, and not on
> `/api/play` or `/mcp`. (`/stream` deliberately stays open even if auth is ever
> added — car head units and hardware players cannot send credentials in a stream
> URL.)
>
> That is a deliberate choice for a LAN appliance holding audio you own. It is
> **not** safe on an untrusted network, and it must **not** be port-forwarded or
> otherwise exposed to the internet. Keep it on your own LAN.

---

## Contents

- [Build and flash](#build-and-flash)
- [The card](#the-card)
- [Hardware: pins, button, PSRAM](#hardware-pins-button-psram)
- [Using it](#using-it)
- [Host build and tests (no board needed)](#host-build-and-tests-no-board-needed)
- [Two-tier acceptance: host and board](#two-tier-acceptance-host-and-board)
- [Architecture](#architecture)
- [Licensing](#licensing)
- [Known limitations and deliberate omissions](#known-limitations-and-deliberate-omissions)

---

## Build and flash

Builds and runs on ESP-IDF **6.1** (the version the board tier below was run on)
and on **5.4.4**; the component metadata asks for `idf: ">=5.4"`. The `esp32s3`
target is the only one supported.

```bash
# One-time, if you have not sourced an ESP-IDF environment yet:
. $IDF_PATH/export.sh

# Build, flash and watch the log (adjust the port):
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbserial-XXXX flash monitor
```

Before the first build, put your network in place:

```bash
cp components/app/board_esp32/wifi_credentials.h.example components/app/board_esp32/wifi_credentials.h
$EDITOR components/app/board_esp32/wifi_credentials.h        # your SSID and password
```

That header is **gitignored**, so a real network password never enters version
control, while still being compiled into the binary. A clean checkout builds
without it (the log warns that the device will not join a network). There is no
runtime provisioning mechanism: changing the credentials means editing the header
and reflashing (a deliberate deferral, not an oversight).

`sdkconfig.defaults` sets the flash size to 16 MB and the application partition to
1.5 MB, matching the N16R8 class of module. If your board has a different flash
size, change `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` there before the first build.

`sdkconfig.defaults` is applied on the first build; `idf.py menuconfig` edits the
result afterwards. If you change `sdkconfig.defaults`, delete `sdkconfig` (or run
`idf.py fullclean`) or the change will not take effect.

After flashing, the log prints the two URLs once the device gets an address:

```
I (radio) stream: http://192.168.1.42:8000/stream  library: http://192.168.1.42:8000/
```

### If the build fails on a component name

`main/CMakeLists.txt` names the ESP-IDF components it needs using their IDF 5.4
names (`esp_driver_gpio`, `esp_driver_sdmmc`, `esp_flash`, `esp_psram`, ...). On a
newer IDF those may have been renamed or merged again; the fix is to adjust that
one line, and the error message names the component.

## The card

* Format the card as **FAT32** (exFAT also works if enabled in `menuconfig`).
* Put your tracks under `/sdcard/music/`, in subdirectories if you like. The tree
  is scanned at boot and by an explicit rescan into a PSRAM arena; there is no
  per-request directory walk. Names are sorted case-insensitively, directories and
  tracks interleave, and the Playlist is that depth-first order.
* Each name may be up to 128 characters; a complete path must fit `FILENAME_MAX`
  (1024 bytes on this build, with a 1025-byte tree buffer). A file whose path is
  longer is skipped and logged, not silently truncated.
* Accepted extensions: `.mp3`, `.flac`, `.wav`, in any case. Any sample rate, mono
  or stereo. Files whose name begins with `.` are ignored (that is how macOS
  AppleDouble `._track.mp3` files are skipped).
* FATFS long names are enabled up to 128 characters (`CONFIG_FATFS_MAX_LFN=128`).
* The Library arena and its JSON live in PSRAM, with a **4 MiB byte budget** and a
  20 000-track cap. At the cap the scan stops, `truncated` becomes true, and the
  index/JSON say so; it does not reboot. A 64-track, 2.28 GB card measured
  `scan=96 ms arena=6285 B json=12343 B`; a forced one-track cap logged
  `truncated=1`. The boot scan is synchronous, with a 16 KB main-task stack.
* The index and downloads use that Playlist order; the live stream plays it in
  order, looping forever.

The MCP endpoint is also advertised over mDNS as `xtape.local` (`_mcp._tcp`, port 8000,
TXT `path=/mcp`, `endpoint=http://xtape.local:8000/mcp`, `device_type=mcp:device`). The
hostname is fixed: **one xtape per LAN**. Discover it with `dns-sd -B _mcp._tcp` (macOS) or
`avahi-browse -rt _mcp._tcp` (Linux).

The default build exposes only the read-only tools. A control build adds `player_play`,
`player_skip`, `player_prev` and `library_rescan`:

```
make -C host control          # host control binary + control-check
XTAPE_MCP_CONTROL=1 idf.py build
```

To try it without a music collection, copy the test corpus from
`host/fixtures/` onto the card:

```bash
make -C host fixtures        # regenerate it (needs ffmpeg), or use what is committed
cp host/fixtures/*.mp3 host/fixtures/*.flac host/fixtures/*.wav /Volumes/YOURCARD/music/
```

## Hardware: pins, button, PSRAM

### Card bus

**The card is wired over SPI**, which is *not* what the specification's §2.3
assumed: that section read 1-bit SDMMC pins (CLK 39 / CMD 38 / D0 40) off the
original example rather than off this board. Nothing is connected to those pins
here, and the mount timed out with `sdmmc_init_ocr: send_op_cond (1) returned
0x107` — the timeout this project's own degradation path then reported. The
capability R2 asks for (the card mounted at `/sdcard`) is unchanged; the bus is
not.

| Signal | GPIO |
|---|---|
| `SD_CS` | 10 |
| `SD_CLK` (SCLK) | 12 |
| `SD_MOSI` (DI) | 11 |
| `SD_MISO` (DO) | 13 |

SPI mode runs at 20 MHz (`SDSPI_HOST_DEFAULT()`), which is the reliable choice on
hand wiring; `host.max_freq_khz` in `components/app/board_esp32/board_esp32.c` raises it if your wiring and card
tolerate it.

Two costs, stated rather than discovered later:

1. **GPIO0 is a strapping pin.** Holding it while resetting enters download mode, which
   is exactly how you flash. It has no runtime action: next and previous are the
   left drawer's two arrows.
2. **A card that will not mount is still the most likely fault**, and a card that
   mounts but has no `/music` directory looks the same from outside. The firmware
   logs both distinctly, and the device is built to survive either: see below.

Because the card is on SPI, the USB-JTAG pins are untouched and on-chip debugging
survives — the cost the specification recorded for the SDMMC wiring does not apply
to this board.

To wire a board the specification's way instead, replace the SPI mount in
`sd_mount()` with `SDMMC_HOST_DEFAULT()` plus an `sdmmc_slot_config_t` of width 1
on those three pins; nothing above that function changes.

### PSRAM is a hard prerequisite, and its mode is not detected

The station needs about 96 KB of encoder state plus a burst ring, and internal DRAM
is the scarce resource on an S3. **`sdkconfig.defaults` enables PSRAM, and you must
make the mode match the chip that is actually fitted.** There is no auto-detect on
the ESP32-S3: choosing the wrong mode corrupts memory at boot rather than failing
cleanly.

```ini
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y      # ← must match the fitted chip. N8R8/N16R8 are normally octal
CONFIG_SPIRAM_USE_MALLOC=y    # required for heap_caps_malloc(MALLOC_CAP_SPIRAM)
```

If your module is quad, set `CONFIG_SPIRAM_MODE_QUAD=y` and remove the `_OCT` line.
The startup log tells you what the firmware actually got, so a wrong setting is
diagnosable rather than mysterious. This is the real log from the N16R8 board
this was developed against:

```
I (327) esp_psram: Found 8MB PSRAM device
I (333) esp_psram: Speed: 80MHz
I (772) esp_psram: SPI SRAM memory test OK
I (919) app: chip: esp32s3, 2 core(s), revision 0.2
I (919) app: flash: 16 MB; psram: 8192 KB (octal mode, CONFIG_SPIRAM_MODE is not auto-detected)
I (944) app: heap at startup: internal 277183 B free, external 8386156 B free
I (1223) icecast: station format: 44100 Hz, 256 kbps CBR, joint stereo, MPEG-1
I (1576) icecast: encoder: 102408 B of internal RAM taken (the encoder's own state; near zero means it landed in external memory)
I (1576) icecast: encoder: 1152 samples/frame (26.1 ms), 95880 B of state inside the vendored library, staging buffers in external memory
I (1588) icecast: burst ring: 20098 B (~500 ms of audio) in external memory, sends at most 2 clients
I (1605) files: download buffer: 16384 B in external memory
I (1613) http: listening on :8000 (backlog 16, max 4 at once)
```

Note the encoder figure: **95 880 B**, not the 95 896 B that a 64-bit host measures.
The struct holds three pointers, so it is smaller on the 32-bit target - which is
why the firmware reports `sizeof()` rather than a hard-coded number.

Every allocation this project makes itself is external-memory-preferred with an
internal fallback, and says which path it took — except one, deliberately. The
encoder allocates its own 95 KB of state inside the vendored library, and P3 asks
for that to be external too; this firmware does the opposite and keeps it in
internal RAM, because that state is the hottest working set in the system (a 40 KB
lookup table plus the MDCT and subband tables) and putting it in PSRAM cost 8 % of
the encoder's throughput when it was measured. `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`
is what holds it there, at Kconfig's ceiling of 131072 bytes — just above the
95 880 B the encoder needs. The startup log reports the difference it makes:
`encoder: 102408 B of internal RAM taken`.

## Using it

| URL | What it does |
|---|---|
| `GET /` | The index: every Track in the Library tree, in Playlist order, with its size, what is playing now, and a link to the stream. Also the device's diagnostic page when no card is mounted. |
| `GET /stream` | The live stream: continuous `audio/mpeg` at the station rate and bit rate. Ask with `Icy-MetaData: 1` and the current track is interleaved every 16 000 encoded bytes. |
| `GET /music/<url-encoded-relative-path>` | The original, untranscoded bytes at any depth, with `Content-Length` and a media type from the extension. One download at a time; a second gets `503`. |
| `GET /api/library` | The prebuilt Library JSON, with `state`, counts and a generation ETag. |
| `GET /api/library.bin` | The same generation as an arena: versioned header, nodes, edges, track index and CRC. |
| `POST /api/play` | `{"path":"Album/song.mp3"}` selects any Track at the next chunk boundary. A bad path is a 404 and leaves the stream alone. |
| `POST /api/rescan` | `{}` starts one asynchronous Library rescan; `202` when started, `409` when one is already running. The old generation stays readable; the broadcast parks on the idle bed for the scan, resumes the current Track, and switches to the new generation at its next pass. |
| `POST /mcp` | MCP (2025-11-25): `initialize`, `ping`, `tools/list`, `tools/call` for `library_stats`, `library_list`, `library_search`, `library_track` and `player_status`. Read-only by default; a build with `XTAPE_MCP_CONTROL=1` adds `player_play`, `player_skip`, `player_prev` and `library_rescan`. |
| anything else | `404`, or `405` for a known path with the wrong method. |

```bash
# Listen on the desktop
ffplay http://192.168.1.42:8000/stream
mpv http://192.168.1.42:8000/stream

# Check it really is MPEG audio, at the station format
curl -s --max-time 5 http://192.168.1.42:8000/stream | ffprobe -

# Download the original file and check it is untouched
curl -O http://192.168.1.42:8000/music/track.flac
cmp track.flac /Volumes/YOURCARD/music/track.flac

# Previous / next track
#   tap the < or > arrow in the DJ block at the right edge
```

### Station format

Default **44 100 Hz / 256 kbps CBR / joint stereo**. Set it in
`components/app/appliance/appliance.c` on the `app_services_cfg_t`:

```c
app_services_cfg_t svc = {
    .music_dir = MUSIC_DIR,
    .out_hz = 44100,      /* 44100 48000 32000 22050 24000 16000 11025 12000 8000 */
    .bitrate = 256,       /* must be one the encoder accepts at that rate */
    .max_clients = 4,     /* 1..4; anything else is refused, not coerced */
};
```

The format is validated at boot: a rate or a rate/bit-rate pair the encoder cannot
represent stops the firmware with a message naming the offending value, rather
than resampling audio into an encoder that cannot take it.

#### Why 44.1 kHz / 256 kbps, and what it costs

Both halves were measured on the board, and the measurement is counter-intuitive:
**the encoder's cost per frame is almost flat against the bit rate and dominated
by the sample rate.** Cost per frame, ESP32-S3 at 240 MHz with the encoder's state
in internal RAM and its code in IRAM:

| format | µs/frame | % of one core | | format | µs/frame | % of one core |
|---|---|---|---|---|---|---|
| 44100/64 | 12 551 | 48.0 % | | 32000/96 | 13 570 | 37.7 % |
| 44100/128 | 13 424 | 51.4 % | | 48000/192 | 13 604 | 56.7 % |
| 44100/192 | 13 718 | 52.5 % | | 22050/64 | 6 175 | 23.6 % |
| 44100/256 | 13 817 | 52.9 % | | 44100/320 | 13 852 | 53.0 % |

(That is on synthetic full-scale noise, which is close to the hardest input a
codec can be given; the tonal fixture corpus measures about 40 % at 44.1 kHz.)

So the bit rate is nearly free — 64 to 320 kbps costs 10 % more CPU for five times
the bits — while the sample rate costs in proportion to frames per second. Quality
at a fixed sample rate rises with the bit rate, and measurably so: SNR of the
encoded stream against its source, pink noise at −10 dBFS, delay-compensated:

| bit rate | 96 | 128 | 160 | 192 | 256 | 320 |
|---|---|---|---|---|---|---|
| SNR | 2.4 dB | 3.3 dB | 4.7 dB | 7.1 dB | 11.9 dB | 15.5 dB |

Read those as a *signal* measurement, not as perceived quality. Pink noise at
−10 dBFS with no redundancy is close to the hardest input an encoder can be given,
so the absolute figures are low by construction; what they establish is that the
bit rate is still buying real accuracy at 256 kbps, monotonically. Whether the
result *sounds* better is a listening question, and the honest answer is that the
measurement ranks the options rather than settling the choice — shine's
psychoacoustic model, not its bit budget, is the limit at the top of that range.

Hence 44.1 kHz at 256 kbps: the specification's recommended sample rate, with
three and a half times the bits of the 32 kHz/96 kbps fallback it started from, for
3 % more CPU than 128 kbps and 32 KB/s per client. 320 kbps is the ceiling and is
one line away; above 192 kbps the perceptual gain is probably smaller than the SNR
suggests, because shine's limit is its psychoacoustic model, not its bit budget.

**The cost is headroom, not correctness, and F4's target is not met.** The
specification's acceptance threshold is "under 35 % of one core", and measured, it
is not reachable at 44.1 kHz (52.9 %) *nor at the fallback the specification names*,
32 kHz/96 kbps (37.7 %). Only 22.05 kHz meets it, at 23.6 %. So no format this
project would want to ship satisfies F4 as written — the figure was proposed rather
than measured (the specification says so itself), and this firmware prefers the
quality the user asked for over a target that measurement has shown to be
unreachable at any usable rate. The functional requirement — that the broadcast
keeps up — is what the soak establishes, and it holds with zero starvation.

If headroom is ever needed for something else, the options in order are 32 kHz /
96 kbps, then 22.05 kHz / 64 kbps. Never *up*: a higher bit rate costs slightly
more CPU, not less, though only slightly — which is the other thing the
measurements corrected (see F4's own note, which assumes otherwise).

### When the station format is wrong

A format the encoder cannot represent (see the validation above) is refused before
anything is allocated or brought up — the check is arithmetic, so it costs nothing
and cannot disturb Wi-Fi's memory — names the offending value, and then idles:

```
E (1057) icecast: station format refused: station rate 44101 Hz is not one the encoder accepts; use one of 44100 48000 ...
E (1071) app: not serving: ESP_ERR_INVALID_ARG
E (1075) app: fix out_hz in main.c and reflash; the device is idle until then
```

Halting rather than aborting is deliberate: an abort reboots the device, which
repeats the message forever and hides everything else in the log. Nothing listens,
and Wi-Fi is never brought up, so the device is inert and obvious rather than on the
network with nothing answering.

### When the card is missing

Booting without a card (or with a badly seated one) does not abort the firmware.
The device still joins Wi-Fi and still answers:

```
$ curl -s http://192.168.1.42:8000/ | head -3
<!doctype html><meta charset="utf-8">...
<h1>No card mounted</h1><p>The audio library is not available, but the device is on the network and reachable...
```

It returns 200, quotes the mount error, and retries the mount every 5 seconds —
inserting a card later brings the library up with no reboot.

---

## Host build and tests (no board needed)

The same portable component sources compile for macOS against a small shim
(`host/include/`, `host/shim/shim.c`) that stands in for the eleven ESP-IDF
functions the code uses, and a board adapter (`host/board_host.c`) that stands in
for Wi-Fi and the card mount. There is no target conditional compilation anywhere
in `components/portable/`: the shim and the host board adapter are the only pieces that
know it is a Mac, which is what keeps "the tests pass on the host" from meaning
"the firmware is nothing like what was tested". The `app/` components — the entry
policy, the board adapter and the screen display — cannot run on the host, so they
are not compiled there, and they are where the board and target choices belong; the
host's own adapter is `host/board_host.c`.
`make -C host layering` checks that the app sources stay out.

```bash
make -C host                # build the runnable server
make -C host test           # unit tests: encoder, pipeline, resampler, format, stream, status,
                            # request parsing, burst/ICY, music directory, cassette model
make -C host integration    # T3-T9 and T12 against the real server over HTTP
make -C host layering       # architectural rules: no sockets in the broadcast, no audio in the HTTP layer
make -C host check          # all three
make -C host fixtures       # regenerate the corpus with ffmpeg
make -C host bench          # encoder cost baseline on the host (T11h)
make -C host tools          # mi_encode_file, mi_frames, mi_bench
```

Run it by hand and poke at it:

```bash
./host/build/bin/icecast_host --music-dir host/fixtures --port 18000
# then, in another shell:
curl -s http://127.0.0.1:18000/ | grep -o 'href="[^"]*"'
curl -s --max-time 5 http://127.0.0.1:18000/stream | ffprobe -
```

`--help` lists every option. The port, the music directory and the station format
are all runtime values, and the default port is 18000 so nothing needs root.

The tools are the pieces worth knowing about:

```bash
# Decode one track, resample it, encode it and flush: the deterministic pipeline
./host/build/bin/mi_encode_file host/fixtures/tone-44100.flac /tmp/out.mp3 44100 128
md5 /tmp/out.mp3

# How many frames the decoder sees in a file
./host/build/bin/mi_frames host/fixtures/tail-44100.flac
```

## Two-tier acceptance: host and board

**A phase is finished only when its tests pass on the host _and_ on the board.** A
test that passes on the host and has never run on the board is not evidence about
the board, and is not reported as if it were.

Four things the host cannot establish, by construction:

1. **Encoder CPU headroom** at 240 MHz with Wi-Fi active. The threshold is under
   35 % of one core with no starvation rebases over an hour's soak; the device logs
   its measured cost once a minute (`encoder: N frames, X us/frame = Y % of one
   core`).
2. **Memory fit** with Wi-Fi, lwIP, the decoder and the encoder all live.
3. **microSD contention and timing** — a macOS file read says nothing about the
   SDMMC bus.
4. **FATFS behaviour** — case sensitivity, long-name limits, and the AppleDouble
   filter all differ from APFS. (There is one test the host is *weaker* on for this
   reason: requesting a track in the wrong case resolves on a case-insensitive
   host filesystem and need not on FATFS.)

### The same audio from host and board

```bash
# Host against host, as a self-check (two servers, same corpus):
sh host/tests/digest.sh http://127.0.0.1:18001 http://127.0.0.1:18002 12

# Host against the board, from the same laptop:
sh host/tests/digest.sh http://127.0.0.1:18000 http://192.168.1.42:8000 12
```

The script captures both streams at once, cuts the same window out of each — a
window delimited by two track changes, because a capture starts wherever the client
connected — and reports whether the bytes match. **It checks that both captures
straddle the same span before comparing anything**, and says `NOT COMPARABLE`
otherwise: the metadata interval is a fixed number of encoded bytes, not a number
of tracks, so two captures can legitimately cover different audio.

What the result means:

* With the source rate equal to the station rate the resampler is a pass-through,
  so only the decoder and the encoder are involved. Both are fixed-point, so the
  bytes **must** be identical on host and board.
* With a rate conversion the resampler's floating-point kernel is in the path. A
  mismatch there is **not** a defect: the correct weaker assertion is equal length
  and a bounded signal-to-noise difference.
* A divergence that is not explained by one of the four properties above is a
  defect.

---

## Board status

The host tier tells you the code is right; only the board tells you the device is.
This is where that stands, on an ESP32-S3 devkit (16 MB flash, 8 MB embedded PSRAM,
chip revision v0.2), flashed over the USB-UART bridge and run on **ESP-IDF v6.1**
(and earlier on v5.4.4).

**Verified on hardware**

| What | Evidence |
|---|---|
| Builds and flashes from a clean checkout | `idf.py build && idf.py -p PORT flash`: the current app is `0x2fa490` (3.12 MB) in the 4 MB factory partition, 26 % free |
| 8 MB PSRAM in octal mode at 80 MHz | `esp_psram: Found 8MB PSRAM device`, `Speed: 80MHz`, `SPI SRAM memory test OK` |
| Every allocation this project makes takes the external-memory path (P2/P3) | `staging buffers in external memory`, `burst ring: 20098 B (~500 ms of audio) in external memory`, `download buffer: 16384 B in external memory` |
| The encoder's state is measured, not assumed | `102408 B of internal RAM taken (the encoder's own state)`, and `95880 B of state inside the vendored library` - 16 B less than the host's 95896, because the struct holds three pointers |
| Free memory in both classes is reported at startup and every minute (P2) | `heap at startup: internal 277183 B free, external 8386156 B free` |
| Station format validated and logged (F1-F3) | `station format: 44100 Hz, 256 kbps CBR, joint stereo, MPEG-1` |
| The wire matches the announcement (T3) | `ffprobe`: `mp3, 44100 Hz, stereo, 256 kb/s`, and the whole capture decodes with zero errors |
| The card mounts over SPI (R2) | `Name: SL32G`, `Type: SDHC`, `card: 30420 MB total` |
| A mounted but empty card is distinguishable from an absent one | `card on SPI: CS 10, CLK 12, MOSI 11, MISO 13`, then `/sdcard/music does not exist` |
| No card: the device degrades instead of aborting (R8/T10) | `no card mounted: ESP_ERR_TIMEOUT (0x107)` then `the device will still serve on the network` |
| Card retries are paced, not a tight loop (T10) | `still no card: ...; retrying in 5 s`, one line per attempt |
| A wrong SSID is diagnosable rather than silent | `Wi-Fi: cannot join ... (reason 201), attempt 1; check components/app/board_esp32/wifi_credentials.h` |
| The HTTP surface, end to end (T3-T9) | `BOARD_URL=http://192.168.0.44:8000 sh host/tests/http_tests.sh`: **80 passed, 0 failed**, three runs in a row with no reboot in between |
| ICY metadata at the advertised interval (T5) | `metadata block at exactly 16000: StreamTitle='...' (2 blocks, 32 bytes)` |
| Downloads are the card's bytes (T7) | all eleven fixtures `byte-identical`, with `Content-Length` and media type checked |
| One download at a time (T9/D6) | slow reader keeps its download byte-intact while the second request gets `503` |
| Download contention does not starve the stream (T9b) | 2-minute soak with repeated downloads: `0 starvation rebases`, memory unchanged, no faults |
| A refused station format serves nothing (T12) | with `out_hz = 44101`: `station format refused: station rate 44101 Hz is not one the encoder accepts; use one of ...`, then `not serving: ESP_ERR_INVALID_ARG`, and no listener, no Wi-Fi, no reboot |
| FATFS case sensitivity is documented (T13) | a wrong-case request (`TONE-44100.FLAC`) returns `200` on this card: FATFS resolves names case-insensitively |
| The encoder's cost, starvation and memory over an hour (T11) | one client on `/stream` for 59 minutes: `encoder: 135795 frames, 10811 us/frame = 41.4% of one core`, `starvation rebases 0`, `asserts/overflows/reboots 0`, internal free at the start and end of the run both 18 751 B |
| The padded end-of-track frame survives on the device (T2c) | a 16 s capture carried `tail-44100` - 34 266 frames, deliberately not a multiple of 1152 - three times, and all 530 808 bytes decode with zero errors |
| Download contention does not starve the stream (T9b) | repeated downloads of the largest track with a listener attached: `starvation rebases 0`, `asserts/overflows/reboots 0` |

| Library tree on a real nested card (ticket 13) | a temporary three-level fixture made `tracks=65 dirs=2 scan=137 ms arena=6430 B json=12631 B`; JSON, binary CRC/parentage, the 88 244 B nested download and `POST /api/play` all agreed, and `player_status` showed `zz-xtape-nested/Deep/one-second.wav` |
| Full card scan at boot (ticket 13) | 64 tracks / 2.28 GB: `state=ok tracks=64 dirs=0 bytes=2277100658 scan=96 ms arena=6285 B json=12343 B psram=5295632 -> 5275936 B truncated=0`; synchronous scan stays |
| MCP through mDNS (ticket 13) | `initialize` and `tools/list` over `http://xtape.local:8000/mcp`, `MCP-Protocol-Version: 2025-11-25`, five read-only tools; `dns-sd` listed `_mcp._tcp`; control tools were absent from the default build |
| Ten-minute rescan/download soak (ticket 13) | 10 min with a listener, repeated 46 MB downloads and four rescans: `starvation rebases: 0`, `asserts/overflows/reboots: 0`, encoder 52.9 %, internal minimum 66 043 -> 57 855 B, external 2 039 048 -> 2 079 160 B |
| Main-task stack at the full card (ticket 13) | the first full scan overflowed the 6 144 B main stack; `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384` fixed it and the same card then booted in 96 ms |
| Library cap diagnostic (ticket 13) | a temporary `max_tracks=1` build logged `tracks=1 ... scan=7 ms arena=228 B json=424 B ... truncated=1`, so truncation is visible at the cap as well as under it |

The hour's result deserves stating precisely: **zero starvation rebases and zero
faults in 59 minutes, with the encoder flat at 41.4 % of one core** (10 786 µs per
frame at the start of the run, 10 812 at the end) and free memory identical at the
start and end. What it does *not* establish is compliance with the specification's
35 % threshold, because 41.4 % is above it — see "Why 44.1 kHz / 256 kbps" above for
why no usable rate meets that number. Internal RAM is also tight: the working
minimum during a track is about 11 KB, which is below the 32 KB the heap reserves
for DMA-capable allocations, so anything added to the radio task should be measured
rather than assumed.

### The Sunton 8048S050C (800x480 panel attached)

The same firmware on the panel board, measured 2026-10-18 with the architecture-review
changes flashed.

| Check | Result |
|---|---|
| Boot | panel up through the port, card mounted, Wi-Fi at `192.168.0.44`, station 44100 Hz / 256 kbps |
| HTTP surface (`BOARD_URL` run) | 54 passed, 0 failed among the checks that do not name a fixture; the 26 failures are `tone-*` / `tail-*` 404s, because the card holds a real library |
| Index and downloads from the card's own library | 4 tracks, labels and sizes self-consistent; every download's length, media type and container magic match; traversal 400; non-audio 404; a wrong-case name resolves (FATFS) |
| One download at a time (D6) | second request 503; the lock is free again about 4 s after an aborted client |
| A refused station format (F1) | `out_hz = 44101`: refuses, names the rate and the accepted set, brings up no Wi-Fi, no card and no listener, and does not reboot |
| 3-minute idle (no listener, no downloads) | encoder 58.8 %, **0 starvation rebases** |
| 5-minute stream-only soak | encoder 59.9 %, **0 starvation rebases**, 0 faults |
| 10-minute soak with repeated downloads | encoder 68.9 %, **17 starvation rebases** (bursty), 0 faults; **the listener stayed connected for the whole run** |

The download path has to share one SD bus and one radio with the broadcast, and a fixed
1 ms yield per 16 KB was measured helping and then not - the contention arrives in bursts.
The download now starts gently and backs off for two seconds whenever
`mini_icecast_lag_ms()` reports the broadcast behind; that took the download soak from
about fifty starvation rebases in twelve minutes to **zero in ten**, with the encoder at
62.3 %. The station rate was not the fix: before the backoff, 32 kHz / 96 kbps still
produced forty rebases in five minutes (61.3 %), so the encoder's frame rate was never the
only bottleneck.

The listener now survives the download. The audio task never writes a socket: each client has
a 10-second send ring in PSRAM and its own permanent sender task, and the download is paced at
about 1.3 MB/s rather than arriving in bursts. Before this, a synchronous send on the audio
task with lwIP's 5.7 KB buffer was dropped in 10-30 s. The same soak still reports 17
starvation rebases, bursty, so the broadcast can still fall a second behind under the
worst-case download; the connection and the audio continue and the pacing clock rebases.
That residual is the open part of ticket 19. The external-memory heuristic in the soak flags a
small drop; the 60 s samples oscillate by more than that and return, so it is allocation
churn, not a leak.

The devkit table above is unaffected - it has no panel, and its documented soak results
stand.

**Touch.** The panel board's GT911 is now the screen's pointer: I2C0 at 0x5D, SDA 19,
SCL 20, INT 18, RST 38, read every 10 ms. It is polled rather than interrupt-driven
because this board's INT line does not carry the touch (the reference project measured it
idling through a press), and the last point the controller reported is latched so one
press is one press rather than one per read. A tap on the screen is **not** an action: next
and previous are the two arrows in the left drawer. A tap on the drawer
itself is absorbed there. Either horizontal swipe slides the transport drawer in from the
left edge and shows the DJ block at the right; while they are out, either horizontal swipe
slides them back out. The vertical swipes reach the same controls: a down swipe brings them
in on a **white** page (`k_col_white` instead of the screen's near-black - the orange line
art and the tape's window chips read as ink on paper), and an up swipe takes them away
again. Once that page is white, the next down swipe turns the playing Track's cover on the
cassette's body - the picture is scaled to the outer frame's width, top-aligned and masked
to the frame minus the label window, the hole a pill whose ends are the two reels. The stack
is **label on top, glow in the middle, cinta at the bottom**: the label covers the body
inside the frame with a real transparent window, the glow lights the tape below it, and the
tape exposed below the frame stays visible under the glow. The label fades in on the down
and out on the up over 1 s; a page flip or a Track change cuts it (the loader reuses the
one buffer). An up swipe fades the cover off
before it takes the controls away. The white
page is the down swipe's, not the controls': the horizontal swipes keep
showing the same drawer and block on the dark screen, and any way out returns the page to
dark and hides the cover. The page flips in one frame at the start of the drawer's slide. Two 2 px bands rounded
by the buttons' 8 px close that screen top and bottom - 768x71 at (16,6), its bottom edge on
the first button's and the first DJ cell's top line, and 768x115 at (16,359), its top edge
on their bottom line - so the drawer, the block and the bands read as one frame. The bands
are filled opaque in the page's colour (`k_col_bg` or `k_col_white`) so they cover the tape
while they slide over it - as are the drawer's buttons and the DJ block's cells - and they
slide in with the rest of the chrome, the header down from above the panel and the footer up
from below it, over the drawer's 260 ms ease-out and back out the same way. A third rectangle, the
470x284 window at (110,76), frames the tape in the same ink and fades in and out over the
same time. The header also carries the K7 mark (`assets/labels/k7b.png`, 75x40) above the P
button, centred on the button column and on the band: it is an A8 mask tinted `COL_PRIMARY`
on the dark page and black (`k_col_bg`) on the white one, so it flips with the page and
slides with the header. The
cassette enters at full scale; while the controls are out it scales to 134/256 (148..550),
so the 120 px drawer on the left and the 192 px DJ block on the right both have room. A
left swipe restores the full view. The drawer's three line-art buttons (P, `<`, `>`) each
prebuild a pressed and an unpressed state, swap them on press and release, and pulse their
scale on a click; `<` and `>` call `mini_icecast_prev()` / `mini_icecast_skip()`, so they
are the transport's next/previous now, while P stays a placeholder.

The DJ block is six 96 px carcases in two touching columns at x=593..784 (the first
column is 1 px right of its nominal 592; the second stays at 688), rows at
76/170/264: GAIN above M and R in the first column, HI, MED and LOW in the second. M and R
are the drawer's buttons moved to the block - the same circle, letter, two states and pulse,
with no action yet. The four knobs are a 44 px circle (30% smaller than the
placeholders'), a 24-dot crown outside it and a line from the centre towards the rim, 0 dB
at twelve o'clock, right cuts and left boosts. The two freed cells keep the 64 px circle
and show the owner's open folder and two-circle icons; their functions are TBD. A tap on a
knob opens a modal edit mode
after 550 ms: a 3x virtual knob (284 px at x=206, top on the GAIN button's top, no outer
square, just the circle, the crown and the line) appears over the cassette, which cuts out;
on close the knob disappears and the cassette cuts back in. Only its centre circle closes,
and a click on the ring is the target point, so the knob jumps to that angle (clockwise
cuts, counter-clockwise boosts). The delay lets a double tap on a small knob reset it to
0 dB instead of opening the knob; both circles blink, and nothing else responds while the
overlay is up. The audio is
`components/portable/tone_eq`: a 200 Hz low shelf, a 1 kHz bell and a 4 kHz high shelf plus
a trim before them, with the DJ ranges (GAIN -12..+12 dB, default 0; bands -26..+6 dB,
default 0) and a 2 dB-per-chunk slew on the PCM chunk before the encoder.

The screen has one Level left: Bare, the cassette alone. The Label cover and Artwork
screens below it, and the two down gestures that reached them, are removed. At the entry
100% pose (256/256) an up swipe at Bare raises the peek: the timeline bar slides up with the
Track's elapsed, a growing fill
and its total, and two rounded tags float over the cassette in the left hub's column - the
song above the hub and the group below it, each hanging from that hub by a thin tether,
bobbing a few pixels out of phase. It cannot be raised while the transport drawer holds the
134/256 control pose; an open peek is closed by a down swipe - which only puts the peek
away, and does not go on to open the controls on the same swipe - and an up swipe while it
is up does nothing. The down swipe's other job, and the up swipe's when it has no peek to
raise, are the controls: down shows them with the white page and turns the Track's cover
on, and up turns the cover off before it takes the controls away. The tag
texts are the Track's ID3/FLAC tags, read by the artwork loader on its own
task; until they land the group is the artist directory and the song the cleaned filename.
The two-finger swipe is the VU meter's toggle: a left shows it, a right hides it, and it
starts hidden with a 220 ms fade. The view still latches the two-finger touch so the same
touch
cannot also trip the one-finger gesture. The Sunton build compiles this path; the board
tier has not yet recorded a finger, so the rows above are unchanged. The removal and the
old cover screens are
[`docs/adr/0019-remove-label-and-artwork-screens.md`](docs/adr/0019-remove-label-and-artwork-screens.md);
the drawer and the horizontal swipes are
[`docs/adr/0016-transport-drawer-and-horizontal-swipes.md`](docs/adr/0016-transport-drawer-and-horizontal-swipes.md);
the vertical swipes and the white controls page are
[`docs/adr/0020-down-swipe-controls-and-the-white-page.md`](docs/adr/0020-down-swipe-controls-and-the-white-page.md);
the DJ block's ranges and the transport arrows (now the left drawer's) are
[`docs/adr/0018-dj-eq-knobs-and-transport-arrows.md`](docs/adr/0018-dj-eq-knobs-and-transport-arrows.md);
the peek's bar, tags and tag reader are
[`docs/adr/0017-peek-timeline-and-tags.md`](docs/adr/0017-peek-timeline-and-tags.md).

**Track artwork is the white page's cover, not a Level.** The old Artwork Level - the
cassette Case and the playing MP3's own cover in its window - is still gone, but the
picture is back inside the cassette: the white page's down swipe prints it on the
outer frame, masked to frame minus the label window, with the tape exposed below the frame
still visible. The pipeline is the one the Artwork
screen used: the locator in `components/portable/artwork` (the ID3v2.3/2.4 `APIC` frame,
or a native FLAC `PICTURE` block) and the TJpgDec decode and fit in `artwork_loader.c`.
The panel board builds it and carries the 464,310 B cover buffer; the old Case backdrop
(`assets/labels/cover.jpg`, 45 KB JPEG, 768,000 B decoded) and the Label cover's source
(`assets/labels/k7logo.png`) stay in the tree unembedded.
The loader also reads the peek's text tags, so there is no separate tag task. The host
tier still builds and tests the locator and the fit (`host/tests/t15_artwork.c`). The old
decision is
[`docs/adr/0013-artwork-level-and-case-window.md`](docs/adr/0013-artwork-level-and-case-window.md),
superseded by ADR 0019 for the screens; the cover's new home is
[`docs/adr/0021-white-page-cover.md`](docs/adr/0021-white-page-cover.md).

**Backlight sleep.** GPIO 2 is now a 400 Hz, 8-bit LEDC duty instead of an on/off pin. The
screen starts at full brightness, then fades to 60% after 2 minutes, 30% after 5
minutes, and 0% after 10 minutes without a touch; a touch fades it back to full from
wherever it is. The first press after a dim only wakes the screen, so it cannot skip a
track or move a Level by accident. The steps and the fades are Kconfig values
(`APP_UI_BACKLIGHT_AWAKE_PCT`, `APP_UI_BACKLIGHT_DIM1_S`/`_PCT`,
`APP_UI_BACKLIGHT_DIM2_S`/`_PCT`, `APP_UI_BACKLIGHT_DIM3_S`/`_PCT`,
`APP_UI_BACKLIGHT_DIM_FADE_MS`, `APP_UI_BACKLIGHT_WAKE_FADE_MS`). If LEDC refuses the
pin, the backlight falls back to a plain output held high, so the panel stays lit and
only dimming is lost. For a visual check, `APP_UI_BACKLIGHT_TEST=y` replaces the
cassette with a white screen and steps the backlight through 100/80/60/40/20/0 every
five seconds, printing the level in large type.

The label cover and the Case are still parked with the two old screens: the label cover is
`assets/labels/k7logo.png` (converted to RGB565A8, 1.1 MB, its black triangle and circle
transparent so the reels showed through) and the Case is `assets/labels/cover.jpg`; neither
is embedded now. The Track picture does not need them - the cover uses the cassette's own
frames. The loader's measurements (the first card track's 239x236 `APIC` became the
603x385 window in 691 ms; 971,248 B of PSRAM before its 464,310 B cover buffer and
504,300 B after, with the Case's 768,000 B backdrop also allocated) are the board's
reference; without the Case the cover costs 464,310 B. The app image is **1.68 MB** with
**60%** of its 4 MB partition free; re-check it after the next build.

**Five faults that only the board could show**, each now fixed and each worth the
tier on its own:

1. **Starting the services before lwIP was initialised** made the first `socket()`
   call trip `assert failed: tcpip_send_msg_wait_sem ... (Invalid mbox)` and reboot
   the device. `net_start()` (stack, non-blocking) and `wifi_wait_connected()` are
   now separate, which is also what lets the whole startup telemetry appear before,
   and independently of, an association.
2. **The connection task's stack overflowed on the first request** -
   `***ERROR*** A stack overflow in task http_conn` - because 6 KB did not cover the
   index's streaming writer plus the request buffer plus lwIP. It is 10 KB now, with
   the worst-case headroom reported in the telemetry.
3. **Internal RAM ran out for the connection stacks**, which showed up as requests
   answered with nothing at all and `no memory for a connection task` in the log.
   Task stacks now take the external-memory-preferred path with an internal
   fallback, and are deleted with `vTaskDeleteWithCaps()` - without which a
   capability-allocated stack is never freed, which leaked 10 KB per request and
   degraded the device across repeated test runs (77 passing, then 68, without a
   reboot). The three clean runs above are the check for that.
4. **Bring-up order decided whether Wi-Fi worked at all.** The broadcast allocates
   the encoder's ~100 KB of internal RAM; `esp_wifi_init()` allocates its
   DMA-capable receive and transmit buffers from the same pool, in contiguous
   chunks of roughly 25 KB. Starting the broadcast first left Wi-Fi unable to get
   them — `wifi:malloc buffer fail`, `Expected to init 16 rx buffer, actual is 14`,
   then the driver's own cleanup aborted and the device rebooted in a loop (20
   resets in a 32-second capture). The same binary with the order changed — validate
   the format first, which is arithmetic and allocates nothing, then the network,
   then the encoder — runs with zero resets. The margin is that thin: it worked
   before by a sliver, and a small addition of static RAM was enough to lose it.
   This is the strongest argument for the LVGL memory plan in
   `docs/research/radio-player-memory-and-lvgl.md`.
5. **The encoder was unoptimised and in the wrong memory.** IDF's project default is
   `-Og`, and with `CONFIG_SPIRAM_USE_MALLOC` the encoder's 95 KB of state is a
   >16 KB allocation, so it landed in PSRAM. Together those made it need 33.4 ms per
   frame - 128 % of one core - so the broadcast starved every couple of seconds.
   Building it at `-O3`, keeping its state internal
   (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=131072`, which is Kconfig's ceiling) and
   placing its code in IRAM brings it to 13.8 ms at 44.1 kHz/256 kbps. The host
   shim has no PSRAM, no IRAM and 64 KB stacks, so none of this was visible there.

### Reproducing the board tier

Two things are needed first, and neither is in the repository:

```bash
cp components/app/board_esp32/wifi_credentials.h.example components/app/board_esp32/wifi_credentials.h
$EDITOR components/app/board_esp32/wifi_credentials.h                 # SSID and password; gitignored
idf.py -p /dev/cu.usbserial-834430 flash

# and, on the card, a music directory holding the corpus:
mkdir -p /Volumes/YOURCARD/music
cp host/fixtures/*.mp3 host/fixtures/*.flac host/fixtures/*.wav /Volumes/YOURCARD/music/
```

Then the same assertions that pass on the host run against the device. The device's
own serial log is the second input: the PSRAM, heap and encoder figures only exist
there, and `SERIAL_LOG` is what lets the script check them.

```bash
PY=~/.espressif/python_env/idf6.1_py3.12_env/bin/python     # any python with pyserial
$PY host/tests/serial_log.py -s 20 -o /tmp/board.log        # boot log, resets the board

BOARD_URL=http://<board-address>:8000 SERIAL_LOG=/tmp/board.log \
  HOST_DIR=host sh host/tests/http_tests.sh                 # T3-T9, 80 assertions
```

The long-running checks have their own script. It resets the board, captures for
the whole run, attaches one stream client and (optionally) downloads the largest
track repeatedly, then reads the device's telemetry and decides for itself:

```bash
sh host/tests/board_soak.sh http://<board-address>:8000 /dev/cu.usbserial-XXXX 60 stream
sh host/tests/board_soak.sh http://<board-address>:8000 /dev/cu.usbserial-XXXX 2 downloads   # T9b
MAX_COST=70 sh host/tests/board_soak.sh ... 60 stream    # when quality, not headroom, is the goal
```

`MAX_COST` defaults to the specification's 35 %; a configuration chosen for quality
is held to its own bound explicitly rather than quietly exempted.

Two acceptance tests are not scripts:

* **T12 on the board** — set `out_hz` to a rate the encoder does not accept (44101,
  say), flash, and watch it refuse to serve: it names the offending rate and stops
  before the listener comes up.
* **T13 (FATFS case sensitivity)** — the suite prints the status code it gets for a
  wrong-case name rather than asserting one, because the specification asks for the
  behaviour to be documented, whichever way the filesystem resolves it. macOS is
  case-insensitive, so this one is board-only by construction.

The host/board digest comparison is in `host/tests/digest.sh`:

```bash
sh host/tests/digest.sh http://127.0.0.1:18001 http://<board-address>:8000 12
```

---

## Architecture

```
main/                       the entry point: app_main and the build surface
components/
  portable/                 every .c compiles on the host as well (H3)
    app_board/              what the services need from a board, and nothing else
    app_services/           composes the services in the one order that fits memory
    http_server/            listening socket, connection workers, routing
      http_parse.c/.h       request-line rules and route matching, no socket
    mini_icecast/           PCM in -> MP3 -> ICY broadcast, client fan-out
      mi_decode.c/.h        mp3/flac/wav -> stereo int16 (vendored dr_libs)
      mi_resample.c/.h      windowed-sinc resampler to the station rate
      mi_encode.c/.h        the 1024<->1152 mismatch, whole frames out
      mi_station.c/.h       the station configuration, resolved once
      mi_burst.c/.h         the burst on connect, as whole encoded frames
      mi_icy.c/.h           the metadata interval and its 16-byte blocks
      mi_ring.c/.h          the per-client send ring: the audio task never writes a socket
      mini_icecast.c/.h     pacing, client list, station status
    tone_eq/                the stream's three-band DJ EQ and trim, before the encoder
    library/                the immutable tree, arena, JSON, Playlist and lookup
    library_http/           GET /api/library[.bin] from the prebuilt views
    mini_files/             GET / index, GET /music/<relative path> download
    cassette/               what the screen shows: reel phases, pack fills, tape ink, title
    artwork/                a Track's picture and text tags: ID3 APIC/TPE1/TIT2, FLAC
                            PICTURE/Vorbis, the cover fit
    dr_libs/                vendored dr_mp3 / dr_flac / dr_wav (public domain / MIT-0)
    shine/                  vendored toots/shine MP3 encoder (LGPL-2.0-only)
  app/                      firmware only: the entry policy, the board adapter and the screen
    appliance/              the entry policy: station format, card retry, console
    board_esp32/            Wi-Fi, SPI card mount, hardware census; credentials live here
    screen_display/         panel and touch bring-up, LVGL's PSRAM allocator, the cassette
                            composition and its parts (tapes, meter, levels, peek, eq, input)
host/                       the host build: shim, board adapter, fixtures, tests, tools
```

The cassette screen is the composition: `cassette_tapes`, `cassette_meter`,
`cassette_levels`, `cassette_peek`, `cassette_buttons`, `cassette_eq` and `screen_input`
are its parts, and the design grid is the portable `cassette_layout` the host tier pins
with `t16`. The view keeps the pose, steps the model and maps intents to actions; no part
reads another's state.

The dependency direction is one-way and is checked by `make -C host layering`:
`mini_files` and `mini_icecast` depend on `http_server`; only the application
layer (`app_services` in `portable/` and `appliance` in `app/`) depends on `mini_files`;
`mini_icecast` owns no socket, parses no request and renders no markup; `cassette`
depends only on the broadcast's status snapshot; `artwork` is a leaf over `audio_format`
with no LVGL, no ESP-IDF and no card. The split between `portable/` and
`app/` is itself a rule: every `.c` under `portable/` must be in `host/Makefile`,
and no source under `app/` may be. The binary search for the seam is in
`docs/adr/0003-extract-http-server-from-mini-icecast.md`; the boot-built Library
tree, its arena, JSON, generation and leases are in
[`docs/adr/0012-boot-built-library-arena.md`](docs/adr/0012-boot-built-library-arena.md).

The services are composed once, in `app_services`, and both entry points call it:
the appliance (`components/app/appliance`) and the host build through
`host/icecast_host.c`. The board adapter is `components/app/board_esp32/board_esp32.c`; the host's is
`host/board_host.c`. The order is not cosmetic — the station is resolved before
anything allocates, the network comes up before the encoder takes its internal RAM,
and the listener starts last — and it now has one home instead of one per entry
point.

Requests are parsed on a pool of connection workers, capped by a connection limit,
with a generous listen backlog — so a client that connects and says nothing, or reads
very slowly, cannot hold up anybody else.

The pool is a deliberate departure from the specification's "for each accepted fd it
starts a short-lived task" (S1). The behaviour S1 asks for is kept; the mechanism is
not, for two measured reasons. A task per request means a 10 KB stack allocation per
request from the scarce memory class, which failed under pressure ("no memory for a
connection task", with the client seeing nothing). And the matching deletion call for
a capability-allocated stack, on ESP-IDF 6.1, spawns a temporary cleanup task when a
task deletes itself - and aborts the device if that allocation fails. Neither is
acceptable once per request, so the workers are created once at startup (failing
there, loudly, if there is no memory for them) and live forever. There is nothing
left to leak and nothing left to abort.

### Three things worth knowing about the encoder

All three were measured rather than assumed, and all three are why `mi_encode.c`
exists as its own module:

1. **`shine`'s output buffers are not frame-aligned.** Its bit writer emits whole
   32-bit words and carries each frame's leftover bits into the next call, so
   measured, only 312 of 1000 calls began with a frame sync. The bytes are a
   correct, frame-continuous MP3 stream; the *call boundaries* are arbitrary.
   `mi_encode.c` walks the frame headers and hands on whole frames, which is what
   the specification requires of everything it broadcasts.
2. **`shine_flush()` does not flush.** It reports zero bytes pending every time —
   after full frames, on a fresh encoder, and repeatedly. A track's final partial
   block is therefore completed with silence (at most 26 ms) by `mi_enc_flush()`,
   and the encoder's last few bits are carried into the next track rather than
   dropped.
3. **Its padding bit lies at some formats.** Where the exact slot count is a whole
   number and shine's double-precision arithmetic lands just below it — measured at
   32000 Hz / 96 kbps — it writes a padding bit for a slot it never fills: 432
   bytes emitted with a header claiming 433. No decoder can parse such a stream
   (`Failed to find two consecutive MPEG audio frames`, per ffmpeg), and a framer
   that believes the header loses every byte of it. `mi_encode.c` therefore
   *measures* this once per format, by encoding silence and asking which reading of
   the padding bit makes the byte stream tile, and clears the bit in the stream
   when it is wrong. It says so in the log when it does.

The encoder's own hot loop is also built at `-O3` rather than the project default
and placed in IRAM by a linker fragment — no line of shine's source changes. Both
were measured on the board, on the synthetic benchmark, at 44.1 kHz/128 kbps:
IRAM placement alone is worth 17 % (16.1 ms per frame down to 13.4 ms), and the
same workload unoptimised (`-Og`) took 33.4 ms, so compiler and placement together
are worth about 2.5x. In IRAM that costs 14 351 B of code (22 913 B including
shine's constant tables and descriptors), taken from the region shared with data —
which is the trade to keep in mind when planning what else wants IRAM. That is the difference between 32 kHz and 44.1 kHz being
affordable at all. A separate 8 % came from keeping its 95 KB of state out of
PSRAM, as described above.

The table in the station-format section is the same benchmark at each format; the
telemetry line from a running device (which measures the real fixture corpus) reads
about 10.8 ms per frame, 41.4 % of one core, at 44.1 kHz/256 kbps. Same encoder,
different content: the synthetic signal is the harder of the two, which is why it
is the number quoted for headroom.

## Licensing

This project's own code is MIT-licensed (`LICENSE`). The vendored MP3 encoder is
not: **`components/portable/shine/` is LGPL-2.0-only**, kept as unmodified source in its own
component so that you can replace or patch it and relink — which is what
LGPL-2.0 section 6 requires. `LICENSES/LGPL-2.0.txt` and `THIRD_PARTY_NOTICES`
ship with the sources, and this project's own licence is unaffected by the
arrangement.

The GPL-3.0 encoder port sometimes suggested for the ESP32
(`fknrdcls/mp3_shine_esp32`) is deliberately **not** used anywhere: it would impose
GPL on the whole project.

## The UI's memory, decided in advance

An LVGL interface is the next thing this firmware grows, and the memory for it is
already arranged and measured rather than left to the default allocator. The decision
is `docs/adr/0004-ui-memory-is-external.md`; the numbers behind it are
`docs/research/radio-player-memory-and-lvgl.md`.

What the board reports, at startup, with the broadcast running:

```
screen: before the screen:     internal 157787 B free (largest 81920 B), psram 7998472 B free
screen: display 320x240 with a 25600 B partial draw buffer in PSRAM
screen: LVGL: 123 objects hold 54276 B in 956 blocks (peak 56312 B), all of it PSRAM
screen: with the screen:       internal 157787 B free (largest 81920 B), psram 7980304 B free
screen: verdict: the screen's memory is external; internal RAM was the same before and after
screen: after freeing it: the screen is gone, LVGL keeps 6188 B of its own state ...
```

Three things make that true, and the second is the one that is easy to get wrong:

1. **`SPIRAM_MALLOC_ALWAYSINTERNAL` is 4096**, so the encoder's 95 KB of state lives in
   PSRAM instead of internal RAM. That is the +7.6 % of encoder cost described above,
   paid for a largest free internal block of 81 920 B instead of 18 432 B.
2. **LVGL gets an explicit PSRAM allocator** (`LV_USE_CUSTOM_MALLOC`, implemented in
   `components/app/screen_display/lvgl_mem.c`). Lowering the threshold alone does not do this:
   `heap_caps_malloc_default()` prefers internal memory for anything *at or below* the
   threshold, so at 4096 every widget would still ask internal first and fall through to
   PSRAM only when internal ran out — silently, which is exactly the failure mode that
   rebooted this device once. Routing LVGL through
   `heap_caps_malloc_prefer(PSRAM first)` makes the placement a decision.
3. **The draw buffer is allocated from PSRAM explicitly**, and `LV_USE_ASSERT_MALLOC` is
   on so a genuine failure is a diagnostic rather than a null dereference later.
4. **The cover costs one buffer, not two.** The white page's cover allocates the old
   Artwork path's 464,310 B RGB565 buffer, but not the Case's 768,000 B backdrop: the
   cassette's frames are the frame now. The loader is one task (it also reads the peek's
   tags) and its stack starts in PSRAM. The app's rodata and instructions stay in PSRAM -
   the panel needs them there; moving either to flash brought the glitches back - which is
   why the boot log reports the PSRAM margin before and after the cover.

The image is 1.68 MB with the cover loader and without the label cover or the Case, so
the app partition is 4 MB with 60 % free (`partitions.csv`) rather than the 1.5 MB
single-app layout. LVGL is a fetched dependency pinned by
`components/app/screen_display/idf_component.yml` and `dependencies.lock`; the host tier
compiles only the portable components and never sees it.

## Putting your own music on the card

The server indexes whatever is playable in the card's `music` directory - `.mp3`,
`.flac` and `.wav`, at any sample rate, mono or stereo - so a real library needs no
conversion step:

```bash
# with the card in a reader
mkdir -p /Volumes/YOURCARD/music
cp ~/Music/*.mp3 ~/Music/*.flac /Volumes/YOURCARD/music/
```

Two things about a real library that synthetic fixtures never exercise, both handled:

* **Awkward names work.** Spaces, accents, apostrophes, percent signs and semicolons are
  percent-encoded in the index's links, HTML-escaped in the link text, and decoded back on
  download. `host/tests/hostile_names.py` builds a library of exactly those names and
  checks all three places a name travels - index, download, and the stream's ICY metadata -
  because an apostrophe or a semicolon in a name is what breaks a metadata block.
* **Filenames are read as UTF-8** (`CONFIG_FATFS_API_ENCODING_UTF_8`). With the default
  ANSI/OEM setting FatFS transcodes long names through the OEM code page, which cannot
  represent `é` or `ú`, so a library with accents in its titles would show mangled names
  even though the pages are declared UTF-8. The card's own long-name support is
  unchanged; this only decides the encoding the filesystem hands back.
* **Cover art shows up when it is a JPEG.** An MP3's ID3 `APIC` (or a FLAC `PICTURE`) is
  read only when the Artwork level is opened; it fills the Case's 603x385 window and is
  centre-cropped, never squeezed. A PNG picture is skipped.

The board tier's index check runs in `--superset` mode: on the host it still insists the
index lists *exactly* the fixture corpus, but against the board it requires every fixture
to be present and accepts the card's own music alongside - while still refusing a
dot-file, a non-audio extension or a wrong size.

The live stream's title is the file's name without its extension, with apostrophes
replaced by spaces: ICY metadata blocks are delimited by apostrophes and semicolons, so a
name containing them cannot be sent verbatim.

The group and song labels on the screen are not that title: they are the Track's own
ID3/FLAC text tags, read by the display's loader for the peek and shown nowhere else. A
listener's ICY client still sees the filename; the card is not re-read for the stream.

## The internal-RAM picture

Worth stating plainly, because it constrains everything added on top of this
firmware — and `docs/research/radio-player-memory-and-lvgl.md` works through what it
means for a UI. Measured on the board during playback, with the encoder's state
deliberately internal:

```
internal: free 35327 B, largest block 18432 B, allocated 294980 B, min ever free 19664 B
external: free 8187420 B, largest block 8126464 B, allocated 198456 B
```

Two of those numbers matter more than the first. **The largest single free internal
block is 18 432 B**, so any larger single internal allocation fails — `esp_wifi_init()`
wants roughly 25 KB of contiguous DMA-capable internal RAM for its receive and
transmit buffers, which is why bringing the encoder up first made Wi-Fi fail and the
device reboot in a loop. And on ESP-IDF 6.1 the 32 768 B DMA reserve is not walled
off: `heap_caps_malloc_default()` (`components/heap/heap_caps.c:107-134`) asks internal
memory first for anything up to `SPIRAM_MALLOC_ALWAYSINTERNAL`, then **silently** falls
through to PSRAM. So a new subsystem's allocations can quietly eat the margin Wi-Fi
needs, and nothing reports it.

The measured lever, if more internal RAM is wanted: lowering
`SPIRAM_MALLOC_ALWAYSINTERNAL` from 131072 to 4096 moves the encoder's 95 KB of state
to PSRAM and takes internal free from 35 327 B to **154 643 B** (largest block 18 432 →
77 824 B) for **+7.6 %** of encoder cost — 41.4 % to 44.5 % of one core.

## Known limitations and deliberate omissions

* **No authentication anywhere** (see the warning at the top) and **no TLS**.
* **No Opus, Vorbis, Ogg, AAC** — MP3 only, for reach and because a fixed-point MP3
  encoder is the only one that fits this power budget.
* **No seek, no range requests, no transcoding on download.** Downloads are the
  original bytes.
* **No Wi-Fi provisioning UI** and no OTA updates; discovery is the fixed
  `xtape.local`, so two devices on one LAN collide. Credentials are compiled in.
* **Card removal is not hot-detected.** If the card is pulled after boot, reads start
  failing, the broadcast skips the Track, and the Library stays stale until you rescan
  (`POST /api/rescan`) or reboot.
* **One download at a time.** A second concurrent download is declined with `503`
  rather than queued, so a 40 MB FLAC cannot starve the broadcast behind it.
* **Track artwork is JPEG only.** A PNG picture in an `APIC` or `PICTURE` block is
  located and reported but not drawn; the case window does not carry LodePNG. A track
  with no usable picture leaves the Case's window empty.
* **The stream is a broadcast**: it cannot be paused or seeked, and every client
  hears the same moment.
* **`shine`'s audio quality is below LAME's.** Real, acknowledged, and unavoidable
  at this power budget. Raising the bit rate buys measurable accuracy, but the
  psychoacoustic model is the limit, not the bit budget.
* **The encoder uses about half of one core at the default station format**, which
  exceeds the specification's 35 % headroom target. Nothing starves - the soak is
  the evidence - but less is left over than the specification assumed, and the
  reason is that a 44.1 kHz station costs what it costs.
* **The firmware builds against ESP-IDF v5.4.4 and runs on an ESP32-S3 N16R8
  devkit**; see the board status section below for exactly what has and has not
  been exercised on hardware.

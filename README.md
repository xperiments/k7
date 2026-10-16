# SD stream server

An ESP32-S3 devkit with a microSD card in it joins your Wi-Fi and serves the card's
music over HTTP:

* **one continuous live MP3 stream** at `http://<device>:8000/stream` — every
  client hears the same track at the same moment, and any player works, because
  the stream format never changes;
* **a browsable index** at `/`, listing the tracks on the card with their sizes
  and what is playing now;
* **byte-exact downloads** at `/music/<name>` — the original `.mp3`, `.flac` or
  `.wav` exactly as it sits on the card, not a transcode.

Anything the card holds at any sample rate is decoded, resampled to one station
rate and encoded to MP3 for the stream. The stream is the universal-compatibility
path; downloads are the fidelity path.

> ## Read this before you deploy it
>
> **This firmware serves a live stream and an unauthenticated file index to
> anyone who can reach port 8000, and its Wi-Fi credentials are compiled into the
> binary.** There is no authentication anywhere: not on the index, not on the
> downloads, and not on the stream (`/stream` deliberately stays open even if auth
> is ever added — car head units and hardware players cannot send credentials in a
> stream URL).
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

Requires ESP-IDF **5.4 or newer** (the component metadata asks for `idf: ">=5.4"`)
for the `esp32s3` target.

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
cp main/wifi_credentials.h.example main/wifi_credentials.h
$EDITOR main/wifi_credentials.h        # your SSID and password
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
* Put your tracks in a **single flat directory**: `/sdcard/music/`. There is no
  recursion into subdirectories and no database — what is in that one directory
  is the library.
* Accepted extensions: `.mp3`, `.flac`, `.wav`, in any case. Any sample rate, mono
  or stereo. Files whose name begins with `.` are ignored (that is how macOS
  AppleDouble `._track.mp3` files are skipped).
* Long file names are enabled up to 128 characters (`CONFIG_FATFS_MAX_LFN=128`).
* Tracks are played in directory order, looping forever.

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
hand wiring; `host.max_freq_khz` in `main/main.c` raises it if your wiring and card
tolerate it.

Two costs, stated rather than discovered later:

1. **GPIO0 is the skip button** and is also a strapping pin. Holding it while
   resetting enters download mode, which is exactly how you flash. During normal
   operation, pressing it skips to the next track.
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
I (921) app: heap at startup: internal 305571 B free, external 8386148 B free
I (1182) icecast: station format: 44100 Hz, 128 kbps CBR, joint stereo, MPEG-1
I (1326) icecast: encoder: 1152 samples/frame (26.1 ms), 95880 B of state inside the vendored library, staging buffers in external memory
I (1327) icecast: burst ring: 12098 B (~500 ms of audio) in external memory, sends at most 2 clients
I (1344) files: download buffer: 16384 B in external memory
I (1355) http: listening on :8000 (backlog 16, max 4 at once)
```

Note the encoder figure: **95 880 B**, not the 95 896 B that a 64-bit host measures.
The struct holds three pointers, so it is smaller on the 32-bit target - which is
why the firmware reports `sizeof()` rather than a hard-coded number.

Every large allocation this project makes is external-memory-preferred with an
internal fallback, and says which path it took. The encoder's own state is
allocated inside the vendored library (`calloc`), which ESP-IDF serves from
internal RAM — that is the one allocation this project cannot redirect, and it is
the number to watch if internal RAM is tight.

## Using it

| URL | What it does |
|---|---|
| `GET /` | The index: every track in `music_dir`, with its size, what is playing now, and a link to the stream. Also the device's diagnostic page when no card is mounted. |
| `GET /stream` | The live stream: continuous `audio/mpeg` at the station rate and bit rate. Ask with `Icy-MetaData: 1` and the current track is interleaved every 16 000 encoded bytes. |
| `GET /music/<url-encoded-name>` | The original, untranscoded bytes, with `Content-Length` and a media type from the extension. One download at a time; a second gets `503`. |
| anything else | `404`. |

```bash
# Listen on the desktop
ffplay http://192.168.1.42:8000/stream
mpv http://192.168.1.42:8000/stream

# Check it really is MPEG audio, at the station format
curl -s --max-time 5 http://192.168.1.42:8000/stream | ffprobe -

# Download the original file and check it is untouched
curl -O http://192.168.1.42:8000/music/track.flac
cmp track.flac /Volumes/YOURCARD/music/track.flac

# Skip to the next track
#   press the BOOT (GPIO0) button
```

### Station format

Default **44 100 Hz / 128 kbps CBR / joint stereo**, which every player accepts.
Set it in `main/main.c` on the `mini_icecast_cfg_t`:

```c
mini_icecast_cfg_t cfg = {
    .music_dir = MUSIC_DIR,
    .out_hz = 48000,      /* 44100 48000 32000 22050 24000 16000 11025 12000 8000 */
    .bitrate = 192,       /* must be one the encoder accepts at that rate */
    .max_clients = 4,     /* 1..4; anything else is refused, not coerced */
    .pcm_cb = on_pcm,
};
```

The format is validated at boot: a rate or a rate/bit-rate pair the encoder cannot
represent stops the firmware with a message naming the offending value, rather
than resampling audio into an encoder that cannot take it. 48 kHz / 192 kbps is
available when fidelity matters more than reach. If the encoder turns out to be too
expensive on real silicon, the fallback is a **lower** rate and bit rate (32 kHz /
96 kbps) — never a higher bit rate.

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

The same component sources compile for macOS against a small shim
(`host/include/`, `host/shim/shim.c`) that stands in for the eleven ESP-IDF
functions the code uses. There is no target conditional compilation anywhere in
`components/`: the shim is the only thing that knows it is a Mac, which is what
keeps "the tests pass on the host" from meaning "the firmware is nothing like what
was tested".

```bash
make -C host                # build the runnable server
make -C host test           # unit tests: encoder, accumulator, resampler guard, format validation
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
This is where that stands, run on an ESP32-S3 devkit (16 MB flash, 8 MB embedded
PSRAM, chip revision v0.2, ESP-IDF v5.4.4), flashed over the USB-UART bridge.

**Verified on hardware**

| What | Evidence |
|---|---|
| Builds and flashes from a clean checkout | `rm -rf build sdkconfig && idf.py build && idf.py -p PORT flash`: 972 KB app in a 1.5 MB partition, 37 % free |
| 8 MB PSRAM in octal mode at 80 MHz | `esp_psram: Found 8MB PSRAM device`, `Speed: 80MHz`, `SPI SRAM memory test OK` |
| Every large allocation takes the external-memory path (P2/P3) | `encoder: ... staging buffers in external memory`, `burst ring: 12098 B ... in external memory`, `download buffer: 16384 B in external memory` |
| Free memory in both classes is reported at startup (P2) | `heap at startup: internal 305571 B free, external 8386148 B free` |
| Station format validated and logged (F1-F3) | `station format: 44100 Hz, 128 kbps CBR, joint stereo, MPEG-1` |
| The encoder's state is measured, not assumed | `95880 B of state inside the vendored library` - 16 B less than on the host, because the struct holds three pointers |
| The HTTP listener comes up on its own port | `http: listening on :8000 (backlog 16, max 4 at once)` |
| The card mounts over SPI (R2) | `Name: SL32G`, `Type: SDHC`, `card: 30420 MB total, 30418 MB free` |
| A mounted but empty card is distinguishable from an absent one | `card on SPI: CS 10, CLK 12, MOSI 11, MISO 13`, then `/sdcard/music does not exist` |
| No card: the device degrades instead of aborting (R8/T10) | `no card mounted: ESP_ERR_TIMEOUT (0x107)` then `the device will still serve on the network` |
| Card retries are paced, not a tight loop (T10) | `still no card: ...; retrying in 5 s`, one line per attempt |
| A wrong SSID is diagnosable rather than silent | `Wi-Fi: cannot join "your-ssid" (reason 201), attempt 1; check main/wifi_credentials.h` |
| The idle-library log is rate-limited, not per loop | one `no playable files` line in 20 s, not one per 2 s loop |
| No reboots or asserts while idle | 20 s capture, zero `Rebooting`/`assert` lines |

One bug was found only here, and is worth recording as the reason the board tier
exists: starting the services before the network stack is initialised makes the
first `socket()` call trip `assert failed: tcpip_send_msg_wait_sem ... (Invalid
mbox)` and reboot the device. The host shim has no lwIP, so no host test could
have caught it. `net_start()` (stack, non-blocking) and `wifi_wait_connected()`
(blocks for an address) are now separate steps, which is what lets the whole
startup telemetry above appear before, and independently of, an association.

**Not yet verified on hardware** - these need an address to reach the device and a
card to serve, neither of which was available:

* every HTTP test: T3 (wire format), T4 (accept loop), T5 (ICY metadata), T6
  (index), T7 (downloads), T8 (path guards), T9 (concurrent downloads)
* T9b (SDMMC contention), T11 (the one-hour CPU and memory soak), T12 on the board,
  T13 (FATFS case sensitivity), and the board halves of T2c
### Running the rest of the board tier

Two things are needed, and neither is in the repository:

```bash
cp main/wifi_credentials.h.example main/wifi_credentials.h
$EDITOR main/wifi_credentials.h                 # SSID and password, gitignored
idf.py -p /dev/cu.usbserial-834430 flash

# and, on the card, a music directory holding the corpus:
mkdir -p /Volumes/YOURCARD/music
cp host/fixtures/*.mp3 host/fixtures/*.flac host/fixtures/*.wav /Volumes/YOURCARD/music/
```

Then the same assertions that pass on the host run against the device:

```bash
BOARD_URL=http://<board-address>:8000 SERIAL_LOG=/tmp/board.log \
  sh host/tests/http_tests.sh
```

`SERIAL_LOG` is optional; with it, the startup-telemetry assertions are checked
against the device's own log (the octal-PSRAM and free-memory lines only exist
there). `host/tests/serial_log.py` produces one:

```bash
~/.espressif/python_env/idf5.4_py3.12_env/bin/python host/tests/serial_log.py -s 15 -o /tmp/board.log
```

T12 on the board is not a command-line check: it means flashing a build with a
deliberately bad `out_hz` and watching it refuse to serve.

---

## Architecture

```
main/                       app_main: NVS, Wi-Fi, card mount + retry, hand-off, skip button, levels
components/
  http_server/              listening socket, per-connection parse and route   (knows nothing about audio)
  mini_icecast/             PCM in -> MP3 -> ICY broadcast, client fan-out
    mi_decode.c/.h          mp3/flac/wav -> stereo int16 (vendored dr_libs)
    mi_resample.c/.h        windowed-sinc resampler to the station rate
    mi_encode.c/.h          the 1024<->1152 mismatch, whole frames out
    mini_icecast.c/.h       pacing, burst, ICY metadata, client list
  mini_files/               GET / index, GET /music/<name> download
  dr_libs/                  vendored dr_mp3 / dr_flac / dr_wav (public domain / MIT-0)
  shine/                    vendored toots/shine MP3 encoder (LGPL-2.0-only)
host/                       the host build: shim, fixtures, tests, tools
```

The dependency direction is one-way and is checked by `make -C host layering`:
`mini_files` and `mini_icecast` depend on `http_server`; nothing depends on
`mini_files`; `mini_icecast` owns no socket, parses no request and renders no
markup. The binary search for the seam is in
`docs/adr/0003-extract-http-server-from-mini-icecast.md`.

Every request is parsed on its own short-lived task, capped by a connection limit,
with a generous listen backlog — so a client that connects and says nothing, or
reads very slowly, cannot hold up anybody else.

### Two things worth knowing about the encoder

Both were measured rather than assumed, and both are why `mi_encode.c` exists as
its own module:

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

## Licensing

This project's own code is MIT-licensed (`LICENSE`). The vendored MP3 encoder is
not: **`components/shine/` is LGPL-2.0-only**, kept as unmodified source in its own
component so that you can replace or patch it and relink — which is what
LGPL-2.0 section 6 requires. `LICENSES/LGPL-2.0.txt` and `THIRD_PARTY_NOTICES`
ship with the sources, and this project's own licence is unaffected by the
arrangement.

The GPL-3.0 encoder port sometimes suggested for the ESP32
(`fknrdcls/mp3_shine_esp32`) is deliberately **not** used anywhere: it would impose
GPL on the whole project.

## Known limitations and deliberate omissions

* **No authentication anywhere** (see the warning at the top) and **no TLS**.
* **No Opus, Vorbis, Ogg, AAC** — MP3 only, for reach and because a fixed-point MP3
  encoder is the only one that fits this power budget.
* **No seek, no range requests, no transcoding on download.** Downloads are the
  original bytes.
* **No Wi-Fi provisioning UI**, no mDNS, no OTA updates. Credentials are compiled
  in.
* **One download at a time.** A second concurrent download is declined with `503`
  rather than queued, so a 40 MB FLAC cannot starve the broadcast behind it.
* **The stream is a broadcast**: it cannot be paused or seeked, and every client
  hears the same moment.
* **`shine`'s audio quality is below LAME's.** Real, acknowledged, and unavoidable
  at this power budget.
* **The firmware builds against ESP-IDF v5.4.4 and runs on an ESP32-S3 N16R8
  devkit**; see the board status section below for exactly what has and has not
  been exercised on hardware.

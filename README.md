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

Before the first build, edit the two lines at the top of `main/main.c`:

```c
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"
```

These are compiled into the binary. There is no provisioning mechanism: changing
them means editing the source and reflashing (a deliberate deferral, not an
oversight).

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

1-bit SDMMC, wired to these pins (`main/main.c`):

| Signal | GPIO |
|---|---|
| `SD_CLK` | 39 |
| `SD_CMD` | 38 |
| `SD_D0` | 40 |

Three costs, stated rather than discovered later:

1. **GPIO39 and GPIO40 are the USB-JTAG pins.** Using them for the card disables
   on-chip JTAG on that port. Debugging over the board's separate USB-UART bridge
   still works — that is the port to use for `idf.py monitor`.
2. **GPIO0 is the skip button** and is also a strapping pin. Holding it while
   resetting enters download mode, which is exactly how you flash. During normal
   operation, pressing it skips to the next track.
3. **Hand-soldered card slots are the most likely source of intermittent
   faults.** The firmware enables the internal pull-ups
   (`SDMMC_SLOT_FLAG_INTERNAL_PULLUP`), but real 10 kΩ pull-ups on `CMD` and `D0`
   may be needed if the card behaves erratically. The device is built to survive a
   card that will not mount: see below.

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
diagnosable rather than mysterious:

```
I (radio) chip: esp32s3, 2 core(s), revision 0.2
I (radio) flash: 8 MB; psram: 8192 KB (octal mode, CONFIG_SPIRAM_MODE is not auto-detected)
I (radio) heap at startup: internal 285000 B free, external 8380000 B free
I (icecast) encoder: 1152 samples/frame (26.1 ms), 95896 B of state inside the vendored library, staging buffers in external memory
I (icecast) burst ring: 12098 B (~500 ms of audio) in external memory, sends at most 2 clients
```

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
* **The firmware build itself was written against ESP-IDF 5.4 documentation and
  has not been compiled here** (no ESP-IDF toolchain in this environment). The host
  build and its tests are what have been executed; see the two-tier acceptance
  section for what that does and does not prove.

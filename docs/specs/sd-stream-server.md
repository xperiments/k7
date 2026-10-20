# SD stream server: specification

**Goal.** An ESP32-S3 devkit, flashed with this firmware, joins Wi-Fi with hardcoded
credentials, reads an audio library off a microSD card, and serves it over HTTP: one
continuous live MP3 stream plus a browsable index from which the original files can be
downloaded.

**Relationship to `docs/research/server/s12-mp3-only-plan.md`.** That plan is the research
input. This document is the buildable specification derived from it, and it **supersedes**
the plan in three places where measurement contradicted it — see §11. Everything in the
plan's §1 (why MP3-only is the easy scope), §2 (what already works), §4 (licensing) and §6
(build order) survives intact and is folded in below.

**Status of every claim.** Claims marked **[measured]** were executed on this machine while
writing this document; the code and commands are in §13. Claims marked **[read]** come from
reading a file directly. Claims marked **[inferred]** are reasoning, not evidence, and are
flagged so they can be disproved on hardware.

---

## 1. Scope

### In scope

| # | Requirement |
|---|---|
| R1 | Boot on an ESP32-S3 devkit **with PSRAM**, join Wi-Fi using credentials compiled into the source |
| R2 | Mount the microSD card over 1-bit SDMMC at `/sdcard` |
| R3 | Decode `.mp3`, `.flac`, `.wav` — any sample rate — from the Library tree |
| R4 | Resample everything to one station rate and encode to **MP3** |
| R5 | Broadcast one continuous ICY stream at `GET /stream` on port 8000 |
| R6 | Serve an HTML index of the Library tree at `GET /` |
| R7 | Serve the **original, untranscoded bytes** of a Track at `GET /music/<relative path>` |
| R8 | Degrade rather than abort when the card is absent or unreadable |

### Out of scope, explicitly

Opus, Vorbis, Ogg, WebM, Matroska, AAC — the plan's §1 removes all of them and this
specification reinstates none. Also excluded: TLS, HTTP authentication, transcode-on-demand,
range/seek support, OTA updates, mDNS, and any runtime mechanism for changing Wi-Fi
credentials. The last one is a deliberate deferral, recorded in §11.

### Non-goals worth stating

This is **not** a general-purpose file server. R7 exists so a phone or laptop on the same
LAN can retrieve the source files; it is not an endpoint intended for internet exposure, and
§7 explains the consequences of that stance.

---

## 2. Target hardware and the constraints it imposes

### 2.1 The board

ESP32-S3 devkit with PSRAM (the N8R8 / N16R8 class is assumed; §2.4 explains what changes
if it is absent). The SoC facts below are **[read]** from
`docs/research/07-esp32s3-hardware-verified.md`:

| Fact | Value | Consequence |
|---|---|---|
| No built-in DAC | `SOC_DAC_SUPPORTED` absent from `soc_caps.h` | Audio leaves over I2S/network only; irrelevant here, since the output is HTTP |
| Cores | 2 | Wi-Fi/lwIP on core 0, radio on core 1 — already how `mini_icecast_start()` pins tasks |
| IRAM / DRAM windows | 448 KB / 480 KB | DRAM is the scarce resource; see §2.3 |
| `SOC_SPIRAM_SUPPORTED` | `1` | Silicon can address PSRAM; the *board* must actually have it fitted |

### 2.2 Hard prerequisite: PSRAM mode is not auto-detected

This is a build-breaking trap, not a style note. **[read]** from
`components/esp_psram/esp32s3/Kconfig.spiram:11-20`:

```
choice SPIRAM_MODE
    prompt "Mode (QUAD/OCT) of SPI RAM chip in use"
    default SPIRAM_MODE_QUAD
```

There is **no auto-detect on the S3**. Choose the wrong mode and the failure is memory
corruption at boot, not a clean error. The station format and the 95 896 B encoder state
(§4.1) both depend on PSRAM actually working, so this must be confirmed before any audio
work is judged.

**Requirement P1.** `sdkconfig.defaults` must enable, and the build must confirm at
runtime:

```
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y      # or _QUAD — must match the fitted chip; not auto-detected
CONFIG_SPIRAM_USE_MALLOC=y    # required for heap_caps_malloc(MALLOC_CAP_SPIRAM)
```

**Requirement P2.** Firmware logs at startup: chip model, flash size, PSRAM size and mode,
and free heap in both internal RAM and PSRAM. The log line is the evidence that P1 took
effect. Without `CONFIG_SPIRAM_USE_MALLOC`, `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` fails
and every PSRAM-preferred allocation silently falls back to internal RAM — which
`mini_icecast.c:398-399` already anticipates with its `malloc` fallback, so the failure would
otherwise be invisible.

**Requirement P3.** Every large allocation must be PSRAM-preferred with an internal fallback,
following the idiom already in `mini_icecast.c:398-399`: `heap_caps_malloc(size,
MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`, falling back to `malloc`. This applies to the burst
ring and to the encoder state and its I/O buffers. The startup log in P2 is what reveals which
path each allocation took, so a PSRAM-less board is diagnosable rather than mysterious.

### 2.3 Pin constraints

The existing pin assignment is **[read]** from `main.c:31-33`: `SD_CLK` 39, `SD_CMD` 38,
`SD_D0` 40.

**[read]** from `esp-idf/docs/en/api-reference/peripherals/gpio/esp32s3.inc:253`: only
**GPIO33–37** are consumed when octal PSRAM is fitted, because they become SPIIO4–SPIIO7 and
SPIDQS. **GPIO38/39/40 are clear of octal PSRAM** — no remap is required for P1 and the SD
bus to coexist. Three real costs remain, and the specification must record them rather than
let a future reader rediscover them:

1. **GPIO39/40 are the USB-JTAG pins.** Reconfiguring them disables USB-JTAG on that port.
   The board has a separate USB-UART bridge (visible as `usbserial-834430` alongside the
   native `usbmodem834101`), so debugging survives over UART — but on-chip JTAG debugging
   does not. Accept, and say so in the README.
2. **GPIO0 is already the skip button** (`main.c:138`) and is a strapping pin. Holding it at
   boot enters download mode. That is a feature, not a bug, but the README must document it.
3. **[inferred]** The 1-bit SDMMC wiring on a hand-soldered card slot is the single most
   likely source of intermittent faults. `main.c:89` already sets
   `SDMMC_SLOT_FLAG_INTERNAL_PULLUP` and warns that real 10 kΩ pull-ups on CMD/D0 may be
   needed. R8 exists precisely because this wiring is expected to fail sometimes.

### 2.4 If the board turns out to have no PSRAM

Not this board, per the decision behind this specification — but the failure mode is worth
one paragraph, because P1/P2 make it survivable. Without PSRAM the budget becomes
approximately:

| Consumer | Bytes |
|---|---|
| Encoder state (`shine_global_config`) | 95 896 **[measured]** |
| Encoder I/O: 1152 stereo `int16_t` in, bitstream out | 4 608 + ~4 096 |
| Burst ring at `burst_ms = 500`, 44.1 kHz | ~88 000 |
| Radio task stack | 24 576 |
| Decoder working set + resampler tables | board-dependent |
| Wi-Fi + lwIP + bootloader | consumes much of the 480 KB DRAM window |

The ring and encoder state are the two candidates for internal RAM, and together they exceed
what is likely to remain. The specification therefore requires the ring and encoder state to
allocate PSRAM-first with an internal fallback (Requirement P3), and the startup log in
P2 is what reveals which path was taken. On a PSRAM-less board, reduce `burst_ms` before
touching anything else.

---

## 3. Firmware structure

The existing `resources/` is a single component that both produces audio and speaks HTTP.
The index and download surface (R6, R7) is a second HTTP consumer, and §5 defect 1 shows the
current accept loop already blocks on one slow client. Extraction is therefore a
prerequisite, not a refactor for its own sake.

```
main/                     app_main: NVS, Wi-Fi, SD mount, config, hand-off
components/
  http_server/            listen socket, accept, per-connection parse+route  (new)
  mini_icecast/           PCM in -> MP3 -> ICY broadcast, client fan-out     (from resources/)
    mi_decode.c/.h
    mi_resample.c/.h
    mini_icecast.c/.h
  library/                boot-built tree, arena, JSON, Playlist and lookup  (new)
  library_http/           GET /api/library[.bin] from the prebuilt views    (new)
  mini_files/             GET / index, GET /music/<relative path> download  (new)
  shine/                  vendored, unmodified, separate licence             (new)
```

**Requirement S1.** `http_server` owns the listening socket. For each accepted fd it starts a
short-lived task that reads and parses the request header and routes it:

- `GET /stream` → hand the fd to `mini_icecast` for registration (under the broadcast lock,
  burst included, exactly as `handle_new()` does today at `mini_icecast.c:237-246`), then the
  routing task exits.
- `GET /` and `GET /music/<relative path>` → served entirely inside the routing task by
  `mini_files`, reading the Library generation.
- Anything else → `404`.

**Requirement S2.** A connection limit caps concurrently-parsing tasks, and `listen()`'s
backlog is raised from today's `2` (`mini_icecast.c:267`) to at least 8. The acceptance test
in §9 T4 is what proves this worked.

**Requirement S3.** `mini_icecast` keeps its broadcast fan-out, burst ring and pacing
unchanged. Its public surface stays `mini_icecast_start` / `_skip` / `_now_playing` plus a new
fd-registration entry point. The PCM callback contract in `mini_icecast.h:10-14` is preserved
verbatim — see §4.4 for the one clarification it needs.

**Requirement S4.** `mini_files` depends on `http_server` and the broadcast state, never the
reverse. `mini_icecast` must not learn about files or HTML.

---

## 4. The audio path

### 4.1 Encoder: `shine`, vendored as its own component

`toots/shine` is the only fixed-point MP3 encoder that fits this power budget. Measured
properties, all **[measured]** on this machine by §13.1 and §13.2:

| Property | Measured value |
|---|---|
| `sizeof(shine_global_config)` | **95 896 B** — matches the plan's ~97.6 KB claim |
| `shine_samples_per_pass()` | **1152** samples = 26.12 ms @ 44.1 kHz |
| First pass output | 416 bytes, sync `ff fb` — **no startup buffering** |
| 100 passes | 41 796 bytes / 2.612 s = **128.0 kbps**, ~418 B/frame |
| `shine_check_config(44100, 128)` | `3` = `MPEG_I` |
| `shine_check_config(8000, 128)` | `-1` — invalid, as expected |
| `SHINE_MAX_SAMPLES` | 1152 |

**Requirement E1 — the config check must accept zero.** **[measured]** from
`src/lib/layer3.h:10`:

```c
enum mpeg_versions { MPEG_I = 3, MPEG_II = 2, MPEG_25 = 0 };
```

`shine_check_config()` returns the *MPEG version*, not a boolean. `MPEG_25` is **0**, so the
MPEG-2.5 rates (8 / 11.025 / 12 kHz) legitimately return 0. The validity test must be
`>= 0`, never a truthiness test and never `> 0`. A truthiness test silently rejects every
MPEG-2.5 configuration as invalid — a bug that no 44.1 kHz test would ever catch.

**Requirement E2 — one frame per call, and the returned pointer is volatile.**
`shine_encode_buffer_interleaved()` requires exactly `shine_samples_per_pass()` samples per
channel and writes exactly one frame per call **[measured]**. The returned buffer is
library-owned and valid only until the next call **[read, `layer3.h`]**. Encode and
immediately hand to `broadcast()`, or copy. This is the one easy use-after-free in the design.

**Requirement E3 — `shine_flush()` is not a flush.** **[measured]**, and this is the plan's
most consequential error. Its implementation is **[read, `src/lib/layer3.c:198-203`]**:

```c
unsigned char *shine_flush(shine_global_config *config, int *written) {
  *written = config->bs.data_position;
  config->bs.data_position = 0;
  return config->bs.data;
}
```

`data_position` is 0 after every encode, so this emits nothing. Measured: three consecutive
calls after 100 full frames each returned a non-NULL pointer with `written = 0`, and a fresh
encoder flushed immediately also wrote 0 bytes. **It cannot drain the bit reservoir and it
cannot emit leftover samples.** Do not rely on it to flush a partial buffer.

### 4.2 The insertion point, and the 1024/1152 mismatch

The encoder must sit between the resampler and `broadcast()`. The plan is right that the only
insertion point is `chunk_append()` → `emit_chunk()` (`mini_icecast.c:305-326`), and right
that `metaint` is already counted over encoded bytes because `client_push()` splits whatever
buffer it is handed.

The complication is granularity. The existing chunker emits **1024** frames
(`MINI_ICECAST_CHUNK_FRAMES`, `mini_icecast.h:16`) and shine consumes **1152**. They do not
divide, so encoded output cannot be frame-aligned to the chunker.

**Requirement E4.** Keep the 1024-frame chunker and add a 1152-frame encoder accumulator
between `emit_chunk()` and `broadcast()`. The chunker, `pace()`, the PCM callback and their
contract stay untouched; one ~4.6 KB staging buffer is added. `pace(1152)` is called once per
**encoded frame**, not once per 1024-frame chunk, so the starvation rebase in
`mini_icecast.c:291-302` continues to measure real transmitted audio.

**Requirement E5 — flush at end of track.** When a file ends, the accumulator holds
0–1151 frames of real audio that would otherwise be dropped (~26 ms per track). Zero-pad the
accumulator to 1152, run one final encode, broadcast the result, then call `shine_flush()` as
a harmless no-op (E3). The zero-padding injects up to 26.12 ms of silence per track boundary;
this is inaudible and is the accepted cost of not dropping audio. Flushing per track also
keeps the encoder's bit reservoir from bleeding across a track boundary.

> **Note for the implementer:** E5 is the one place where the plan's *intent* (a flush that
> forces the accumulator to drain) survives but its *mechanism* (calling `shine_flush`) does
> not exist. The plan's §3 API list omits `shine_flush` entirely; a spec that trusted that
> list would have shipped the silent tail-drop.

### 4.3 Station format

The station format is **configurable and validated at boot, not switchable at runtime**.
`shine_initialise()` copies its config **[read, `layer3.h`]**, so changing format mid-broadcast
means freeing and re-allocating 95 896 B while clients are attached. That is a reboot, not a
setting.

**Requirement F1.** `out_hz` must be rejected at startup unless it is one of
{44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000} — the rates `shine` accepts
**[measured, `samplerates[9]`]**. The existing default, 44100, is valid; so is the
`mini_icecast.h:23` example of 48000. Anything else, including a plausible-looking 44100-adjacent
rate, must fail loudly rather than resample into an encoder that cannot represent it.

**Requirement F2.** The bitrate must be checked against the rate with
`shine_check_config(out_hz, bitrate)` using E1's `>= 0` rule, and the resulting MPEG version
logged.

**Requirement F3.** Valid combinations, per the plan's §3 table **[read]**, worst case is
MPEG-I at 32–320 kbps. Recommended default: **44100 Hz / 128 kbps CBR / joint stereo** —
`shine_check_config(44100, 128) == 3` **[measured]**, the universal compatibility point.
48 kHz / 192 kbps is available when fidelity outweighs reach. `shine` is CBR-only: there are
no `vbr` references anywhere in `src/`.

**Requirement F4.** Measure and log encoder cost on hardware. Time
`shine_encode_buffer_interleaved()` with `esp_timer_get_time()` averaged over at least 1000
frames, and log free internal + PSRAM heap at startup and every 60 s. The plan's §7 leaves
encoder CPU unverified (its figure — 53.4 % of one core — comes from an unverified port
README). **The acceptance threshold is < 35 % of one 240 MHz core with no starvation events
over a 1-hour soak.** If it exceeds that, the fallback is **32 kHz / 96 kbps** — fewer frames
per second, less CPU. 48 kHz / 192 kbps is the *wrong* fallback under CPU pressure: identical
frame count and a higher bitrate, so it costs more, not less.

### 4.4 One contract clarification

`mini_icecast.h:10-14` and `main.c:101-102` both claim the PCM handed to `pcm_cb()` is "the
very same PCM that goes on the wire". After §4.2 that is no longer literally true: the
callback sees a 1024-frame chunk, while the wire sees 1152-frame encoded frames about 26 ms
later.

**Requirement C1.** Update the comment in `mini_icecast.h` and `main.c` to say the callback
receives the same PCM *content*, as unencoded chunks, up to one encoder frame (~26 ms) ahead
of the wire. The `on_pcm()` visualizer in `main.c:103-113` needs no code change; only the
promise needs to stop overstating itself.

> **Superseded (ticket 16).** The callback was retired after the cassette screen became
> the display: `mini_icecast_status_t` now carries the per-channel RMS level, computed
> where the PCM already passes through, and nothing consumes raw PCM. C1 was satisfied
> while the callback existed; there is no callback left to describe. A future consumer of
> raw PCM - an FFT, a second sink - gets an interface designed for it then, rather than an
> unused hook kept for a hypothetical one.

---

## 5. Defects to fix

These are independent of the encoder. Four come from the plan's §5 and are **re-verified here
against the code**; two are new, introduced by R6/R7.

| # | Defect | Evidence | Requirement |
|---|---|---|---|
| D1 | `accept_task` calls `handle_new(fd)` inline (`mini_icecast.c:274`), and `handle_new` does a blocking header read with a **2 s** `SO_RCVTIMEO` (`:195-197`). One silent client stalls every new connection. | **[read]** | Fixed structurally by S1/S2. This is a prerequisite for R6/R7, not a cleanup: a slow download would otherwise stall all connects. |
| D2 | `"icy-name: %s\r\n"` has a space after the colon (`mini_icecast.c:225`); real Icecast emits none. | **[read]** | Emit `icy-name:` with no space. Low stakes against permissive parsers, but this server is the natural loopback fixture for a client-side parser, so canonical headers matter. |
| D3 | `max_clients` is documented `1..4` (`mini_icecast.h:22`) but silently coerced to 2 (`mini_icecast.c:395`) while `clients[]` is sized `MAX_CLIENTS 4` (`:32`). | **[read]** | Honour the value across the full 1..4 range, and reject out-of-range with a diagnostic log instead of silent coercion. |
| D4 | Resampler stack overflow at extreme downsampling: `double row[256]` at `mi_resample.c:47` is indexed by `T`; `T = 2 * ceil(ZERO_X / fc)` with `fc = 0.95 * out/in` for downsampling. `48000 → 3000` gives `T ≈ 540`, `44100 → 1000` gives `T ≈ 1486`. | **[read, arithmetic re-derived]** | Not reachable at a 44.1/48 kHz station, but `out_hz` is user-configurable, so clamp or assert `T <= 256` at construction and fail `mi_rs_new()` with a diagnostic rather than overflowing the radio task's stack. |
| D5 | **New.** Path handling for R7. Nothing sanitizes a filename from a request today because nothing accepted one. | **[read]** | Decode one path segment at a time; reject an empty segment, `.`/`..`, a leading `.`, a NUL, a `/` or `\` inside a segment, and a non-audio node. Resolve only through the Library tree, then open `root + relative path`; never join a request path directly. |
| D6 | **New.** SDMMC contention between the decoder and downloads. | **[read]** | Serialize downloads behind a mutex; exactly **one** download at a time (R7), returning `503` to a second requester rather than queueing it. A 40 MB FLAC download is the worst case for card arbitration, and the radio must never starve. Also raise `max_files` above `main.c:79`'s `4` only if measurement demands it — with one download plus one decoder, 4 is ample. |

---

## 6. HTTP surface

### 6.1 `GET /stream` — the live stream

The stream consumes the Library Playlist: depth-first, name-sorted, no per-pass
`readdir()`. Unchanged from today except for the output format:

- `Content-Type: audio/mpeg` (was `audio/wav`, `mini_icecast.c:224`).
- The WAV header emission is **deleted** — both `wav_header()` (`mini_icecast.c:181-191`) and
  its call site (`:236`, `:243`). MP3 is a self-delimiting frame stream: no container, no
  length sentinel. This is the plan's §3 edits 1 and 2, and it is the whole reason the
  endless-WAV problems in the plan's §1 table disappear.
- `icy-metaint` is advertised when the client opts in with `Icy-MetaData: 1`, and
  `StreamTitle='...';` blocks are emitted as today. ICY metadata is meaningful for MP3 — it is
  meaningless and destructive in a PCM stream, which is exactly one of the problems this
  change removes.
- Default `metaint` stays **16 000**, which **[measured]** is exactly 1.000 s of audio at
  128 kbps. That is a fine metadata cadence and it preserves `mini_icecast.h:24`'s existing
  default, so nothing needs changing.

### 6.2 `GET /` — the index

**Requirement I1.** One HTML page listing every Track in the Library tree in Playlist order,
each row showing the relative path, its size, and a link to
`/music/<url-encoded-relative-path>`. The tree may be nested; there is no pagination.

**Requirement I2.** The page also shows the current title via `mini_icecast_now_playing()`
(`mini_icecast.c:415`), and a link to `/stream`.

**Requirement I3.** When the card is not mounted (R8), the index returns HTTP 200 with a page
stating that no card is present and quoting the mount error — not a 500, and not a connection
refusal. The index is the diagnostic surface.

Filenames are url-encoded in links and url-decoded on receipt. FATFS LFN is already configured
for 128 chars (`sdkconfig.defaults`, `CONFIG_FATFS_MAX_LFN=128`), so a 128-byte path buffer is
sufficient, but the decode must not overflow it.

### 6.3 `GET /music/<relative path>` — download

**Requirement R7a.** Serve the file's **original bytes**, untranscoded, with a
`Content-Length` from `stat()` and a `Content-Type` chosen by extension (`.mp3` →
`audio/mpeg`, `.flac` → `audio/flac`, `.wav` → `audio/wav`). No transcoding: see ADR 0002 for
why raw bytes beat normalising everything to MP3.

**Requirement R7b.** Apply D5's path guard before any filesystem call.

**Requirement R7c.** Hold D6's mutex for the duration; `503` a second concurrent download.

**Requirement R7d.** Read in bounded chunks (8–16 KB) and honour a send timeout so a client
that stops reading cannot pin the card forever. Use the same `CLIENT_STALL_MS` discipline the
stream path already has (`mini_icecast.c:36`, `:198`).

---

## 7. Access model and its consequences

`GET /` and `GET /music/<relative path>` are **open to anyone who can reach port 8000**. There is no
authentication, by decision and not by omission.

The reasoning: the card holds audio you own, the device is intended for your own LAN, and any
auth scheme needs a credential store that contradicts R1's "hardcoded for now" deferral. The
cost is stated rather than hidden:

**Requirement A1.** The README must state, in the deployment section and not in a footnote,
that this firmware serves both a live stream and an unauthenticated file index, that the Wi-Fi
credentials are compiled into the binary, and that it must not be placed on an untrusted
network or port-forwarded.

**Requirement A2.** `/stream` stays open even if auth is added later. Hardware players, car
head units and VLC cannot supply Basic credentials in a stream URL, and closing it would
defeat R5's purpose.

---

## 8. Licensing obligation

`shine` is **LGPL-2.0-only**: its `COPYING` is "GNU *Library* General Public License, Version
2, June 1991" and the string "or later" occurs zero times **[read, plan §4]**. LGPL-2.0 §6
permits distributing a work that uses the library "under terms of your choice" provided the
library's use is noticed, its licence is supplied, and the recipient can relink against a
modified library.

**Requirement L1.** Keep `shine` as its own component directory, vendored as **unmodified
source**. Never copy its files into `mini_icecast/` or `main/`, and never pre-build it into a
blob. Shipping as source in the consumer's build tree is what makes §6a's relink path exist.

**Requirement L2.** Ship `LICENSES/LGPL-2.0.txt` and a `THIRD_PARTY_NOTICES` entry naming
`shine`.

**Requirement L3.** **Do not use `fknrdcls/mp3_shine_esp32`** — it is GPL-3.0 and would
impose GPL on the entire project. If its Xtensa optimisations are ever wanted, they must come
from a differently-licensed source or be written fresh.

**Requirement L4.** The project's own components' licence is unaffected; LGPL-2.0 §6 is
precisely the clause that permits this arrangement.

---

## 9. Acceptance tests

Every test in this table must pass **twice: first on the macOS host, then on the ESP32-S3
board** (see §10). The host pass is not a rehearsal — it is where findings are meant to be
caught, because it is cheap, scriptable and diffable. The board pass is what confirms the
host result transfers.

| # | Phase | Method | Runs on | Pass condition |
|---|---|---|---|---|
| T1 | Vendor `shine` | Host unit test: a `main()` built with the host compiler, or a Unity test in `components/portable/shine/test/`, asserting `shine_check_config(44100,128)==3`, `shine_check_config(8000,128)==-1`, `shine_samples_per_pass()==1152`, `sizeof(shine_global_config)==95896`, and 100 encoded frames == 41 796 bytes | **host only** | Exact values match §4.1 — the probes in §13.1/§13.2 are the reference implementation |
| T2 | Encoder harness | Host unit test: 2620 chunks of 1024 frames (= 2 682 880 frames) through the accumulator; assert every `broadcast()` call receives whole MP3 frames and total bytes == expected | **both** | No partial frames, no dropped frames at the 1024→1152 boundary |
| T2b | End-of-track flush (E5) | Host unit test: feed a file with a deliberately non-multiple-of-1152 frame count, then flush; assert the tail is neither dropped nor double-encoded | **both** | Output frames == `ceil(frames / 1152)`; residual padded with silence, not lost |
| T2c | Resampler guard (D4) | Host unit test: `mi_rs_new(44100, 1000)` and `mi_rs_new(48000, 3000)` | **host only** | Return `NULL` with a diagnostic; no out-of-bounds write. Board reruns it as T2c too, to prove the padded frame survives on-device |
| T3 | End-to-end wire format | `curl -s http://<addr>:8000/stream \| ffprobe -` | **both** | `ffprobe` reports **MPEG audio, 44100 Hz, 128 kb/s, joint stereo**, not WAV. On the host this runs against the host build of the real server (§10.5) |
| T4 | Accept loop (D1) | Open a TCP connection to the port and send nothing; then immediately connect a real client | **both** | Second client receives headers within 2 s; no 2 s stall |
| T5 | ICY metadata | `curl -s -H 'Icy-MetaData: 1' http://<addr>:8000/stream \| head -c 40000` and inspect | **both** | `icy-metaint: 16000` present; a `StreamTitle='...';` block appears at byte offset 16 000 of the body |
| T6 | Index | `curl -s http://<addr>:8000/` | **both** | Lists exactly the `.mp3/.flac/.wav` Tracks in the Library tree, in Playlist order, with sizes matching `ls -l` (host) / the FATFS listing (board) |
| T7 | Download | `curl -s -o out.flac http://<addr>:8000/music/<relative-path>.flac && cmp out.flac <path>` | **both** | Byte-identical, at any depth |
| T8 | Path guard (D5) | `curl -s -o /dev/null -w '%{http_code}' 'http://<addr>:8000/music/..%2f..%2fetc%2fpasswd'` and variants | **both** | `400` or `404`; never file contents |
| T9 | Download concurrency (D6) | Start two downloads of a large FLAC simultaneously | **both** | One streams, the other gets `503`; `/stream` audio does not glitch |
| T9b | Decoder/download contention | Download a large FLAC repeatedly while `/stream` is being probed | **board only** | Zero starvation rebases logged; `ffprobe` never reports a discontinuity |
| T10 | Card absent (R8) | Boot with the card removed | **board only** | Wi-Fi associates, `GET /` returns a diagnostic page, log shows the mount error; card inserted later is picked up by the background retry |
| T11 | CPU and memory (F4) | 1-hour soak with one client on `/stream` | **board only** | Encoder < 35 % of one core; free heap stable over the hour; zero starvation rebases logged. The host analogue is T11h below |
| T11h | Encoder cost, host baseline | Time the encode loop with `clock_gettime(CLOCK_MONOTONIC)` over ≥ 1 M frames on the host | **host only** | Recorded as a baseline for scaling, **not** as a pass/fail. Host MHz tells you nothing about S3 MHz (see §10.7) |
| T12 | Format validation (F1/F2) | Start the server with `out_hz = 44101`, and with `out_hz = 8000, bitrate = 64` | **both** | First fails loudly; second starts and logs MPEG-2.5 (`shine_check_config` returned 0, accepted per E1) |
| T13 | Case sensitivity (bonus) | Request `/music/<NAME>.MP3` in the wrong case | **board only** | Documented behaviour, whichever way FATFS resolves it. **Not** a host test — macOS APFS/HFS+ is case-insensitive by default, so the host would give a falsely reassuring result. This is the one place the host is *weaker* than the board, which is why it is called out |

---

## 10. Host testing on macOS

**Every test in §9 is executed on this macOS host first, then on the board.** This section says
what that requires, because "run it on the host" is not free: the server is written against
ESP-IDF and lwIP, and three of its dependencies are missing from the workspace.

### 10.1 What the host is genuinely enough to prove

The reason this works at all is that the two determinism-critical layers are pure fixed-point
arithmetic with no platform dependence:

- **`shine`** is fixed-point throughout. Its output is bit-identical on ARM64 macOS and Xtensa
  **[measured for the host half: §13.1/§13.2 reproduce exactly]**.
- **`mi_decode.c`** includes only `dr_mp3.h`, `dr_flac.h`, `dr_wav.h` and libc — **no ESP-IDF
  headers at all** **[read]**. It is already a host-compilable file.
- **`mi_resample.c`** is portable C.

So the *encoded byte stream* can be validated on the host with full confidence, and the board
pass is then a confirmation rather than a discovery.

### 10.2 What the host cannot prove — state this plainly

Four things are board-only, and no amount of host testing substitutes for them:

1. **CPU headroom.** Encoder cost at 240 MHz Xtensa with Wi-Fi active. T11.
2. **Memory fit.** Internal DRAM and PSRAM pressure with lwIP, Wi-Fi and the decoder live.
3. **microSD contention and timing.** T9b. A macOS file read has no bearing on the SDMMC bus.
4. **FATFS behaviour.** Case sensitivity, LFN limits, and the `._foo.mp3` AppleDouble filter at
   `mini_icecast.c:378` all behave differently on FATFS than on APFS.

The host is strictly *weaker* than the board in exactly these four respects, and strictly
*stronger* in that it is scriptable, repeatable, and diffable.

### 10.3 Prerequisites that do not currently exist

The host build cannot be attempted today. Three blockers, all **[read]**:

| Blocker | Detail |
|---|---|
| Missing vendored decoders | `dr_mp3.h`, `dr_flac.h`, `dr_wav.h` are **nowhere in the workspace**. `mi_decode.c` includes them; nothing supplies them. |
| Missing include directory | `resources/CMakeLists.txt:2` registers `INCLUDE_DIRS "include"`, and **`include/` does not exist**. |
| Missing third_party directory | The same file registers `PRIV_INCLUDE_DIRS "third_party"`, which **does not exist** and presumably held the `dr_*` headers. |
| Stale host-test instructions | `resources/host_convert.c:1` documents `gcc -O2 -I../include -I../third_party ...`, which cannot work against directories that are absent. This file is the right *shape* for a host test and is currently unbuildable. |

**Requirement H1.** Vendoring the three `dr_*` single-header libraries (public domain / MIT-0,
per `docs/research/server/s4-server-synthesis.md`) is a prerequisite for both firmwares, not
only for host tests. Record it as such.

### 10.4 The shim, and its one architectural rule

The ESP-IDF surface actually used by the broadcast path is small — **[read]** from
`mini_icecast.c`:

```
esp_heap_caps.h  esp_log.h  esp_timer.h  freertos/{FreeRTOS,semphr,task}.h  lwip/sockets.h
heap_caps_malloc  MALLOC_CAP_SPIRAM  MALLOC_CAP_8BIT
esp_timer_get_time
xSemaphoreCreateMutex  xSemaphoreTake  xSemaphoreGive
xTaskCreatePinnedToCore  vTaskDelay  portMAX_DELAY
ESP_LOGE  ESP_LOGI  ESP_LOGW
```

Eleven functions and five macros. A `host/` directory provides them:

| IDF surface | Host stand-in |
|---|---|
| `lwip/sockets.h` | macOS's own `<sys/socket.h>`, `<netinet/in.h>`, `<netinet/tcp.h>` |
| `xSemaphore*` | `pthread_mutex_t` |
| `xTaskCreatePinnedToCore` | `pthread_create`, core pinning ignored |
| `esp_timer_get_time` | `clock_gettime(CLOCK_MONOTONIC)` |
| `heap_caps_malloc`, `MALLOC_CAP_*` | `malloc`, caps ignored |
| `ESP_LOG*` | `fprintf(stderr, ...)` |
| `task.h` delays, `portMAX_DELAY` | `nanosleep`, `INT_MAX` |

**Requirement H2.** This shim lives in `host/` and is referenced only by the host build target.
**No `#ifdef CONFIG_IDF_TARGET_ESP32S3` may appear in any production component.** If the host
build needs a behaviour change, it gets it through an injected portability header, not through
conditional compilation sprinkled into working firmware. This is the rule that keeps "the
tests pass on the host" from becoming "the firmware is nothing like what was tested".

**Requirement H3.** The host build must compile **the same component source files** as the
firmware — the same `mini_icecast.c`, `mi_decode.c`, `mi_resample.c`, `mini_files.c`,
`http_server.c`. It may add files; it may not fork them. A copied `mini_icecast_host.c` would
make the whole exercise worthless.

### 10.5 The tests worth doing first, because they need no board

**T3 is the headline host test and must be done before any hardware exists.** The host build of
the real server compiles, binds to a high port (not 8000, so it cannot collide with anything),
serves a fixture directory, and is probed exactly as the board would be:

```bash
# Build the host server (production sources + host/ shim)
make -C host            # or: cmake -B build-host && cmake --build build-host

# Run it against a fixture corpus on a high port
./host/icecast_host --music-dir ./host/fixtures --port 18000 &

# T3: prove the wire format is MPEG audio, not WAV
curl -s --max-time 5 http://127.0.0.1:18000/stream | ffprobe -

# T5: prove ICY metadata framing at exactly 16000 encoded bytes
curl -s --max-time 5 -H 'Icy-MetaData: 1' http://127.0.0.1:18000/stream \
  | head -c 40000 | xxd | grep -A2 -i streamtitle

# T4: prove the accept-loop fix (D1) — silent client first, real client second
exec 3<>/dev/tcp/127.0.0.1/18000        # connect, send nothing
curl -s --max-time 3 -o /dev/null -w '%{http_code}\n' http://127.0.0.1:18000/

# T6/T7: index and byte-exact download
curl -s http://127.0.0.1:18000/ | grep -o 'href="[^"]*"'
curl -s -o /tmp/out.flac http://127.0.0.1:18000/music/tone.flac
cmp /tmp/out.flac ./host/fixtures/tone.flac && echo BYTE-IDENTICAL

# T8: path guard
curl -s -o /dev/null -w '%{http_code}\n' 'http://127.0.0.1:18000/music/..%2f..%2fetc%2fpasswd'
```

**Requirement H4.** Every command above must succeed on the host with no board attached and no
privileges. This is the concrete meaning of "test everything on OSX first".

### 10.6 Determinism checks the host makes possible

Because the encoder and decoders are fixed-point, encoding a fixture on the host and on the
board must produce **byte-identical output**. That is a far stronger assertion than "ffprobe
says it is MP3", and it is only available because both runs are on the same architecture family
for the integer paths.

A single caveat, recorded so nobody over-claims: **`mi_resample.c` uses `double` and `float`**
for its kernel. Its output could differ in the last bits between ARM64 and Xtensa. So:

**Requirement H5.** Assert byte-exactness on the **fixed-point** paths (`shine` output from
identical PCM input; `dr_*` decoded PCM). For the resampler, assert sample-rate correctness and
SNR bounds, **not** bit-exactness — unless the resampler is verified bit-exact, in which case
promote the assertion and say so.

A workable end-to-end digest check:

```bash
curl -s --max-time 5 http://127.0.0.1:18000/stream \
  | ffmpeg -i - -map 0:a -f md5 -        # compare host against board
```

### 10.7 Fixtures

The fixture corpus must exist on both machines in the same form, and must be generated rather
than hand-assembled so it is reproducible. `ffmpeg` is available on this host (version 9.0.2):

**Requirement H6.** Provide `host/fixtures/Makefile` (or a script) generating, for each of
`.mp3`, `.flac`, `.wav`: the same 440 Hz test tone at **44100, 48000 and 32000 Hz**, plus one
non-44100 input so the resampler is actually exercised, and one file whose frame count is
**deliberately not a multiple of 1152** to drive T2b's flush path. Regenerable with one command,
committed so both platforms share exactly the same bytes.

### 10.8 Platform behaviours that must be abstracted, not assumed

Two host/board differences will otherwise produce tests that pass on one and fail on the other
for uninteresting reasons:

| Difference | Handling |
|---|---|
| Ports below 1024 need root on macOS | Host tests bind a high port; the port must be a parameter, not a compiled-in default |
| macOS `SO_RCVTIMEO`/`SO_SNDTIMEO` take `struct timeval`, and lwIP follows BSD | Already compatible; assert it in T4 rather than assuming |
| Path case sensitivity | T13 is board-only (see §9) |
| `music_dir` path | Must be a runtime argument in the host build — the host uses a fixture directory, the board uses `/sdcard/music`; it builds the Library generation, which is what the broadcast consumes |

### 10.9 The two-tier acceptance statement

**Requirement H7.** A phase of this project is complete only when:

1. its §9 tests pass on the macOS host, and
2. the same tests pass on the ESP32-S3 board,

with any host/board divergence either explained by §10.2's four board-only properties, or
treated as a defect. A test that passes on the host and has never been run on the board is
**not** evidence about the board, and must not be reported as if it were.

---

## 11. Where this specification contradicts the plan

Three corrections, all from measurement rather than opinion:

1. **`shine_flush()` is not a flush.** The plan's §3 API list omits it entirely, and its
   implied remedy for a partial buffer would emit nothing. Measured: `written = 0` on every
   call. See E3 and E5 — the end-of-track flush must be implemented as zero-padding plus one
   final encode.
2. **`shine_check_config()` returning 0 means success.** The plan says to "log the result" but
   the return value is an `enum mpeg_versions` where `MPEG_25 = 0` **[measured]**. A
   truthiness test rejects every MPEG-2.5 configuration. See E1.
3. **The plan's §5 defect list is missing the two defects this specification's own R6/R7
   introduce**: unsanitized filenames (D5) and decoder/download SDMMC contention (D6). Both
   are consequences of the index decision, which the plan does not cover at all.

Everything else in the plan's §1, §2, §3 (edits 1 and 2), §4 and §6 is confirmed and carried
forward. The plan's §5 D1–D4 are confirmed against the code, including the arithmetic behind
D4 (`T ≈ 540` at 48000→3000, `T ≈ 1486` at 44100→1000, both exceeding `row[256]`).

---

## 12. Open questions

These are genuinely unresolved and should not be treated as settled by this document.

1. **Encoder CPU on real silicon.** §4.3's 35 % threshold is a *proposal*, not a measurement.
   The plan's figures (53.4 % of one core) come from an unverified port README. T11 is what
   settles it, and the fallback table in F4 is contingent on that number.
2. **Total memory with Wi-Fi, lwIP, decoder and encoder all live.** No board was available
   while writing this, so the budget in §2.4 is arithmetic, not observation. P2's startup log
   is what turns it into evidence.
3. **Whether PSRAM mode is QUAD or OCT on the actual module.** §2.2 makes this a
   build-breaking prerequisite. Read the module marking (N8R8 → typically OCT) or try both.
4. **`shine` audio quality versus LAME.** Real, acknowledged, and unavoidable at this power
   budget — `shine` is the only fixed-point MP3 encoder that fits.
5. **Wi-Fi credential provisioning.** Deferred by decision. The current edit-the-source-and-reflash
   path means a credentials change is a full rebuild; the natural follow-up is NVS + a captive
   portal or a build-time `Kconfig` string. Not in this scope.
6. **Whether real target clients (car head unit, hardware internet radio) accept this stream.**
   MP3 removes the format question that killed endless-WAV, but an hour of smoke testing
   against the actual clients is still worth spending.

---

## 13. Reproduction

### 13.1 Frame granularity and bitrate accuracy (T1's reference)

```bash
git clone --depth 1 https://github.com/toots/shine && cd shine
gcc -O2 -Isrc/lib -o probe probe.c src/lib/*.c -lm   # probe.c from this session
./probe
```

Measured output:

```
shine_check_config(44100,128) = 3
shine_check_config(48000,192) = 3
shine_check_config(8000,128)  = -1 (expect -1)
shine_samples_per_pass = 1152  (= 26.12 ms @44100)
  pass 0 -> 416 bytes, sync ff fb
  pass 1 -> 420 bytes, sync 7f c2
  pass 2 -> 416 bytes, sync ff fb
first pass emitted 416 bytes (no startup buffering if >0)
100 frames = 2.612 s of audio -> 41796 bytes = 128.0 kbps, 418.0 B/frame
metaint 16000 encoded bytes = 1.000 s of audio
```

Note pass 1's `7f c2`: the bit reservoir means encoded output is not reliably frame-aligned
even on alternating passes, which is the empirical basis for E4.

### 13.2 Flush behaviour (E3's reference)

```bash
gcc -O2 -Isrc/lib -o probe2 probe2.c src/lib/*.c -lm
./probe2
```

Measured output:

```
A: 100 passes -> emits=100 nulls=0 bytes=41796
A: shine_flush #0 -> ptr=non-NULL written=0
A: shine_flush #1 -> ptr=non-NULL written=0
A: shine_flush #2 -> ptr=non-NULL written=0
A: after flush total=41796 (+0 in 0 flush calls)
B: flush with zero input -> ptr=non-NULL written=0
C: one pass -> ptr=non-NULL written=416
C: then flush -> ptr=non-NULL written=0
D: sizeof(shine_global_config) = 95896, SHINE_MAX_SAMPLES=1152
```

Both probes are throwaway files under `/tmp`; T1 should reimplement their assertions as a
committed host test rather than depending on them.

---

## References

| Document | What it supplies |
|---|---|
| `docs/research/server/s12-mp3-only-plan.md` | The input plan; §1 scope, §4 licensing, §6 build order carried forward |
| `docs/research/server/s0-server-code-review.md` | The original review behind defects D1–D4 |
| `docs/research/server/s11-licensing-verified-and-packaging.md` | `idf_component.yml` shape: `version`, `description`, `license`, `targets: [esp32s3]`, `dependencies.idf: ">=5.4"` |
| `docs/research/07-esp32s3-hardware-verified.md` | SoC memory map, no DAC, `SOC_SPIRAM_SUPPORTED` |
| `docs/adr/0001-serve-original-bytes-not-transcoded.md` | Why downloads are not MP3 (§6.3) |
| `docs/adr/0002-vendor-shine-as-separate-lgpl-component.md` | Why `shine` is a separate component (§8) |
| `docs/adr/0003-extract-http-server-from-mini-icecast.md` | Why the HTTP layer is extracted (§3) |
| `docs/adr/0012-boot-built-library-arena.md` | The boot-built Library tree that replaced the flat Music-directory model in §6.2/§6.3 |
| `docs/specs/CONTEXT.md` | Glossary for the terms this specification depends on |
| `CONTEXT-MAP.md` | How this context relates to the radio-player context |

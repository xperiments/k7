# S7 — Concrete encoder + muxer components to vendor (MP3 and Opus-in-Ogg)

**Question:** which *obtainable* implementations should the ESP32-S3 radio server vendor for
MP3 encoding and Opus-in-Ogg output, and what is the integration cost?

**Method / date:** read the actual source of every candidate (cloned repos + registry
component zips), queried the ESP Component Registry API, inspected the prebuilt
`libesp_muxer.a` symbol/string tables for the Ogg muxer, and ran host measurements of `shine`
on 2026-10-16. Cross-references [s2-encoder-feasibility.md](s2-encoder-feasibility.md) and does
not re-derive its CPU/bandwidth/licence findings. `s5-ogg-container-and-muxing.md` did **not**
exist at the time of writing.

**Headline:** the picture is much better than s2 assumed.

1. **MP3 — `toots/shine`, LGPL-2, vendored as source.** No registry component exists. Two
   community ESP32 ports exist; the viable one is the 2023 `anton-malakhov` revival of
   `fknrdcls`'s 2019 port, but `fknrdcls` ships a **GPL-3.0** LICENSE — a licence trap s2
   flagged and I confirmed. Vendor upstream `toots/shine` and add the ~4 ESP32-specific
   changes yourself. Measured state: **97.6 KB** (95,896 B config + 4 KB bitstream), latency
   **1152 samples = 26.1 ms** at 44.1 kHz.
2. **Opus — `esphome/micro-opus` v0.4.1 (Apache-2.0 AND BSD-3-Clause) supersedes everything
   s2 considered.** It is a self-contained IDF component shipping **full libopus 1.4 source**
   (261 `.c` files in the registry package), fixed-point, with **Xtensa LX7 inline-assembly
   patches for CELT *and* SILK**, and it exposes the plain **C** `opus.h` encoder API. Its own
   ESP32-S3 @240 MHz benchmark table shows **48 kHz stereo music at complexity 0–10 is *all*
   real-time (worst case 1.6×)** — this **overturns** s2's "music-grade Opus is probably not
   real-time" inference (s2 §4, §8). It still has no Ogg muxer.
3. **Ogg muxer — a reusable one DOES exist: `espressif/esp_muxer` v1.2.3**, which has
   `ESP_MUXER_TYPE_OGG` + `ESP_MUXER_ADEC_OPUS`, generates `OpusHead`/`OpusTags`, and whose
   `muxer_data_callback` is documented "suitable for living stream scenario". Catch: it is a
   **prebuilt static library under the restricted Espressif Modified MIT** and the Ogg muxer
   source is not public. The permissive alternatives are `georgik/ogg` (real libogg 1.3.5,
   BSD-3) or **writing ~200–400 lines yourself**, for which two complete Apache-2.0 reference
   implementations exist inside the esphome-libs repos.
4. **MP3 is never Ogg-encapsulated.** ffmpeg's Ogg muxer explicitly rejects MP3, and Icecast's
   Ogg plugin only knows Vorbis and Opus. Keep the bare-frame + ICY `metaint` path.

---

## 1. MP3 encoder — `shine`

### 1.1 Repositories

| Repo | What it is | Licence | Last touched |
|---|---|---|---|
| [`toots/shine`](https://github.com/toots/shine) | Canonical maintained fork (of `savonet/shine`); "blazing fast mp3 encoding library implemented in fixed-point arithmetic" | **LGPL-2** — `COPYING` is "GNU LIBRARY GENERAL PUBLIC LICENSE Version 2, June 1991", read directly | commit `ab5e352` 2023-01-01 |
| [`fknrdcls/mp3_shine_esp32`](https://github.com/fknrdcls/mp3_shine_esp32) | 2019 ESP32 port; real ESP-IDF component (`component.mk` with `COMPONENT_SRCDIRS := .`) plus a PlatformIO `library.json` (`"frameworks": "Arduino"`) and `example_freeRTOS_task_snippet.c` | **GPL-3.0** — GitHub API `license.spdx_id = "GPL-3.0"`, and the tree contains **both** `COPYING` (LGPL-2) **and** `LICENSE` (GPL-3, "GNU GENERAL PUBLIC LICENSE Version 3") | pushed 2019-12-09, 25★ |
| `anton-malakhov/mp3_shine_esp32` (gitflic.ru) | 2023 revival of the above; the source of s2's "ESP32-S3: Single core takes 53.4 % of real time" quote | unknown | unknown |
| [`arendst/Tasmota` `lib/lib_audio/mp3_shine_esp32`](https://github.com/arendst/Tasmota/tree/development/lib/lib_audio/mp3_shine_esp32) | fknrdcls port **vendored inside a real, shipping ESP-IDF project**; sources renamed `.c` → `.cpp` | inherits fknrdcls's LGPL-2 + GPL-3 pair | — |

**The GPL-3 trap is real and confirmed.** `fknrdcls`'s repo root carries a GPL-3 `LICENSE`
alongside the upstream LGPL-2 `COPYING`. s2 said "do not use that one in a permissively-licensed
library"; that stands, but note the practical consequence: the *only* ESP32 port with shipping
proof (Tasmota) is GPL-3-or-LGPL-2-ambiguous, so **the port cannot simply be lifted**.

**Xtensa assembly is not in upstream shine — it is in the ports.** `toots/shine`'s
`src/lib/mult_*_gcc.h` set is `noarch`/`sarm`/`mips` only. The Tasmota-vendored port adds
`src/mult_xtensa_gcc.h`, which is real inline asm:

```c
#define asm_mul(x,y) ({ int result; asm ("mulsh %0, %2, %1" : "=r" (result) : "r" (x), "r" (y)); result; })
```

That file is **not** in fknrdcls's GitHub root listing, so the Xtensa asm appears to be a
Tasmota/anton-malakhov addition. Its performance contribution is **UNVERIFIED** — no source
attribute a number to it. (`-Os` is what the fknrdcls README claims is needed for "full
48000Hz Joint Stereo performance on one core".)

### 1.2 Exact API (quoted from `src/lib/layer3.h`)

```c
typedef struct shine_global_flags *shine_t;          /* opaque handle */

void shine_set_config_mpeg_defaults(shine_mpeg_t *mpeg);
int  shine_find_bitrate_index(int bitr, int mpeg_version);
int  shine_find_samplerate_index(int freq);
int  shine_mpeg_version(int samplerate_index);
int  shine_check_config(int freq, int bitr);         /* -1 on error, else mpeg_version */

shine_t shine_initialise(shine_config_t *config);    /* NULL on alloc failure */

#define SHINE_MAX_SAMPLES 1152
int  shine_samples_per_pass(shine_t s);

unsigned char *shine_encode_buffer(shine_t s, int16_t **data, int *written);
unsigned char *shine_encode_buffer_interleaved(shine_t s, int16_t *data, int *written);
unsigned char *shine_flush(shine_t s, int *written);
void shine_close(shine_t s);
```

Config types (same header):

```c
enum channels { PCM_MONO = 1, PCM_STEREO = 2 };
enum modes { STEREO = 0, JOINT_STEREO = 1, DUAL_CHANNEL = 2, MONO = 3 };
typedef struct { enum channels channels; int samplerate; } shine_wave_t;
typedef struct { enum modes mode; int bitr; enum emph emph; int copyright; int original; } shine_mpeg_t;
typedef struct { shine_wave_t wave; shine_mpeg_t mpeg; } shine_config_t;
```

`shine_encode_buffer_interleaved` is the one a WAV/decoder pipeline wants.

### 1.3 Mono/stereo, CBR/VBR, sample rates, latency, RAM

- **Mono and stereo: yes** — `PCM_MONO`/`PCM_STEREO` and `MONO`/`STEREO`/`JOINT_STEREO`/
  `DUAL_CHANNEL` all exist. Note the port wrapper configures plain `STEREO` for 2 channels,
  not joint stereo (s2 §3).
- **CBR only. There is no VBR.** A case-insensitive grep for `vbr`/`variable bitrate` across
  `src/` and the README returns **zero hits**. Bitrate is fixed at init via `config.mpeg.bitr`
  and 1152 samples always map to one fixed-size frame. Bit-rate *reservoir* logic exists
  (`reservoir.c`, `shine_ResvFrameEnd`) but that is MP3's standard bit reservoir within CBR,
  not VBR.
- **Sample rates / bitrates:** `layer3.h` documents 44100/48000/32000 (MPEG-I),
  22050/24000/16000 (MPEG-II), 11025/12000/8000 (MPEG-2.5) with per-version bitrate tables
  (MPEG-I 32–320 kbps; MPEG-II/2.5 8–160 kbps).
- **Latency — measured, not inferred.** `shine_samples_per_pass()` returns
  `granules_per_frame * GRANULE_SIZE` (`layer3.c:76`), i.e. **1152** for MPEG-I and **576** for
  MPEG-II. I built upstream shine on host and drove it: at 44.1 kHz stereo/128 kbps the **first
  call already returns a complete frame** (416 bytes written after exactly 1152 samples), so
  there is no startup buffering beyond one pass:

  | Rate | MPEG version | samples_per_pass | wall time |
  |---|---|---|---|
  | 44100 | 3 (MPEG-I) | 1152 | **26.12 ms** |
  | 48000 | 3 | 1152 | 24.00 ms |
  | 32000 | 3 | 1152 | 36.00 ms |
  | 22050 / 24000 / 16000 | 2 (MPEG-II) | 576 | 26.12 / 24.00 / 36.00 ms |

  On top of the one-frame emission latency there is **512 samples (≈11.6 ms at 44.1 kHz) of
  polyphase filterbank history** (`HAN_SIZE 512`, `types.h:48`; the ring buffer in
  `l3subband.c`), which is *content* delay rather than buffering delay. **Practical lead
  requirement: one frame (26 ms) + ~12 ms of filterbank delay ≈ 40 ms**, negligible against the
  existing `lead_ms` default of 1500 ms.
- **RAM — measured.** `sizeof(shine_global_config)` = **95,896 bytes**; `shine_initialise`
  `calloc`s that in **one contiguous block** (hence the port README's *"init the encoder ASAP
  … needs large contiguous chunks of RAM"*), plus a separately `malloc`'d bitstream buffer of
  `BUFFER_SIZE 4096` bytes that I confirmed does **not** grow under sustained encoding.
  **Total ≈ 99,992 B ≈ 97.6 KB.** On a 512 KB-SRAM part with PSRAM absent this is a serious
  line item, and it *must* be contiguous internal RAM or PSRAM (shine does not use
  `heap_caps_*`).
- Host sanity measurement: 60 s of 44.1 kHz stereo/128 kbps encoded in 0.229 s CPU =
  **262× realtime on an Apple M1** using the unmodified upstream source. This only proves the
  tree builds and runs; the S3 numbers remain s2's two independent ESP32 measurements
  (~1.8× realtime).

### 1.4 Is there an ESP-IDF component / registry entry?

**No.** The registry API returns literally `{"error":"ComponentNotFoundError","messages":["No
component found"]}` for `https://components.espressif.com/api/components?q=shine`, and also for
`q="mp3 encoder"`. There is **no `idf_component.yml` anywhere in `toots/shine`**. Any use is
**source vendoring**. The only ESP-IDF-shaped packaging that exists is fknrdcls's
`component.mk` (legacy GNU-make IDF, not CMake) — and its own README warns
**"Last updated FEB 2019 - Compatibility with current IDF unknown!!"**.

### 1.5 `esphome/micro-mp3` — decoder only, confirmed; no esphome MP3 encoder

`esphome/micro-mp3` v0.4.0 (Apache-2.0) is a **decoder**, and there is no encoder anywhere in
the tree: `include/micro_mp3/mp3_decoder.h` + `src/mp3_decoder.cpp` + a fork of OpenCore's
`src/opencore-mp3dec/`. Its `idf_component.yml` description says "Streaming MP3 decoder …
forked from OpenCore". A grep for `encod` in `include/`+`src/` hits only the decoder header
(which merely *name-drops* encoding when describing gapless trimming of encoder delay). **The
registry's "decoder" label is correct and there is no sibling MP3 encoder** — `micro-*` covers
flac (enc+dec), opus (enc+dec), vorbis (dec), mp3 (dec), wav, ogg-demuxer. `esphome/esp-audio-libs`
is a different, differently-licensed (`Custom`) wrapper and is not an MP3 encoder either.
**Verdict: shine remains the only viable MP3 encoder; nothing in the esphome family replaces it.**

---

## 2. Opus encoder

### 2.1 Recommendation: `esphome/micro-opus` v0.4.1 — the finding that changes the plan

| | |
|---|---|
| Registry | `esphome/micro-opus` **v0.4.1**, Apache-2.0 (component `license:` field reads `"Apache-2.0 AND BSD-3-Clause"`), 118,252 downloads, maintainer Kevin Ahrendt (Open Home Foundation) |
| Upstream | bundles **libopus 1.4** — `cmake/config.h.in:49` `#define PACKAGE_VERSION "1.4"`, submodule commit `22244de5a79bd1d6d623c32e72bf1954b56235be` |
| Vendored in the registry package? | **Yes — the published zip contains the full libopus source** (261 `.c` files under `lib/opus/`, `lib/opus/include/opus.h` present). Not a submodule stub. |
| Language | C (libopus) + a little C++ for wrappers; the encoder API you call is **plain C** |
| IDF constraint | `idf: ">=4.4"` |

**Why this beats everything s2 considered.** s2 §4 concluded Opus was only real-time at
complexity 0 and that music-grade Opus was "probably not real-time", on the strength of
`esp_audio_codec`'s 24.9 %-at-complexity-0 figure and the community `esp-libopus` port's
numbers. `micro-opus` ships its **own ESP32-S3 encode benchmark** that contradicts that:

[MUSIC, 48 kHz stereo, AUDIO mode, fixed-point, ESP32-S3 @ 240 MHz](https://github.com/esphome-libs/micro-opus/blob/main/examples/encode_benchmark/README.md)
(values = real-time multiple):

| Complexity | 64 kbit/s | 96 kbit/s | 128 kbit/s | 192 kbit/s |
|---|---|---|---|---|
| 0 | 3.2× | 3.0× | 2.8× | 2.6× |
| 2 | 2.6× | 2.5× | 2.4× | 2.3× |
| 5 | 2.0× | 1.9× | 1.8× | 1.8× |
| 8 | 1.8× | 1.7× | 1.6× | 1.6× |
| 10 | 1.8× | 1.7× | 1.6× | 1.6× |

The README's own key observation: **"All configs real-time capable — Worst case 1.5× real-time
(complexity 8/10 VOIP speech)"**. At complexity 5 (Opus's usual quality default) 48 kHz stereo
music runs at **1.8–2.0× realtime**, and complexity 8 and 10 are free relative to each other.
**This is a full-quality music Opus encoder on an S3**, not the speech-grade complexity-0
fallback s2 was forced to accept. Caveats, stated plainly: this is the *project's own published
benchmark*, not something I re-ran on hardware; it was run with PSRAM enabled (its log shows
"Free PSRAM: 16774624"); and it requires the **fixed-point** build — the same README's
floating-point table shows SILK failing real-time entirely at 16 kbit/s
(`1.1× @10k, 0.7× @16k`, then auto-skipped).

**Fixed-point plus Xtensa LX7 inline assembly.** `patches/README.md` documents patches for
"ESP32-S3 Xtensa LX7 optimizations" and claims **"~17-25% improvement for CELT operations"**.
The patches are genuine `__asm__` on `mulsh`, `clamps`, and `round.s`/`float.s`, applied to
both CELT and SILK:

```c
/* patches/celt/xtensa/fixed_lx7.h */
static OPUS_INLINE opus_int32 MULT16_32_Q16_lx7(opus_int16 a, opus_int32 b) {
    opus_int32 res;
    __asm__ volatile("mulsh %0, %1, %2\n\t" : "=r"(res) : "r"(SHL32(a, 16)), "r"(b));
    return res;
}
```

The patch README is also honest about its own risk: *"These patches are designed and tested
against the specific Opus commit in `lib/opus/`. If you update the submodule, patches may fail
to apply or produce incorrect results."* — i.e. **libopus is pinned at 1.4 and cannot be bumped
without re-doing the patches.**

**Exact API — plain C, exposed to consumers.** `CMakeLists.txt` registers
`"${OPUS_STAGED_DIR}/include"` in `INCLUDE_DIRS` (the `INCLUDE_DIRS` block at line ~84) and
`${OPUS_ENCODER_SOURCES}` is in the source list (lines 70 and 127), including `src/opus_encoder.c`
and `silk/enc_API.c` (`cmake/sources.cmake:44,153`). So a consumer does `#include "opus.h"` and
gets upstream libopus's documented C API. The README has an explicit **"Basic Encoding"**
section:

```c
OpusEncoder *encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &error);
opus_encoder_ctl(encoder, OPUS_SET_BITRATE(128000));
int bytes = opus_encode(encoder, pcm_data, frame_size, opus_data, sizeof(opus_data));
```

Upstream-verified constraints from `opus.h` in the same package:
- `opus_encode(OpusEncoder *st, const opus_int16 *pcm, int frame_size, unsigned char *data, opus_int32 max_data_bytes)` with
  *"`frame_size`: Number of samples per channel in the input signal. This must be an Opus frame size
  for the encoder's sampling rate. For example, at 48 kHz the permitted values are 120, 240, 480, 960, 1920, and 2880."*
  (`include/opus.h:237-244`). **20 ms at 48 kHz = 960 samples/channel**, which is in the list. 20 ms is directly available — no 60 ms compromise.
- Sample rates: *"Fs is the sampling rate and must be 8000, 12000, 16000, 24000, or 48000"* (`opus.h:380`).
  **22.05 kHz is not an Opus input rate at all** — the server's "22.05 kHz mono" option from s2 §7
  is incompatible with Opus; use 16 kHz or 24 kHz mono instead.
- Mono and stereo: `opus_encoder_create(Fs, channels, ...)` with `channels` 1 or 2.
- Full control surface: `OPUS_SET_COMPLEXITY`, `OPUS_SET_BITRATE`, `OPUS_SET_VBR`,
  `OPUS_SET_SIGNAL`, `OPUS_SET_DTX`, `OPUS_SET_LSB_DEPTH`, etc. — all present in the bundled
  `opus.h`.

**Memory** (micro-opus README memory table):

| Type | Size | Notes |
|---|---|---|
| State | **~30–50 KB per instance** | Encoder state |
| Pseudostack | **120 KB+ per thread** | default `CONFIG_OPUS_PSEUDOSTACK_SIZE=120000`, range 60000–240000 |
| OggOpus buffers | 1–61 KB | demuxer only, not needed for encoding |

The pseudostack is the catch: **120 KB, configurable down to 60 KB**, and in
`THREADSAFE_PSEUDOSTACK` mode it is per-thread and PSRAM-preferring. With shine's 97.6 KB and
Opus's 30–50 KB + 120 KB, **an S3 without PSRAM cannot host both encoders at once**, and Opus
alone wants ~150–170 KB. The README states the pseudostack is *"heavily accessed during
encode/decode"* — putting it in PSRAM is the intended configuration and will cost some speed
relative to the benchmark if the benchmark was run with an internal-RAM pseudostack (the
README's own recommended S3 settings enable octal PSRAM, so the 1.6–3.2× figures include it).

**Maintenance risk: low but young.** Version 0.4.1 on the registry, git `main` is already
**0.5.0** (`idf_component.yml` `version: "0.5.0"`), last commit 2026-10-07, with CI, fuzzing,
conformance vectors, QEMU tests, clang-tidy and pre-commit configs. It is the same author and
the same quality bar as `micro-flac` (already recommended by s2). Its own package excludes
`examples/**` and `tests/**`, so the encode_benchmark is *not* in the registry zip — it exists
only in git.

**Gap: no encoder wrapper and no muxer.** `include/micro_opus/` contains only
`ogg_opus_decoder.h` and `opus_packet_decoder.h`. `src/opus_header.cpp` is **parse-only**
(`parse_opus_head`, 175 lines) — there is **no OpusHead serializer**. `lib/micro-ogg-demuxer`
is bundled (demux only). So Opus encoding works via raw libopus C and you supply your own
framing.

### 2.2 The alternative: `78/esp-opus-encoder` v2.4.1 + `78/esp-opus` v1.0.5

Read both, including the registry-distributed zips.

- **`78/esp-opus` v1.0.5** is libopus **1.5.x** (the tree contains `dnn/`, `dred_encoder.c`,
  and `OPUS_SET_DRED_DURATION_REQUEST` in `opus_defines.h` — DNN/DRED landed upstream in 1.5).
  Its `CMakeLists.txt` builds `-DFIXED_POINT=1 -DDISABLE_FLOAT_API -DUSE_ALLOCA` and adds
  `-DOPUS_XTENSA_LX7` only for `esp32s2`/`esp32s3` (lines 175–191). The LX7 optimisation is
  **SILK-only**: `silk/macros.h:150` and `silk/SigProc_FIX.h:638` include
  `silk/xtensa/macros_lx7.h` / `SigProc_FIX_lx7.h`; there is **no `celt/xtensa/`** directory.
  So it is *newer upstream* but *less Xtensa-optimised* than micro-opus.
- **`idf: ">=5.0"`** for esp-opus, **`idf: ">=5.3"`** for esp-opus-encoder.
- **C++ only, no C binding.** `include/opus_encoder.h` includes `<functional> <vector> <memory>
  <mutex>` and declares `class OpusEncoderWrapper`. The exact API:

```cpp
class OpusEncoderWrapper {
public:
    OpusEncoderWrapper(int sample_rate, int channels, int duration_ms = 60);
    ~OpusEncoderWrapper();
    inline int sample_rate() const;
    inline int duration_ms() const;
    void SetDtx(bool enable);
    void SetComplexity(int complexity);
    bool Encode(std::vector<int16_t>&& pcm, std::vector<uint8_t>& opus);
    void Encode(std::vector<int16_t>&& pcm, std::function<void(std::vector<uint8_t>&& opus)> handler);
    bool IsBufferEmpty() const;
    void ResetState();
};
#define MAX_OPUS_PACKET_SIZE 1000
```

  **C is available, but only by bypassing the wrapper**: the wrapper's only job is to call
  `opus_encoder_create`/`opus_encode`, and `78/esp-opus` is stock libopus with a C API. A C
  consumer simply depends on `78/esp-opus` and ignores `esp-opus-encoder` entirely.
- **Controls and minimum frame duration.** `SetComplexity` and `SetDtx` exist; there is **no
  bitrate setter at all** — the wrapper silently uses libopus's default bitrate for the
  rate/channel count. The default constructor is `duration_ms = 60`, with `OPUS_APPLICATION_VOIP`
  hard-coded and **DTX enabled by default** — music-hostile defaults. `frame_size_` is computed
  as `sample_rate / 1000 * channels * duration_ms`, then passed to `opus_encode` as
  `frame_size`, which libopus documents as **per-channel** samples. For mono this is correct
  (16 kHz × 60 ms = 960 = the 60 ms limit at 16 kHz). **For stereo it is 2× too large**, so at
  48 kHz stereo the encoder would be told "1920 samples/channel" = 40 ms while the caller's
  buffer holds only 20 ms of interleaved stereo — a **buffer over-read**. This is an unverified
  code reading (I did not execute it on an S3), but the arithmetic and the libopus doc comment
  are both explicit. Minimum usable frame duration is whatever libopus allows (2.5 ms); the
  wrapper's `duration_ms` parameter can request 20 ms.
- **Risk signals.** The `78/esp-opus-encoder` repo has **no README and no LICENSE file**
  (root listing: `CMakeLists.txt`, `idf_component.yml`, `include/`, three `.cc` files), and the
  registry's advertised `license.txt` for it is a **404** (`NoSuchKey` on
  `components-file.espressif.com/components/78/esp-opus-encoder/2.4.1/license.txt`). The
  registry nonetheless labels it "MIT". `78/esp-opus` likewise declares `license: MIT` in its
  `idf_component.yml` and the registry repeats "MIT", while its actual `COPYING` is **3-clause
  BSD** ("Copyright 2001-2023 Xiph.Org, Skype Limited, …") and `LICENSE_PLEASE_READ.txt` is the
  BSD/Clear-BSD contribution statement. **The registry's licence metadata for both `78/`
  components is wrong, and for the encoder there is no licence text at all.** It is widely used
  (340,849 downloads) but is not a clean dependency.

**Verdict:** `micro-opus` for the codec (C API, permissive, CELT+SILK LX7 asm, published S3
benchmark) over `78/esp-opus-encoder` (C++-only, undocumented licence, missing bitrate control,
VOIP/DTX defaults, apparent stereo frame-size bug).

### 2.3 `esp_audio_codec`'s Opus encoder — explicit tradeoff

Independently confirmed here (not just taken from s2): `espressif/esp_audio_codec` v2.6.2 ships
**no Ogg muxer** — the only Ogg artifacts in the whole tree are
`include/simple_dec/impl/esp_ogg_parse.h` and `esp_ogg_dec.h`, i.e. **decoder-side**. Its README
states for the encoder table *"OPUS | Supports raw opus only"* and separately *"Supports decode
VORBIS frame only, need remove OGG header"*. Its `include/encoder/impl/esp_opus_enc.h` is a
genuinely nicer *config surface* than raw libopus: a single struct with
`sample_rate` (8/12/16/24/48 k), `channel` (mono/dual), `bitrate`, `frame_duration`
(2.5/5/10/20/40/60/80/100/120 ms), `application_mode` (VOIP/AUDIO/LOWDELAY), `complexity`,
`enable_fec`, `enable_dtx`, `enable_vbr`, plus a documented per-rate/per-duration bitrate range
table and `ESP_OPUS_BITRATE_AUTO`.

**The tradeoff is purely licence and closure.** Its `LICENSE` is the *Espressif Modified MIT*:
*"Permission is hereby granted for use EXCLUSIVELY with Espressif Systems products"* and
*"Redistribution of the Software in source or binary form FOR USE WITH NON-ESPRESSIF PRODUCTS is
strictly prohibited."* xtape targets ESP32 only, so the first clause is satisfied — but it is
**not vendorable, not forkable into a permissive library, and not redistributable as source**,
which conflicts with a component whose whole point is to be vendored. It is also distributed as
a mix of source and prebuilt libs. **Verdict: technically the most convenient Opus config API,
but `micro-opus` wins on licence (Apache-2.0/BSD-3), source availability, and measured
high-complexity S3 performance.**

---

## 3. Ogg muxers — is there any reusable one?

**Yes — one production-grade one, plus one permissive library path, plus copyable reference code.**

### 3.1 `espressif/esp_muxer` v1.2.3 — a real Ogg+Opus muxer, verified at the binary level

This is the answer to the crux question and it was not in s2. It is on the registry as
`espressif/esp_muxer` v1.2.3 and is also vendored in
`espressif/esp-adf-libs` at `esp_muxer/`.

- Its README's support matrix marks **`OPUS` with `Y` under the `OGG` column and `N` under every
  other container** — OGG is the only container that carries Opus.
- `esp_muxer.h` defines `ESP_MUXER_TYPE_OGG = ESP_MUXER_FOURCC('O','G','G',' ')` and
  `ESP_MUXER_ADEC_OPUS = ESP_MUXER_FOURCC('O','P','U','S')`.
- `include/impl/ogg_muxer.h` exposes `ogg_muxer_register()` and `ogg_muxer_config_t` with a
  `page_cache_size` *"Cache data until data size exceed cache size … small input frames can be
  gathered into page"* — exactly the right knob for a live stream.
- **Streaming to a socket is a first-class, documented use case.** `esp_muxer_config_t` carries
  `muxer_data_callback data_cb` described as *"Muxer output callback can be coexist with url
  pattern. **It is suitable for living stream scenario.**"* The test app's
  `is_muxer_support_streaming()` whitelist (binary-confirmed list
  `{TS, FLV, WAV, OGG}`) **includes `ESP_MUXER_TYPE_OGG`** — so the live path is intended for
  Ogg, not just files.
- **It builds `OpusHead`/`OpusTags` itself.** The prebuilt `lib/esp32s3/libesp_muxer.a` contains
  the strings `OpusHead`, `OpusTags`, `opus_comment`, `opus_header_t`, `ogg_muxer_write_single_page`,
  and the error string **`"Only support OPUS currently now %d"`** — confirming both that Opus is
  the sole supported Ogg codec and that the header/tag generation is internal. Consumers only
  pass `channel`, `sample_rate`, `bits_per_sample`; the audio `codec_spec_info` field is
  documented as needed for AAC only — *"Other audio format no need to provide"*.
- The official test app drives it with Espressif's own Opus encoder:
  `get_prefer_audio_codec(ESP_MUXER_TYPE_OGG)` returns `ESP_MUXER_ADEC_OPUS`, and
  `aud_enc_info_t` wraps `esp_opus_enc_config_t` — i.e. **encode with `esp_audio_codec`, mux
  with `esp_muxer`** is the sanctioned Espressif pairing.
- **Code cost is tiny:** `ogg_muxer.c.obj` in the S3 archive is
  `ogg_muxer_add_audio_packet` (0x76a = 1898 B), `ogg_muxer_write_single_page` (0x1ad = 429 B),
  `ogg_muxer_open` (0xdc), `ogg_muxer_add_audio_stream` (0xf1), plus small stubs — roughly
  **~1.5 KB of Xtensa text** for the Ogg muxer proper.

**Two hard caveats.**
1. **Licence.** `esp_muxer/LICENSE` is the *Espressif Modified MIT*: use *"EXCLUSIVELY with
   Espressif Systems products"*, and *"Redistribution … FOR USE WITH NON-ESPRESSIF PRODUCTS is
   strictly prohibited."* xtape runs on ESP32-S3 so it may **depend** on it, but it cannot be
   copied into a permissive component or redistributed standalone.
2. **No source.** Only headers plus `lib/<target>/libesp_muxer.a` are published; the archive's
   debug path reveals `/builds/adf/esp-adf-libs-source/esp_muxer/src/impl/ogg_muxer/ogg_muxer.c`,
   which is **not** in the public `esp-adf-libs` checkout (that tree contains only
   `src/muxer_reg.c` and `include/impl/*.h`). You cannot fix or even read the muxer. There is
   also no `idf_component.yml` version constraint beyond `version: "1.2.3"` and it is not on
   GitHub (`espressif/esp-muxer` and `espressif/esp_muxer` both 404).

### 3.2 `georgik/ogg` v1.3.5~1 — real libogg, BSD-3, IDF component

- Registry: `georgik/ogg` v1.3.5~1, **BSD-3-Clause license**, description "Ogg bitstream
  (ESP-IDF component)", homepage `xiph.org/ogg`, repo
  [`georgik/esp-idf-component-ogg`](https://github.com/georgik/esp-idf-component-ogg).
  Only 11 downloads — **experimental, effectively unmaintained**.
- It is a thin wrapper over real libogg: `.gitmodules` pins
  `path = ogg, url = https://github.com/xiph/ogg.git`, `idf_component.yml` requires `idf >=5.0.0`,
  and `CMakeLists.txt` registers exactly `ogg/src/framing.c` + `ogg/src/bitwise.c` — libogg's two
  compile units (**2138 + 1089 = 3227 lines** upstream).
- libogg's `COPYING` is Xiph's 3-clause BSD. libogg gives you the actual muxing primitives
  (`ogg_stream_init`, `ogg_stream_packetin`, `ogg_stream_flush`, `ogg_stream_pageout`) including
  lacing and CRC — so **you write only the Opus-specific part**: `OpusHead`, `OpusTags`, and the
  granulepos rule (48 kHz sample count including pre-skip). That is the RFC 7845 "Encapsulation
  of Opus in Ogg" mapping, and it is small.
- **Verdict: the clean permissive path.** It is the only BSD-3 Ogg *library* available as a
  registry dependency, at the cost of 3.2 k lines of C and a hand-written Opus mapping.

### 3.3 Hand-writing it is a ~200–400 line job, and two working references are already in-tree

This is not speculative. Two complete, Apache-2.0, byte-correct Ogg page writers exist inside
the esphome-libs repositories:

- [`micro-opus/tests/support/ogg_mux.h`](https://github.com/esphome-libs/micro-opus/blob/main/tests/support/ogg_mux.h) —
  **208 lines, header-only C++**. Contains the full `crc32` table (Ogg polynomial `0x04C11DB7`),
  `make_ogg_page()` (lacing, segment count, the exact-multiple-of-255 terminating-segment rule,
  CRC written back into the header), `make_opus_head_family0/1()` (version, channels, pre_skip,
  input sample rate, gain, mapping family) and `make_opus_tags()`. Its own comment claims it
  "Builds valid Ogg pages, OpusHead, and OpusTags". It is **excluded from the registry package**
  (`files: exclude: tests/**`), so it must be taken from git — but it is Apache-2.0 and copyable.
- [`micro-ogg-demuxer/tests/ogg_page_builder.{h,cpp}`](https://github.com/esphome-libs/micro-ogg-demuxer/tree/main/tests) —
  **239 lines**, same author, same licence, with `append_packet_lacing()` implementing the
  RFC 3533 terminating-zero rule explicitly.

A production muxer is these two files plus: accumulate Opus packets, flush a page when the page
cache fills or on a time boundary, track `granulepos`, mark BOS on page 0 and EOS at the end,
and keep granulepos on the *last completed packet* of a page. Two independent, correct,
permissively-licensed implementations of the fiddly parts (CRC, lacing, headers) already exist.

### 3.4 Does `78/esp-opus` or any Opus port ship Ogg?

**No, in all three cases.** `78/esp-opus` is stock libopus — no `ogg` directory, `opusfile` not
present. `esphome/micro-opus` ships an Ogg Opus **decoder** only (`ogg_opus_decoder.cpp` +
parse-only `opus_header.cpp` + the bundled `micro-ogg-demuxer`, whose README says it "handles the
container only" and "no libogg"). `esp_audio_codec` ships Ogg **parsing** for the decoder. The
registry has no `libogg`, `opusfile` or `icecast` component
(`ComponentNotFoundError` for each query).

### 3.5 Existing ESP32 projects that stream Opus-in-Ogg

Searched GitHub's repository API for `esp32+icecast`, `esp32+opus+stream`, `ogg+muxer`,
`esp32+ogg`. Findings, with the honest caveat that this is **thin**:

- [`slastra/downlink`](https://github.com/slastra/downlink) — *"ESP32-S3 Icecast Ogg Opus player
  → PCM5102A I2S DAC (ESP-IDF v6)"*. This is the **decode** direction, and it is direct
  confirmation that **ESP32-S3 + Icecast + Ogg Opus is a real, working combination today**. It
  tells us nothing about a muxer.
- [`wavefarm/Internet-Radio-Toolkit`](https://github.com/wavefarm/Internet-Radio-Toolkit) (GPL-3.0)
  — *"ESP32 based Audio Streaming over Icecast"*, an Arduino sketch with a `Streamer.cpp`. It is
  a **source client** — the closest structural analogue to xtape. I did **not** read
  `Streamer.cpp`, so what it streams (MP3 via VS1053 hardware encoder, or something else) is
  **UNVERIFIED**.
- No ESP32 project that muxes **Opus** into Ogg itself surfaced. Every relevant hit was either
  a player (`downlink`, `zllovesuki/esp32-radio` over WebRTC, `tankyhsu/atom-echo-voice-assistant`
  which uses raw Opus for voice) or unrelated. **UNVERIFIED / LIKELY: there is no public ESP32
  Opus-in-Ogg muxer to copy.** The realistic sources of a muxer are therefore exactly the three
  options in §3.1–3.3, not a community project.
- Corroborating that shine *does* get used in real ESP32 firmware: Tasmota vendors the port at
  `lib/lib_audio/mp3_shine_esp32`, and a Tasmota mirror shows it under `lib_audio` alongside the
  rest of Tasmota's audio codecs.

---

## 4. MP3-in-Ogg vs bare MP3

**MP3 output needs no container, and no real server or client Ogg-encapsulates MP3.** Verified
from two independent primary sources:

- **ffmpeg's Ogg muxer refuses MP3 outright.** `libavformat/oggenc.c:542-550` gates on
  `codec_id ∈ {AV_CODEC_ID_VORBIS, THEORA, SPEEX, FLAC, OPUS, VP8}` and otherwise logs
  `"Unsupported codec id in stream %d"` and returns `AVERROR(EINVAL)`. **`AV_CODEC_ID_MP3` is
  not in the list.** Line 589 shows the dedicated `AV_CODEC_ID_OPUS` branch calling
  `ogg_build_opus_headers()`, and the separate `ff_opus_muxer` is declared with
  `.p.mime_type = "audio/ogg"`, `.p.name = "opus"` — so ffmpeg treats Ogg-Opus as a first-class
  output format and Ogg-MP3 as an error.
- **Icecast's Ogg plugin only knows Vorbis and Opus.** `src/format_ogg.c` includes
  `format_vorbis.h` and `format_opus.h`, and dispatches to `initial_vorbis_page()` (line 241) or
  `initial_opus_page()` (line 266) — there is no MP3 branch. `src/format_opus.c` validates the
  `OpusHead` magic, version and channel count and reads `OpusTags`, which is precisely the
  contract a muxer must satisfy for Icecast to accept the stream. Meanwhile MP3 has its own
  plugin: `src/format_mp3.c` sets `contenttype = "audio/mpeg"` (line 101) and
  `src/format.c:410` handles `strcasecmp(var->name, "icy-metaint")`. So Icecast's MP3 path is
  **bare frames + ICY metadata**, exactly what `mini_icecast.c` already implements.

**Consequence for xtape:** the MP3 output path keeps `Content-Type: audio/mpeg`, bare MPEG
frames, and the existing `send_meta()`/`metaint` now-playing mechanism unchanged — **no container
work at all**. The Ogg work is required *only* if Opus is chosen. This also settles the metadata
asymmetry s2 §4 noted: MP3 keeps ICY; Opus-in-Ogg must use `OpusTags` (and Icecast's live
metadata for Ogg is the `OpusTags`-re-emission path, not `icy-metaint`).

---

## 5. Integration cost

### 5.1 Summary table

| Component | Purpose | Upstream | Version | Licence | Language | IDF component? | API maturity | Verdict |
|---|---|---|---|---|---|---|---|---|
| `toots/shine` | MP3 encoder | `toots/shine` (fork of `savonet/shine`) | `ab5e352`, 2023-01-01 | **LGPL-2** (`COPYING`) | C (23 files, ~3.5 k lines + 10 KB tables) | **No** — nothing on the registry | Simple, stable, unchanged since 2023; no VBR; `double` `log()` in `l3loop.c` | **Vendor as source** + write `idf_component.yml`/CMakeLists |
| `fknrdcls/mp3_shine_esp32` | MP3 encoder (ESP32 port) | as above | 2019-12-09 | **GPL-3.0** repo label, ships **both** LGPL-2 `COPYING` and GPL-3 `LICENSE` | C (Tasmota renames to `.cpp`) | Yes — legacy `component.mk` only; no `idf_component.yml` | README: *"Compatibility with current IDF unknown!!"* | **Avoid** — GPL-3 ambiguity; lift only the `mult_xtensa_gcc.h` idea |
| `anton-malakhov/mp3_shine_esp32` | MP3 encoder (S3 revival) | as above | gitflic, 2023 | unknown | C | likely, via `component.mk`/`idf_component.yml` | unknown | **UNVERIFIED** (gitflic returned HTTP 502); s2's 53.4 % figure rests on it |
| `esphome/micro-mp3` | MP3 **decoder** | OpenCore pvmp3 fork | 0.4.0 | Apache-2.0 | C++ wrapper / C core | Yes, `idf >=4.4` | Mature | **Decoder only — not an encoder** (confirmed from tree) |
| `esphome/micro-opus` | **Opus encoder + decoder** | libopus 1.4 (`22244de`) | **0.4.1** (git main 0.5.0) | **Apache-2.0 AND BSD-3-Clause** | C (libopus) + C++ wrappers | **Yes**, `idf >=4.4`, ships full libopus source | New but high-quality: CI, fuzzing, conformance, QEMU | **RECOMMENDED Opus encoder** |
| `78/esp-opus` | libopus 1.5.x for ESP32 | libopus 1.5.x | 1.0.5 | `idf_component.yml` says MIT; actual `COPYING` is **BSD-3**; registry label wrong | C | Yes, `idf >=5.0` | Stock upstream + IDF CMake | Viable if you need 1.5.x; **no CELT LX7 asm** |
| `78/esp-opus-encoder` | C++ Opus wrapper | — | 2.4.1 | **No LICENSE file; registry `license.txt` 404s**; registry labels it MIT | **C++ only** | Yes, `idf >=5.3` | Undocumented; no bitrate setter; VOIP+DTX defaults; apparent stereo frame-size bug | **Not recommended** |
| `espressif/esp_audio_codec` | Opus/AAC encoders, decoders | Espressif | 2.6.2 | **Espressif Modified MIT** (Espressif products only; no redistribution for non-Espressif) | C, partly prebuilt | Yes, `idf >=4.4` | Mature, first-party | Only if you need its config API; **not vendorable** |
| **`espressif/esp_muxer`** | **Ogg/Opus muxer** (+MP4/TS/FLV/WAV/CAF/AVI) | Espressif | **1.2.3** | **Espressif Modified MIT** | **C, prebuilt `.a` only** — no source | Yes | Mature; `data_cb` designed for live streaming; test app pairs it with `esp_audio_codec` | **RECOMMENDED if the restricted licence is acceptable** |
| `georgik/ogg` | libogg 1.3.5 | `xiph/ogg` | 1.3.5~1 | **BSD-3-Clause** | C (3,227 lines: `framing.c` + `bitwise.c`) | Yes, `idf >=5.0.0` | Thin wrapper, 11 downloads, effectively unmaintained | **The permissive path** — needs ~200–400 lines of Opus mapping |
| `esphome/micro-ogg-demuxer` | Ogg **demuxer** | — | git main | Apache-2.0 | C++11, no libogg | Bundled inside micro-opus | — | Reference code only (its `tests/ogg_page_builder` is a muxer) |

### 5.2 Exact recommended encoder APIs

**MP3 — `shine`, `int16_t` interleaved in, MP3 frame bytes out:**

```c
shine_config_t config = {0};
config.wave.channels   = PCM_STEREO;   /* or PCM_MONO */
config.wave.samplerate = 44100;
shine_set_config_mpeg_defaults(&config.mpeg);
config.mpeg.bitr = 128;
config.mpeg.mode = JOINT_STEREO;       /* upstream supports it; ports used plain STEREO */
if (shine_check_config(config.wave.samplerate, config.mpeg.bitr) < 0) { /* reject */ }

shine_t s = shine_initialise(&config);
int spp = shine_samples_per_pass(s);   /* 1152 @ MPEG-I, 576 @ MPEG-II */

int written = 0;
unsigned char *frame = shine_encode_buffer_interleaved(s, pcm_interleaved, &written);
/* frame is library-owned, valid only until the next call; may be NULL */
frame = shine_flush(s, &written);      /* then */
shine_close(s);
```

**Opus — `micro-opus`, libopus C API, 20 ms at 48 kHz stereo:**

```c
#include "opus.h"

int err;
OpusEncoder *enc = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &err);
opus_encoder_ctl(enc, OPUS_SET_BITRATE(96000));
opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));   /* 5 = 1.8-2.0x realtime on S3, measured */
opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
opus_encoder_ctl(enc, OPUS_SET_VBR(1));

#define FRAME_PER_CH 960                          /* 20 ms @ 48 kHz; 1920 interleaved samples */
int16_t pcm[FRAME_PER_CH * 2];
uint8_t packet[1500];
int n = opus_encode(enc, pcm, FRAME_PER_CH, packet, sizeof(packet));  /* frame_size = PER-CHANNEL */
```

Note `opus_encode`'s `frame_size` is **per channel**, and legal values at 48 kHz are
120/240/480/960/1920/2880.

### 5.3 What must be written from scratch

1. **`idf_component.yml` + `CMakeLists.txt` for `shine`** (small), plus deciding how to compile
   the `.c` files cleanly under IDF and whether to add the Xtensa `mulsh` macros.
2. **The Opus-in-Ogg muxer if you do not take `esp_muxer`**: page assembly, CRC, lacing, BOS/EOS
   flags, granulepos tracking, `OpusHead` (pre-skip!) and `OpusTags`. **Estimate ~200–400 lines**,
   and the two Apache-2.0 implementations in §3.3 are directly reusable starting points. If you
   *do* take `esp_muxer`, this is **zero lines**.
3. **An encoder-callback/ring change if the wire format becomes compressed**: s2 §7 already
   notes the burst ring is length-prefixed so it carries variable-length frames, but `bytes/s`
   sizing and `metaint` semantics change; for Opus the ICY metadata machinery becomes dead code
   and must be replaced by an `OpusTags` (or Icecast-side) mechanism.
4. **Opus resampling to 48 or 24 kHz** — Opus has no 22.05 kHz input mode, so s2 §7's
   "22.05 kHz mono" station option is unavailable for the Opus path.

### 5.4 The honest bottom line on effort

- **MP3 only:** vendor `shine` (~3.5 k lines of C you do not write, plus its LGPL-2 obligations
  already analysed in s2 §3), add a small IDF component wrapper, **no muxer at all**, keep ICY.
  **This is a small, low-risk job.**
- **Opus-in-Ogg via `esp_muxer`:** the least *code* — two registry dependencies
  (`esphome/micro-opus` + `espressif/esp_muxer`, optionally `espressif/esp_audio_codec` for its
  nicer encoder config) and a thin glue layer. Cost is licence: you are shipping a proprietary,
  Espressif-restricted binary blob you cannot read or patch, and you cannot vendor it.
- **Opus-in-Ogg permissively:** `esphome/micro-opus` + `georgik/ogg` (or the ~300 lines of
  hand-written page builder). **This is real new code — roughly 200–400 lines of Ogg/Opus
  mapping on top of libogg — but it is not a research problem**, because correct reference
  implementations of every fiddly part are already in hand under Apache-2.0. Call it a
  few days, dominated by getting granulepos/pre-skip and page-flush timing right and by testing
  against a real client.

---

## 6. VERIFIED vs UNVERIFIED — explicit ledger

**VERIFIED (source, registry zip, or binary read in this pass):**

- `toots/shine` is LGPL-2 (`COPYING`), has the exact API quoted in §1.2, supports mono and
  stereo, and has **no VBR** anywhere in `src/` or the README (grep).
- shine **latency and RAM, measured by me on host**: `samples_per_pass` = 1152 (MPEG-I) /
  576 (MPEG-II); the first call already emits a frame; `sizeof(shine_global_config)` = **95,896 B**
  in one contiguous `calloc` + a 4,096 B bitstream buffer = **99,992 B ≈ 97.6 KB**;
  262× realtime on Apple M1 (build/run sanity only).
- **No `shine` component exists** on the registry (`ComponentNotFoundError`), and none exists
  for `libogg`/`opusfile`/`icecast`/`mp3 encoder`.
- `fknrdcls/mp3_shine_esp32` is **GPL-3.0** per the GitHub API and ships GPL-3 `LICENSE` +
  LGPL-2 `COPYING`; its README says *"Last updated FEB 2019 - Compatibility with current IDF
  unknown!!"*; it is C, with legacy `component.mk`, a PlatformIO `library.json`, and a FreeRTOS
  example. Tasmota vendors it (with Xtensa `mulsh` asm not present in fknrdcls's GitHub root).
- `esphome/micro-mp3` **v0.4.0 is decoder-only** — no encoder in the tree.
- **`esphome/micro-opus` v0.4.1 ships full libopus 1.4 source** (261 `.c` files, `PACKAGE_VERSION
  "1.4"`, submodule commit `22244de`), **builds the encoder** (`OPUS_ENCODER_SOURCES` in
  `CMakeLists.txt:70,127`; `silk/enc_API.c` in `sources.cmake:153`), **exports `opus.h`**
  (`INCLUDE_DIRS` includes `lib/opus/include`), is Apache-2.0/BSD-3, `idf >=4.4`, and has real
  CELT+SILK `mulsh`/`clamps`/`round.s` LX7 inline asm in `patches/`.
- micro-opus's README publishes an ESP32-S3 @240 MHz **encode** benchmark: 48 kHz stereo music
  real-time at **every** complexity 0–10, worst case 1.6×; fixed-point required (float SILK fails
  at 16 kbit/s); memory table ~30–50 KB state + 120 KB pseudostack/thread. (Read as *reported
  measurements in the project's own README* — I did not run them on hardware.)
- micro-opus has **no OpusHead serializer** (`src/opus_header.cpp` is parse-only,
  `parse_opus_head`) and **no muxer**; `lib/micro-ogg-demuxer` is bundled and is demux-only.
- **`espressif/esp_muxer` v1.2.3 has a working Ogg/Opus muxer**: README matrix marks OPUS `Y`
  only for OGG; `ESP_MUXER_TYPE_OGG` + `ESP_MUXER_ADEC_OPUS` in `esp_muxer.h`;
  `muxer_data_callback` documented as *"suitable for living stream scenario"*; OGG is in the
  test app's live-streaming whitelist; the prebuilt S3 archive contains `OpusHead`, `OpusTags`,
  `ogg_muxer_write_single_page` and the error string *"Only support OPUS currently now %d"*.
  Its licence is Espressif Modified MIT (Espressif-products-only, non-Espressif redistribution
  prohibited) and **the source is not published**.
- `georgik/esp-idf-component-ogg` (`georgik/ogg` v1.3.5~1, **BSD-3**) vendors libogg via
  submodule and compiles `framing.c` (2138 lines) + `bitwise.c` (1089 lines); libogg's `COPYING`
  is Xiph BSD-3.
- Two complete Apache-2.0 Ogg **page writers** exist in esphome-libs test dirs
  (`micro-opus/tests/support/ogg_mux.h`, 208 lines, incl. CRC table + OpusHead/OpusTags;
  `micro-ogg-demuxer/tests/ogg_page_builder.*`, 239 lines). Both are excluded from the registry
  zips via `files: exclude: tests/**`.
- `78/esp-opus` is libopus **1.5.x** (has `dnn/`, DRED defines), builds
  `FIXED_POINT=1 DISABLE_FLOAT_API USE_ALLOCA` + `OPUS_XTENSA_LX7` for S2/S3, with LX7
  optimisations in **silk only** (no `celt/xtensa/`). Its `idf_component.yml` claims MIT while
  `COPYING` is BSD-3.
- `78/esp-opus-encoder` v2.4.1 is a **C++-only** wrapper (`<functional> <vector> <mutex>`,
  `class OpusEncoderWrapper`), has **no LICENSE file** (registry `license.txt` = HTTP 404),
  defaults to `duration_ms = 60`, `OPUS_APPLICATION_VOIP` and DTX on, exposes **no bitrate
  setter**, and computes `frame_size_ = sample_rate/1000*channels*duration_ms` which it passes as
  libopus's *per-channel* `frame_size`.
- `esp_audio_codec` v2.6.2 has **no Ogg muxer** (only decoder-side `esp_ogg_parse.h` /
  `esp_ogg_dec.h`); its README says the Opus encoder "Supports raw opus only"; its licence is
  Espressif Modified MIT ("EXCLUSIVELY with Espressif Systems products"; non-Espressif
  redistribution prohibited); its `esp_opus_enc.h` config struct exposes sample rate, channel,
  bitrate, frame duration 2.5–120 ms, application mode, complexity, FEC, DTX and VBR.
- **MP3 is not Ogg-encapsulated in practice**: ffmpeg's `oggenc.c:542-550` rejects any codec
  outside `{VORBIS, THEORA, SPEEX, FLAC, OPUS, VP8}`, with `AV_CODEC_ID_MP3` absent; Icecast's
  `format_ogg.c` dispatches only to `initial_vorbis_page()` / `initial_opus_page()`, while
  `format_mp3.c` uses `audio/mpeg` and `format.c:410` handles `icy-metaint`. `ff_opus_muxer`
  carries `.p.mime_type = "audio/ogg"`.

**UNVERIFIED / LIKELY:**

- **`anton-malakhov/mp3_shine_esp32` and its "53.4 % of real time" S3 figure.** `gitflic.ru`
  returned **HTTP 502** on fetch and the GitHub search API surfaces only the `fknrdcls` repo, so
  s2's key shine-on-S3 number is carried forward on s2's testimony, not re-read here.
- **Whether micro-opus's benchmark numbers hold with the pseudostack in PSRAM and with a
  realistic decode+resample load alongside.** The README's recommended S3 settings enable octal
  PSRAM, suggesting the published numbers already include it — but that is an inference.
- **The `78/esp-opus-encoder` stereo frame-size bug** is a code reading, not a reproduction: the
  arithmetic and libopus's documented per-channel `frame_size` are both explicit, but I did not
  run it on hardware.
- **Whether `esp_muxer`'s Ogg muxer emits a `pre_skip` matching the encoder's lookahead, and
  whether it requires `codec_spec_info` for Opus.** The audio `codec_spec_info` doc says only AAC
  needs it, and `OpusHead` strings are present — but the source is unavailable, so the exact
  pre-skip/granulepos behaviour is **not verifiable** without testing on hardware.
- **Any ESP32 project that muxes Opus into Ogg itself.** None was found; `slastra/downlink` is a
  player, and `wavefarm/Internet-Radio-Toolkit` (a source client) was not read. My conclusion
  that no copyable community muxer exists is **LIKELY, not proven**.
- **Browser/client support for a live, endless Ogg-Opus HTTP stream** — unchanged from s2 §8's
  open item; not tested here. `slastra/downlink` shows a player *can* consume Icecast Ogg Opus,
  which is encouraging but is one project.
- `-Os` and the Xtensa `mulsh` asm in the shine ports are claimed to be needed/helpful for
  "full 48000Hz Joint Stereo … on one core"; **no source quantifies either**.

---

## 7. Build recommendation

**Depend on (registry):**
- `esphome/micro-opus` **v0.4.1** — Opus encoder, C API, Apache-2.0/BSD-3, `idf >=4.4`. This is
  the single most important upgrade over s2: it makes **complexity-5 music Opus at 1.8–2.0×
  realtime on an S3** a measured, supported configuration rather than a hope.

**Vendor as source:**
- `toots/shine` (LGPL-2) for MP3, plus a new `idf_component.yml`/`CMakeLists.txt`. Take the
  `mulsh` idea from the GPL-3 port if you want it, but **re-implement the macro rather than
  copying the GPL-3 file**, and do not vendor `fknrdcls` wholesale. Budget ~97.6 KB contiguous
  RAM and ~55 % of one core (s2 §3).
- **One of:**
  - `georgik/ogg` (BSD-3, libogg) **plus ~200–400 lines of RFC 7845 Opus mapping that you write**,
    using the two Apache-2.0 page builders in §3.3 as the starting point; **or**
  - the ~300-line muxer written directly from those references with no libogg dependency at all
    (libogg is only 3.2 k lines and mostly does lacing/CRC, which the 208-line reference already
    does) — likely the *smaller* total footprint for a fixed 1-stream, 1-codec use case.

**Depend on, if the restricted licence is acceptable:**
- `espressif/esp_muxer` **v1.2.3** — the only production Ogg/Opus muxer that exists for this
  platform, streaming-capable, and it removes all muxer work. But it is a **prebuilt, unreadable
  `.a` under a licence that permits use on Espressif products and forbids non-Espressif
  redistribution**. If xtape stays a personal/ESP32-only component this is fine and is the
  cheapest path to Opus; if the component is meant to be published permissively or audited,
  it is a blocker. `espressif/esp_audio_codec`'s Opus encoder is the same tradeoff and is
  **not needed** if you take micro-opus.

**Must be written from scratch:** the Ogg framing layer (if not taking `esp_muxer`), the
`OpusHead`/`OpusTags` packets with a correct pre-skip, the page-flush timing for a live stream,
the encoder-to-ring-buffer glue, and the 48/24 kHz resample for Opus's restricted input rates.

**Is this a significant amount of new code?** Not for MP3: shine is a vendor-and-wrap job with
**no container at all** (§4). For Opus-in-Ogg it is **real but bounded**: ~200–400 lines of
container code for which correct, permissively-licensed references for every subtle part are
already in hand, versus ~0 lines and a licensing compromise if you accept `esp_muxer`. There is
**no unavoidable research risk** left in either path — the remaining work is ordinary
integration and hardware testing. The earlier conclusion that Opus on an S3 is only viable at
speech-grade complexity should be **retired**: micro-opus's published S3 benchmark says
otherwise, and that changes the recommendation space materially.

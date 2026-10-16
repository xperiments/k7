# Codec / decode layer for an ESP32‑S3 internet‑radio **player** component

Axis: *which formats must be decoded, with which library, at what cost.*
All sources fetched **2026‑10‑16** (host clock). Companions: `00-audit-of-resources.md`
(what `resources/` is), `02-packaging-and-licensing.md` (registry/licensing rules),
`03-transport-verified-locally.md` (ICY/HTTP transport). This file owns format facts,
decoder candidates, and the memory/CPU budget.

---

## 0. Where xtape starts, and why none of it carries over

`resources/` is a mini Icecast **server**: `SD → dr_mp3/dr_flac/dr_wav → resample → TCP`
([`resources/mi_decode.c`](../resources/mi_decode.c)). Its decode layer is structurally
wrong for a player for three independent reasons:

1. **It is file-shaped.** `mi_dec_open()` takes a path, `fopen()`s it, and supplies
   `seek`/`tell` callbacks — required by `drmp3_init`/`drflac_open`/`drwav_init`.
   A live stream has no length and no seek target.
2. **It is pull-shaped.** `mi_dec_read(d, out, max_frames)` decides when to decode.
   A stream arrives when the socket delivers it.
3. **Its format set is a server's.** FLAC/WAV are exported for local files; both are
   irrelevant or marginal for internet radio (§5).

The useful inheritance is the *seam* (`open/hz/read/close`), not the implementation.
Note dr_mp3 itself concedes the point: its frame-count helpers are documented
"**Cannot be used for infinite streams such as internet radio**" —
[`dr_mp3.h`](https://raw.githubusercontent.com/mackron/dr_libs/master/dr_mp3.h).

---

## 1. What stations actually stream

There is **no usable public codec census**. The Icecast Yellow Pages directory — the one
dataset that would answer this — is empty: `https://dir.xiph.org/yp.xml` returned
`<directory></directory>`, and [`dir.xiph.org/codecs`](https://dir.xiph.org/codecs)
renders "No Codecs to show" (both fetched 2026‑10‑16). Any percentage distribution you
read elsewhere is unsourced. What *is* verifiable is what specific, large stations
publish:

| Source (primary) | Evidence |
|---|---|
| **BBC** — [codecs, bitrates and protocols FAQ](https://www.bbc.com/sounds/help/questions/about-bbc-sounds-and-our-policies/codecs-bitrates) | Audio Factory profiles: **AAC-LC 320 kbps**, **AAC-LC 128 kbps**, **HE-AACv1 96 kbps**, **HE-AACv1 48 kbps** (all stereo). Delivered over **HLS and MPEG‑DASH**, not ICY: "We no longer provide ICY (Shoutcast) streams of the BBC's domestic services." |
| **SomaFM** — [direct stream links](https://somafm.com/fluid/directstreamlinks.html) | Per channel: **AAC 128 kbps**, **MP3 128 kbps**, **AAC‑HE 64 kbps**, **AAC‑HE 32 kbps**. HE‑AAC is offered only at 32/64 kbps — exactly where its bitrate advantage lives. |
| **Radio Paradise** — [RP forum, Feb 2025, ffprobe of a live stream](https://apps.radioparadise.com/rp3.php?name=Forum&file=showtopic&topic_id=26668) | `Audio: aac (HE-AACv2), 44100 Hz, stereo, fltp, 63 kb/s` with `icy-br: 64`, `StreamTitle`, `StreamUrl`; RP staff reply: the channel "is limited to 64k", and "some clients have it available in flac like the web player". |

Reading of the above (characterisation, not a census):

* **MP3 (MPEG‑1/2 Layer III)** is the other half of the baseline — every legacy
  Icecast/SHOUTcast endpoint, typically 128 kbps, sometimes 96/192/320.
* **AAC-LC** is the modern default where bandwidth is not scarce (128–320 kbps).
* **HE‑AAC / HE‑AACv2 ("AAC+")** is *the* codec for 32–96 kbps endpoints. Two of the
  three publishers above ship it, and RP's flagship low-bitrate stream is HE‑AACv2.
* **Opus** is real but niche (RP advertises lossless/high-quality options; Opus is
  common on community/self-hosted Icecast). **Vorbis** is legacy-niche.
  **FLAC** exists only as a "quality" tier (see §5).
* **MPEG Layer II** survives as a legacy/digital-radio artefact, not as a mainstream
  internet-radio codec. Decoder support is cheap, so support it and forget it.

**Practical conclusion:** MP3 + the AAC family (LC **and** HE‑AAC v1/v2) is the mandatory
set. Everything else is optional.

---

## 2. HE‑AAC: the biggest technical trap

### 2.1 The three profiles

| Name | MPEG‑4 Audio Object Type | What it adds |
|---|---|---|
| AAC‑LC | `AOT_AAC_LC = 2` | plain transform codec |
| HE‑AAC v1 ("AAC+") | `AOT_SBR = 5` | **SBR** — Spectral Band Replication |
| HE‑AAC v2 ("aacPlus v2", "eAAC+") | `AOT_PS = 29` | SBR **+ PS** — Parametric Stereo |

Verified in FFmpeg's object-type table
([`mpeg4audio.h`](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/mpeg4audio.h):
`AOT_AAC_LC = 2`, `AOT_SBR = 5`, `AOT_PS = 29`) and in FAAD2
([`neaacdec.h`](https://raw.githubusercontent.com/knik0/faad2/master/include/neaacdec.h):
`#define LC 2`, `#define HE_AAC 5`, plus `ps_present_flag`, `ps`).

### 2.2 What SBR does to the sample rate (the "2× slow" failure)

SBR is *not* a core coder: the bitstream carries a low-band AAC core plus a side
payload that describes the high band. The decoder must **reconstruct the high band and
emit PCM at twice the core sample rate**. FAAD2's public API encodes this reality: its
frame info reports `sbr` as `0: off, 1: on; upsample, 2: on; downsampled,
3: off; upsampled`, and the decoder configuration exposes a flag literally named
`dontUpSampleImplicitSBR` ([`neaacdec.h`](https://raw.githubusercontent.com/knik0/faad2/master/include/neaacdec.h)).
FDK AAC frames HE‑AAC as the mainstream codec for exactly these low rates:
"AAC's HE-AAC and HE-AAC v2 versions are regarded as today's most efficient general
perceptual audio codecs" ([`NOTICE`](https://raw.githubusercontent.com/mstorsjo/fdk-aac/master/NOTICE)).

Consequence for a player: a **plain AAC‑LC decoder** fed an HE‑AAC stream decodes only
the core. The core of a 44.1 kHz HE‑AAC stream is 22.05 kHz, so the PCM arrives **one
octave low and at half the sample rate**. Written to an I2S DAC clocked for 44.1 kHz it
plays **half-speed / slowed-down**; written to a 22.05 kHz DAC it plays correct pitch but
band-limited, *which is the "works but sounds muffled" variant of the same bug*.
(VERIFIED at the mechanism level: SBR upsample semantics + the 22.05 kHz core signal
rate are in the citations above; the specific audible artefact is **INFERENCE** — I
found no vendor document that phrases it as "2× slow".)

### 2.3 ADTS cannot signal SBR — it must be detected in-band

A live AAC stream is a bare sequence of **ADTS frames** — no container, no
`AudioSpecificConfig` (ASC) that a container would normally carry. The ADTS header layout,
verified field-by-field in
[FFmpeg `libavcodec/adts_header.c`](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/adts_header.c):

```
syncword                     12 bits    must be 0xFFF  (else AAC_PARSE_ERROR_SYNC)
ID (MPEG version)             1 bit
layer                         2 bits
protection_absent             1 bit     (0 = CRC present)
profile_objecttype            2 bits    -> object_type = value + 1
sample_frequency_index        4 bits    (0 = 96000 … 12 = 7350)
private_bit                   1 bit
channel_configuration         3 bits
original/copy, home           2 bits
copyright id bit/start        2 bits
aac_frame_length             13 bits    (whole frame incl. header)
adts_buffer_fullness         11 bits
number_of_raw_data_blocks     2 bits    -> samples = (rdb + 1) * 1024
CRC                           16 bits    only if protection_absent == 0
```

The decisive detail is the **2-bit `profile` field**: it can express only
Main / LC / SSR / LTP. There is **no way to signal SBR or PS in ADTS** (these are
MPEG‑4 constructs; ADTS is the MPEG‑2 AAC transport, reused by MPEG‑4/ADTS). Hence a
decoder must treat SBR/PS as *implicit*: FFmpeg models this explicitly as
`int sbr; ///< -1 implicit, 1 presence` and `int ps; ///< -1 implicit, 1 presence`
([`mpeg4audio.h`](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/mpeg4audio.h)),
and FAAD2 exposes `dontUpSampleImplicitSBR` precisely because an ADTS stream gives it no
other signal. Detection works by scanning the frame's fill/extension elements
(`EXT_SBR_DATA` / `EXT_SBR_DATA_CRC` for SBR, `EXTENSION_ID_PS` for PS).

**Requirements this imposes on the library's decoder interface:**

* the input path must be *push* bytes with a byte-consumed return, because ADTS framing
  (`aac_frame_length`, 13 bits) and garbage resync are the parser's job;
* the reported **output** sample rate must be the post-SBR rate (44100), and the
  component must be prepared for the *core* rate to be half that;
* resampling must react to a rate that is only known after the first frame (xtape's
  `mi_resample.c` already accepts an arbitrary in/out rate pair — keep that);
* mono→stereo promotion: HE‑AACv2 PS synthesises two channels from one, so the channel
  count can also change at stream start.

### 2.4 Which decoders get this right

* **Espressif `esp_audio_codec` AAC decoder** — "Supports AAC-LC, HE-AAC, HE-AACv2 …
  Supports decoding both with and without ADTS header … User can control AAC-Plus
  decoding behavior, to decrease cpu and memory usage if no need AAC-Plus decoding"
  ([README](https://github.com/espressif/esp-adf-libs/blob/master/esp_audio_codec/README.md)).
* **FAAD2** — "Freeware Advanced Audio (AAC) Decoder **including SBR decoding**"; handles
  `RAW/ADIF/ADTS/LATM` ([`neaacdec.h`](https://github.com/knik0/faad2/blob/master/include/neaacdec.h)).
* **fdk-aac** — HE‑AAC/v2 and xHE‑AAC; license is the blocker (§4).
* A **plain LC-only** decoder (e.g. Espressif's older ADF `aac_decoder` lineage, or a
  hand-rolled LC decoder) is disqualified for radio unless it is explicitly SBR-capable.

---

## 3. MP3 in a live stream

* **Layers.** Layer III is the norm; Layer I/II must still parse. minimp3 documents
  `MINIMP3_ONLY_MP3` as the switch that *strips MP1/MP2 code*, i.e. both are supported by
  default ([minimp3 README](https://github.com/lieff/minimp3/blob/master/README.md));
  ESP-IDF's simple decoder documents MP3 "Supports layer 1, 2, 3"; dr_mp3 reports
  `info->layer`.
* **Free-format bitrate.** Legal MP3 with no bitrate index; frame length must be inferred.
  minimp3: "For free format there minimum 3 frames needed to do proper sync: 2 frames to
  detect frame length and 1 next frame to check detect is good." dr_mp3 carries
  `DRMP3_MAX_FREE_FORMAT_FRAME_SIZE 2304 /* more than ISO spec's */` and a
  `free_format_bytes` field.
* **Tags in the stream.** A stream can begin with an **ID3v2** tag — `"ID3"`, 10-byte
  header, 4×7-bit size, skip-to-end-of-tag
  ([ID3v2.3.0](https://id3.org/id3v2.3.0)); some encoders insert ID3v2 tags *mid-stream*
  (typically carrying `TIT2`/`TRSN`), which is a resync event, not an error.
  **ID3v1/APE** tags sit at end-of-file and are irrelevant live, but dr_mp3 models them
  anyway (`DRMP3_METADATA_TYPE_ID3V1/ID3V2/APE/XING/VBRI`; `streamStartOffset` is
  documented as "used for skipping ID3v2 and VBR tags").
* **Xing/LAME header is first-frame only** (dr_mp3 exposes `DRMP3_METADATA_TYPE_XING`
  and `isVBR`/`isCBR`). On an infinite stream it is at best informational; never use it
  for duration/seeking. This is why dr_mp3's `drmp3_get_pcm_frame_count` is documented
  as unusable for internet radio.
* **Resync is mandatory, not optional.** Streams splice, ad-insert, and emit garbage.
  minimp3 "will analyze the input buffer to properly sync with the MP3 stream, and will
  skip ID3 data, as well as any data which is not valid", and recommends "as many as
  10 consecutive MP3 frames (~16 KB) in the input buffer at a time" (short buffers cause
  false sync and "squealing"). dr_mp3 implements `drmp3d_find_frame()` with
  `DRMP3_MAX_FRAME_SYNC_MATCHES 10` and returns the number of **bytes consumed**, so a
  ring buffer can discard exactly the right prefix.
* **Patents: expired.** "That day [2017‑04‑23], the last of the core mp3 patents which
  were part of the licensing program by Fraunhofer and Technicolor, expired", ending the
  program ([Fraunhofer IIS Audio Blog, 2017‑05‑18](https://www.audioblog.iis.fraunhofer.com/mp3-software-patents-licenses)).
  Same source's caveat, worth keeping: the program's end "does not automatically mean
  that all mp3 technology is available license-free" — implementation-specific patents
  may exist. **MP3 requires no royalty for a decoder today; AAC does (§4).**

---

## 4. Candidate decoders

### 4.1 Comparison

RAM/CPU figures marked *(Espressif)* are from Espressif's own table, measured on
**ESP32‑S3R8** in the `esp_audio_codec` README; "heap" excludes task stack.

| Library | Formats | License (exact) | RAM per instance | Push-bytes streaming API? | ESP-IDF integration | Maintained? |
|---|---|---|---|---|---|---|
| **`espressif/esp_audio_codec`** (in `esp-adf-libs`) | AAC‑LC/HE/HEv2, MP3 (L1‑3), FLAC, Vorbis, Opus, ALAC, AMR, ADPCM, G711, SBC, LC3, G722; simple decoder also parses AAC/MP3/WAV/FLAC/M4A/TS/OGG containers | **Espressif Modified MIT** (`LicenseRef-Espressif-Modified-MIT`): exclusive use with Espressif products; redistribution **for non‑Espressif products prohibited** | *(Espressif)* AAC 51.2 KB, MP3 28 KB, FLAC 89.4 KB, OPUS 26.6 KB; stack ~20 KB | **Yes** — dec = frame-boundary input; *simple decoder* accepts arbitrary-length input, "streaming decode only not support seek" | First-party: `idf_component.yml` v2.6.2, `idf >= 4.4`; **prebuilt static libs** per target incl. `lib/esp32s3` (source in `src/` is only the registration glue) | Active (repo pushed 2026‑10‑13) |
| **`espressif/esp-adf`** (framework) | wraps the above + services | repo reports `Other`/NOASSERTION (GitHub); per-component licenses vary | n/a | n/a | Yes, but it is a *framework*, not a library; `master` has been restructured into `esp_*_service` components — no `audio_pipeline` on master | Active (pushed 2026‑10‑16) |
| **dr_mp3** (`mackron/dr_libs`) | MP3 L1‑3, free format, ID3/APE/Xing metadata | **Public domain or MIT‑0** (dual, stated in header) | ~`drmp3dec` state: `float mdct_overlap[2][288]` + `qmf_state[960]` + `reserv_buf[511]` + scratch ≈ **~20 KB** (computed from the struct, not vendor-measured) | **Yes** at the low level: `drmp3dec_init`/`drmp3dec_decode_frame(dec, mp3, mp3_bytes, pcm, info)` — no seek. The *high-level* `drmp3` API wants read/seek/tell callbacks and is file-oriented | None official; single header, drop into a component | Mature/stable, low activity |
| **minimp3** (`lieff/minimp3`) | MP3 L1‑3, free format, skips ID3 | **CC0‑1.0** | smallest of the MP3 options (~70 KB source, ~30 KB binary); fixed static struct `mp3dec_t` | **Yes** — one frame per call from a caller buffer; returns `frame_bytes` consumed | None official; single header | Active-ish (pushed 2026‑07‑27) |
| **libmad** | MP3 L1‑3 | **GPLv2** | fixed-point, ~few tens of KB | Requires its own input/callback plumbing; not a push API | None official | Effectively frozen |
| **Helix MP3** (RealNetworks, various ports) | MP3 | RPSL / RealNetworks (per-port files differ — **check the port you use**) | small, fixed-point, designed for ARM | frame-at-a-time | Ports exist (`pschatzmann/arduino-libhelix` — **not fetched, NOT VERIFIED**) | ports vary |
| **FAAD2** (`knik0/faad2`) | AAC: Main/LC/SSR/LTP + **HE‑AAC (SBR) + HE‑AACv2 (PS)**; `RAW/ADIF/ADTS/LATM` | **GPLv2‑or‑later**, and explicitly: "**Any non‑GPL usage of this software or parts of this software is strictly forbidden.**" (`neaacdec.h`; repo `COPYING` is the 18 KB GPLv2 text) | not documented; SBR+PS state dominates | **Yes** — `NeAACDecInit` on a buffer (auto-detects ADTS), then `NeAACDecDecode(h, &info, buf, len)`; `info.bytesconsumed`; header-type enum includes ADTS | None first-party; vendored by ESP32‑audioI2S | Active (pushed 2026‑10‑01) |
| **fdk-aac** (`mstorsjo/fdk-aac`) | AAC‑LC/HE/HEv2/ELD/xHE‑AAC, ADTS/raw/LATM | **Fraunhofer FDK AAC Codec Library for Android** license: BSD-ish copyright grant, but "**NO EXPRESS OR IMPLIED LICENSES TO ANY PATENT CLAIMS … ARE GRANTED**"; "You may use this … only for purposes that are authorized by appropriate patent licenses"; binary redistribution must ship/ offer the **complete source**; modified versions must be renamed | large (not embedded-tuned) | Yes (raw/ADTS byte input) | None official | Active |
| **libopus** (+ `opusfile`) | Opus (Ogg or raw packets) | **BSD‑3‑Clause** (Xiph), plus royalty-free patent grants listed in `COPYING` (Xiph/Microsoft/Broadcom IPR declarations) | libopus decoder ≈ few tens of KB (encoder-free build much smaller) | libopus: **yes** (`opus_decode` on a packet). `opusfile` handles Ogg framing and explicit unseekable streaming (its API documents `OP_HOLE`/`OP_EBADLINK` "unseekable, chained streams", recommends ≥120 ms buffers) | None official; build as a component | Active |
| **libvorbis / tremor** | Vorbis | BSD‑3‑Clause (Xiph) | tremor is the fixed-point embedded variant | Vorbis needs Ogg demux first; packet-level decode is push-capable | None first-party | Frozen (Xiph) |
| **FFmpeg** (`aac`, `mp3` decoders) | everything | **LGPL‑2.1‑or‑later** (stated in every file header, e.g. `adts_header.c`) | desktop-scale; not an ESP32 target | libavcodec is send/receive push-capable | Not used on ESP32 | Active |

Answers to the three questions the axis explicitly asks:

* **A. HE‑AAC capable:** `esp_audio_codec` AAC (**verified in its README**), FAAD2
  (**verified**), fdk-aac (**verified**). Not HE‑AAC: dr_mp3/minimp3/libmad/Helix MP3
  (MP3 only).
* **B. GPL / restrictive:** **FAAD2 = GPLv2+** (hard blocker for a vendorable library);
  **ESP32‑audioI2S = GPLv3** and **YoRadio = GPL‑3.0** (whole projects, §6);
  **fdk-aac** is permissive copyright-wise but imposes source-availability on binary
  redistribution and grants **no patent license**; **`esp_audio_codec` is Espressif-only**
  (fine inside an ESP32‑S3 component, hostile to "others will vendor this" portability).
* **C. Viable for pushed bytes with no seek:** dr_mp3 (low-level API), minimp3,
  `esp_audio_codec` (simple decoder, no seek by design), FAAD2 (`NeAACDecDecode`),
  fdk-aac, libopus/opusfile.

### 4.2 The licensing trap, stated plainly

**MP3 is free; AAC is not.** No MP3 royalty applies since the last core patent expired in
2017. AAC is patent-encumbered *regardless of which decoder implementation you choose*:
fdk-aac's own license says patent licenses "may be obtained through Via Licensing"
([NOTICE](https://raw.githubusercontent.com/mstorsjo/fdk-aac/master/NOTICE)), and the DRM
Consortium's IPR page confirms the mechanism ("The AAC Pool License … applies to **both
encoder and decoder implementations**" and covers HE‑AACv2/xHE‑AAC) with a published
per-device order of magnitude of "around 1.10 USD … or around 0.76 USD as a special price
for emerging markets" for the total DRM technology royalty, part of which is the audio
codec ([drm.org IPR page](https://www.drm.org/drm-technology-and-logo-intellectual-property-rights-ipr/),
data "as of March 2025"). ~~The **AAC patent pool rate card itself could not be loaded**
(via-la.com licensing pages render client-side and returned no rates) — treat exact
per-unit AAC fees as **UNVERIFIED**.~~ **CORRECTED 2026-10-16:** the Via LA AAC page *does*
render the rate card server-side ($0.98/unit for the first 500,000 units, declining to
$0.10; $15,000 initial fee, or $1,000 for small entities; >2-channel products count 1.5
units). See [`server/s1-aac-licensing.md`](server/s1-aac-licensing.md) §2.2, which carries
the verbatim table and the "components or implementations are not covered" clause that
matters for a library.

So the trap has two layers, and they are independent:

1. **Copyright/software license** — FAAD2 (GPLv2) and ESP32‑audioI2S (GPLv3) cannot be
   vendored into a library that others redistribute under another license. `esp_audio_codec`
   can be *depended on* but not *bundled* for non-Espressif use.
2. **Patent license** — shipping an AAC **decoder** in a product requires an AAC pool
   license (or a licensed component). No open-source AAC decoder license changes that;
   fdk-aac says so explicitly.

The escape hatch is architectural: make the decoder a **pluggable backend** with a
permissive default (MP3), and treat AAC as an opt-in dependency of the consumer. The
sibling packaging doc reaches the same conclusion from the registry side
(`02-packaging-and-licensing.md`).

**Server-direction asymmetry (not covered above).** This section is the *player* answer,
and a player is decode-only and must play incoming HE-AAC streams, so it is forced to
solve the decode question. A **PCM-serving server is not**: see
[`server/s1-aac-licensing.md`](server/s1-aac-licensing.md) §5. It emits raw PCM in WAV
(no encoder exists in its pipeline), so MP3/FLAC/WAV is a zero-royalty design and AAC
does not enter at all unless the author deliberately adds an AAC decoder for `.aac`/`.m4a`
SD files or an encoder for a compressed mount. That note also re-verifies the encoder-side
FOSS licences (fdk-aac, FFmpeg `aacenc`, vo-aacenc, faac) against the decoder-only table
above, and establishes that the AAC per-unit fee applies to encoder-only, decoder-only and
combined products alike.

---

## 5. ESP32‑S3 budget: what is actually left for a decoder

### 5.1 Chip facts (VERIFIED)

Espressif's own product page: "ESP32-S3 is a dual-core XTensa LX7 MCU, capable of running
at 240 MHz. Apart from its **512 KB of internal SRAM** … It has 45 programmable GPIOs …
supports larger, high-speed octal SPI flash, and PSRAM with configurable data and
instruction cache"
([espressif.com/en/products/socs/esp32-s3](https://www.espressif.com/en/products/socs/esp32-s3)).
ESP-IDF adds: "ESP32-S3 has a few hundred kilobytes of internal RAM", up to **32 MB** of
PSRAM virtual address space shared with flash instructions/rodata
([Support for External RAM](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/external-ram.html)).
(528 KB − reserved/ROM-mapped; the "512 KB" is the headline and the usable heap after
Wi-Fi is much smaller — see below.)

### 5.2 Code placement: flash is normal, IRAM is a scarce resource

ESP-IDF's model: everything not explicitly placed in IRAM/RTC is executed from flash via
the MMU cache, and "**accessing cached flash data is as fast as accessing other types of
internal memory**"; "As IRAM is limited, most of an application's binary code must be
placed into IROM instead." IRAM is only needed for ISRs registered with
`ESP_INTR_FLAG_IRAM`, flash-safe code, and demonstrably timing-critical functions
([Memory Types](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/memory-types.html)).

Applied here: **the decoder goes in flash** (IROM, cached, ~72 MHz+ effective at 240 MHz
core with a good hit rate). IRAM belongs to the I2S/DMA ISR and the stream-fed ring-buffer
glue. Micro-optimising the MDCT with `IRAM_ATTR` is a waste of the scarcest memory in the
system unless a measurement proves otherwise.

### 5.3 PSRAM: usable for buffers, dangerous for decoder scratch

ESP-IDF documents the constraints that matter here
([external-ram](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/external-ram.html)):

* "When flash cache is disabled (for example, if the flash is being written to), the
  external RAM also becomes inaccessible. Any read operations from or write operations to
  it will lead to an illegal cache access exception."
* "The bandwidth that DMA accesses external RAM is very limited, especially when the core
  is trying to access the external RAM at the same time"; DMA descriptors cannot live
  there.
* "when accessing large chunks of data (> 32 KB), the cache can be insufficient, and
  speeds will fall back to the access speed of the external RAM. Moreover, accessing large
  chunks of data can 'push out' cached flash, possibly making the execution of code slower
  afterwards."
* Octal PSRAM can be clocked to 120 MHz (`CONFIG_SPIRAM_SPEED`); XiP-from-PSRAM notes that
  *octal PSRAM at 80 MHz is faster than quad flash at 80 MHz*.

Guidance for this component (INFERENCE from the above, plus one measured anchor):
put the **HTTP/stream ring buffer and any large const tables in PSRAM** (bulk sequential
access, exactly the case PSRAM is for), and keep the **decoder instance, its scratch, and
I2S DMA buffers in internal SRAM** — decoder state is small, hot, hard-random-access, and
must not be mid-decode when NVS/OTA disables the cache. No ESP32‑S3 measurement of "AAC
decode with scratch in PSRAM" was found: **UNVERIFIED**, assume 2–4× slower and don't do it.

### 5.4 Working budget (anchor = Espressif's measured figures)

| Consumer | Cost | Source |
|---|---|---|
| AAC decode, 48 kHz stereo | **51.2 KB heap**, 6.75 % CPU | *(Espressif)* README table |
| MP3 decode, 44.1 kHz stereo | **28 KB heap**, 8.17 % CPU | *(Espressif)* |
| Opus decode, 48 kHz stereo | **26.6 KB heap**, 5.86 % CPU | *(Espressif)* |
| FLAC decode, 44.1 kHz stereo | 89.4 KB heap, 8.0 % CPU | *(Espressif)* |
| Decoder task stack | **~20 KB** ("To support all decoders") | *(Espressif)* |
| AAC‑Plus (HE‑AAC) decode | "higher memory and CPU usage" than the AAC‑LC figures above — **no number published** | *(Espressif)* note 2 |
| Wi‑Fi + lwIP + I2S DMA + app | **not measured here** — INFERENCE: plan on 150–250 KB of the 512 KB before the decoder | — |

Interpretation: the Espressif CPU percentages are a fraction of **one 240 MHz core**, so
AAC‑Plus at, say, 2–3× AAC‑LC is still ~15–20 % of a core — i.e. a decoder is affordable
if it owns one core's headroom, and the real risk is **not** CPU but the *sum of heap*:
Wi‑Fi + decoder + 20 KB stack + 16 KB-ish MP3 sync buffer + I2S DMA, on a part whose
useful internal heap is a few hundred KB. Concretely: **budget ~60–120 KB of internal heap
for one active decoder plus its input buffer, and require PSRAM for anything above that.**

---

## 6. FLAC / WAV / Ogg / Opus and the now-playing problem

**WAV: no role.** Uncompressed 44.1 kHz stereo = 1.41 Mbit/s; it is a file/loopback
format, not a radio format. Do not implement it in the player path (it is in xtape only
because xtape *serves* files).

**FLAC: a quality tier, not a radio format.** Radio Paradise offers it to capable clients
(forum quote in §1), and it costs 89.4 KB of heap plus ~700–1400 kbit/s of Wi‑Fi
bandwidth — a poor trade on an ESP32 radio. Optional, last.

**Ogg/Vorbis/Opus: technically fine, metadata-poor.** Opus is genuinely attractive on
merit (best quality at 32–96 kbps, BSD‑3, royalty-free patent grants, tiny decode cost).
But Ogg is a page/packet container, so a player needs an Ogg demuxer (that is what
`opusfile` is: libopus + Ogg, with documented support for **unseekable** streams and
`OP_HOLE` handling for page loss). Crucially, **ICY now-playing does not exist for it**:

> "If there is metadata in the FLAC or Opus (Ogg) stream, **Icecast will not modify it**.
> Icecast's update interface is only implemented for MP3, AAC and possibly Vorbis streams,
> as metadata updates for MP3 and AAC are not possible except as out-of-band ICY updates."
> — Paul Martin (Icecast), [icecast mailing list, 2019‑08‑14](http://lists.xiph.org/pipermail/icecast/2019-August/014739.html)

The same thread notes clients that *expect* ICY treat an in-stream Ogg metadata change
(usually a new stream serial number) as end-of-stream. So for an Ogg-family stream a
player gets now-playing information **only** by (a) parsing the Ogg comment header once at
connect time (Vorbis comments — station-level info, not per-track), (b) polling a
side-channel HTTP/JSON metadata endpoint if the station exposes one (Radio Paradise does;
most do not), or (c) reading the station's Icecast `/status-json.xsl` or admin interface
(per *stream*, not per track, and not always reachable). This is exactly why
[`resources/mini_icecast.c`](../resources/mini_icecast.c) exists in xtape: ICY emission is
an MP3/AAC feature, implemented by the *server*, and the player side of it is a documented
ICY metadata protocol (fixed `icy-metaint` interval, length-prefixed `StreamTitle`) — the
sibling `03-transport-verified-locally.md` covers the transport half.

Also worth noting for scope: the **BBC no longer offers ICY at all** (HLS/DASH only, §1),
so a "complete" library that wants BBC needs HLS segmenting + HE‑AAC, or it composes
Espressif's `esp_hls_stream` (see `02-packaging-and-licensing.md` §1).

---

## 7. Existing ESP32 internet-radio projects

| Project | Decoders it actually wraps | License | Plausible vendoring target? |
|---|---|---|---|
| [Schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | Own `src/*_decoder/` trees: `mp3_decoder`, `aac_decoder` (**`libfaad`** inside), `flac_decoder`, `opus_decoder`, `vorbis_decoder`, `wav_decoder` (verified via GitHub contents API); README: "Supported codecs: wav, mp3, aac, flac, opus, vorbis", containers "ogg, mp4, m3u8", "Plays also icy-streams", and it is **multi-core only** ("only works on multi-core chips like ESP32-S3 … does not work on the ESP32-S2, ESP32-C3") | **GPLv3** (`LICENSE` is the verbatim GPLv3 text) — and its AAC path is FAAD2, itself GPLv2 | **No.** GPLv3 is incompatible with a library others vendor under a permissive license. Excellent *reference* for ICY handling, I2S plumbing and PSRAM use (`psram_unique_ptr.hpp`). |
| [e2002/yoradio](https://github.com/e2002/yoradio) | "Web-radio **based on ESP32-audioI2S library**" (repo description) — i.e. inherits the same FAAD2-based decode set | **GPL‑3.0** (GitHub license API) | **No** for the same reason. Useful for UX, playlist, and web-config patterns. |
| [espressif/esp-adf](https://github.com/espressif/esp-adf) | Wraps `esp_audio_codec` etc. from `esp-adf-libs`; the classic `audio_pipeline`/`audio_board` components are **not** in the `components/` listing on `master` (which now contains `esp_*_service` components) | repo-level license reported by GitHub as `Other`/NOASSERTION; per-component licenses vary | **No as a whole**; the individual `esp-adf-libs` components are the real dependencies. |
| [espressif/esp-adf-libs](https://github.com/espressif/esp-adf-libs) → [`esp_audio_codec`](https://github.com/espressif/esp-adf-libs/tree/master/esp_audio_codec) | The actual decoder library: AAC/MP3/FLAC/Vorbis/Opus/ALAC/AMR/… decoders + a container-parsing "simple decoder", shipped as **prebuilt libs** per target | **Espressif Modified MIT** (`esp_audio_codec/LICENSE`) | **As a dependency, yes; as vendored source, no** (clause 3). Optionally require it via the registry. |
| **ka-radio** | — | — | **NOT VERIFIED.** I could not locate a repository that owns this name within this pass; no primary source found. Do not cite it. |

---

## 8. VERIFIED vs UNVERIFIED

### VERIFIED (primary source read in this pass)

1. ESP32‑S3 = dual-core Xtensa LX7 @ 240 MHz, **512 KB internal SRAM**, optional PSRAM
   with configurable data/instruction cache — Espressif product page.
2. ESP32‑S3 external RAM constraints: cache-disabled inaccessibility, limited DMA
   bandwidth, >32 KB cache fallback, 120 MHz octal option, XiP notes — ESP-IDF
   *Support for External RAM*.
3. IRAM vs IROM execution model and the "cached flash ≈ internal memory" claim — ESP-IDF
   *Memory Types*.
4. dr_mp3 is **public domain or MIT‑0**, has a low-level **push** API
   (`drmp3dec_decode_frame`), free-format support, ID3/APE/Xing metadata typing, and
   documents frame-count APIs as unusable for internet radio — `dr_mp3.h`.
5. minimp3 is **CC0‑1.0**, decodes one frame per call from a caller buffer, skips ID3 and
   invalid data, wants ~10 frames (~16 KB) buffered, needs ≥3 frames for free-format
   sync, supports MP1/MP2 unless `MINIMP3_ONLY_MP3` — minimp3 README + repo license.
6. ADTS field-by-field layout, `0xFFF` syncword requirement, 13-bit `aac_frame_length`,
   and the 2-bit profile field — FFmpeg `adts_header.c`.
7. "Implicit" SBR and PS signaling as a decoder concept — FFmpeg `mpeg4audio.h`
   (`sbr; -1 implicit`, `ps; -1 implicit`), plus `AOT_SBR=5`, `AOT_PS=29`.
8. FAAD2 is **GPLv2-or-later** with "any non-GPL usage … strictly forbidden"; it supports
   HE‑AAC/SBR (and PS), ADTS/RAW/ADIF/LATM, has `dontUpSampleImplicitSBR`, and reports SBR
   upsample state — `neaacdec.h` + repo `COPYING`.
9. fdk-aac's license grants **no patent license** and permits use "only for purposes that
   are authorized by appropriate patent licenses"; binary redistribution requires making
   the complete source available; modified versions must be renamed — `NOTICE`.
10. `esp_audio_codec` AAC decoder supports **AAC‑LC, HE‑AAC, HE‑AACv2**, with and without
    ADTS; simple decoder accepts arbitrary-length input, streaming only, no seek; measured
    heap/CPU on ESP32‑S3R8 (AAC 51.2 KB/6.75 %, MP3 28 KB/8.17 %, OPUS 26.6 KB/5.86 %,
    FLAC 89.4 KB/8.0 %; ~20 KB stack); AAC‑Plus costs more, unquantified; MP3 simple
    decoder supports layers 1‑3, and the M4A/OGG containers are parsed — README +
    `idf_component.yml` (v2.6.2).
11. `esp_audio_codec` is **Espressif Modified MIT** (exclusive to Espressif products;
    redistribution for non-Espressif products prohibited) — `LICENSE`.
12. The decoders ship as **prebuilt per-target static libraries** (`lib/esp32s3`, …) with
    only registration glue in `src/` — GitHub contents listings.
13. **ESP32-audioI2S is GPLv3** and its AAC path is **libfaad**; it declares S3-class
    multi-core chips only — `LICENSE`, README, contents listings.
14. **YoRadio is GPL‑3.0** and is explicitly built on ESP32-audioI2S — repo API.
15. BBC Audio Factory ships AAC‑LC 320/128 and HE‑AACv1 96/48 over HLS/MPEG‑DASH and has
    dropped ICY for domestic services — BBC Sounds help page.
16. SomaFM offers, per channel, AAC 128, MP3 128, AAC‑HE 64, AAC‑HE 32 — SomaFM direct
    stream links page.
17. A live Radio Paradise stream is **HE‑AACv2 @ 44.1 kHz, 63 kb/s**, with ICY metadata
    present; RP staff confirms the 64k cap and a FLAC tier for capable clients — RP forum
    (2025‑02).
18. Icecast does **not** rewrite metadata for FLAC/Opus (Ogg) streams; its update interface
    is MP3/AAC/(maybe Vorbis); MP3/AAC metadata is out-of-band ICY — Icecast mailing list
    (project developer).
19. The Icecast public directory is **empty** (`<directory></directory>`), so no codec
    census is available from it.
20. libopus is **BSD‑3‑Clause** with royalty-free patent grants (Xiph/Microsoft/Broadcom
    IPR declarations); `opusfile` supports **unseekable, chained streams** and recommends
    ≥120 ms buffers — `COPYING` + opusfile API docs.
21. The last core MP3 patent in the Fraunhofer/Technicolor program expired **2017‑04‑23**,
    ending the licensing program; implementation-specific patents may remain — Fraunhofer
    IIS Audio Blog.
22. DRM Consortium IPR page: the **AAC pool license applies to decoders as well as encoders** and
    covers HE‑AACv2/xHE‑AAC; order-of-magnitude per-device royalty ~1.10 USD (~0.76 USD
    emerging markets) for the total DRM technology royalty — drm.org.

### UNVERIFIED / LIKELY (inference, or source not obtainable)

1. **Any percentage distribution** of codecs across internet radio (MP3 vs AAC vs Opus …).
   No primary dataset exists that I could reach; the Icecast directory is empty. The §1
   characterisation is from named stations, not a census.
2. **The audible "2× slow / half-speed" symptom wording** for LC-only decoders on HE‑AAC.
   Mechanism is verified; the exact artefact is inference.
3. **Exact AAC patent-pool per-unit fees** for a decoder. via-la.com's licensing pages are
   client-rendered and returned no rate card; only the DRM page's derived figures exist.
4. **HE‑AAC (AAC‑Plus) heap/CPU on ESP32‑S3.** Espressif states it is higher than the
   AAC‑LC numbers but publishes no value.
5. **Decoder-scratch-in-PSRAM performance.** No measurement found. Espressif's
   general PSRAM guidance suggests "don't".
6. **`arduino-libhelix` / other Helix ports** — repository, license and API not fetched.
   Helix licensing is per-port; must be checked before use.
7. **ka-radio** — no repository located; no license or decoder facts.
8. **Vorbis/Tremor and FLAC decoder memory** for libvorbis/dr_flac (only the Espressif
   FLAC figure, 89.4 KB, was measured by a first party).
9. **Wi‑Fi/lwIP/I2S heap consumption** on ESP32‑S3 — no primary figure cited here; the
   150–250 KB planning number is a placeholder, not a measurement.
10. **ESP-ADF branch topology** (which branch still carries the classic `audio_pipeline`
    components) — not verified; only that `master`'s `components/` listing lacks them.

---

## 9. RECOMMENDATION

### Minimal viable codec set (ship this first)

**MP3 (Layer I/II/III, free-format, ID3/APE skip, in-stream resync) + AAC (ADTS, AAC‑LC and
HE‑AAC v1/v2 with SBR/PS).** Nothing else. That single pair covers the BBC's 96/48 kbps
HE‑AACv1 tiers (once an HLS segmenter feeds it), SomaFM's four streams, Radio Paradise's
64k HE‑AACv2 stream, and the great majority of Icecast/SHOUTcast endpoints. It is also
*exactly* the pair the ADTS/ICY path needs — no container parser required.

Decoder choice for the MVP, given both ends of the trade-off:

* **Default: depend on `espressif/esp_audio_codec` (registry, v2.6.x, `idf >= 4.4`).**
  One dependency buys MP3 + correctly-handled HE‑AAC/HE‑AACv2 + a "simple decoder" that
  takes arbitrary-length pushes and does the ADTS/MP3 framing for you, at 51 KB / 28 KB
  heap and <10 % of one core, with prebuilt `esp32s3` libs Espressif maintains. Its
  Espressif-only license is *satisfied by construction* on an ESP32‑S3 target, and the
  sibling packaging doc already requires it to stay an **external, optional** dependency
  rather than a vendored tree — which also keeps the prebuilt-blob problem out of your
  source tree.
* **Fallback: `dr_mp3` (public domain / MIT‑0) as the permissive MP3-only backend.**
  It is the only MP3 option that is simultaneously (a) permissively licensed for
  vendoring, (b) explicitly push-shaped at the low level, and (c) still maintained. This
  is what makes the "others will vendor this" story true for the MP3 case.
* **Do not** build the MVP around FAAD2 or ESP32-audioI2S: GPLv2/GPLv3 respectively, so
  anyone who vendors your library inherits copyleft. Do not build it around fdk-aac: its
  license grants no patent rights and imposes source-availability on binary shipments,
  and it is oversized for an MCU anyway.

Design consequences to bake into the interface now (they come straight from §2):

1. `feed(const uint8_t*, size_t) -> bytes consumed` (push), not `read(frames)`.
2. Decoder reports `{sample_rate, channels}` **after** the first frame; the component must
   re-clock I2S and reconfigure the resampler when it learns the real rate (22.05 kHz core
   vs 44.1 kHz SBR output is the normal case, not an edge case).
3. Never assume channel count: HE‑AACv2 PS and mono streams both exist.
4. A decoder-backend seam (`decoder_ops` struct) with `esp_audio_codec` and `dr_mp3`
   implementations behind it — this is what makes the optional/patent-encumbered path
   separable.

### Complete codec set (later, only with demand)

Add, in this order:

1. **Opus** via **libopus + `opusfile`** (BSD‑3, royalty-free patents, ~27 KB, 5.9 % CPU,
   and opusfile explicitly handles unseekable/chained Ogg). Accept up front that
   **now-playing metadata will be missing for these streams** — document it, and expose a
   hook so an application can poll a station-specific JSON endpoint. Ogg/Ogg-page
   parsing is the entire added cost.
2. **Vorbis** (tremor, BSD‑3) — same Ogg machinery as Opus, so it is nearly free once (1)
   exists; only worth it for legacy stations.
3. **FLAC** (`dr_flac`, public domain/MIT‑0, or the Espressif decoder) — optional quality
   tier; 89.4 KB heap and up to ~1.4 Mbit/s of Wi‑Fi. Lowest priority.
4. **WAV — do not implement.** Uncompressed audio is not a radio stream; it belongs to
   xtape's server role only.
5. **Layer II** needs no work beyond choosing a decoder that already lists it
   (`esp_audio_codec` MP3 simple decoder: "Supports layer 1, 2, 3") — it is covered by the
   MVP.

If BBC-style content becomes a requirement, that is not a codec problem but a **transport**
one (HLS/MPEG‑DASH + HE‑AAC): compose Espressif's `esp_hls_stream` rather than writing a
segmenter, and keep it out of the core codec set.

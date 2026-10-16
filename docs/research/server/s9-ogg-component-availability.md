# Ogg/Opus component availability — verified directly from the registry (2026-10-16)

These are checks I ran myself against the ESP Component Registry API and upstream source,
because component availability decides the whole output-format plan. Each entry is a fact
about what exists today, with the evidence.

Method: `curl "https://components.espressif.com/api/components?q=<term>"` and raw file reads
from the referenced repositories.

## The headline: an Ogg muxer DOES exist, under a permissive licence

An earlier note (`s2-encoder-feasibility.md`) concluded that adopting Opus means "writing or
vendoring an Ogg muxer" because `esp_audio_codec` has none. That is out of date as a
*availability* statement — two Ogg options exist in the registry:

### `georgik/ogg` — v1.3.5, **BSD-3-Clause**, the permissive path

- Registry: `georgik/ogg`, version `1.3.5~1`, licence `BSD-3-Clause license`,
  description "Ogg bitstream (ESP-IDF component)", dependency `idf: >=5.0.0`.
- Its `.gitmodules` is decisive about what it actually is:
  ```
  [submodule "ogg"]
      path = ogg
      url = https://github.com/xiph/ogg.git
  ```
  So this component is a thin IDF build wrapper (a `CMakeLists.txt`, an `idf_component.yml`,
  an `include/ogg/` shim) around **upstream `xiph/ogg` (libogg)**. Users get libogg, not a
  reimplementation.
- **Why that gives the muxer:** upstream libogg contains the *encoding* half of the Ogg
  layer, not just the parser — `ogg_stream_init`, `ogg_stream_packetin`,
  `ogg_stream_pageout`, `ogg_stream_flush`, and `ogg_page_checksum_set` are all part of
  libogg's public API. A component that builds upstream libogg therefore provides page
  generation, and the project only has to write the Opus-specific packetisation on top
  (`OpusHead`/`OpusTags`, granule positions), per RFC 7845.
- Caveat to check at integration time: whether the wrapper's `CMakeLists.txt` compiles the
  full libogg source set or only a decode subset. Reading the repo listing shows
  `CMakeLists.txt` + `include/` + the `ogg` submodule and no vendored copy of the sources,
  which is consistent with "builds upstream" — but this should be confirmed by actually
  building it, since the submodule contents were not fetched here.

### `espressif/esp_muxer` — v1.2.3, **Espressif Modified MIT**, the turnkey path

- Registry: `espressif/esp_muxer` v1.2.3, licence `Custom`, description "Espressif Muxer to
  mux audio and video stream data into container", repository `espressif/esp-adf-libs`.
- Its README carries the supported-container/codec matrix, which is **exactly** the pairing
  this project needs:

  |       | MP4 | TS | FLV | WAV | CAF | **OGG** | AVI |
  |-------|-----|----|-----|-----|-----|---------|-----|
  | MP3   | Y   | Y  | Y   | Y   | N   | **N**   | Y   |
  | OPUS  | N   | N  | N   | N   | N   | **Y**   | N   |

  Two conclusions from that one table:
  1. **Opus-in-Ogg is supported out of the box** (`ESP_MUXER_TYPE_OGG` +
     `ESP_MUXER_ADEC_OPUS` — both symbols confirmed in `include/esp_muxer.h`).
  2. **MP3-in-Ogg is `N`.** So MP3 output cannot be Ogg-encapsulated here, which matches the
     wider finding that MP3 is served as a bare frame stream with ICY metadata.
- **Licence caveat:** its `LICENSE` is the *Espressif Modified MIT License*, i.e. the same
  restricted terms as `esp_audio_codec` — "MUST BE USED IN CONJUNCTION WITH ESPRESSIF
  SYSTEMS PRODUCTS" and "Redistribution … FOR USE WITH NON-ESPRESSIF PRODUCTS is strictly
  prohibited". Usable as a registry dependency for an ESP32-S3 project; **not vendorable**
  into a library that others redistribute for other targets.

**So the recommendation depends on the licence posture chosen:** `esp_muxer` is the least
code, `georgik/ogg` (BSD-3, upstream libogg) is the portable one. Because the project already
decided that Espressif-licensed components may be *depended on* but not *vendored*, the
clean split is `georgik/ogg` for the core and `esp_muxer` as an optional accelerator.

## Decoders: a complete permissive family already exists (`esphome-libs/micro-*`)

The registry contains a coherent set under namespace `esphome`, all Apache-2.0 (one
dual-licensed), which together cover the entire requested **input** list:

| Component | Version | Licence | Covers |
|---|---|---|---|
| `esphome/micro-decoder` | 0.4.0 | Apache-2.0 | umbrella: "C++ audio decoding library with HTTP/HTTPS streaming, supporting **FLAC, MP3, Opus, Vorbis, and WAV**" |
| `esphome/micro-vorbis` | 0.1.0 | Apache-2.0 **AND** BSD-3-Clause | "Ogg Vorbis audio decoder for ESP32 with **integer-only** decoding and PSRAM support" |
| `esphome/micro-opus` | 0.4.1 | Apache-2.0 | "Opus audio codec library for ESP32" |
| `esphome/micro-mp3` | 0.4.0 | Apache-2.0 | streaming fixed-point MP3 decoder, "frame synchronization, PSRAM-aware allocation, lazy init" |
| `esphome/micro-flac` | 0.3.0 | Apache-2.0 | FLAC decoder **and encoder**, Xtensa assembly optimizations |
| `esphome/micro-wav` | (dependency of micro-decoder) | — | WAV |

`esphome/micro-decoder` depends on `micro-flac`, `micro-mp3`, `micro-opus`, `micro-vorbis`,
`micro-wav`, and `idf: >=5.5`.

Three caveats that matter for *this* project:

1. **It is C++.** `micro-decoder` is described as a "C++ audio decoding library". A C library
   forcing C++ on every consumer is a real integration cost — the same objection recorded
   against `78/esp-opus-encoder`. The individual decoders may or may not be usable directly
   from C; that needs checking per component.
2. **It is a *client* decoder** ("with HTTP/HTTPS streaming") — built to pull a stream down,
   not to read files off an SD card and re-serve them. Whether it can decode from a file
   handle without the HTTP layer is unverified and is the key integration question.
3. **`micro-decoder` requires `idf >= 5.5`**, which is a stricter floor than the rest of the
   component (the sibling `georgik/ogg` needs only `>=5.0.0`). If the project targets
   IDF 5.4, it cannot use the umbrella component.

## What is still missing from the registry

Searches for **`webm`**, **`matroska`**, and **`ebml`** returned **no relevant components**.
So Matroska/EBML demuxing has no ready-made ESP-IDF component and must either be hand-written
(audio-only, seekable — far simpler than a general demuxer) or WebM dropped from the input
list. This is the single largest new piece of code implied by the expanded requirements.

## Encode side, for completeness

- **Opus encode:** `78/esp-opus-encoder` v2.4.1 (**MIT**, requires idf ≥5.3, depends on
  `78/esp-opus`) or `espressif/esp_audio_codec` (restricted licence, measured 24.9% CPU at
  complexity 0).
- **MP3 encode:** no encoder component found in the registry (`shine` search returned
  nothing relevant), so `shine` would be vendored. Note `esphome/micro-flac` is the only
  member of that family that also *encodes*.

## Reproduction

```bash
for q in ogg opus vorbis webm matroska ebml shine; do
  echo "--- $q"
  curl -s "https://components.espressif.com/api/components?q=$q" | python3 -m json.tool | grep -E '"name"|"license"'
done
curl -sL https://raw.githubusercontent.com/espressif/esp-adf-libs/master/esp_muxer/README.md
curl -sL https://raw.githubusercontent.com/georgik/esp-idf-component-ogg/master/.gitmodules
```

# S5 — Emitting a live, never-ending Ogg stream: what must be implemented

**Question:** the radio server (`resources/mini_icecast.c`) answers every client with an
endless HTTP body. If the station moves to Opus (or Vorbis/MP3), what exactly has to be built
to emit a *valid, live, never-ending* Ogg-encapsulated stream — and what are the hard
constraints? Read with [s2-encoder-feasibility.md](s2-encoder-feasibility.md) (encoder CPU,
licensing — **not re-researched here**), [s1-aac-licensing.md](s1-aac-licensing.md),
[s3-wire-format-and-clients.md](s3-wire-format-and-clients.md),
[s4-server-synthesis.md](s4-server-synthesis.md) and
[../radio-player-codecs.md](../radio-player-codecs.md).

**Method / date:** read RFC 3533 and RFC 7845 in full, the Vorbis I spec, libogg's
`src/framing.c` + `src/crctable.h`, Icecast 2.5.0 master, the WHATWG HTML spec, Chromium
media source, MDN, the ESP Component Registry API, ffmpeg's Ogg muxer/demuxer, and Espressif's
`esp_muxer`. All quotes are verbatim from those sources. The minimal page writer in
[s5-ogg-page-writer.c](s5-ogg-page-writer.c) is **executable and verified**:
`s5-ogg-verify.c` reproduces **27/27 page headers byte-for-byte** from libogg's own
hard-coded test vectors. 2026-10-16.

**Headline:** the container is small — **~130 lines of C, no dependency, no licence** — and
the format is fully specified for a live stream that never ends and never seeks. Three traps
account for almost every field bug and all three are confirmed against libogg by execution
(§8). The licence-clean path is to hand-roll; the only pre-existing ESP32 Ogg **muxer** is
Espressif's `esp_muxer`, which is a **closed-source binary blob** under a
"Espressif products only" licence (§4). **MP3 must not go in Ogg** (§5). Per-track "now
playing" *is* achievable inside the stream, and RFC 7845 names Internet radio when it says how
(§6).

---

## 1. The Ogg container, from the normative source

### 1.1 Page structure

[RFC 3533 §6](https://www.rfc-editor.org/rfc/rfc3533.txt) defines the page header. Field
offsets, all little-endian ("The LSb (least significant bit) comes first in the Bytes. Fields
with more than one byte length are encoded LSB (least significant byte) first"):

| Offset | Size | Field | Notes for a live writer |
|---|---|---|---|
| 0 | 4 | capture pattern | `"OggS"` = `0x4f 0x67 0x67 0x53` |
| 4 | 1 | `stream_structure_version` | **0**; `libogg` re-encodes any page with version > 0 |
| 5 | 1 | `header_type_flag` | bit 0x01 continued, 0x02 bos, 0x04 eos |
| 6 | 8 | `granule_position` | see §1.4; `-1` when no packet completes on the page |
| 14 | 4 | `bitstream_serial_number` | random; **unique per logical stream** |
| 18 | 4 | `page_sequence_number` | per logical stream, increments from 0 |
| 22 | 4 | `CRC_checksum` | computed with these 4 bytes **zeroed** |
| 26 | 1 | `number_page_segments` | `n` |
| 27 | `n` | `segment_table` | lacing values |

Sizes are fixed by the spec: "`header_size = number_page_segments + 27`" and
"`page_size = header_size + sum(lacing_values: 1..number_page_segments)`", with
"Pages are of variable size, usually 4-8 kB, maximum 65307 bytes". `65307 = 27 + 255 + 255×255`.

RFC 3533 §4 requires the header ordering that a muxer must honour:

> "Ogg also allows but does not require secondary header packets after the bos page for logical
> bitstreams and these must also precede any data packets in any logical bitstream. These
> subsequent header packets are framed into an integral number of pages, which will not contain
> any data packets."

### 1.2 The 255-byte lacing model

[RFC 3533 §5](https://www.rfc-editor.org/rfc/rfc3533.txt):

> "Ogg divides each packet into 255 byte long chunks plus a final shorter chunk. … Note that a
> lacing value of 255 implies that a second lacing value follows in the packet, and a value of
> less than 255 marks the end of the packet after that many additional bytes. **A packet of 255
> bytes (or a multiple of 255 bytes) is terminated by a lacing value of 0.** Note also that a
> 'nil' (zero length) packet is not an error; it consists of nothing more than a lacing value of
> zero in the header."

Consequences a page writer must implement:

- Lacing count for a packet of `len` bytes is `len/255 + 1` — an **exact multiple of 255 still
  gets the extra `0` terminator**. Getting this wrong silently drops the last byte of every
  such packet. (I hit this bug while writing the reference implementation; §8.)
- A page holds at most 255 lacing values, so a packet larger than 255×255 bytes must span pages.
- The "continued packet" flag is set on a page iff the page's **first** lacing value belongs to
  a packet that started on an earlier page. It is *not* "the previous page ended with 255".

### 1.3 The CRC — the classic bug

[RFC 3533 §6](https://www.rfc-editor.org/rfc/rfc3533.txt) states only:

> "CRC_checksum: a 4 Byte field containing a 32 bit CRC checksum of the page (including header
> with zero CRC field and page content). **The generator polynomial is 0x04c11db7.**"

The polynomial alone is insufficient — init value, input/output reflection and final XOR are
unspecified there. libogg's `src/framing.c` is the de-facto normative answer, and says so in
the code:

```c
polynomial = 0x04c11db7; /* The same as the ethernet generator
                            polynomial, although we use an
                            unreflected alg and an init/final
                            of 0, not 0xffffffff */
```

So: **poly `0x04c11db7`, MSB-first / unreflected, init `0`, final XOR `0`, CRC field zeroed
while computing, over `header || body` in that order.** libogg zeroes bytes 22–25
(`ogg_page_checksum_set`), CRCs the header then the body with the *same* running register
(`_os_update_crc` called twice), and writes the result little-endian at offset 22. This is
*not* Ethernet CRC-32 despite the shared polynomial — that one reflects and uses
`0xffffffff`/`^0xffffffff`. Verified: my implementation reproduces libogg's hard-coded
`head1_0` CRC `0x91eced15` and all 27 of its page vectors (§8).

### 1.4 Granule position on a live stream

RFC 3533 §4 is explicit that the container has no clock: "Ogg does not have a concept of
'time': it only knows about sequentially increasing, unitless position markers." The codec
mapping gives it meaning. The one container-level rule

> "A special value of -1 (in two's complement) indicates that **no packets finish on this
> page**."

is subtler than it reads, and the naive reading is wrong: it is *not* "the page ends on a
lacing value of 255". A page can begin with the tail of a long packet, have several packets
complete, and then begin another long packet. libogg implements the correct rule by tracking,
while scanning the page's segments, the granule of the **last lacing value < 255**; that is
the emitted `granulepos`, and it stays `-1` only if no lacing value < 255 occurs at all. I
reproduced both the wrong and the right versions and libogg's own vectors distinguish them
(§8, `head2_6`).

For a live, unseekable stream the granule is nonetheless *load-bearing*: RFC 7845 uses it to
express pre-skip and stream start (§2.3), and it is the only clock a demuxer has.

### 1.5 Chaining vs multiplexing — how "changing track" works

RFC 3533 §4 distinguishes the two multiplexing modes:

> "Ogg knows two types of multiplexing: concurrent multiplexing (so-called 'Grouping') and
> sequential multiplexing (so-called 'Chaining')."
>
> "**In chaining, complete logical bitstreams are concatenated. The bitstreams do not overlap,
> i.e., the eos page of a given logical bitstream is immediately followed by the bos page of
> the next. Each chained logical bitstream MUST have a unique serial number** within the scope
> of the physical bitstream."

So a radio server that changes track or format **chains**: terminate the current logical
stream with an eos page, then start a fresh BOS page sequence with a **new random serial
number** and a new set of header packets. Concretely for Opus that means a whole new
`OpusHead` + `OpusTags`, which is exactly what makes per-track metadata possible (§6). The
serial number must change; page sequence numbers restart at 0. Nothing else in the stream
needs rewriting — that is the point of chaining, and RFC 7845 §4.5 confirms it is designed for
this.

A **never-ending** stream has no eos page at all. That is legal and expected: RFC 7845 §3 —
"A logical stream ends with a page with the 'end of stream' flag set, but implementations need
to be prepared to deal with truncated streams that do not have a page marked 'end of stream'."

---

## 2. Opus in Ogg — RFC 7845

[RFC 7845](https://www.rfc-editor.org/rfc/rfc7845.txt) is a Standards Track document
(April 2016) and is the mapping to implement.

### 2.1 Two mandatory header packets, and where they go

RFC 7845 §3:

> "There are two mandatory header packets. The first packet in the logical Ogg bitstream MUST
> contain the identification (ID) header, which uniquely identifies a stream as Opus audio. …
> **It is placed alone (without any other packet data) on the first page of the logical Ogg
> bitstream and completes on that page. This page has its 'beginning of stream' flag set.**"
>
> "The second packet in the logical Ogg bitstream MUST contain the comment header … **It MAY
> span multiple pages, beginning on the second page of the logical stream. However many pages
> it spans, the comment header packet MUST finish the page on which it completes.**"

and, importantly for a live server, §3 also blesses headers-pasted-on-front:

> "The first audio data page could have the 'continued packet' flag set … if, for example, it
> was a live stream joined mid-broadcast, with the headers pasted on the front."

`OpusHead` layout (§5.1), 19 bytes for channel mapping family 0:

| Offset | Size | Field | Value a radio server should write |
|---|---|---|---|
| 0 | 8 | magic `"OpusHead"` | fixed |
| 8 | 1 | `Version` | **`1`** — "The version number MUST always be '1' for this version" |
| 9 | 1 | `Channel Count` | 1 or 2 for family 0; "This value MUST NOT be zero" |
| 10 | 2 | `Pre-skip` (LE) | see §2.3 |
| 12 | 4 | `Input Sample Rate` (LE) | the *original* rate, metadata only ("This field is _not_ the sample rate to use for playback"); `0` = "unspecified" |
| 16 | 2 | `Output Gain` (Q7.8, signed LE) | **0** — "A muxer SHOULD set this field to zero" |
| 18 | 1 | `Channel Mapping Family` | **0** for mono/stereo |

"All fields in the ID headers are REQUIRED, except for 'channel mapping table', which MUST be
omitted when the channel mapping family is 0" (§5.1), and family 0 is defined in §5.1.1.1:
"Allowed numbers of channels: 1 or 2. RTP mapping. … 1 channel: monophonic (mono). 2 channels:
stereo (left, right)."

`OpusTags` (§5.2) is: `"OpusTags"` (8 bytes) + **the Vorbis comment structure minus the
framing bit**:

> "The comment header consists of a 64-bit 'magic signature' field, followed by data in the
> same format as the [VORBIS-COMMENT] header used in Ogg Vorbis, **except (like Ogg Theora and
> Speex) the final 'framing bit' specified in the Vorbis specification is not present.**"

The Vorbis comment structure ([v-comment.html](https://xiph.org/vorbis/doc/v-comment.html))
is `vendor_length:u32le`, `vendor_string`, `user_comment_list_length:u32le`, then
`length:u32le` + UTF-8 string per comment, each `NAME=value`. For Opus **stop after the comment
list** — there is no framing bit. Adding one is a common porting mistake and makes the packet
one byte too long.

### 2.2 Granule position convention (the drift trap)

RFC 7845 §4, verbatim:

> "The granule position MUST be zero for the ID header page and the page where the comment
> header completes. That is, the first page in the logical stream and the last header page
> before the first audio data page both have a granule position of zero."
>
> "**The granule position of an audio data page is in units of PCM audio samples at a fixed
> rate of 48 kHz** (per channel; a stereo stream's granule position does not increment at twice
> the speed of a mono stream). It is possible to run an Opus decoder at other sampling rates,
> but all Opus packets encode samples at a sampling rate that evenly divides 48 kHz.
> **Therefore, the value in the granule position field always counts samples assuming a 48 kHz
> decoding rate**, and the rest of this specification makes the same assumption."

This is the classic A/V-drift source: **the granule counter is always 48 kHz**, regardless of
the encoder's input rate. A station fed 44.1 kHz PCM increments the granule by 960 per 20 ms
frame (`20 ms × 48000`), *not* by 882. §4.3 gives the conversion:
`'PCM sample position' = 'granule position' - 'pre-skip'`, and
`'playback time' = 'PCM sample position' / 48000`.

### 2.3 Pre-skip, and the live-stream case

§4.2: pre-skip is "the number of samples that SHOULD be skipped (decoded but discarded) at the
beginning of the stream", covering encoder lookahead. §7 gives the procedure for encoders
derived from the reference implementation:

> "the number of samples can be queried with
> `opus_encoder_ctl(encoder_state, OPUS_GET_LOOKAHEAD(&delay_samples));` … the implementation
> SHOULD set the 'pre-skip' header value to (delay_samples + extra_samples)"

For a live station, set `pre-skip = OPUS_GET_LOOKAHEAD` at encoder init and use it in every
chained `OpusHead`.

**What a live/unseekable stream puts in the initial granulepos** is answered by §4.5:

> "The granule position of the first audio data page with a completed packet MAY be larger than
> the number of samples contained in packets that complete on that page. However, it MUST NOT
> be smaller, unless that page has the 'end of stream' flag set. Allowing a granule position
> larger than the number of samples allows the beginning of a stream to be cropped or **a live
> stream to be joined without rewriting the granule position of all the remaining pages**."

Practical rule: start the first audio page's granule at `pre-skip` (or higher if you are
deliberately joining late) and add exactly the 48 kHz sample count of the packets that complete
on each subsequent page. §4 also warns the alternative is a stream error: "an implementation
MUST treat any stream as invalid if the granule position is smaller than the number of samples
contained in packets that complete on the first audio data page with a completed packet, unless
that page has the 'end of stream' flag set."

Non-monotonic granule is *not* an option even for a lossy live source — §4.1: "Implementations
that fail to do so still MUST NOT increment the granule position for a page by anything other
than the number of samples contained in packets that actually complete on that page."

### 2.4 Packet size limits

§6: "Demuxers SHOULD treat audio data packets as invalid … if they are larger than 61,440
octets per Opus stream." At 90 kbps a 20 ms packet is ~225 bytes, so this never binds for a
radio station; relevant only as a reminder that 61,440 is the practical ceiling.

---

## 3. Vorbis in Ogg — decode-side only

The Vorbis mapping lives in the [Vorbis I specification](https://xiph.org/vorbis/doc/Vorbis_I_spec.html),
§4.2 and Appendix A.

- Three header packets, not two: §4.2 — "A Vorbis bitstream begins with three header packets.
  The header packets are, in order, the identification header, the comments header, and the
  setup header." §4.2.1 gives the common prefix: a `packet_type` octet (1, 3, 5) followed by
  the six octets `"vorbis"`, and "The packets must occur in the order of identification,
  comment, setup."
- Appendix A.2 fixes the page layout with a concrete size: "The first Vorbis packet (the
  identification header) … is placed alone in the first page of the logical Ogg stream. **This
  results in a first Ogg page of exactly 58 bytes**" (= 27 header + 1 lacing + 30 bytes of
  `\x01vorbis` identification header). "The second and third vorbis packets (comment and setup
  headers) may span one or more pages beginning on the second page of the logical stream.
  However many pages they span, the third header packet finishes the page on which it ends.
  **The next (first audio) packet must begin on a fresh page.**"
- The comment header *does* carry a framing bit (v-comment.html steps 7–8), unlike Opus.
- Granule convention differs from Opus in one respect that matters: it is **in the stream's own
  sample rate**, not always 48 kHz — "The granule position of pages containing Vorbis audio is
  in units of PCM audio samples (per channel…)", and the trimming mechanism is different:
  "The granule (PCM) position of the first page need not indicate that the stream started at
  position zero."
- Appendix A.1.1 also notes the restriction a muxer must respect: "The Ogg stream must be
  unmultiplexed (only one stream, a Vorbis audio stream, per link)" and "The Ogg stream may be
  chained".

**How much of this applies to xtape: almost none.** The author wants Vorbis as an *input*
format, and [s2-encoder-feasibility.md §5](s2-encoder-feasibility.md) already establishes from
source that `libvorbis` is float-only and `tremor` is decode-only, so there is no Vorbis
encoder to write headers *for*. Everything in this section is therefore a **decoder-side**
requirement: the input path must demux three header packets, honour the `packet_type` ordering
and the framing bit, and treat granule as stream-rate PCM samples. The only thing that carries
over to output is the *shape* of the comment header (which Opus reuses, minus the framing bit)
and the chaining model. If Vorbis output is ever wanted, it needs a working encoder first —
which does not exist on this MCU.

---

## 4. Is there an Ogg implementation small enough to reuse?

### 4.1 libogg: small, BSD-3, already ported — but you get the decoder too

`xiph/ogg` `COPYING` is a 3-clause BSD-style licence from the Xiph.Org Foundation (verified:
`COPYING`, 1,466 bytes, "Redistribution and use in source and binary forms … are permitted
provided the following conditions are met"). No copyleft, no patent constraints claimed.

Size, measured from the master tree:

| File | Bytes | Lines | Needed for muxing? |
|---|---|---|---|
| `src/framing.c` | 66,904 | 2,138 | yes |
| `src/bitwise.c` | 30,633 | 1,089 | yes (packet bit I/O) |
| `src/crctable.h` | 23,902 | 278 | yes (8×256 CRC table) |
| `include/ogg/ogg.h` | 8,373 | 209 | yes |
| `include/ogg/os_types.h` | 4,781 | 158 | yes |
| `include/ogg/config_types.h.in` | 600 | 26 | yes (must be generated/ported) |

Compiled for a **host** (`cc -Os`, x86-64) as a portability proxy: `framing.o` text section
12,957 bytes + `bitwise.o` 3,780 bytes ≈ **16.7 KB of code plus ~1 KB of CRC table**. On
Xtensa expect the same order of magnitude; treat the exact number as UNVERIFIED.

The catch is that `framing.c` contains **both** halves — `ogg_stream_*` (encoder/muxer) and
`ogg_sync_*`/`ogg_stream_pagein`/`ogg_stream_packetout` (decoder/demuxer). The only build knob
is `--disable-crc`, and `configure.ac:164-167` describes it as *"Disable CRC in the demuxer"* —
it does not remove the muxer's CRC or the demuxer code. So a muxer-only build pays for the whole
demuxer unless you edit the file.

**An ESP-IDF port already exists and is on the registry** —
[`georgik/ogg`](https://components.espressif.com/components/georgik/ogg) v1.3.5~1,
`BSD-3-Clause`, repo [`georgik/esp-idf-component-ogg`](https://github.com/georgik/esp-idf-component-ogg).
It is a thin wrapper: `idf_component_register(SRCS "ogg/src/framing.c" "ogg/src/bitwise.c" …)`,
with `.gitmodules` pointing `ogg` at `https://github.com/xiph/ogg.git`. `README.md` is 136
bytes: "Wrapper of Ogg codecs for ESP-IDF." So it is **full libogg, encoder and decoder, fetched
as a submodule** — you cannot use it without vendoring all of libogg. It has 11 total downloads
on the registry and no target list, i.e. essentially unused and unvalidated on an S3.

`os_types.h` is the porting surface (it maps `ogg_int64_t`, `ogg_uint32_t` etc. and has
`__EMX__`/`_WIN32`/generic branches). No ESP32 branch ships upstream, so this is a
"it probably compiles unmodified, nobody has tested it" situation.

### 4.2 The finding that changes the picture: `espressif/esp_muxer` muxes Ogg+Opus

[s2-encoder-feasibility.md](s2-encoder-feasibility.md) states that "no Ogg muxer exists in the
available components" and that "adopting Opus means writing or vendoring an Ogg muxer". **That
is no longer true, and the correction matters.** Espressif publishes
[`espressif/esp_muxer`](https://components.espressif.com/components/espressif/esp_muxer)
(currently v1.2.3, "Espressif Muxer to mux audio and video stream data into container"), whose
[README feature table](https://github.com/espressif/esp-adf-libs/blob/master/esp_muxer/README.md)
contains an **OGG column with OPUS = Y** — and nothing else in that column:

| | MP4 | TS | FLV | WAV | CAF | **OGG** | AVI |
|---|---|---|---|---|---|---|---|
| MP3 | Y | Y | Y | Y | N | **N** | Y |
| OPUS | N | N | N | N | N | **Y** | N |

Corroborating source facts:

- `esp_muxer/Kconfig` has `config ESP_MUXER_OGG_SUPPORT`, `bool "Support OGG Muxer"`, `default y`.
- `esp_muxer/include/impl/ogg_muxer.h` exists and exposes
  `ogg_muxer_config_t { esp_muxer_config_t base_config; uint32_t page_cache_size; }` — the
  comment explains the page policy: *"Cache data until data size exceed cache size … small input
  frames can be gathered into page. Page is flushed into storage when page cache is full.
  Through this method to decrease the page head overloading."*
- It supports **live streaming**, not just files. `esp_muxer.h`:
  `muxer_data_callback data_cb; /*!< Muxer output callback can be coexist with url pattern.
  It is suitable for living stream scenario. */` and `esp_muxer_file_writer_t` with
  `on_write`/`on_seek`/`on_close`.
- Prebuilt libraries ship for `esp32s3`: `esp_muxer/lib/esp32s3/libesp_muxer.a` (961,708 bytes).

**The disqualifying detail: there is no `ogg_muxer.c`.** The tree contains
`src/muxer_reg.c` (2,441 bytes) and `include/impl/ogg_muxer.h` — the Ogg implementation is
**only inside the prebuilt `.a`**. You cannot read it, audit it, patch it, or confirm it emits a
correct CRC. And the licence is not MIT-as-such: `esp_muxer/LICENSE` is *Espressif Modified MIT*
("SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT") and states verbatim:

> "Permission is hereby granted for use **EXCLUSIVELY with Espressif Systems products**. …
> The Software MUST BE USED IN CONJUNCTION WITH ESPRESSIF SYSTEMS PRODUCTS. …
> **Redistribution of the Software in source or binary form FOR USE WITH NON-ESPRESSIF
> PRODUCTS is strictly prohibited.**"

That is fine for xtape's firmware on an ESP32-S3, and incompatible with the project's
packaging goal of a reusable component ([../02-packaging-and-licensing.md](../02-packaging-and-licensing.md)).
Also note the table shows **OGG only accepts OPUS** — it cannot emit Vorbis or FLAC-in-Ogg.

### 4.3 Registry sweep — what else exists

Queried `https://components.espressif.com/api/components?q=<term>` for `ogg`, `opus`,
`vorbis`, `webm`, `matroska`, `muxer`, `demux`:

- **No component provides an Ogg muxer in source form** other than `esp_muxer` (§4.2).
- **No Opus or Vorbis encoder** other than through `espressif/esp_audio_codec` (encoder side)
  — consistent with s2's finding that `esp_audio_codec` ships no MP3/FLAC/Vorbis encoder.
- Decode-only, listed for completeness: `esphome/micro-vorbis` ("Ogg Vorbis audio decoder for
  ESP32 with integer-only decoding and PSRAM support", Apache-2.0 AND BSD-3-Clause),
  `esphome/micro-opus`, `esphome/micro-decoder`, `esphome/micro-flac`,
  `esphome/micro-mp3`.
- `georgik/ogg` (§4.1) is the only libogg port.
- No component mentions Ogg-MP3, WebM or Matroska muxing for audio output.

One caveat on the search API: `?q=` is fuzzy and returns unrelated hits (e.g. `inkplate`,
`mcp23x17` for `q=ogg`), and the endpoint caps at 20 results per page, so it is a *good* but not
exhaustive inventory. Absence of a component is evidence, not proof.

### 4.4 Verdict on reuse

| Option | Licence | Source readable? | Code size | Verdict |
|---|---|---|---|---|
| Hand-roll the page writer | yours (no new licence) | yes, it's yours | **132 lines of C; body buffer 65 KB max, shrinkable** | **recommended** |
| Vendor `xiph/ogg` | BSD-3, clean | yes | ~3,500 lines / ~17 KB code | viable fallback; you get an unused demuxer |
| `georgik/ogg` component | BSD-3 | yes (it's libogg via submodule) | same as above | same as vendoring, plus a submodule and an unvalidated IDF wrapper |
| `espressif/esp_muxer` | Espressif-products-only, **binary blob** | **no** | 962 KB `.a` for S3 | reject for a portable component; consider only if you accept a closed blob |

Porting libogg is not hard, but it is strictly more work than writing the muxer: the muxer needs
one logical stream, no seeking, no demuxing, no bit-level packet I/O (`bitwise.c` is only needed
for the demuxer's `b_o_s`/granule bookkeeping and for Vorbis bitstreams), and no `config_types.h`
porting. The realistic comparison is ~130 lines you fully understand and can prove correct
(§8) versus ~3,500 lines plus an IDF wrapper you would need to validate on the S3 anyway.

---

## 5. MP3-in-Ogg: not a thing you should emit

**VERDICT: MP3-in-Ogg is a dead, unofficial mapping with no normative home and no mainstream
implementation. An MP3 output path needs no Ogg muxer at all — it needs bare MPEG frames plus
ICY metadata.**

Evidence, all primary:

1. **There is no registered Ogg MP3 codec identifier.** RFC 5334's codec table
   ([RFC 5334 §4](https://www.rfc-editor.org/rfc/rfc5334.txt), "The following table illustrates
   how those header values map into strings that are used in the 'codecs' parameter") lists
   exactly: `dirac`, `flac`, `theora`, `vorbis`, `celt`, `cmml`, `jng`, `kate`, `midi`, `mng`,
   `pcm`, `png`, `speex`, `yuv4mpeg`. RFC 7845 later adds `opus`. **No MPEG/MP3 entry exists.**
   The current [Xiph `MIMETypesCodecs` wiki](https://wiki.xiph.org/MIMETypesCodecs), which RFC
   5334 defers to as the up-to-date table ("An up-to-date version of this table is kept at
   Xiph.org"), likewise has no MP3 row.
2. **The Xiph wiki mapping was deleted.** The XiphWiki deletion log records:
   `23:01, 12 May 2007 Saoshyant deleted page OggMP3 (content was: '#REDIRECT Ogg MP3' (and the
   only contributor was 'Pmsyyz'))` ([Special:Log](https://wiki.xiph.org/Special:Log)). Both
   `OggMP3` and `Ogg_MP3` now return "There is currently no text in this page", and the Internet
   Archive has no capture of either. So the informal mapping does not even have a citable
   specification text today — treat any blog describing its magic header as a secondary,
   unverifiable source.
3. **ffmpeg cannot do it.** `libavformat` contains `oggparsedirac.c`, `oggparseflac.c`,
   `oggparseogm.c`, `oggparseopus.c`, `oggparseskeleton.c`, `oggparsespeex.c`,
   `oggparsetheora.c`, `oggparsevorbis.c`, `oggparsevp8.c` — **no `oggparsemp3.c`**. The
   demuxer's codec table (`libavformat/oggdec.c`, `ogg_codecs[]`) is:
   `ff_skeleton_codec, ff_dirac_codec, ff_speex_codec, ff_vorbis_codec, ff_theora_codec,
   ff_flac_codec, ff_opus_codec, ff_vp8_codec, ff_old_dirac_codec, ff_old_flac_codec,
   ff_ogm_video_codec, ff_ogm_audio_codec, ff_ogm_text_codec, ff_ogm_old_codec`. And the muxer
   rejects MP3 outright — `libavformat/oggenc.c` errors with
   `"Unsupported codec id in stream %d"` for anything outside
   `{VORBIS, THEORA, SPEEX, FLAC, OPUS, VP8}`.
4. **MDN says MP3 is not containerised at all:** "A prime example is the MP3 audio file, which
   isn't stored in a conventional container. Instead, an MP3 file is essentially a stream of
   MPEG-1 Audio Layer III-encoded frames, often accompanied by metadata such as ID3 tags. These
   files use the `audio/mpeg` MIME type and the `.mp3` extension"
   ([MDN Containers](https://developer.mozilla.org/en-US/docs/Web/Media/Guides/Formats/Containers)).
5. **Icecast/Shoutcast reality** confirms the same in code: Icecast's MP3 plugin is
   `src/format_mp3.c`, which serves `Content-Type: audio/mpeg` and is the *only* plugin that
   implements `icy-metaint` (§6).

So for MP3 output: encode with shine (LGPL-2; CPU and licence already analysed in
[s2 §3](s2-encoder-feasibility.md)), send an `audio/mpeg` response, and emit bare MPEG frames.
No muxer, no page writer, and the existing `send_meta()`/`metaint` machinery in
`mini_icecast.c:75-109` keeps working. The author's phrase "MP3 / Opus in Ogg" should be read as
"MP3, and/or Opus in Ogg" — the two formats have completely different wire strategies and MP3
is the *cheaper* one to ship precisely because it needs no container work.

---

## 6. Live "now playing" for an Ogg stream

Cross-reference first: [s2 §4](s2-encoder-feasibility.md) and
[../radio-player-codecs.md](../radio-player-codecs.md) already established that **`icy-metaint`
does not apply to Ogg**. Extended and confirmed against Icecast 2.5.0 master by grepping the
whole tree:

- `grep -rn metaint` over the whole Icecast tree matches **only** `src/format.c` (a config-name
  check at line 410) and `src/format_mp3.c` (lines 117, 234, 748, where `icy-metaint:%u` is
  written), plus prose in `ChangeLog`. No Ogg source file mentions it.
- `grep -rn "Transfer-Encoding"` across the entire Icecast tree returns **nothing** — Icecast
  does not chunk and does not send `Content-Length` for a live stream (the one
  `Content-Length: 221183499` at `format_mp3.c:732` is explicitly "hack for flash player"
  gated on `x-flash-version` or an MSIE user-agent, and is MP3-only).

(a) **`OpusTags` is metadata, but sent once per logical stream.** It is the *second packet of
the stream* (RFC 7845 §3) and its granule must be 0 (§2.2). It is not a per-track channel. The
practical consequence: to change the title you must start a **new logical stream**, i.e. chain.

(b) **RFC 7845 explicitly designs chaining for exactly this.** §7.2 "Continuous Chaining":

> "In some applications, such as **Internet radio**, it is desirable to cut a long stream into
> smaller chains, e.g., **so the comment header can be updated**. This can be done simply by
> separating the input streams into segments and encoding each segment independently. The
> drawback of this approach is that it creates a small discontinuity at the boundary due to the
> lossy nature of Opus."

§7.2 then gives a five-step recipe to avoid the discontinuity (encode the last frame with
prediction disabled via `OPUS_SET_PREDICTION_DISABLED(1)`, set the last page's granule near the
end of that frame, start the next segment with a copy of that frame, set the second stream's
pre-skip to join them, and keep the encoder state). So **yes — per-track now-playing is
achievable inside Ogg**, by chaining, and the normative spec names the use case. It costs: one
encoder discontinuity per track (mitigable), a new random serial per track, and client support
that is not universal.

(c) **What Icecast actually does** (`format_ogg.c`, `format_opus.c`, `format_vorbis.c`,
2.5.0 master) — this is the behaviour to imitate or beat:

- It *does* handle chaining. `process_initial_page()` treats a BOS page after
  `ogg_info->bos_completed` as a new link and calls `free_ogg_codecs(ogg_info)` with the comment
  *"need to zap old list of codecs when next group of BOS pages appear"*, then re-dispatches to
  `initial_vorbis_page` / `initial_opus_page` / … .
- It *does* push new headers to already-connected clients.
  `write_buf_to_client()` compares `client_data->headers != refbuf->associated` and calls
  `send_ogg_headers()`, so a client that has been listening for hours receives the new header
  pages when the source chains.
- For **Opus** it passes the source's header pages through verbatim:
  `format_opus.c` `process_opus_page()` calls `format_ogg_attach_header(ogg_info, page)` with the
  page as received, and only *parses* `OpusHead` (channels from `packet[9]`, input rate from
  bytes 12–15) and `OpusTags` (`metadata_xiph_read_vorbis_comments`) for its own bookkeeping.
- For **Vorbis** it de-muxes and re-muxes into a *new* `ogg_stream_state new_os`, so it can
  rewrite the comment header on the fly — `format_vorbis.c:331` `vorbis_commentheader_out(&plugin->vc, &header)`
  with the note `/* NOTE: we could build a separate comment packet each time */`.
- Where does the metadata *go*? Into server state, not the byte stream, when the source does not
  chain: `format_ogg.c update_comments()` does `stats_event(source->mount, "artist", ...)`,
  `stats_event(..., "title", ...)`, `stats_event(..., "display-title", ...)`,
  `logging_playlist(...)`, `playlist_push_track(...)` and `event_stream_emit_vc(...)`.
- It is triggered by `ogg_info->log_metadata`, which the codec handlers set when they see a new
  comment header.

(d) **Side-channel HTTP endpoints** are the practical path for a small server, because Icecast's
own now-playing is exposed out-of-band:

- `web/status-json.xsl` exists in the tree and its own header comment says verbatim:
  *"/!\ /status-json.xsl is deprecated since 2.5.x. This file is kept for compatibility with
  2.4.x only. Please migrate to /admin/publicstats and consider /admin/eventfeed."*
  ([status-json.xsl](https://github.com/xiph/Icecast-Server/blob/master/web/status-json.xsl))
- The fields are the same ones `update_comments()` writes (`artist`, `title`, `display-title`).
  For xtape the equivalent is a trivial `GET /status.json` (or `/status-json.xsl` for
  familiarity) returning `{"artist":…,"title":…}` plus a `GET /` HTML page that polls it.

**Bottom line for §6:** an Ogg/Opus stream carries now-playing metadata in `OpusTags` — once per
logical stream. Per-*track* updates inside the stream require **chaining** (new serial, new
`OpusHead` + `OpusTags`), which RFC 7845 §7.2 specifies for Internet radio and Icecast
implements and forwards to live listeners. Whether a *particular* player re-reads chained
`OpusTags` mid-playback is client-dependent and **was not verified here** (see §9). The
always-working answer is a side-channel JSON endpoint; ship both.

---

## 7. Client compatibility for a live Ogg/Opus HTTP stream

### 7.1 The web platform explicitly supports unbounded media

The WHATWG HTML spec treats "streaming radio" as a first-class case
([html.spec.whatwg.org §media](https://html.spec.whatwg.org/multipage/media.html)):

> "Returns `Infinity` for unbounded streams."
>
> "…If no media data is available, then the attributes must return the Not-a-Number (NaN) value.
> If the media resource is not known to be bounded (**e.g. streaming radio**, or a live event
> with no announced end time), then the attribute must return the positive `Infinity` value."
>
> "Update the `duration` attribute with the time of the last frame of the resource, if known…
> If it is not known (**e.g. a stream that is in principle infinite**), update the `duration`
> attribute to the value positive `Infinity`."
>
> "If an 'infinite' stream ends for some reason, then the duration would change from positive
> Infinity to the time of the last frame or sample in the stream, and the `durationchange` event
> would be fired."
>
> "The media element **stall timeout** is an implementation-defined length of time, which should
> be **about three seconds**."

So a never-ending response with no `Content-Length` is not a hack: it is the model the spec
describes, and `duration === Infinity` is the expected observable. The stall timeout is the one
operational constraint — the server must not go quiet for ~3 s (xtape's `lead_ms`/burst ring
already exists for this).

### 7.2 Chromium accepts a response with no Content-Length as a stream

From Chromium's `third_party/blink/renderer/platform/media/`:

`multi_buffer_data_source.cc`, `StartCallback()`:

```cpp
// All responses must be successful. Resources that are assumed to be fully
// buffered must have a known content length.
bool success =
    reader_ && reader_->Available() > 0 && url_data_ &&
    (!AssumeFullyBuffered() || url_data_->length() != kPositionNotSpecified);
if (success) {
  total_bytes_ = url_data_->length();
  streaming_ =
      !AssumeFullyBuffered() && (total_bytes_ == kPositionNotSpecified ||
                                 !url_data_->range_supported());
```

and `AssumeFullyBuffered()` is:

```cpp
bool MultiBufferDataSource::AssumeFullyBuffered() const {
  return !url_data_->url().ProtocolIsInHttpFamily();
}
```

So for an `http://` URL with **no `Content-Length`**, `total_bytes_ == kPositionNotSpecified`
and the response is accepted and classified `streaming_ = true`. In
`resource_multi_buffer_data_provider.cc` the comment is explicit:

```cpp
// Expected content length can be |kPositionNotSpecified|, in that case
// |content_length_| is not specified and this is a streaming response.
int64_t content_length = response.ExpectedContentLength();
```

Two operational details fall out of that file:

- Blink issues a `Range:` request, and a plain `200 OK` is explicitly tolerated:
  *"We accept a 200 response for a Range:0- request, trusting the Accept-Ranges header, because
  Apache thinks that's a reasonable thing to return."*
- **Do not advertise `Accept-Ranges: bytes` unless you implement ranges** —
  `if (accept_ranges.contains("bytes")) destination_url_data->set_range_supported();` and a
  range-supporting response sets `streaming_` to *false*. For an endless stream, omit
  `Accept-Ranges` and ignore `Range:`.

### 7.3 Icecast's real wire behaviour = the reference to copy

`grep -rn "Transfer-Encoding"` over the entire Icecast 2.5.0 tree: **no matches**. Icecast
serves a live mount as an ordinary `HTTP/1.0 200 OK` with `Content-Type` from the plugin's
`contenttype`, no `Content-Length`, no chunking, and keeps the connection open. RFC 7845 §9
gives the type: "The RECOMMENDED mime-type for Ogg Opus files is `audio/ogg`", with the optional
`codecs` parameter: `audio/ogg; codecs=opus`. MDN specifies `audio/ogg` for the Ogg container
and `application/ogg` as the fallback when the content is unknown.

### 7.4 Safari / iOS

MDN's container tables state it directly, with versions:

> "**Note:** Safari 18.4+ (on macOS 15.4+, iOS 18.4+, iPadOS 18.4+, and visionOS 2.4+) added
> support for Opus and Vorbis codecs in Ogg containers."
> — [MDN Containers](https://developer.mozilla.org/en-US/docs/Web/Media/Guides/Formats/Containers)

MDN's Ogg audio codec table then lists Opus with a "Yes" in each of the four engine columns
(Chrome/Edge/Firefox/Safari).

**But MDN contradicts itself, and this matters.** MDN's *audio codec* guide still carries the
older, narrower statement for Opus ([MDN Audio codecs](https://developer.mozilla.org/en-US/docs/Web/Media/Guides/Formats/Audio_codecs)):

> "Safari supports Opus in the `<audio>` element **only when packaged in a CAF file**, and only
> on macOS High Sierra (10.13) or iOS 11."

So the same vendor's own documentation disagrees: the Containers page says Ogg Opus works in
Safari 18.4+, the Audio codecs page says CAF only. caniuse's raw data, which is maintained
independently, never awards Safari a full `y` for Opus — only `a` (partial) in every release,
with notes `"Supported only when packaged in a CAF file and on macOS High Sierra/iOS 11 or later
(constant bit-rate only)"`, `"Supported in a WebM container (all bitrates)"`
([caniuse opus.json](https://raw.githubusercontent.com/Fyrd/caniuse/main/features-json/opus.json),
secondary source). Note also that the Containers page lists **no CAF row at all**, so it is the
newer of the two pages.

**Conclusion: treat the Safari/iOS floor as "18.4+ per MDN Containers, CAF-only per MDN Audio
codecs" and verify it on a real device.** A pre-18.4 iOS device is the case where MP3 remains
the only zero-risk output. That is nonetheless a materially better position than the
unknown-length WAV case in [s3-wire-format-and-clients.md](s3-wire-format-and-clients.md),
because every *current* engine has a documented Ogg Opus path.

### 7.5 The one client bug that specifically hits a never-ending stream

> "**Warning:** Ogg Opus audio files longer than **12h 35m 39s** are truncated and exhibit
> seeking issues when played on Firefox Linux 64 bit (Firefox bug 1810378)."
> — [MDN Containers](https://developer.mozilla.org/en-US/docs/Web/Media/Guides/Formats/Containers)

The bug is real and open: **Bugzilla 1810378**, "Ogg opus file load/playback issues when length >
approx 12h", status `UNCONFIRMED`, product/component `Core :: Audio/Video: Playback`, filed
2023-01-16 ([bugzilla.mozilla.org](https://bugzilla.mozilla.org/show_bug.cgi?id=1810378)). The
threshold is consistent with a signed 32-bit sample count at 48 kHz: 2^31 / 48000 = 44,739 s =
**12 h 25 m 39 s** (my arithmetic; MDN's 12h35m39s is 10 minutes higher, so the exact overflow
expression is UNVERIFIED).

This matters more for a radio station than for a file, because a station that plays continuously
crosses 12 h 25 m every day. It strengthens the case for **chaining on every track change**: a
chained stream restarts the granule counter at each new logical stream, so a decoder that only
handles 32-bit granules never accumulates past the limit. This is a genuine, non-obvious
argument for chaining that has nothing to do with metadata.

### 7.6 What is *not* verified here

No browser was actually pointed at a live Ogg Opus stream from an ESP32 in this pass, and
neither VLC nor Apple's media stack was exercised. §7.1–§7.5 establish the platform's *stated*
and *coded* behaviour; the empirical check on the real clients is still worth doing before
committing (see §9).

---

## 8. Implementation sketch: the minimal Ogg page writer

Reference implementation: [s5-ogg-page-writer.c](s5-ogg-page-writer.c) — **132 lines** including
the header comment block; the implementation proper (the `#define`s, the struct, the CRC,
`oggmux_init`, `oggmux_flush`, `oggmux_packet`) is **104 lines, of which 89 are code**. That is
the entire cost of Ogg output; there is no separate container library. Verification harness:
[s5-ogg-verify.c](s5-ogg-verify.c) + auto-generated vectors
[s5-ogg-vectors.h](s5-ogg-vectors.h).

### 8.1 State (one logical stream)

```c
typedef struct {
    uint32_t serial, pageno;
    int      bos_done;      /* BOS page already emitted?                 */
    int      eos;           /* caller signalled end of stream             */
    int      more_pkts;     /* a packet will still complete on this page  */
    uint8_t  body[OGG_MAX_BODY];            /* 65025 = 255*255          */
    uint16_t lacing[OGG_MAX_SEGS];          /* low byte size, bit 8 = starts packet */
    int      nseg, body_len, pkts_done;
    uint64_t granule;       /* granule of the packet just submitted      */
    uint64_t page_gp;       /* granule of the last packet COMPLETED here */
    int      page_gp_valid;
} oggmux_t;
```

**RAM: ~65 KB for the body buffer.** On an S3 with PSRAM this is a single PSRAM allocation; if
that is unwelcome, shrink it — the buffer only needs to hold one page, and a 4 KB page budget
needs 4 KB, with the 255-segment path driving the spill. The `uint64_t`/`int` fields are
trivial; the lacing array is 510 bytes.

### 8.2 Per-page algorithm

1. **Append a packet** (`oggmux_packet`): `nseg_total = len/255 + 1`; for each chunk `i`, if
   `nseg == 255` flush first (this is the packet-spans-pages case), then push lacing value
   `chunk` (255 for all but the last, which is `len - 255*(nseg_total-1)` — **`0` for exact
   multiples of 255**), OR in `PKT_START` **only when `i == 0`**, and copy the bytes. When
   `i == nseg_total-1`, record `page_gp = granulepos`.
2. **Flush decision** (`oggmux_flush`): nothing pending → 0. Not `bos_done` → force. Otherwise
   apply the page-close policy: flush when `nseg == 255`, or when the page is "full enough"
   (the reference uses libogg's `body_len > 4096 && pkts_done >= 4` heuristic; a radio server
   would more sensibly use a *time* trigger — see §8.4).
3. **Build the header**: `"OggS"`, version 0, flags = `0x01` if `!(lacing[0] & PKT_START)`,
   `| 0x02` if `!bos_done`, `| 0x04` if `eos && !more_pkts`; granulepos = `0` on the BOS page
   else `page_gp_valid ? page_gp : -1`; serial; pageno++; four zero CRC bytes; `nseg`; the lacing
   table; then the body.
4. **CRC**: `crc = ogg_crc(ogg_crc(0, hdr, hlen), body, blen)`, written little-endian at offset
   22.
5. **Reset** page state (`nseg`, `body_len`, `page_gp_valid`, `pkts_done`) but **not** `pageno`,
   `serial` or `bos_done`.

### 8.3 Driving it for a live Opus station

```c
oggmux_init(&m, esp_random());                    /* new serial per chain link */
oggmux_packet(&m, opushead, 19, 0, out, 0);  oggmux_flush(&m, out, 1);  /* BOS, alone */
oggmux_packet(&m, opustags, n,  0, out, 0);  oggmux_flush(&m, out, 1);  /* ends its page */
uint64_t g = pre_skip;                            /* RFC 7845 §4.5 */
for (;;) {
    enc = opus_encode(enc, pcm, 960, frame, sizeof frame);   /* 20 ms @ 48 kHz */
    g += 960;                                     /* ALWAYS 48 kHz, whatever the input rate */
    oggmux_packet(&m, frame, enc, g, out, 0);
    while ((w = oggmux_flush(&m, out, 0)) > 0) broadcast(out, w);
}
/* track change: */
oggmux_packet(&m, last_frame, n, g, out, /*eos=*/1);
oggmux_flush(&m, out, 1);                         /* eos page */
oggmux_init(&m, esp_random());                    /* NEW serial -> chain */
```

### 8.4 Page granularity and latency

- **Segment cap binds before the byte cap for small frames.** One Opus packet is one lacing value
  unless it exceeds 255 bytes. 255 segments is therefore 255 packets: at 20 ms frames that is
  5.1 s per page, at 2.5 ms frames only 637 ms. With ~225-byte packets at 90 kbps, the byte
  budget binds first.
- **Concrete choice: one page per ~0.5 s.** Icecast's Vorbis re-muxer uses exactly this:
  `source_vorbis->page_samples_trigger = source_vorbis->vi.rate / 2;` (`format_vorbis.c:534`),
  i.e. half a second of samples. libogg's nominal page size is 4096 bytes
  (`ogg_stream_flush_i(os, og, force, 4096)`) which is ≈0.5 s at 64 kbps — the same choice from
  the byte side.
- **Overhead stays in the spec's 1–2 % target** (RFC 3533 §3: "use no more than approximately
  1-2% of bitstream bandwidth"). At 20 ms frames and 0.5 s pages, `nseg = 25` so the header is
  52 bytes against ~4,000–5,600 payload bytes ≈ 1 %. At 5 ms frames the same page needs 100
  lacing values → 127 bytes, ≈2.3 %, i.e. outside the target. Rule of thumb: **≥10 ms frames and
  ≥0.25 s pages**.
- **Start latency**: page 0 is `OpusHead` alone (47 bytes on the wire = 27 + 1 + 19), page 1 is
  `OpusTags`, then audio. A player can start once it has those plus one audio page. If the first
  audio page is allowed to fill to 0.5 s, worst-case start latency is ~0.5 s plus the encoder's
  pre-skip. **Flush the first audio page early** (e.g. after 2–3 frames, ~50 ms) to cut that to
  ~100 ms. The spec's own 4-packet floor in libogg's heuristic exists for file overhead, not
  latency — for a live station, prefer a time trigger.
- **Never go quiet for ~3 s** (WHATWG's stall timeout, §7.1). The existing `lead_ms` burst ring
  and `pace()` already cover this.

### 8.5 Verification: the writer is byte-exact against libogg

libogg's own test suite hard-codes the expected page headers for ten packet sequences in
`src/framing.c` (`head1_0` … `head4_6`). [s5-ogg-verify.c](s5-ogg-verify.c) replays all ten
sequences through the writer and compares the emitted page headers byte for byte:

```
  OK   single page encoding                       1 page(s), headers byte-identical to libogg
  OK   basic page encoding                        2 page(s), headers byte-identical to libogg
  OK   basic nil packets                          2 page(s), headers byte-identical to libogg
  OK   initial-packet lacing > 4k                 2 page(s), headers byte-identical to libogg
  OK   single packet page span                    3 page(s), headers byte-identical to libogg
  OK   page spill expansion                       3 page(s), headers byte-identical to libogg
  OK   max packet segments                        3 page(s), headers byte-identical to libogg
  OK   very large packets                         4 page(s), headers byte-identical to libogg
  OK   continuation resync in very large packets  4 page(s), headers byte-identical to libogg
  OK   zero data page (1 nil packet)              3 page(s), headers byte-identical to libogg

ALL CHECKS PASSED: 27/27 page headers match libogg exactly (0 failures)
```

The vectors cover: nil packets, packets of exactly 254/255/256 bytes, a 4,345-byte packet, a
65,500-byte packet spanning pages, the 255-segment limit, a 130,049-byte packet spanning four
pages, and zero-data pages. **Getting to 27/27 required fixing three bugs, each of which is a
trap worth naming** (all three were found by execution, not by reading):

1. **The 255-multiple terminator.** My first loop was `do { … } while (len > 0)`, which drops the
   terminating `0` lacing value for a packet whose length is an exact multiple of 255. Symptom:
   the *last* lacing value of every 255/510/765…-byte packet silently missing. RFC 3533 §5
   states the rule; libogg implements it as `lacing_vals = bytes/255 + 1`.
2. **`PKT_START` on a continuation.** Marking "this segment starts a packet" whenever `nseg == 0`
   is wrong, because a page opened *mid-packet* also starts at `nseg == 0`. Symptom: the
   "continued packet" flag (0x01) missing from every page that resumes a split packet, so a
   demuxer discards the first packet after each spill. The rule is: mark it only on the packet's
   **first** segment (`i == 0`).
3. **Granule = `-1` for the wrong reason.** "Set `-1` if the page's last lacing value is 255" is
   the natural misreading of RFC 3533 §6 and it is wrong. A page may contain several *completed*
   packets and then the beginning of a long one; its granule is the granule of the last packet
   that completed. libogg keeps `granule_pos = -1` and overwrites it for every lacing value
   `< 255` while scanning. libogg's `head2_6` vector distinguishes the two behaviours exactly;
   my wrong version failed on 5 of 27 vectors and my correct version passes.

This is why the deliverable includes runnable code rather than pseudocode: all three bugs are
invisible in review and obvious to a test vector.

---

## 9. VERIFIED vs UNVERIFIED

**VERIFIED — read from the normative/primary source in this pass:**

- Ogg page layout, field offsets, little-endian rule, `header_size = 27 + nseg`, maximum page
  65307 bytes (RFC 3533 §6).
- Lacing rules including the `0` terminator for multiples of 255 and the "nil packet is legal"
  rule (RFC 3533 §5). *Confirmed by execution* (§8.5).
- CRC: poly `0x04c11db7`, unreflected, init 0, final 0, CRC field zeroed, header then body
  (RFC 3533 §6 for the polynomial; libogg `src/framing.c` for the rest). *Confirmed by
  reproducing libogg's hard-coded CRCs* (§8.5).
- `granulepos == -1` means "no packet completes on this page", not "the page ends on 255"
  (RFC 3533 §6 + libogg `ogg_stream_flush_i`). *Confirmed by execution* (§8.5).
- Chaining semantics: eos page immediately followed by a BOS page, unique serial per link
  (RFC 3533 §4). Icecast's `free_ogg_codecs()` on a new BOS confirms it is implemented in
  practice.
- RFC 7845 §3: two mandatory header packets; `OpusHead` alone on the first page, BOS set;
  `OpusTags` may span pages but must finish on the page where it completes; audio follow.
- RFC 7845 §4: granule is 0 on the ID-header page and the page where the comment header
  completes; granule counts 48 kHz samples per channel regardless of encoder input rate.
- RFC 7845 §4.5: the initial granule MAY exceed the samples on that page; it MUST NOT be
  smaller; the allowance exists so "a live stream [can] be joined without rewriting the granule
  position of all the remaining pages".
- RFC 7845 §5.1 `OpusHead` layout, `Version == 1`, mandatory fields, channel mapping family 0
  for mono/stereo with the table omitted; §5.2 `OpusTags` = Vorbis comment structure **without**
  the framing bit; `Output Gain` "SHOULD" be 0.
- RFC 7845 §7.2: chaining is the Internet-radio mechanism for updating the comment header.
- Vorbis I §4.2 (three header packets, order, `packet_type` 1/3/5 + `"vorbis"`) and Appendix A.2
  (58-byte first page, third header finishes its page, next packet starts a fresh page, granule
  = end PCM sample position of the last packet completed, `-1` for a wholly spanned page).
- Vorbis comment header structure incl. the framing bit ([v-comment.html](https://xiph.org/vorbis/doc/v-comment.html)).
- MP3-in-Ogg: RFC 5334's codec table has no MPEG/MP3 entry; the Xiph `OggMP3` wiki page was
  **deleted 2007-05-12** (XiphWiki deletion log) and its redirect target is empty; ffmpeg has no
  `oggparsemp3.c`, its `ogg_codecs[]` table omits MP3, and `oggenc.c` rejects it with
  "Unsupported codec id in stream"; MDN states MP3 "isn't stored in a conventional container".
- ESP registry: `georgik/ogg` is a BSD-3 wrapper that compiles libogg's `framing.c` +
  `bitwise.c` from a git submodule; **`espressif/esp_muxer` provides an Ogg/Opus muxer**, with
  `ESP_MUXER_OGG_SUPPORT default y`, a live-stream `data_cb`, a `page_cache_size` knob, an
  `esp32s3` prebuilt `libesp_muxer.a` — and **no `ogg_muxer.c` source** anywhere in the tree.
  Its licence is `LicenseRef-Espressif-Modified-MIT`, "EXCLUSIVELY with Espressif Systems
  products", and forbids redistribution for use with non-Espressif products.
- libogg licence is BSD-3 (Xiph) and its source footprint is `framing.c` 2,138 lines +
  `bitwise.c` 1,089 lines + `crctable.h` 278 lines.
- `icy-metaint` exists only in Icecast's `format.c` (config) and `format_mp3.c`; the whole
  Icecast tree contains **no** `Transfer-Encoding`; the lone `Content-Length` inside
  `format_mp3.c` is a Flash/MSIE-only hack. `status-json.xsl` is deprecated in 2.5.x in favour
  of `/admin/publicstats` and `/admin/eventfeed`.
- WHATWG HTML: `duration` returns `Infinity` for unbounded streams, naming "streaming radio";
  the stall timeout "should be about three seconds".
- Chromium accepts a missing `Content-Length` over HTTP as a streaming response
  (`ResourceMultiBufferDataProvider` comment + `MultiBufferDataSource::StartCallback`), accepts
  a `200` for a `Range:0-` request, and treats `Accept-Ranges: bytes` as a promise of range
  support.
- MDN: "Safari 18.4+ (on macOS 15.4+, iOS 18.4+, iPadOS 18.4+, and visionOS 2.4+) added support
  for Opus and Vorbis codecs in Ogg containers"; and the Firefox >12h35m39s Ogg Opus warning.
- Bugzilla 1810378 is real, `UNCONFIRMED`, `Core :: Audio/Video: Playback`, "Ogg opus file
  load/playback issues when length > approx 12h".

**UNVERIFIED / LIKELY:**

- **Empirical browser playback.** No browser, VLC or Apple media stack was pointed at a live Ogg
  Opus stream from an ESP32. §7 is spec-and-source evidence, not a smoke test. The exact
  Safari/iOS behaviour for an *endless* Ogg Opus response (as opposed to a file) is UNVERIFIED;
  MDN documents the container/codec support, not the streaming case.
- **Which MDN page about Safari is current.** MDN's Containers page says Safari 18.4+ supports
  Opus in Ogg; MDN's Audio codecs page says "only when packaged in a CAF file, and only on macOS
  High Sierra (10.13) or iOS 11"; caniuse never marks Safari fully supported for Opus. The
  discrepancy is not resolvable from documentation alone — **this needs a real iOS 18.4+ test**.
  The Containers page is newer (it documents a feature that did not exist when the older
  sentence was written, and it is the page MDN points at from its audio-format guides), so I
  lean to it, but that is a judgment call, not a verification.
- **Whether real players re-read chained `OpusTags` mid-playback.** RFC 7845 §7.2 specifies the
  mechanism and Icecast forwards the headers, but per-client honouring was not tested. The
  mailing-list report already cited in [s2 §4](s2-encoder-feasibility.md) ("Meta Data for opus
  and FLAC do not work") is consistent with this being rough.
- **The exact Firefox truncation threshold.** MDN says 12h35m39s; a signed 32-bit 48 kHz sample
  counter overflows at 12h25m39s. The precise expression is not stated in the bug.
- **libogg's compiled size on Xtensa.** The ~17 KB figure is a host `-Os` proxy; no Xtensa
  build was measured.
- **Whether libogg compiles unmodified for ESP32-S3.** No ESP32 branch in `os_types.h`; the
  `georgik/ogg` component has 11 downloads and no declared targets. Plausible, untested.
- **Whether `esp_muxer`'s Ogg output is actually correct.** It is a binary blob; the README's
  `Y`, the header and the Kconfig are verifiable, the emitted bytes are not. Checking it would
  mean disassembling or capturing a stream and running a demuxer over it.
- **The registry sweep is not exhaustive.** `?q=` is fuzzy and capped at 20 results.
- **Vorbis-in-Ogg output.** Not a real question today — there is no Vorbis encoder on this MCU
  ([s2 §5](s2-encoder-feasibility.md)) — but if one appeared, the Vorbis mapping would need the
  three-header/framing-bit/link-granule details in §3 implemented from scratch.
- **Encoded quality of complexity-0 Opus for music** and all encoder CPU/licence questions:
  deliberately not re-researched; see [s2](s2-encoder-feasibility.md).

---

## 10. Recommendation

**Hand-roll the muxer.** Concretely:

1. **Write the page writer; do not port libogg.** The whole job is 132 lines
   ([s5-ogg-page-writer.c](s5-ogg-page-writer.c)), it needs no
   dependency, no `config_types.h` port, no licence file, and it is already proven byte-exact
   against libogg's own test vectors 27/27. Porting libogg gives you ~3,500 lines, an entire
   unused demuxer, and still requires validation on the S3 — for a muxer that only ever writes
   one logical stream that never seeks.
2. **Keep libogg as the safety net, not the plan.** If the hand-rolled writer ever produces a
   stream some decoder rejects, `xiph/ogg` is BSD-3 and the `georgik/ogg` IDF wrapper exists;
   swapping in `ogg_stream_init/packetin/pageout` is a small, contained change. The verification
   harness makes the hand-rolled path the *lower*-risk option, not the higher one.
3. **Do not use `espressif/esp_muxer`** unless you consciously accept a closed-source 962 KB
   binary blob under an Espressif-products-only licence that forbids redistribution for
   non-Espressif use. It is the only off-the-shelf Ogg/Opus muxer for this chip and it does
   support live output — but it is unauditable, it cannot emit Vorbis or FLAC-in-Ogg, and it
   conflicts with the project's packaging goal.
4. **MP3 stays out of Ogg.** Ship MP3 (shine) as bare MPEG frames with `audio/mpeg` and the
   existing ICY `metaint` path; ship Opus as Ogg with `audio/ogg; codecs=opus` and no ICY.
   Two formats, two wire strategies, no shared container work.
5. **Page policy: one page per ~0.5 s, but flush the first audio page after ~50 ms.** Use
   ≥10 ms Opus frames so lacing overhead stays near 1 %. Never let the socket go quiet for ~3 s.
6. **Chain on every track change** — new random serial, eos page, fresh `OpusHead`/`OpusTags`,
   granule restarting at `pre_skip`. This buys per-track metadata (RFC 7845 §7.2), accidentally
   sidesteps the ~12 h granule overflow that bites Firefox, and is the only in-stream metadata
   mechanism that exists. Follow §7.2's five steps if the boundary click is audible.
7. **Expose now-playing out-of-band too.** `OpusTags` per chain link is not honoured by every
   player; add a small JSON endpoint (Icecast's `/status-json.xsl` shape, or `/admin/publicstats`
   for familiarity) and let the station's own web page read it. That is also what
   [s2 §4](s2-encoder-feasibility.md) already concluded about ICY being dead for Ogg.
8. **State the client floor honestly.** Ogg Opus works in every current engine, but
   **Safari/iOS need 18.4+** for Opus in an Ogg container. If a pre-18.4 iOS device matters,
   MP3 is the only zero-risk output.

**Bottom line:** the Ogg container is the *smallest* part of this project. It is a fully
specified ~130-line state machine with exactly three subtle rules — the 255-multiple lacing
terminator, the continued-packet flag, and the "-1 only if nothing completes here" granule — all
three of which the accompanying runnable implementation gets right and proves against libogg.
The hard problems remain where [s2](s2-encoder-feasibility.md) left them: Opus quality at
complexity 0, and whether the target clients will actually play a live Ogg stream from a
240 MHz microcontroller.

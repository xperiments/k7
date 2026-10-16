# S1 — AAC licensing for the *server* direction

Scope: the ESP-IDF **radio server** component (ESP32-S3): reads MP3/FLAC/WAV off SD,
decodes to PCM, resamples to one fixed station format (stereo 16-bit), serves an
endless WAV stream (`Content-Type: audio/wav`). **It encodes nothing.**

This note *extends* the player-angle notes rather than repeating them:
[`radio-player-codecs.md`](../radio-player-codecs.md) §3 (MP3 expiry) and §4.2
(AAC trap), and [`02-packaging-and-licensing.md`](../02-packaging-and-licensing.md)
(Espressif `Custom` licenses). Where those say "AAC pool rate card could not be
loaded", this note **corrects that** — see §2.2.

Everything below was read on **2026-10-16** (UTC). Patent/copyright is kept strictly
separate: **copyright binds the code; patents bind the technique**, and a permissive
copyright license on a decoder file says nothing about the patents that file practices.

---

## 1. Baseline: MP3 and the three formats that are actually in the pipeline

### 1.1 MP3 patents — VERIFIED, no royalty

Fraunhofer IIS's own mp3 page, verbatim:

> On April 23, 2017, Technicolor's mp3 licensing program for certain mp3 related
> patents and software of Technicolor and Fraunhofer IIS has been terminated.

— [Fraunhofer IIS, "mp3" product page](https://www.iis.fraunhofer.de/en/ff/amm/consumer-electronics/mp3.html)
(primary, vendor's own page; last-verified today).

The companion announcement gives the mechanism and date, verbatim:

> That day, the last of the core mp3 patents which were part of the licensing program
> by Fraunhofer and Technicolor, expired.

— [Fraunhofer Audio Blog, "Alive and Kicking – mp3 Software, Patents and Licenses", 2017-05-18](https://www.audioblog.iis.fraunhofer.com/mp3-software-patents-licenses)
(primary). Same post carries the caveat worth keeping: the programme's end "does not
automatically mean that all mp3 technology is available license-free" — there "might
still be some implementation-specific patents".

**Status: VERIFIED — no MP3 royalty applies to decoding MP3 in this product today.**
The residual "implementation-specific patents" risk is not a licensing *programme* any
vendor administers, and no primary source asserts a live MP3 pool.

### 1.2 The software licenses of the three decoders in use

The component wraps exactly three single-header libraries
(`resources/mi_decode.c`: `dr_mp3.h`, `dr_flac.h`, `dr_wav.h`, with `DR_*_NO_STDIO`).
Each header's first line states the license; each ends with the full text.
**All three are dual-licensed "public domain OR MIT-0"** — VERIFIED by reading the
headers, not a summary:

| Header | First line of file (verbatim) | Alternatives at end of file |
|---|---|---|
| `dr_mp3.h` | "MP3 audio decoder. Choice of public domain or MIT-0." (v0.7.4) | `ALTERNATIVE 1 - Public Domain (www.unlicense.org)` / `ALTERNATIVE 2 - MIT No Attribution` |
| `dr_flac.h` | "FLAC audio decoder. Choice of public domain or MIT-0." (v0.13.4) | same two alternatives |
| `dr_wav.h` | "WAV audio loader and writer. Choice of public domain or MIT-0." (v0.14.6) | same two alternatives |

Sources: [`dr_mp3.h`](https://raw.githubusercontent.com/mackron/dr_libs/master/dr_mp3.h),
[`dr_flac.h`](https://raw.githubusercontent.com/mackron/dr_libs/master/dr_flac.h),
[`dr_wav.h`](https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h)
(primary; upstream repo has a top-level [`LICENSE`](https://github.com/mackron/dr_libs)
too). dr_mp3's public-domain text is the Unlicense; the MIT-0 text is "Copyright 2023
David Reid … without restriction". The MP3 path is additionally built on minimp3 (CC0)
— same file, final block.

**Consequence for a distributable library:** no attribution is even *required* under
either alternative, but MIT-0/public-domain code imposes **no patent grant either** —
which is fine only because MP3's patents are gone (§1.1). This is the key contrast with
AAC in §3.

### 1.3 FLAC and WAV/PCM — VERIFIED royalty-free

Xiph's own license page, verbatim:

> There are no licensing fees or royalties of any kind for use of the formats or their
> specifications, or for distributing, selling, or streaming media in the FLAC or Ogg
> FLAC formats. […] The reference implementation libraries are licensed under the New
> BSD License. […] Neither the FLAC nor Ogg FLAC formats nor any of the implemented
> encoding/decoding methods are covered by any known patent.

— [xiph.org/flac license page](https://xiph.org/flac/license.html) (primary).
The BSD text is [`xiph/flac` `COPYING.Xiph`](https://raw.githubusercontent.com/xiph/flac/master/COPYING.Xiph)
(3-clause: notice retention, binary notice, no endorsement). Note the caveat that the
*utilities* (`flac`, plugins) are GPL — libFLAC/libFLAC++ are the BSD parts. For this
system the decoder is `dr_flac.h` anyway (§1.2), so even the GPL utilities never enter.

**WAV/PCM:** the container is the RIFF/WAVE spec (Microsoft/IBM, 1991) and the payload
is uncompressed linear PCM samples. No pool, no royalty, no codec licence is asserted
anywhere for stereo 16-bit PCM in a WAV container. **Status: VERIFIED by absence of any
licensing programme** — this is an inference from the absence of a primary source rather
than a vendor statement, but it is the strongest kind of absence: no entity has ever
offered a WAV/PCM patent licence. (The legacy Microsoft patent on
`WAVEFORMATEXTENSIBLE` concerns multichannel/24-bit extensible headers, not the
plain stereo 16-bit `fmt ` chunk this server emits — **LIKELY, secondary-recollection,
not needed for this format**.)

**Net baseline: MP3 + FLAC + WAV-from-dr_libs is fully royalty-free and
public-domain/MIT-0/BSD at the code level. The entire MP3/FLAC/WAV stack is clean.**

---

## 2. AAC patents — and the encoder side specifically

### 2.1 The pool covers encoding *and* decoding — VERIFIED

Via LA's AAC programme FAQ, verbatim:

> An AAC patent license is needed by manufacturers or developers of end-user **encoder
> and/or decoder** products.

and

> Are there use fees for AAC? No. **License fees are due on the sale of encoders and/or
> decoders only.** There are no patent license fees due for the distribution of
> bit-streams encoded in AAC, whether such bit-streams are broadcast, streamed over a
> network, or provided on physical media.

— [Via LA, AAC licensing programme](https://www.via-la.com/licensing-programs/aac/)
(primary: the pool operator's own page). Licensors listed there include Fraunhofer-Gesellschaft,
Dolby, Philips, ETRI, NTT, Panasonic, Samsung, Sony, VoiceAge, Orange, Microsoft, Google,
NEC — i.e. the patent owners, not implementers.

The second quote is the single most decision-relevant sentence in this whole note for a
**server**: the royalty attaches to the **product that contains the codec**, not to the
bytes that product streams. Streaming AAC or streaming PCM is identical in fee terms.

### 2.2 The rate card *is* obtainable — CORRECTION to the player note

`radio-player-codecs.md` §4.2 records the AAC rate card as unloadable and marks per-unit
fees UNVERIFIED. **That is now superseded.** The page renders server-side today and the
fee table is in the HTML. Verbatim:

> **Volume (per unit\*/annual reset) — Per Unit Fee**
> For the first 1 to 500,000 units **$0.98**
> For units 500,001 to 1,000,000 $0.78
> For units 1,000,001 to 2,000,000 $0.68
> For units 2,000,001 to 5,000,000 $0.45
> For units 5,000,001 to 10,000,000 $0.42
> For units 10,000,001 to 20,000,000 $0.22
> For units 20,000,001 to 50,000,000 $0.20
> For units 50,000,001 to 75,000,000 $0.15
> For units 75,000,001 and more $0.10
> \*Licensed Products with more than two channels count as 1.5 units.

and the one-off entry cost:

> There is an initial fee of **$15,000** due upon execution of the license. This fee is a
> one-time charge and not an annual fee or an annual minimum. Small entities, defined as
> those with fifteen or fewer employees and with annual gross revenues of less than US
> $1 million, qualify for a reduced initial fee of **$1,000**.

— same [Via LA AAC page](https://www.via-la.com/licensing-programs/aac/) (primary).
**Status: VERIFIED** (read directly off the pool operator's HTML on 2026-10-16). Caveats
to carry: the table is an annual-reset volume schedule, the licence term is five years
renewable, and "Alternative Rate Structure" (R2/emerging-market) terms exist but are not
itemised in the rendered text — **those stay UNVERIFIED**.

### 2.3 The clause that governs xtape's distribution model — VERIFIED

> Licensee's **end-user products**, when sold directly or through distribution to end
> users, are covered by the license. Licensee's products that are **not end-user
> products (e.g. components or implementations) are not covered by the license** and
> would require that the party incorporating such a component or implementation into its
> end-user product obtain a license.

— same [Via LA AAC page](https://www.via-la.com/licensing-programs/aac/) (primary).

This is the structural fact for a **reusable library**: a codec *library* is not a
licensable unit; the obligation lands on **whoever ships the end-user product** built on
it. That has a direct consequence for what the author of xtape can and cannot promise
(§5).

### 2.4 fdk-aac grants no patent licence — VERIFIED, verbatim

The crux, because a permissive *copyright* licence does not transfer *patent* rights.
From fdk-aac's own `NOTICE` (the full licence, not a summary):

> 3. NO PATENT LICENSE
>
> NO EXPRESS OR IMPLIED LICENSES TO ANY PATENT CLAIMS, including without limitation the
> patents of Fraunhofer, ARE GRANTED BY THIS SOFTWARE LICENSE. Fraunhofer provides no
> warranty of patent non-infringement with respect to this software.
>
> You may use this FDK AAC Codec software or modifications thereto only for purposes
> that are authorized by appropriate patent licenses.

and, separately in its introduction:

> The Fraunhofer FDK AAC Codec Library for Android ("FDK AAC Codec") is software that
> implements the MPEG Advanced Audio Coding ("AAC") **encoding and decoding** scheme
> […] Patent licenses for necessary patent claims for the FDK AAC Codec (including those
> of Fraunhofer) may be obtained through Via Licensing (www.vialicensing.com) or through
> the respective patent owners individually **for the purpose of encoding or decoding**
> bit streams in products that are compliant with the ISO/IEC MPEG audio standards.

— [`mstorsjo/fdk-aac` `NOTICE`](https://raw.githubusercontent.com/mstorsjo/fdk-aac/master/NOTICE)
(primary: the licensor's own text). Note the *copyright* grant is otherwise BSD-like, but
it adds two non-OSI conditions worth flagging for redistribution: binary redistributors
"must make available free of charge copies of the complete source code", and modified
versions must be renamed to "Third-Party Modified Version of the Fraunhofer FDK AAC Codec
Library for Android".

**Status: VERIFIED.** This is the explicit statement that using fdk-aac requires a
separate patent licence — from the party that owns the patents.

---

## 3. Is there any FOSS AAC **encoder** that is safe to ship?

| Encoder | Exact licence (primary) | Patent grant? | Practical for embedded consumer product? |
|---|---|---|---|
| **fdk-aac** (`mstorsjo/fdk-aac`) | Fraunhofer FDK AAC Codec Library licence — BSD-like copyright, plus the §2.4 conditions | **No** — §3 "NO EXPRESS OR IMPLIED LICENSES TO ANY PATENT CLAIMS" | No. Large, not embedded-tuned; and binary redistribution forces complete-source availability. |
| **FFmpeg native `aac`** (`libavcodec/aacenc.c`) | **LGPL-2.1-or-later** — verbatim file header: "FFmpeg is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License … either version 2.1 of the License, or (at your option) any later version." ([`aacenc.c`](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/aacenc.c), [`aacenc.h`](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/aacenc.h)) | **No patent grant anywhere in FFmpeg.** FFmpeg's own legal page: "Does FFmpeg use patented algorithms? A: We do not know, we are not lawyers…" and "once you start trying to make money from patented technologies, the owners of the patents will come after their licensing fees. Notably, MPEG LA is vigilant and diligent about collecting for MPEG-related technologies" ([ffmpeg.org/legal.html](https://www.ffmpeg.org/legal.html)). | No. LGPL-2.1 in a statically linked ESP32 firmware is the classic relink-requirement problem, *and* it is desktop-scale code. |
| **vo-aacenc** (`mstorsjo/vo-aacenc`) | **Apache-2.0** — `COPYING` is the verbatim Apache License 2.0; `NOTICE` contains its §3 patent clause ([`COPYING`](https://raw.githubusercontent.com/mstorsjo/vo-aacenc/master/COPYING)) | **Only a contributor patent grant, and it is the wrong patents.** Apache-2.0 §3 grants a "perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable" patent licence, but "**only to those patent claims licensable by such Contributor that are necessarily infringed by their Contribution(s)**" ([`NOTICE`](https://raw.githubusercontent.com/mstorsjo/vo-aacenc/master/NOTICE)). That reaches VisualOn/contributor-owned claims, **not** the Fraunhofer/Dolby/Philips/… AAC pool claims listed at §2.1. | No. Unmaintained VisualOn Stagefright lineage, no HE-AAC. |
| **faac** (`knik0/faac` → `FreewareAdvancedAudio/faac`) | **LGPL-2.1** — `COPYING` is verbatim LGPLv2.1 ([`COPYING`](https://raw.githubusercontent.com/FreewareAdvancedAudio/faac/master/COPYING)) | **No patent grant.** | No. LGPL, legacy quality, and the repo itself now redirects (README: "This project has moved to https://github.com/FreewareAdvancedAudio/faac"). |

### The blunt answer to §3

**No open-source AAC encoder is "safe to ship without a patent licence".** Every one of
the four is copyright-permissive-or-not as tabulated, and **none of them conveys the
pool's patent rights** — fdk-aac's licensor *says so explicitly* (§2.4). Apache-2.0's
patent clause (§3, vo-aacenc) is the only real patent grant among them and it is a
*contributor-scoped* grant that does not reach the AAC essential claims. This is not a
licence-choice problem that better research can solve: **the patents bind the technique,
so swapping implementation cannot change the obligation.** The only ways to ship an AAC
encoder are (a) take the Via LA pool licence (§2.2 costs), (b) license bilaterally from
the patent owners, or (c) rely on the end-user product's own pre-existing pool licence —
note Via LA's own FAQ: "Google pay[s] license fees on behalf of its Android OEM
partners? **No.** … Manufacturers of Android-based products must obtain their own AAC
patent licenses".

---

## 4. Decode-side re-check for the server direction

**(a) Is a decoder's patent obligation the same as an encoder's?** **Yes — VERIFIED.**
§2.1: a licence "is needed by manufacturers or developers of end-user **encoder and/or
decoder** products", and "License fees are due on the sale of encoders and/or decoders
only." The pool does not publish a cheaper decoder-only tier on the page; the same unit
schedule applies. Decode is not a loophole — and it is, if anything, the *more* commonly
enforced side because every player contains a decoder.

One nuance the server specifically should note, verbatim from the same page:

> A device is considered a multichannel product if it includes a decoder that receives an
> AAC signal and decodes that signal into three or more channels. […] a device which
> **transcodes** a 5.1 channel AAC signal into a different (compressed or uncompressed)
> format [is a multichannel product]. As an exception to the above, a device that decodes
> a multichannel AAC signal and **only down mixes to stereo** (either during or after
> decoding), without providing any multichannel output, would not be considered a
> multichannel product.

→ The 1.5× "more than two channels" multiplier (§2.2) is a **multichannel** concept.
xtape's fixed station format is **stereo 16-bit**, so a server that decodes 5.1 AAC and
downmixes to its stereo station format sits in the *stereo* tier, not the multichannel
tier — but a transcoding server that *provided* 3+ channels would not. **Status: VERIFIED
for the stereo case; this is the one place the server's fixed-format design has a
licensing (not just architectural) payoff.**

**(b) Are any of MP3/FLAC/WAV a patent concern?** **No.**
- **MP3:** programme terminated 2017-04-23 (§1.1), VERIFIED.
- **FLAC:** "Neither the FLAC nor Ogg FLAC formats nor any of the implemented
  encoding/decoding methods are covered by any known patent" and "no licensing fees or
  royalties of any kind" ([xiph.org](https://xiph.org/flac/license.html)), VERIFIED.
  Reference libs BSD (`COPYING.Xiph`); the pipeline uses `dr_flac.h` (public domain/MIT-0).
- **WAV/PCM:** no codec licence exists to take; trivial and unencumbered (§1.3).

So the only format in the entire question that carries a patent obligation is **AAC**,
and only if an AAC decoder is added.

---

## 5. The decisive asymmetry: PCM-serving server vs. compressed-emitting server

Ground this in the actual pipeline (`resources/mi_decode.c` → `mi_resample.c` →
`mini_icecast.c`), because it is what makes the answer different from the player note.

```
SD file (.mp3/.flac/.wav)
  → mi_dec_open(): extension switch K_MP3|K_FLAC|K_WAV        [DECODE, per file]
  → drmp3 / drflac / drwav  →  native-rate PCM
  → mi_rs_process(): windowed-sinc → ONE station format (stereo int16)   [RESAMPLE]
  → mini_icecast: endless WAV, Content-Type: audio/wav        [SERVE, no encoder]
```

There is **no encoder stage in the diagram**. Read the asymmetry off that diagram:

1. **A server that only decodes what it stored is constrained only by the files the user
   puts on the SD card.** Its licence surface is exactly the set of *decoders* compiled
   in. Today that set is MP3/FLAC/WAV, all clean (§1). The station format it emits is
   raw PCM in a WAV container — **unencumbered**. Therefore: adding AAC support changes
   the product's licensing position only by adding *one decoder* to that set, and the
   only reason to do it is that the author wants `.aac`/`.m4a` files to be playable off
   the SD card. Nothing about the serving path obliges AAC.

2. **A server that must emit compressed audio has a real encoder problem.** A power-user
   case — transcode the station to AAC to save bandwidth, or offer an AAC "mount" to
   save WiFi/4G — would require an **AAC encoder, which the current architecture does not
   have at all**. That is where §3 becomes blocking: the encoder is the hard case, there
   is no safe FOSS one, and the fee is per end-user unit (§2.2). If the author ever adds
   an encoder, `fdk-aac`'s copyright licence is usable but its §3 disclaimer plus the
   pool licence is the actual cost.

3. **The server's fixed stereo station format is a licensing asset.** Per §4(a), keeping
   output at stereo means even a 5.1-AAC-decoding server stays in the stereo rate tier.
   The architecture already does this for free.

**Why this differs from the player note:** `radio-player-codecs.md` §4.2 asks "which AAC
*decoder* can I ship" because a player is a decode-only device that must play *incoming*
streams (BBC/Radio Paradise are HE-AAC) — so it is forced to answer the decode question.
A PCM-serving server can decline the question entirely. The player's mandatory AAC is
the server's optional AAC.

---

## Verdict

**Where does AAC enter for a server supporting MP3/FLAC/WAV? Nowhere — unless the author
chooses to decode AAC files, or later adds an encoder.**

- **Not via MP3/FLAC/WAV.** All three are royalty-free; the decoder code is public
  domain/MIT-0 (dr_libs) and BSD (FLAC reference) (§1).
- **Not via the wire format.** The server emits raw PCM inside WAV; there is no encoder
  stage, so no AAC patent is practised on the output path (§5.1).
- **Only via a decision:** (i) *decode-side* — add an AAC/HE-AAC decoder so `.aac`/`.m4a`
  files on the SD card play. That puts an AAC decoder in the end-user product and a pool
  licence obligation on whoever ships that product ($0.98/unit first 500k; $15,000 or
  $1,000 initial fee) — no FOSS licence avoids it (§2, §4). (ii) *encode-side* — add an
  AAC encoder to emit a compressed station. Same obligation, no safe FOSS option at all
  (§3).
- **For the library's licensing posture:** a *library* is not the licensable unit
  (§2.3), so the author cannot and should not promise AAC patent coverage to downstream
  users. The clean, consistent-with-`02-packaging-and-licensing.md` move is the same
  pluggable-backend seam already recommended there: keep the **default build
  MP3/FLAC/WAV only** (fully clean, fully distributable), and if AAC is wanted, make it
  an **opt-in external dependency** whose consumer takes the pool licence. Do not vendor
  fdk-aac/FAAD2 into the core.

**Honest one-liner:** MP3/FLAC/WAV is a **zero-royalty, zero-encoder** design; AAC is
orthogonal to it, and it costs money and a licence only if you deliberately add an AAC
codec — never because of what the server already does.

---

## Confidence ledger

**VERIFIED (primary source read in this session)**
- MP3: licensing programme terminated 2017-04-23; last core patent expired — Fraunhofer
  mp3 page + Audio Blog.
- dr_mp3 / dr_flac / dr_wav: "public domain or MIT-0", Unlicense + MIT-0 texts present.
- FLAC: no fees/royalties, "not covered by any known patent", reference libs New BSD.
- AAC pool covers encoder **and** decoder; fees due on sale of encoders/decoders only;
  no bitstream distribution fees — Via LA AAC page.
- AAC per-unit schedule ($0.98 → $0.10) and $15,000 / $1,000 initial fee; 1.5-unit
  multichannel rule; components/implementations not covered — Via LA AAC page.
- fdk-aac §3 "NO EXPRESS OR IMPLIED LICENSES TO ANY PATENT CLAIMS … only for purposes
  that are authorized by appropriate patent licenses" — fdk-aac NOTICE.
- FFmpeg `aacenc.c`/`.h` LGPL-2.1-or-later file header; FFmpeg legal page disclaims
  patent knowledge and warns MPEG LA collects.
- vo-aacenc Apache-2.0 + §3 contributor-scoped patent grant; faac LGPLv2.1.
- Pipeline shape (decode → resample to fixed stereo int16 → WAV over HTTP) from
  `resources/mi_decode.c`, `mi_resample.h`, `main.c`.

**UNVERIFIED / LIKELY (do not quote as fact)**
- Via LA "Alternative Rate Structure" / R2 emerging-market AAC rates — not itemised in
  the rendered page.
- Any *current* live MP3 implementation-specific patent — Fraunhofer's caveat, no
  primary source identifies an active pool or rate.
- WAV/PCM "no patents at all" is an argument from absence of any licensing programme,
  not a vendor statement.
- Whether the $0.98 tier covers *decode-only* products at a different (lower) rate than
  encode+decode — the page shows one schedule and does not split by function; **treat
  "same fee for decoder-only" as the page's plain reading, not a stated rate.**
- Exact FRAND/bilateral terms for any individual patent owner.

**Sources classified secondary (used only for orientation, not cited as authority):**
Lumenci's AAC-licensing blog appeared in search results; nothing here relies on it.

# S6 — Complete codec & container licensing/patent matrix for the server's format set

Scope: the redistributable ESP32-S3 radio-server library. **Output:** MP3, Opus-in-Ogg.
**Input:** MP3, Ogg Vorbis, Opus, WebM (Matroska/EBML carrying Opus or Vorbis), plus FLAC
and WAV.

Read on **2026-10-16** (UTC), all primary sources fetched and read directly. This note
**extends** the existing research and does not repeat it:

- [`s1-aac-licensing.md`](s1-aac-licensing.md) §1 — MP3 patent expiry, `dr_libs`
  public-domain/MIT-0, FLAC "not covered by any known patent", WAV.
- [`s2-encoder-feasibility.md`](s2-encoder-feasibility.md) §3–§5 — shine/LAME, Opus
  encode feasibility, `libvorbis` float-only, tremor decode-only.
- [`../radio-player-codecs.md`](../radio-player-codecs.md) §4 — decoder table.

**New here:** exact licence *versions* (LGPL-**2.0** not 2.1), the Vorbis patent statements
verbatim, the Opus patent-grant/withholding split read off the IETF filings themselves,
the whole Matroska/WebM block, and both MP3 pools' closure.

Copyright and patents are kept strictly separate: **copyright binds the code; patents bind
the technique.** A permissive copyright licence says nothing about patents.

---

## 1. The matrix

| Component | Format | Licence (exact) | Patent grant? | Redistribution conditions | Verdict |
|---|---|---|---|---|---|
| `dr_mp3.h` ([file](https://raw.githubusercontent.com/mackron/dr_libs/master/dr_mp3.h)) | MP3 decode | **Public domain (Unlicense) OR MIT-0**, dual. Header line 1: *"MP3 audio decoder. Choice of public domain or MIT-0."*; blocks at EOF: `ALTERNATIVE 1 - Public Domain (www.unlicense.org)` / `ALTERNATIVE 2 - MIT No Attribution`, `Copyright 2023 David Reid` | **No** (neither alternative mentions patents) | None. Attribution not even required | **SHIP** — MP3 patents are gone (§2.1) |
| `esphome-libs/micro-mp3` ([LICENSE](https://raw.githubusercontent.com/esphome-libs/micro-mp3/main/LICENSE), [NOTICE](https://raw.githubusercontent.com/esphome-libs/micro-mp3/main/NOTICE)) | MP3 decode | **Apache-2.0** (verbatim text; byte-identical to the registry copy at [`license.txt`](https://components-file.espressif.com/components/esphome/micro-mp3/0.4.0/license.txt)). Fork of PacketVideo OpenCore | **Yes — §3**, but only "to those patent claims licensable by such Contributor that are necessarily infringed by their Contribution(s)" | Retain notices; **include the `NOTICE` file** (it carries the PacketVideo/OpenCore attribution); state modifications. No trademark grant | **SHIP** — permissive alternative to dr_mp3; attribution required |
| `libvorbis` ([`COPYING`](https://raw.githubusercontent.com/xiph/vorbis/master/COPYING)) | Ogg Vorbis decode | **BSD-3-Clause**, `Copyright (c) 2002-2020 Xiph.org Foundation` | **No patent clause in the licence.** Patent position is Xiph's format-level statement, not a code grant (§2.2) | Retain copyright + conditions + disclaimer in source; reproduce in docs/materials for binaries; **no endorsement** using Xiph's name | **SHIP** |
| `tremor` ([`COPYING`](https://gitlab.xiph.org/xiph/tremor/-/raw/master/COPYING), [README](https://gitlab.xiph.org/xiph/tremor/-/raw/master/README)) | Ogg Vorbis decode (integer) | **BSD-3-Clause** — byte-identical to libogg's `COPYING` (only the copyright year differs). README: *"No floating point operations are used at any point in either setup or decode"* → **decode-only** | **No** | As libvorbis | **SHIP** (decode only) |
| `libogg` ([`COPYING`](https://raw.githubusercontent.com/xiph/ogg/master/COPYING)) | Ogg container (mux + demux) | **BSD-3-Clause**, `Copyright (c) 2002, Xiph.org Foundation` | **No** | As libvorbis | **SHIP** |
| `xiph/opus` ([`COPYING`](https://raw.githubusercontent.com/xiph/opus/main/COPYING)) | Opus encode **and** decode | **BSD-3-Clause** (no endorsement by "Internet Society, IETF or IETF Trust") **plus** three patent-licence URLs appended to `COPYING`: Xiph [ipr/1524](https://datatracker.ietf.org/ipr/1524/), Microsoft [ipr/1914](https://datatracker.ietf.org/ipr/1914/), Broadcom [ipr/1526](https://datatracker.ietf.org/ipr/1526/) | **Yes** — royalty-free, irrevocable, from three of the patent holders; **four others disclosed potentially royalty-bearing terms and name no grant** (§2.3) | Retain copyright + conditions + disclaimer; no endorsement. Patent grant **terminates** if you file an Opus patent claim | **SHIP** (both directions) — see §4 for the encode/decode asymmetry |
| Matroska specification ([`LICENSE.md`](https://raw.githubusercontent.com/ietf-wg-cellar/matroska-specification/master/LICENSE.md), [README](https://raw.githubusercontent.com/ietf-wg-cellar/matroska-specification/master/README.md)) | Matroska container | **CC-BY-4.0** (spec text). README: *"The work in this repository is licensed under the Creative Commons Attribution 4.0 International (CC BY 4.0)"*. Its component table gives libEBML/libMatroska **LGPL**, libEBML2/Core C **BSD** | **Explicitly NO** — CC-BY-4.0 §2(b)(2): *"Patent and trademark rights are not licensed under this Public License."* | Attribution + licence notice if you redistribute the spec text; no trademark rights | **SHIP** — container is unencumbered in practice, but **you get no patent grant from it** (§2.4) |
| `webmproject/libwebm` ([`LICENSE.TXT`](https://raw.githubusercontent.com/webmproject/libwebm/main/LICENSE.TXT), [`PATENTS.TXT`](https://raw.githubusercontent.com/webmproject/libwebm/main/PATENTS.TXT)) | Matroska/WebM mux + demux (C++) | **BSD-3-Clause**, `Copyright (c) 2010, Google Inc.` + separate **Additional IP Rights Grant (Patents)** | **Yes** — but only for *"these implementations"* = *"the copyrightable works that implement the WebM codecs distributed by Google as part of the WebM Project"*, and only claims *"licensable by Google that are necessarily infringed by these implementations"*; terminates on litigation | BSD-3 notice retention; patent grant is code-scoped and litigation-terminated | **SHIP** — but it is C++ and desktop-scale; a hand-written EBML reader is smaller and carries no licence at all (**LIKELY**) |
| `vi/mkvparse` ([source](https://raw.githubusercontent.com/vi/mkvparse/master/mkvparse.py), [README](https://github.com/vi/mkvparse)) | Matroska parse (Python) | **MIT declared in-file** — `# Licence==MIT; Vitaly "_Vi" Shukela 2012`, and README ends `Licence=MIT`. **No `LICENSE`/`COPYING` file exists** (GitHub API reports no licence) | **No** | Retain the copyright/notice lines | **Reference only** — Python, not vendorable into a C component; MIT-by-declaration, not by licence file |
| `Matroska-Org/foundation-source` ([`LICENSE`](https://raw.githubusercontent.com/Matroska-Org/foundation-source/master/LICENSE)) | EBML + Matroska parse (C) — libEBML2/libMatroska2 | **BSD-3-Clause**, `Copyright (c) 2010, Matroska (non-profit organisation)` | **No** | BSD-3 notice retention; no endorsement | **SHIP** — this is the permissive C path; matroska.org confirms *"License: BSD"* ([libraries page](https://www.matroska.org/downloads/libraries.html)) |
| `Matroska-Org/libebml` ([`LICENSE.LGPL`](https://raw.githubusercontent.com/Matroska-Org/libebml/master/LICENSE.LGPL)), `libmatroska` ([same](https://raw.githubusercontent.com/Matroska-Org/libmatroska/master/LICENSE.LGPL)) | EBML + Matroska parse (C++) | **LGPL-2.1** (`GNU LESSER GENERAL PUBLIC LICENSE Version 2.1, February 1999`); matroska.org: *"License: LGPL 2.1 or later"* | **No** | LGPL-2.1 static-link obligations | **AVOID for this library** — C++, LGPL; use libEBML2 (BSD) instead |
| `toots/shine` = `savonet/shine` ([`COPYING`](https://raw.githubusercontent.com/toots/shine/master/COPYING)) | MP3 **encode** | **GNU Library General Public License, Version 2, June 1991 = LGPL-2.0** — *not* LGPL-2.1. Source files carry **no** per-file licence header and no "or later" clause → **LGPL-2.0-only** | **No patent grant** | LGPL-2.0: ship the licence text, ship shine's complete source, and permit relinking/ modification by the customer. Static linking into a single ESP32 image is the classic case this bites | **SHIP WITH CONDITIONS** — patents clear (§2.1), but LGPL-2.0 means isolate shine as a separate component and publish its source |
| `fknrdcls/mp3_shine_esp32` ([repo](https://github.com/fknrdcls/mp3_shine_esp32)) | MP3 encode (ESP32 port) | **GPL-3.0** (GitHub API `spdx_id: GPL-3.0`) | No | Copyleft | **DO NOT USE** — matches s2's warning |
| `lameproject/lame` ([`COPYING`](https://raw.githubusercontent.com/lameproject/lame/master/COPYING), [`LICENSE`](https://raw.githubusercontent.com/lameproject/lame/master/LICENSE)) | MP3 encode | **LGPL-2.0** (same 1991 Library GPL text) **plus** a non-standard `LICENSE` file with extra demands | No | `LICENSE` asks that you *"Fully acknowledge that you are using LAME"* and that *"If you make modifications to LAME, you \*must\* release these modifications back to the LAME project"* — an added condition beyond the LGPL | **AVOID** — ruled out on performance (s2 §3) *and* carries extra terms |
| `esphome-libs/micro-flac` ([`LICENSE`](https://raw.githubusercontent.com/esphome-libs/micro-flac/main/LICENSE)) | FLAC encode/decode | **Apache-2.0** (no `NOTICE` file: `/NOTICE` → 404) | **Yes — §3**, contributor-scoped | Retain notices; state modifications | **SHIP** |
| `dr_flac.h`, `dr_wav.h` | FLAC / WAV decode | **Public domain OR MIT-0** (verified in [s1](s1-aac-licensing.md) §1.2) | No | None | **SHIP** — cross-ref, not re-verified here |
| `espressif/esp_audio_codec` ([`LICENSE`](https://raw.githubusercontent.com/espressif/esp-adf-libs/master/esp_audio_codec/LICENSE)) | AAC/MP3/Vorbis/Opus decode | **Espressif Modified MIT** — *"Redistribution … FOR USE WITH NON-ESPRESSIF PRODUCTS is strictly prohibited"* | No | Not vendorable; depend-on only | **Cannot be bundled** — external dependency only ([`02-packaging-and-licensing.md`](../02-packaging-and-licensing.md) §1) |

Ogg **muxing** for the Opus output path is the same `libogg` row (BSD-3); the Ogg Opus
encapsulation is [RFC 7845](https://www.rfc-editor.org/rfc/rfc7845.txt), which contains
**zero** occurrences of the word "patent".

---

## 2. Patent position, stated separately (this is where the real answers are)

### 2.1 MP3 — no live programme, encoding included. VERIFIED

Every known MP3 patent programme is closed, and I checked both operators plus the vendor:

- Fraunhofer IIS's own mp3 page, verbatim: *"On April 23, 2017, Technicolor's mp3 licensing
  program for certain mp3 related patents and software of Technicolor and Fraunhofer IIS has
  been terminated."* — [iis.fraunhofer.de](https://www.iis.fraunhofer.de/en/ff/amm/consumer-electronics/mp3.html).
- Mechanism and date, verbatim: *"That day, the last of the core mp3 patents which were part
  of the licensing program by Fraunhofer and Technicolor, expired."* — [Fraunhofer Audio
  Blog, 2017-05-18](https://www.audioblog.iis.fraunhofer.com/mp3-software-patents-licenses).
- **The second pool**, which the earlier notes did not cover: Sisvel/Audio MPEG (Philips,
  TDF, IRT, BRW patents), verbatim: *"Sisvel and its subsidiary Audio MPEG managed the MPEG
  Audio licensing programme. […] **The Audio MPEG licensing programme is no longer active**"*
  — [Sisvel, MPEG Audio (legacy programme)](https://www.sisvel.com/licensing-programmes/legacy-programs/mpeg-audio/).
- The legacy portals are dead: `audiompeg.com` now serves a "this domain is for sale"
  parking page; `mp3licensing.com` does not resolve.
- Neither current pool operator lists an **active** MP3 programme: 0 literal occurrences of
  "mp3" on [Via LA's licensing-programmes page](https://www.via-la.com/licensing-programs/),
  and [Sisvel's programme index](https://www.sisvel.com/licensing-programmes/) lists
  "MPEG Audio" only under **Legacy programmes**, whose own page states it "is no longer
  active" (above).

**Answer to "does MP3 encoding carry any remaining patent obligation?": No — no programme
exists and no vendor asserts one.** The honest caveat is Fraunhofer's own, verbatim:
*"the end of the mp3 licensing program does not automatically mean that all mp3 technology
is available license-free now. Apart from the core mp3 patents included in the licensing
program, there might still be some implementation-specific patents (or patents for other
functional enhancements) that have not expired."* No primary source identifies such a
patent or a rate for it. **No encoding-specific programme exists.**

### 2.2 Vorbis — Xiph's statement, verbatim. VERIFIED

This is the historical statement the question asks for; it is Xiph's own, on the project
page and in the FAQ:

- *"Ogg Vorbis is a fully open, non-proprietary, **patent-and-royalty-free**, general-purpose
  compressed audio format for mid to high quality (8kHz-48.0kHz …) audio and music at fixed
  and variable bitrates from 16 to 128 kbps/channel."* — [xiph.org/vorbis](https://xiph.org/vorbis/).
  The same page adds: *"The libvorbis reference implementation provides both a standard
  encoder and decoder under a BSD license."*
- [Vorbis FAQ](https://www.xiph.org/vorbis/faq/), verbatim: *"Ogg Vorbis is a new audio
  compression format. … It is different from these other formats because it is completely
  free, open, and **unpatented**."* and *"**What licensing applies to the Ogg Vorbis
  format?** The Ogg Vorbis specification is in the public domain. It is completely free for
  commercial or noncommercial use. That means that commercial developers may independently
  write Ogg Vorbis software which is compatible with the specification for no charge and
  without restrictions of any kind."* and *"**Are there licensing fees for distributing,
  selling, or streaming media in the Ogg Vorbis format?** No."* and *"Vorbis is patent and
  license-free, so you will never need to pay anyone in order to sell, give away, or stream
  your own music."*
- The specification's own colophon, verbatim: *"**Anyone may freely use and distribute the
  Ogg and Vorbis specification, whether in a private, public or corporate capacity.** However,
  the Xiph.Org Foundation and the Ogg project (xiph.org) reserve the right to set the Ogg
  Vorbis specification and certify specification compliance. Xiph.Org's Vorbis software CODEC
  implementation is distributed under a BSD-like license. This does not restrict third
  parties from distributing independent implementations of Vorbis software under other
  licenses."* — [Vorbis I specification](https://xiph.org/vorbis/doc/Vorbis_I_spec.html).
- The earliest primary record, Monty on the Vorbis list, 2000-05-15, verbatim: *"We believe
  that we are free of any patent encumberance and are currently doing full-blown legal patent
  reviews to make sure of it. Vorbis has been designed to avoid patents, we just need to
  verify that we've succeeded."* — [lists.xiph.org](http://lists.xiph.org/pipermail/vorbis/2000-May/012596.html).

**Verdict: no Vorbis patent licence is required, per the format's owner.** Note the wording
is weaker than FLAC's ("not covered by any known patent"): Xiph says *unpatented* and *public
domain*, from a 2000-era review, and discloses no patent list. That is a statement of belief
by the owner, not a grant or a warranty — treat it as such.

### 2.3 Opus — grant from three holders, withholding declared by four. VERIFIED

`COPYING` appends only the three *granting* holders. The full picture is on
[opus-codec.org/license](https://opus-codec.org/license/), which is unambiguous:

- Banner, verbatim: *"Opus has a freely available specification, a BSD-licensed, high-quality
  reference encoder and decoder, and protective, royalty-free licenses for the required
  patents. The copyright and patent licenses for Opus are automatically granted to everyone
  and do not require application or approval."*
- Scope, verbatim: *"**You can encode or decode Opus-compatible streams for any purpose at no
  cost.** You can integrate the reference Opus encoder and decoder in any application,
  program or product, even commercially, at no cost. You can create your own compatible
  implementations of the Opus specification and give them away or sell them."*
- The grant text itself (Xiph), from [IPR #1524](https://datatracker.ietf.org/ipr/1524/),
  verbatim: *"Xiph.Org Foundation ("Xiph") hereby grants to you a perpetual, worldwide,
  non-exclusive, no-charge, royalty-free, irrevocable (except as stated in this license)
  license under Licensed Patents to make, have made, use, offer to sell, sell, import,
  transfer, and otherwise run, modify … and reproduce any Implementation."* — where
  *"An Implementation means any Reference Implementation, or another implementation that
  complies with the Specification"*, i.e. **encoder or decoder**.
- Microsoft's filing ([IPR #1670](https://datatracker.ietf.org/ipr/1670/)) is even more
  explicit, with a separate encoder promise: *"**Encoder Patent Promise.** I … irrevocably
  promise not to assert my Necessary Encoder Claims against you for your Encoder
  Implementation."*
- **The withholding.** Same page, verbatim: *"While Xiph.Org, Broadcom, and Microsoft filed
  IPR disclosures giving royalty-free licenses to their patents used in Opus, **four companies
  that did not directly participate in the development of Opus, Qualcomm, Huawei, France
  Telecom, and Ericsson, filed IPR disclosures with potentially royalty-bearing terms**."*
  Xiph's mitigation, verbatim: *"external counsel Dergosits & Noah has advised us that **Opus
  can be implemented without the need to license the patents disclosed by Qualcomm, Huawei,
  France Telecom, or Ericsson**."*

The France Telecom filing was analysed in public by Opus's own authors, and the analysis is
encode-specific, verbatim: *"On January 18th, France Telecom filed an IPR disclosure against
Opus citing a single patent under non-royalty free terms. … **The patent is directed to
encoders, therefore it cannot affect the Opus specification, which only includes conformance
tests for the decoder**"* and *"The Opus specification only demands specific behavior from
decoders, leaving the encoder largely unspecified. Even if France Telecom were to continue to
assert its patent against Opus, there's no limit to what we could change in the encoder to
avoid whatever theory they have."* — [Mozilla Hacks, "Defending Opus", 2013-02-06](https://hacks.mozilla.org/2013/02/defending-opus/).

[RFC 6716](https://www.rfc-editor.org/rfc/rfc6716.txt) itself contains **zero** occurrences of
"patent" — the RFC grants nothing; the grants live in the IPR filings and on opus-codec.org.

### 2.4 Matroska / WebM — the container is unencumbered by statement, not by grant. VERIFIED

This is the least-covered row and the answer has three independent parts.

**(a) The format's own statement.** matroska.org's legal page, verbatim:

> *"Matroska is an open and free technology. **Anyone can use it or modify it for their own
> needs without paying any license or patents.** However the name and the logo may NOT be used
> freely under some circumstances. … The name and logos may only be used without any
> permission in non commercial context. For commercial use (reading, writing, extending) we
> ask that you contact us to make sure we can verify Matroska is used correctly. We do ask
> that if you derive value from using Matroska that you are invited to sponsor us …"*

— [matroska.org/legal.html](https://www.matroska.org/legal.html). Note precisely what this is:
a no-cost/patent-free assertion plus **trademark** restrictions and a sponsorship *request*.
It is not a patent grant and not a copyright licence.

**(b) The specification's copyright licence explicitly excludes patents.** The official spec
repo (the one that generates RFC 9559) is CC-BY-4.0, and CC-BY-4.0 §2(b)(2) says verbatim:
*"**Patent and trademark rights are not licensed under this Public License.**"* —
[spec `LICENSE.md`](https://raw.githubusercontent.com/ietf-wg-cellar/matroska-specification/master/LICENSE.md).
Its component table lists libEBML/libMatroska as LGPL and libEBML2/Core C as BSD; the free
path to an EBML/Matroska parser is **libEBML2/libMatroska2, BSD-3-Clause**
([`LICENSE`](https://raw.githubusercontent.com/Matroska-Org/foundation-source/master/LICENSE),
confirmed by matroska.org's own [libraries page](https://www.matroska.org/downloads/libraries.html):
*"License: LGPL 2.1 or later"* for libEBML/libMatroska, *"License: BSD"* for libEBML2/libMatroska2).
[RFC 9559](https://www.rfc-editor.org/rfc/rfc9559.txt) carries only the standard IETF
boilerplate and, like RFC 3533 and RFC 7845, **no** patent or royalty text.

**(c) Google's grant is a *codec* grant, not a *container* grant.** The WebM licensing pages
separate three instruments, and none of them covers the Matroska container structure:

- **Software** — BSD-3-Clause, Google ([webmproject.org/license/software](https://www.webmproject.org/license/software/)).
- **Additional IP Rights Grant (Patents)** — *"'These implementations' means the copyrightable
  works that implement the WebM codecs distributed by Google as part of the WebM Project.
  Google hereby grants to you a perpetual, worldwide, non-exclusive, no-charge, royalty-free,
  irrevocable (except as stated in this section) patent license … where such license applies
  only to those patent claims, both currently owned by Google and acquired in the future,
  licensable by Google that are necessarily infringed by these implementations of WebM."* —
  [license/additional](https://www.webmproject.org/license/additional/), reproduced verbatim as
  libwebm's [`PATENTS.TXT`](https://raw.githubusercontent.com/webmproject/libwebm/main/PATENTS.TXT).
- **Bitstream License** — *"Google hereby grants to You a perpetual … royalty-free … patent
  license to make, have made, use, offer to sell, sell, import, and otherwise transfer
  implementations of the WebM Specifications, where such license applies only to those patent
  claims … licensable by Google that are necessarily infringed by implementation of the WebM
  Specifications. … «WebM Specifications» means **the specifications to the WebM codecs** as
  embodied in the source code to the WebM codecs or any written description of such
  specifications"* — [license/bitstream](https://www.webmproject.org/license/bitstream/).

**So: the WebM "royalty-free" claim is about the *codecs* (VP8/VP9, Vorbis/Opus) and about
Google's own implementations of them.** It is not a patent licence to the EBML/Matroska
container format, and it does not reach a third party's own parser. The container's clean
position rests on (a) the Matroska association's statement and (b) the absence of any
container patent programme — not on a grant. **LIKELY (inference), but it is the strongest
available reading, and no primary source asserts a Matroska container patent.**

**(d) Does a WebM file containing video drag in VP8/VP9 royalties?** Two facts, both primary:

- Google's position, from its own FAQ: *"Google does not demand royalty payments for use of
  the codecs WebM supports (VP8/VP9 video and Vorbis/Opus audio)"*, and *"The patent grant
  applies to both hardware and software implementations."* —
  [WebM FAQ, Licensing](https://www.webmproject.org/about/faq/).
- But a **third-party royalty-bearing VP9 pool exists**, run by Sisvel: *"Sisvel offers
  royalty-bearing, non-transferable, non-assignable, non-exclusive licences … to patents
  related to the VP9 specification. The licence offered covers any consumer product making
  use of the VP9 specification. … royalty rates are offered on a per product basis …
  Consumer Display Device EUR 0.24 / EUR 0.18; Consumer Non-Display Device EUR 0.08 /
  EUR 0.06 … **The licence offered by Sisvel does not cover components or subassemblies such
  as, without limitation, chipsets, semiconductor components or embedded modules.**"* —
  [Sisvel, Video coding platform: VP9](https://www.sisvel.com/licensing-programmes/audio-and-video-coding-decoding/video-coding-platform/).
  Google's own FAQ concedes the general case: *"there is no way to prevent third parties from
  demanding licensing fees on these types of technologies."*

**Audio-only answer: yes, the audio-only path avoids it — provided the library never decodes
a VP8/VP9 bitstream.** Skipping `SimpleBlock`s whose track number belongs to a video track
is container parsing, not video decoding, so no VP8/VP9 patent is practised. The moment a
VP8/VP9 decoder ships, the Sisvel VP9 pool question becomes live for whoever ships the
end-user product (**LIKELY** — the pool's own wording scopes it to products, not components,
which is exactly the s1 §2.3 component/end-user split again).

---

## 3. One-line verdict per format

| Format | Safe to ship in a permissively-licensed library? |
|---|---|
| MP3 (decode) | **Yes** — `dr_mp3` public domain/MIT-0, or `micro-mp3` Apache-2.0 (+NOTICE); patents expired |
| MP3 (encode) | **Yes on patents**; the code is the constraint — shine is **LGPL-2.0-only** (relink + source obligations), LAME is worse |
| Ogg Vorbis (decode) | **Yes** — libvorbis/tremor BSD-3, no Vorbis patent licence needed |
| Opus (decode) | **Yes** — BSD-3 + royalty-free grants from Xiph/Microsoft/Broadcom |
| Opus (encode) | **Yes**, with the same caveat as decode — the only royalty-bearing claims are third-party and encoder-directed (§4) |
| Ogg container (mux/demux) | **Yes** — libogg BSD-3; RFC 3533/7845 say nothing about patents and need to |
| Matroska/WebM container (audio only) | **Yes** — but on a statement-plus-absence basis, **not** on a patent grant; respect the Matroska name/logo |
| WebM carrying VP8/VP9 video | **No, if you decode the video** — Sisvel's VP9 pool is royalty-bearing. Audio-only demux is fine |
| FLAC | **Yes** — Xiph: *"not covered by any known patent"* ([s1](s1-aac-licensing.md) §1.3); `micro-flac` is Apache-2.0 |
| WAV/PCM | **Yes** — no licence exists to take ([s1](s1-aac-licensing.md) §1.3) |

---

## 4. Does the *combination* matter?

**Yes, in three specific ways — and no in one.**

1. **Encoder vs decoder for Opus: the grants are symmetric, the third-party claims are not.**
   Xiph's grant covers *"any Implementation"*; Microsoft filed a separate **Encoder** Patent
   Promise ([IPR #1670](https://datatracker.ietf.org/ipr/1670/)). So shipping the encoder does
   **not** differ from shipping the decoder *with respect to the granting holders*. It differs
   with respect to the **withholding** ones: the single analysed royalty-bearing claim is
   *"directed to encoders"* (Mozilla/Opus authors, above). Practically, the encoder is the
   side where a third party's theory would land — and the reference encoder is exactly what
   xtape would ship.
2. **A container carries obligations distinct from its codecs — but not patent ones here.**
   Ogg: libogg BSD-3 (code) + Xiph's statement covering *"the Ogg and Vorbis specification"*.
   Matroska: CC-BY-4.0 spec (**patents expressly not licensed**, §2(b)(2)) + the Matroska
   association's no-cost assertion + **trademark rules** on the name and logo. So the
   container adds an *attribution/no-endorsement* obligation and a *trademark* obligation;
   it adds no patent licence and (on the evidence) no patent liability.
3. **Per-file licences differ inside one project.** `libwebm` = BSD-3 **+ a patent grant**;
   `libebml` = LGPL-2.1; `libebml2` = BSD-3; `mkvparse` = MIT-in-a-comment-only. Choosing
   "Matroska support" does not imply choosing one obligation set — the file you take decides.
4. **No, the combination does not create a pool.** No entity licenses Ogg or Matroska as a
   bundle, and no container pool exists. Two pools *do* exist in the neighbourhood —
   Sisvel VP9/AV1 (video) and Via LA AAC (audio) — and neither is triggered by an audio-only
   MP3/Opus/Vorbis/FLAC/WAV library.

---

## 5. What attribution the library must ship

Concretely, for **this exact set**, a `LICENSES/` directory plus a `THIRD_PARTY_NOTICES.md`:

| Must ship | Because |
|---|---|
| **Apache-2.0 full text** | `micro-mp3` and `micro-flac` (if used) — Apache-2.0 §4(a) requires a copy of the Licence |
| **`micro-mp3`'s `NOTICE` verbatim** | Apache-2.0 §4(d). It is the only place the PacketVideo/OpenCore attribution and copyright appear: *"Copyright (C) 1998-2009 PacketVideo … Licensed under the Apache License, Version 2.0"* |
| **A statement of modifications**, if you patch Apache-2.0 files | Apache-2.0 §4(b) |
| **BSD-3 notice + conditions + disclaimer** for Xiph code | `libogg`, `libvorbis`, `tremor`, `libopus` — source redistribution must retain the notice; **binary** redistribution must reproduce it "in the documentation and/or other materials provided with the distribution". One combined Xiph BSD-3 notice with the per-project copyright lines (`Copyright (c) 2002-2020 Xiph.org Foundation`; `Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic, Jean-Marc Valin, Timothy B. Terriberry, CSIRO, Gregory Maxwell, Mark Borgerding, Erik de Castro Lopo, Mozilla, Amazon`) covers all four |
| **BSD-3 notice for Google** (`Copyright (c) 2010, Google Inc.`) **and its patent grant**, if `libwebm` is vendored | libwebm's `LICENSE.TXT` conditions **and** its `PATENTS.TXT`; the patent grant is part of what you are redistributing |
| **BSD-3 notice for Matroska** (`Copyright (c) 2010, Matroska (non-profit organisation)`) if libEBML2/libMatroska2 is vendored | foundation-source `LICENSE` |
| **CC-BY-4.0 attribution + licence notice** | Only if you copy specification text from the Matroska spec repo (e.g. element tables into comments/docs). Reading it and writing your own parser is not redistribution |
| **LGPL-2.0 full text, shine's complete source, and a relink/modification path** | shine, if used. Plus the standard LGPL offer for the object files / build scripts needed to relink. This is the one entry that changes the library's *packaging*, not just its notices |
| **No notice required** | `dr_mp3`, `dr_flac`, `dr_wav` (public domain or MIT-0) — include them as a courtesy note only |
| **Trademark footnote** | State that Matroska/WebM/vp8/vp9 are third-party names used descriptively, and that Xiph/Google/Matroska do not endorse xtape. Google's licences grant no trademark rights; matroska.org forbids free commercial use of the name and logo |

Do **not** put `esp_audio_codec` in this file as a bundled component — if it is referenced at
all it must be an external registry dependency with its own Espressif-only terms.

---

## 6. Rows I could NOT verify, and why

- **The four Opus royalty-bearing IETF filings, individually (Qualcomm, Huawei, France
  Telecom, Ericsson).** I verified the *claim* from [opus-codec.org/license](https://opus-codec.org/license/)
  and retrieved the France Telecom analysis from Opus's authors, but **not the filings
  themselves**: datatracker's IPR-search endpoint returns **HTTP 403** without JavaScript
  (`…/ipr/search/?submit=draft&id=rfc6716`), `/doc/rfc6716/ipr/` is a **404**, and the
  `/api/v1/ipr/…` endpoints 404. The grant-side filings I *did* retrieve directly:
  [1524](https://datatracker.ietf.org/ipr/1524/) Xiph, [1526](https://datatracker.ietf.org/ipr/1526/)
  Broadcom, [1670](https://datatracker.ietf.org/ipr/1670/) and [1914](https://datatracker.ietf.org/ipr/1914/)
  Microsoft. *Do not quote disclosure numbers for the four withholding holders from this note.*
- **`anton-malakhov/mp3_shine_esp32`'s `COPYING`.** The gitflic.ru raw URL returns **HTTP 502**
  (an error page, not the file). s2 records it as carrying the same LGPL-2 `COPYING`;
  **UNVERIFIED here.** `fknrdcls`'s GPL-3.0 is verified via the GitHub API.
- **Whether a Matroska container patent has ever been asserted.** I found no container patent
  programme and no assertion from any primary source. That is an argument from absence — the
  same shape as the WAV/PCM argument in s1 §1.3 — **not** a vendor warranty.
- **Whether the Matroska association's "contact us for commercial use" is a licence
  condition.** The text reads as a request (*"we ask"*, *"you are invited to sponsor"*), not a
  condition; I have no clarifying primary source. Treat the *trademark* restriction as binding
  and the sponsorship as optional.
- **`libebml2`/`libMatroska2`'s current release state.** matroska.org says *"There are no
  separate releases available at the moment"* — the BSD code exists only in git
  (`foundation-source`), whose `LICENSE` I did read. No tagged release was verified.
- **Whether `shine` is "LGPL-2.0-only" or "LGPL-2.0-or-later".** No per-file header and no
  "or later" statement appears anywhere in the repository I could read, so the strict reading
  is **2.0-only**; a distributor's packaging (e.g. a Debian `copyright` file) might say `2+`.
  Treat as 2.0-only unless you find an explicit "or later".

---

## 7. VERIFIED vs UNVERIFIED

**VERIFIED (primary source read in this session, 2026-10-16)**

- `dr_mp3.h`: line 1 *"Choice of public domain or MIT-0"*; EOF blocks `ALTERNATIVE 1 - Public
  Domain (www.unlicense.org)` / `ALTERNATIVE 2 - MIT No Attribution`, `Copyright 2023 David Reid`.
- `micro-mp3`: Apache-2.0 `LICENSE` in `esphome-libs/micro-mp3`, byte-identical to the file the
  ESP registry serves; `NOTICE` names OpenCore/PacketVideo.
- `libvorbis` `COPYING` = BSD-3-Clause, `Copyright (c) 2002-2020 Xiph.org Foundation`.
- `tremor` `COPYING` = BSD-3-Clause, byte-identical to libogg's; README states decode-only.
- `libogg` `COPYING` = BSD-3-Clause.
- `xiph/opus` `COPYING` = BSD-3-Clause + Xiph/Microsoft/Broadcom patent-licence URLs.
- Opus licence page verbatim terms incl. the encoder/decoder grant and the four-company
  withholding paragraph; Microsoft's separate Encoder Patent Promise in IPR #1670.
- France Telecom disclosure described as *"directed to encoders"* by Opus's authors.
- RFC 6716 / RFC 3533 / RFC 7845 / RFC 9559: zero "patent" occurrences.
- Vorbis patent statement: xiph.org/vorbis, Vorbis FAQ, Vorbis I spec colophon, Monty 2000 post.
- Matroska spec `LICENSE.md` = CC-BY-4.0 with the "Patent and trademark rights are not
  licensed" clause; spec README confirms CC-BY-4.0; matroska.org/legal.html's no-cost
  statement and trademark rules; matroska.org libraries page = LGPL 2.1 / BSD split.
- `libwebm` `LICENSE.TXT` BSD-3 + `PATENTS.TXT`; WebM software / additional-IPR / bitstream
  licence pages verbatim; WebM FAQ royalty-free + hardware-implementation answers.
- Sisvel VP9 pool active with published rates; **Sisvel/Audio MPEG "no longer active"**;
  audiompeg.com parked; mp3licensing.com unresolvable; Via LA lists no MP3 programme.
- Fraunhofer mp3 page + Audio Blog quotes re-read today.
- `shine` `COPYING` = **GNU Library GPL Version 2, June 1991 (LGPL-2.0)**; `savonet/shine`
  identical; no per-file headers. `lame` `COPYING` = same; `lame` `LICENSE` adds conditions.
- `foundation-source` (libEBML2/libMatroska2) = BSD-3-Clause; `libebml`/`libmatroska` =
  LGPL-2.1; `vi/mkvparse` declares MIT in-file with **no** `LICENSE` file (GitHub API: no licence).
- `micro-flac` `LICENSE` = Apache-2.0; no `NOTICE` file (404).

**UNVERIFIED / LIKELY**

- The individual IETF filings of Qualcomm, Huawei, France Telecom, Ericsson (403/404, above).
- `anton-malakhov/mp3_shine_esp32`'s licence text (gitflic 502).
- "No Matroska container patent exists / has been asserted" — absence of evidence.
- "Audio-only demux does not practise VP8/VP9" — a legal inference, not a vendor statement;
  the Sisvel pool's component exclusion supports it but does not state it.
- `libebml2`/`libMatroska2` current release/tag status.
- `shine` as 2.0-only vs 2.0-or-later.
- Micro-flac's exact provenance/derivation (licence verified; the README was not read here).

---

## 8. Bottom line

**The requested set is shippable.** Output MP3 + Opus-in-Ogg and input MP3/Ogg
Vorbis/Opus/WebM/FLAC/WAV is a **zero-royalty** format set at both ends:

- **No codec in this set has a live patent programme.** MP3's two pools (Fraunhofer–
  Technicolor and Sisvel–Audio MPEG) are both closed; Vorbis is called *"patent-and-royalty-free"*
  by its owner; Opus is granted royalty-free by Xiph, Microsoft and Broadcom, and the four
  withholding disclosures are third-party, encoder-directed and publicly analysed as
  non-infringing. FLAC and WAV were already clear in [s1](s1-aac-licensing.md).
- **Nothing in the audio-only set is blocked by a licence either.** The only restrictive
  licence in the whole set is **shine's LGPL-2.0**, and that is a packaging obligation
  (publish shine's source, keep it relinkable) rather than a prohibition. Everything else is
  public domain/MIT-0/Apache-2.0/BSD-3/CC-BY.
- **The one genuinely unsafe thing you could add is a VP8/VP9 video decoder**, because
  Sisvel operates a royalty-bearing VP9 pool. An audio-only WebM path that skips video
  tracks avoids it. (AAC remains the other unsafe addition, per [s1](s1-aac-licensing.md).)

**If you wanted to drop the only restrictive dependency**, the permissive alternative to
shine-as-LGPL is: keep the MP3 *encode* path optional behind the same pluggable-backend seam
already recommended in [`02-packaging-and-licensing.md`](../02-packaging-and-licensing.md),
and make **Opus-in-Ogg the default output** (libopus is BSD-3 and needs no LGPL compliance at
all), with **FLAC** (`micro-flac`, Apache-2.0) as the lossless option. That gives a fully
permissive-by-default encoder set with MP3 as an opt-in.

**The Matroska/WebM verdict in one sentence:** the container is free to use and no patent
programme covers it — the Matroska association says so in terms (*"without paying any license
or patents"*) — but the specification's own licence **expressly withholds patent rights**
(CC-BY-4.0 §2(b)(2)), so WebM's "royalty-free" claim is a claim about the **codecs** (VP8/VP9,
Vorbis/Opus) and about **Google's own implementations**, not a patent grant covering the EBML
container or your hand-written parser; use libEBML2/libMatroska2 (BSD-3) or write the parser
yourself, and do not use the Matroska name or logo commercially.

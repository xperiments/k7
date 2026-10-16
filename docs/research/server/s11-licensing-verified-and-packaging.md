# Licensing decisions verified directly (2026-10-16)

These are the licence facts I confirmed by reading the files myself, because they change how
the component must be packaged. The full matrix with 60 citations is in
`s6-codec-licensing-matrix.md`; this file records only what I verified first-hand and what it
implies for the default build.

## The bottom line: the requested format set is shippable and royalty-free at both ends

Every codec in the set — MP3, Opus, Vorbis, FLAC, WAV/PCM, and the Ogg container — has **no
live patent programme** as of this date. Nothing in the audio-only set is blocked. The only
restrictive licence is `shine` (LGPL), and that is a *packaging obligation*, not a
prohibition.

## `shine` is LGPL-2.0-only — stricter than the earlier note claimed

**Verified by reading the file.** `toots/shine` `COPYING` (and the `savonet/shine` mirror,
identical) is:

```
                  GNU LIBRARY GENERAL PUBLIC LICENSE
                       Version 2, June 1991
```

Two details that matter and were previously reported imprecisely:

1. It is the **GNU *Library* GPL v2** (the old name), not LGPL v2.1. Same document family,
   different version.
2. **The string "or later" appears zero times** in the file. There is therefore no
   "LGPL-2.0-or-later" escape hatch — it is **LGPL-2.0-only**, and a distributee cannot
   elect to use a later LGPL.

The practical consequence for a reusable library, and the reason this drives the packaging
recommendation: shipping `shine` linked into the component means the component must provide
the LGPL notice, the **complete corresponding source** of the LGPL'd parts, and a **relink
path** for the user. That is a real, documentable obligation — not something to discover
after publishing.

Note the contrast with the rest of the stack, which is why this one stands out:

| Component | Licence | Vendorable / simple? |
|---|---|---|
| `esphome/micro-*` (mp3, flac, wav, opus, vorbis) | Apache-2.0 | yes |
| `georgik/ogg` (upstream libogg) | BSD-3-Clause | yes |
| `libopus` | BSD-3-Clause | yes |
| `dr_mp3` / `dr_flac` / `dr_wav` | public domain / MIT-0 | yes — **no notice required** |
| **`shine` (MP3 encode)** | **LGPL-2.0-only** | **packaging obligation** |

## The recommendation this implies

**Make Opus-in-Ogg the default output and keep MP3 *encode* as an opt-in backend.**

- Opus-in-Ogg default: `libopus` is BSD-3 with a symmetric royalty-free patent grant, and the
  Ogg muxer is BSD-3 (`georgik/ogg`). **Zero LGPL, zero royalty surface in the default build.**
- `micro-flac` (Apache-2.0) is the lossless option, and it *encodes* as well as decodes.
- MP3 **encode** via `shine`, behind a pluggable backend and a Kconfig flag, so a consumer who
  wants MP3 accepts the LGPL terms knowingly — and the library's default build stays clean.

This is the same architecture already recommended for AAC (a codec vtable with permissive
defaults), so it costs no additional design: `shine` simply becomes another backend rather
than a core dependency. Notably, MP3 *decode* is unaffected — `micro-mp3`/`dr_mp3` are
permissive — so MP3 **input** support carries none of this burden.

## Attribution the library must ship

Concretely, for the recommended set:

- Apache-2.0 licence text (`micro-mp3`, `micro-flac`, and any other Apache-2.0 component).
- **`micro-mp3`'s `NOTICE` verbatim** — verified present and required:
  ```
  microMP3
  Copyright 2026 Kevin Ahrendt

  This product includes software from the OpenCore MP3 decoder library
  (opencore-mp3dec), originally developed by PacketVideo:

    Copyright (C) 1998-2009 PacketVideo
    Licensed under the Apache License, Version 2.0
    https://android.googlesource.com/platform/external/opencore/+/refs/heads/main/codecs_v2/audio/mp3/dec/
  ```
  Apache-2.0 §4(d) makes propagating `NOTICE` mandatory, so this is not optional.
- A combined Xiph BSD-3 notice (libogg / libopus, and libvorbis/tremor if used).
- Only **if** `shine` is included: LGPL-2.0 text + its complete source + a relink statement.
- `dr_mp3`/`dr_flac`/`dr_wav` require **no** notice (public domain / MIT-0) — a small benefit
  of keeping them over Apache-2.0 alternatives where the choice is free.

## What is *not* safe, if the scope ever grows

- **A VP8/VP9 video decoder.** Sisvel runs a live royalty-bearing VP9 pool. An **audio-only**
  WebM path that skips video tracks avoids it — which is another argument for the
  "drop WebM" recommendation, since the moment video decoding is added the royalty picture
  changes.
- **AAC**, per `s1-aac-licensing.md`.

## Matroska/WebM container position (for completeness)

- The **container** is free in practice: matroska.org states it can be used *"without paying
  any license or patents"*.
- But the **spec repository** is CC-BY-4.0, whose §2(b)(2) states verbatim **"Patent and
  trademark rights are not licensed under this Public License"** — so copying spec text grants
  no patent rights. And Google's WebM patent grants are scoped to *"these implementations"* /
  *"the specifications to the WebM codecs"*, i.e. Google's own codec code, **not** the EBML
  container and **not** a third-party parser.
- Permissive parser option if WebM is ever added: **libEBML2/libMatroska2 (BSD-3)** from
  `Matroska-Org/foundation-source`. Avoid `libebml`/`libmatroska` (LGPL-2.1). Do not use the
  Matroska name/logo commercially.

## Corrections to earlier notes, folded back

1. `shine` is LGPL-2.0(-only), not LGPL-2.1 — corrected above and in `s2-encoder-feasibility.md`.
2. MP3 had a **second** pool that the first licensing pass missed — Sisvel/Audio MPEG
   (Philips/TDF/IRT/BRW). It too is closed: Sisvel's own page says *"The Audio MPEG licensing
   programme is no longer active"*, and via-la.com lists no MP3 programme. So there is **no
   encoding-specific MP3 programme surviving**, which is what makes MP3 *encode* royalty-free.
3. `esphome/micro-mp3` is really `esphome-libs/micro-mp3` (the registry namespace is
   `esphome`), and it ships the `NOTICE` above.

## Could not verify

- The four **withholding** Opus IETF IPR filings individually (Qualcomm, Huawei, France
  Telecom, Ericsson). The IETF datatracker IPR search returns HTTP 403 without JavaScript and
  the per-RFC IPR endpoints 404, so **their disclosure numbers must not be quoted**. What is
  citable is Xiph's own statement and its counsel's advice, plus the grant-side filings
  (Xiph, Broadcom, Microsoft) — note Microsoft's includes a separate "Encoder Patent Promise",
  and that the one analysed royalty-bearing claim (France Telecom) is described as
  *"directed to encoders"*, which is relevant precisely because this project ships an encoder.
- `anton-malakhov/mp3_shine_esp32`'s `COPYING` (gitflic returned HTTP 502). Separately,
  `fknrdcls/mp3_shine_esp32` is **GPL-3.0** and must not be used.
- "No Matroska container patent exists" is an argument from absence, not a vendor warranty.
- `libebml2` has no tagged release (git only).

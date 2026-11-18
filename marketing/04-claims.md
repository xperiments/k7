# Phase 4 — Claims

**Read with:** [`03-audience-and-proof.md`](./03-audience-and-proof.md). Audience keys: **A** builder,
**B** library owner, **C** cassette person, **D** design buyer (Phase 3 §1).

**Rules applied.** Every claim below is either backed by a file and line, or it is refused. No
number appears that is not measured in the repo with its conditions attached. Where a number is
needed and does not exist, the row says **measure it first** and gives the method — I have not
invented one.

Status vocabulary: **Verified** (evidence in the tree, and the claim is exactly what it says) ·
**Needs testing** (implemented, but something about the claim is unproven — usually on hardware) ·
**Don't say** (false, or true-but-misleading, or legally unsafe).

---

## 1. Identity and ownership

| # | Claim | Segment | Evidence in repo | Status | Safer wording |
|---|---|---|---|---|---|
| 1 | "A cassette-shaped music player" | B, D | hero copy, `web/src/site/Site.tsx:42-44`; README title `README.md:1-10` | **Don't say** | "A cassette-shaped deck that plays to the speakers on your network." The device has no audio output (Phase 1 §1); "player" invites the one question that spoils the first ten seconds. |
| 2 | "Turns a card into a tape" | all | card mark `docs/specs/card-mark.md:14-24`; lists on the card `docs/specs/playlist-format.md`; Snapshot `docs/specs/snapshot.md` | **Verified** | Keep as is. It is the product, and it is literally true. |
| 3 | "No account, no cloud" | B | no auth anywhere `README.md:25-39`; no remote service in the firmware | **Verified** | Keep. Add the second half where there is room: "It serves your own network — and it has no password, so keep it off the internet." |
| 4 | "Nothing leaves your network" | B | LAN-only by design; but the device serves the LAN to anyone who can reach it `README.md:23-39` | **Verified, narrowly** | Never shorten to "nothing leaves your machine" about the device. True of the analyser only (`web/README.md:3-4`). Say which one you mean. |
| 5 | "Private and secure" | B | no authentication, no TLS, plain HTTP, default hotspot password `12345678` `README.md:25-39` | **Don't say** | "No account. No cloud. Not secure — keep it on your own network." The honesty is the brand (Phase 2 §5). |
| 6 | "Open source" | A | firmware MIT `LICENSE:1-3`; app AGPL-3.0-only `web/package.json:6`; root `package.json:5` says ISC | **Needs testing** | "MIT firmware, AGPL app." Always split it. The root `package.json` ISC line is a contradiction to fix before anyone audits the claim. |
| 7 | "K7 is free software, AGPL-3.0-only" (current site footer) | all | `web/src/site/Site.tsx:76` | **Don't say** | The *app* is AGPL-3.0; the *firmware* is MIT. The footer currently implies both are AGPL. Fix before the campaign points at the site. |
| 8 | "K7 is the French word for cassette" | all | `docs/adr/0062-xtape-becomes-k7.md:3` | **Verified (FR)** | "K7 — French for cassette." Do **not** add Catalan: the campaign's own market file says "Not supported" (`docs/research/marketing/market-2026-11-03.md:70`). |

---

## 2. What it plays, and what it costs you in quality

This is the group where the product has the most to lose and the most to gain. The repo is
unusually honest here; the campaign must be equally so, because the alternative is a comment
section that writes the correction for you.

| # | Claim | Segment | Evidence in repo | Status | Safer wording |
|---|---|---|---|---|---|
| 9 | "Plays MP3, FLAC and WAV" | B | `components/portable/audio_format/src/audio_format.c:12-17` | **Verified** | Keep. Add the bound: "up to 96 kHz" — above that the track is skipped, not played (`mini_icecast.h:39-47`, `README.md:1147-1155`). |
| 10 | "Any sample rate, mono or stereo" | B | `README.md:142-145`; resampler `mi_resample.c` | **Verified, with the 96 kHz ceiling** | "Any rate up to 96 kHz, mono or stereo; everything is resampled to the station rate." |
| 11 | "Lossless", "your FLACs stay lossless" | B | everything is decoded and re-encoded to MP3 256 kbps `README.md:20-21` | **Don't say** | "Your files stay on the card untouched. What leaves is a 256 kbps MP3 stream — the card is the master, the stream is the broadcast." |
| 12 | "Hi-fi", "audiophile", "studio quality" | B, D | `shine` is below LAME and the README says so `README.md:1274-1276`; SNR table `README.md:352-357` | **Don't say** | Say nothing about fidelity. Show the EQ and the room instead. If pushed: "It is radio quality, deliberately — 256 kbps, and the encoder is the limit, not the bit rate." |
| 13 | "44.1 kHz / 256 kbps CBR joint stereo" | B | `components/app/appliance/appliance.c:220-228`; validated at boot | **Verified** | Quote it exactly, with "default" — it is configurable. |
| 14 | "Gapless" | B | no gap between tracks, but `shine_flush()` does not flush, so a track's tail is zero-padded to 1152 samples — up to 26 ms `components/portable/mini_icecast/include/mi_encode.h:20-30,73-74` | **Needs testing** | "Tracks run into each other without a gap; the encoder pads the last block with up to 26 ms of silence." Do not say "gapless" until someone measures it on a mixed set. |
| 15 | "It plays to any player" | B | one continuous `audio/mpeg` stream; ICY metadata opt-in `README.md:6-8,289` | **Verified, narrowly** | "Anything that can open an MP3 stream URL: VLC, mpv, ffplay, a phone, a streamer." Not "any speaker" — a speaker needs the bridge. |
| 16 | "You can pause, seek or skip" | B | the broadcast cannot be paused or seeked; every client hears the same moment `README.md:1272-1273` | **Don't say** | "It is radio, not a queue. One stream, the same moment for everyone." (Skipping exists only through `/api/play` or the MCP control build — not a user-facing transport.) |
| 17 | "It shuffles your library" | B | boots shuffled; xorshift32 seeded from `esp_random()` `appliance.c:239-243` | **Verified** | Keep — it is a nice surprise ("it starts on its own, like a radio"). |
| 18 | "3-band EQ with a trim" | B | `tone_eq.h:29-37`; 200 Hz shelf, 1 kHz bell, 4 kHz shelf, −26…+6 dB bands, ±12 dB trim | **Verified** | Quote the bands; they are real and unusual for a streamer. |
| 19 | "Tape sound", "wow and flutter", "analogue warmth" | C | no tape FX anywhere in the audio path; "tape" is a drawing (`components/portable/cassette/`) | **Don't say** | "The tape is on the screen. The sound is clean 256 kbps MP3 — we did not fake the hiss." |

---

## 3. The device

| # | Claim | Segment | Evidence in repo | Status | Safer wording |
|---|---|---|---|---|---|
| 20 | "800×480 touchscreen" | A, D | `components/app/screen_display/panel_rgb.c:30-31,41`; GT911 touch, polled | **Verified** | Keep. Quote 800×480; it is exact and it is a good number. |
| 21 | "The reels turn at real tape speed" | C, D | 47.625 mm/s, 22 mm hub, per-reel phase `components/portable/cassette/include/cassette.h:8-12,38-44` | **Verified** | This is the claim to film rather than explain: "the reels turn at the speed the tape would". |
| 22 | "It winds the tape when the track changes" | C, D | an 8 s reel-change rewind that lands on normal play `cassette.h:76-84` | **Verified** | Keep. It is the single most surprising thing about the screen. |
| 23 | "Touch works" | A | GT911 code verified; **"the board tier has not yet recorded a finger"** `README.md:732-733` | **Needs testing** | "Touch on a 5-inch panel" only after one recorded board run with a finger. Until then, say "the panel is a touch panel" — and test it. |
| 24 | "The label prints on the screen" | C, D | label sheet packed and read in place `docs/specs/label-templates.md:3-8`; `docs/adr/0071` | **Needs testing** | Built and host-tested; the panel proof is a design ticket (`docs/design/label-templates/issues/08-prove-the-label-on-the-panel.md`). Film it before you claim it. |
| 25 | "LED light show" | D | 8 × WS2812B, six effects, 30 Hz `docs/specs/led-strip.md:3,70-77`; driver started at boot `appliance.c:312` | **Needs testing** | "Eight pixels, six effects" is fine as a spec; "a light in the room" needs the board visual pass that is still open (`led-strip.md:121-131`). The strip is capped at 25 % power because full white browned the board out (`README.md:274-282`) — do not show it as a lamp. |
| 26 | "Battery powered", "portable", "unplugged" | D | no battery, charger or PMIC anywhere; 5 V USB only | **Don't say** | "USB-C powered." Ever. A battery claim is the fastest way to a refund request. |
| 27 | "Bluetooth" | D, C | no Bluetooth in the firmware (`sdkconfig.sunton`: `CONFIG_BT_ENABLED` unset) | **Don't say** *(for the cassette)* | Say it only about the **bridge**, and only when the bridge exists: "the player takes the stream and drives a DAC, or Bluetooth." |
| 28 | "Multi-room", "whole-home", "sync" | D | one device per LAN; the hostname is fixed and two devices collide `README.md:1264-1265` | **Don't say** | "One tape, one room." It is a limitation and it is also the better line. |
| 29 | "Works off-grid / anywhere" | D | needs a Wi-Fi network; the card is prepared on a computer `README.md:98-109` | **Don't say** | "It works on your network, not on someone else's." |
| 30 | "One continuous stream, everyone hears the same moment" | B, D | `docs/specs/CONTEXT.md:43-45`; `README.md:6-8` | **Verified** | Keep — it is the best sentence in the set and it needs no qualifier. |

---

## 4. The app

| # | Claim | Segment | Evidence in repo | Status | Safer wording |
|---|---|---|---|---|---|
| 31 | "Measures tempo, key, mood and energy in your browser; files never uploaded" | B | `web/README.md:3-4`; workers and ONNX/Essentia in the tab | **Verified** | Keep. It is the free product and the 60-second proof (Phase 3 §4). |
| 32 | "Accurate key detection" | B | "Key detection is the weakest field… treat the strength as a hint" `web/README.md:233-236` | **Don't say** | "Key, with a strength reading. It is a hint, not a verdict — and your own key tags are trusted instead." |
| 33 | "It recommends music" / "AI DJ" / "AI discovers" | B, D | the assistant writes filters, never results; never sees a track or an artist `docs/specs/k7-app.md:496-503`; `docs/adr/0053` | **Don't say** | "Describe the set. It writes the filter. You keep the taste." |
| 34 | "Runs on Chrome's built-in model, offline, no key" | A | `web/README.md:186-203`; ADR 0084 | **Verified, with conditions** | Add the conditions in the same breath: "Chrome 148+ on a computer, ~22 GB free, and the first use downloads a few gigabytes." Without them it is a support ticket. |
| 35 | "The model is right 10 times out of 12" | A | twelve-sentence spike, measured `docs/specs/k7-app.md:496-500` | **Verified as a measurement** | Quote it as a measurement, with the sentence: "on twelve sentences it got ten right and all twelve were schema-valid". Do not turn 10/12 into a percentage claim. |
| 36 | "It writes BPM, key, mood and energy into your files" | B | tag writing plus a 402×188 label JPEG, in place on a read-write card; a dropped folder gets a copy `web/README.md:29-40` | **Verified** | Must be disclosed *before* the first run, not after: "On a card you open for writing, it saves what it measured into the files — and a label image. On a folder you drop, it saves a copy instead." |
| 37 | "Scans your whole library" | B | 20 000-track cap and a 4 MiB arena; at the cap the index says `truncated` `README.md:146-150` | **Verified, with a ceiling** | "Up to 20 000 tracks." Do not say "unlimited". |
| 38 | "Covers and labels" | B | JPEG only; a PNG picture is skipped `README.md:1268-1271` | **Needs testing** | "Cover art, when it is a JPEG." |

---

## 5. The system, the kit and the money

| # | Claim | Segment | Evidence in repo | Status | Safer wording |
|---|---|---|---|---|---|
| 39 | "The cassette and the player work together" | all | the player is a researched context that was never built (`CONTEXT-MAP.md:59-61`); research only `docs/research/player-*` | **Don't say** *(today)* | "The cassette broadcasts; anything on your network can play it. A dedicated player with a DAC and Bluetooth is the next piece." Say "next", not "works with". |
| 40 | "It is a real product" | all | firmware, site, releases — but no case, BOM, certification or price | **Needs testing** | "It is a real, working system you can build today. A kit is planned." The distinction between *working* and *buyable* must be explicit. |
| 41 | "Kits avoid certification" | A | **contradicted by the repo's own compliance brief**: a kit is electrical equipment and the seller is likely the manufacturer — CE/EMC, RoHS, WEEE `docs/research/marketing/compliance-2026-11-03.md:113-117`; "no fourth route" `:119-120` | **Don't say** — *never, in public or in the plan* | Nothing public. Internally: "a kit is a smaller product with the same duties." |
| 42 | "CE / FCC approved", "certified", "ready to ship" | all | certification is unstarted; budget €3–10k and 6–10 weeks `compliance-2026-11-03.md:14-18,93` | **Don't say** | Nothing. Until the declaration exists, silence. |
| 43 | "Pre-order", "Buy", "Available now" | D | no checkout; the site's only CTAs are flash and install `web/src/site/Site.tsx:47-50` | **Don't say** | "Join the build list" — until an entity, terms, a refund policy and a cleared compliance position exist (Phase 6). |
| 44 | "€X" (any price) | D | every figure in the repo is a planning estimate, labelled as one `docs/research/marketing/opportunities.md:273-301` | **Needs a decision** | A range is not a claim until it is real. See Phase 6 §4 — I will not put a number in copy without your answer. |
| 45 | "Assembles in N minutes" | A | no assembly guide exists; the kit is not defined | **Measure it first** | Method: one person who has never seen the project, one stopwatch, one unedited video. Until then: "hand assembly" with no time. |

---

## 6. Numbers you may quote today, with their conditions

Copy these exactly or not at all. Each is measured in the repo, and each has a condition that makes
it honest.

| Number | Condition | Where |
|---|---|---|
| 44.1 kHz, 256 kbps CBR, joint stereo | the default station format, configurable | `appliance.c:220-228` |
| 4 listeners | configured maximum, not a tested limit | `appliance.c:220-228` |
| 41.4 % of one core | one client, 59-minute soak, devkit, 44.1/256 | `README.md:561` |
| 0 starvation rebases in 59 minutes | same run | `README.md:561` |
| 58.8–59.9 % of one core | the panel board, idling and streaming, 3- and 5-minute soaks | `README.md:594-595` |
| 96 ms | boot scan of a 64-track / 2.28 GB card | `README.md:148` |
| 20 000 tracks / 4 MiB | library arena caps, after which the index says `truncated` | `README.md:146-150` |
| 8 seconds | the reel-change wind | `cassette.h:76-84` |
| 8 pixels, 25 % power | LED strip, capped because full white browned the board | `README.md:274-282` |
| 2 MB / format v5 | Snapshot partition and its format (**not v4** — the README is stale) | `snapshot.h:27`; `README.md:1200` |
| 2,074,544 B | the 0.6.1 app image | `web/firmware.json` |
| 890 | tests in the web suite | commit `a927684` |

**Numbers you must measure before using** (methods included, because otherwise someone invents
them):

| Needed | Why it sells | How to measure |
|---|---|---|
| Card-in → sound-out | the single most useful number a buyer wants | record the screen and the speaker on one clock; state the card and the player |
| Kit assembly time | the difference between "I could" and "I would" | one naive builder, one stopwatch, unedited video |
| Weight and dimensions | D buyer, shipping cost, and the kit's price | a kitchen scale and calipers, on the finished shell |
| Power draw at the wall | "what does it cost to run" | a USB power meter, idle and streaming, with the strip on |
| Measuring pass on a real library | B's conversion asset | 1 000 tracks, one machine, wall clock, per-file average |
| Boot-to-idle time | support cost and the "it just works" claim | serial log timestamps from power-on |

---

## 7. Where this product is most likely to be overpromised

Five sentences that would convert well and are false. Written here so they are refused at review,
not softened in the edit.

1. **"A cassette player that streams your library."** Two claims, one false (no tape transport), one
   misleading (no local audio). Use Phase 2's statement A.
2. **"Lossless / hi-fi / your FLACs in full quality."** Everything is re-encoded at 256 kbps. The
   card is the master; the stream never is.
3. **"Private and secure."** No authentication, no TLS, a default hotspot password. The opposite
   claim is the true and more interesting one.
4. **"A kit, so no certification."** False, and the repo's own compliance file says so. This one is
   a liability, not a shortcut.
5. **"AI that knows your library."** The model never sees a track, an artist, a path or a
   measurement; it writes filters from aggregates. Anything warmer than that is a lie with a nice
   demo.

---

## Appendix A — The models: what the app actually runs, and what dropping ONNX would cost

You asked me to review which models are in use, and whether the ONNX ones are necessary. Here is the
inventory, from `web/weights.lock.json`, `web/src/audio/models.ts` and `web/README.md`.

### A.1 The inventory

| Model | Size | What it produces | Runtime | Licence |
|---|---|---|---|---|
| `discogs-effnet-bsdynamic-1.onnx` | 18.03 MB | the embedding the mood heads and danceability sit on | ONNX Runtime Web | **CC BY-NC** |
| `msd-musicnn-1.onnx` | 3.17 MB | the embedding the DEAM head sits on | ONNX Runtime Web | **CC BY-NC** |
| `mood_{happy,sad,aggressive,relaxed,party}-discogs-effnet-1.onnx` | 5 × ~0.51 MB | six mood probabilities (the five heads plus danceable) | ONNX Runtime Web | **CC BY-NC** |
| `danceability-discogs-effnet-1.onnx` | 0.51 MB | danceability | ONNX Runtime Web | **CC BY-NC** |
| `deam-msd-musicnn-2.onnx` | 0.08 MB | valence and arousal — and **energy is arousal** | ONNX Runtime Web | **CC BY-NC** |
| Essentia.js (WASM) — Degara beats | — | **tempo** | WebAssembly | **AGPL-3.0** |
| Essentia.js (WASM) — KeyExtractor | — | **key / Camelot** | WebAssembly | **AGPL-3.0** |
| Gemini Nano | — (Chrome's, a few GB) | the **assistant's filters** | Chrome's built-in model | Chrome's own terms — not a repo asset, no licence of yours |

**Total ONNX: 9 files, 24.4 MB.** All committed and hash-pinned (`web/weights.lock.json`).

### A.2 So your three statements, checked

- *"The only ones currently in use are for music analysis."* — **Correct.** Nothing else in the app
  uses ONNX. The assistant does not touch them.
- *"For local AI, we're using Chrome's Gemini Nano."* — **Correct.** Ask runs on the Prompt API and
  never sees the weights (`docs/adr/0084`).
- *"I'm not sure the ONNX are necessary."* — **They are not necessary for the app to work, but they
  are what make it measure anything beyond tempo and key.** Precisely: drop the ONNX models and you
  lose **mood, energy (valence/arousal), danceability** — three of the four things the free product
  advertises (`web/README.md:5-20`), and the vocabulary two thirds of Ask's sentences use. Tempo and
  key survive, because those come from Essentia.js, not ONNX.

### A.3 The licence trade, stated exactly

- **Dropping ONNX removes the CC BY-NC restriction.** The app would no longer contain non-commercial
  weights, so the sentence in `web/NOTICE:28` — "the combined application is non-commercial" —
  would stop being true.
- **It does not remove the AGPL.** Essentia.js (tempo, key) is AGPL-3.0, and so is the app. The app
  stays AGPL either way. That is fine — the Site already offers the source
  (`web/src/config.ts`), which is what AGPL asks for — but "drop ONNX so the app can be closed" is
  not a thing that happens.
- **Where the NC restriction actually bites.** The weights run in the *visitor's* browser, not on
  your server. A free public app that a buyer happens to use for their own listening is a grey zone,
  not an obvious breach. What is clearly outside it: selling the app, selling access to it, or
  shipping a paid bundle whose value depends on those models. The question for a lawyer is narrow —
  *does selling a hardware kit, alongside a free app that fetches NC weights, make the kit's buyer's
  use commercial?* — and it deserves a written answer rather than a guess.
- **MTG offers a proprietary licence on request** (`web/NOTICE:27`). That is the clean route if the
  measurement is central to the pitch, and it is worth one email before any product decision.

### A.4 My recommendation

**Keep the ONNX models for now, and separate the app from the money.** Three reasons:

1. **They are the product's only "measured music" story.** Tempo and key alone is a DJ utility;
   tempo, key, mood, energy and danceability is *a library that knows itself* — which is what the
   Snapshot, the Ask filters and the collector audience are built on (Phase 3 §1B).
2. **The commercial risk sits in bundling, not in the file.** Keep the app free, unbundled, source
   offered, and never describe the kit as including it. Put that in the terms, not just the copy.
3. **The decision is reversible; the positioning is not.** If MTG says no, the fallback is a
   deliberate, well-communicated reduction to tempo/key — "measured for DJs, not for vibes" — which
   is a narrower product but an honest one. Do not quietly delete three quarters of the analysis and
   keep advertising "mood".

**What to do this week:** one email to MTG asking for a commercial licence quote for a small
hardware run, with the intended volume; and one decision recorded — *the app is free and is not part
of the kit* — before Phase 6 prices anything.

<!-- Phase 5 (site) follows in 05-website.md, after the Phase 4 check-in. -->

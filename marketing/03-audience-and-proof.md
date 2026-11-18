# Phase 3 — Audience, and what must be exposed to sell it

**Read with:** [`01-project-understanding.md`](./01-project-understanding.md),
[`02-soul-and-positioning.md`](./02-soul-and-positioning.md).

**Two corrections from the 2026-11-16 check-in are folded in:**

- The commercial route is **a kit** (Phase 1's "no object" stands, but the object is a kit that
  arrives as parts, not a finished appliance). See §5 for the one legal assumption in that plan that
  the repo's own research contradicts.
- The system is **two devices**: the cassette-shaped source, and a player that receives the stream
  and drives a DAC (and Bluetooth) into speakers. The second device is a *researched context that was
  never built* (`CONTEXT-MAP.md:59-61`) — not a component that exists.

INFERENCE: those two facts, taken together, mean the campaign is selling **a pair that only half
exists**, and the honest job for the next 90 days is to make the half that exists undeniable and the
half that doesn't *credible*.

---

## 1. The four audiences

Ordered by how little proof they need, which is the only ordering that matters before the bridge
exists. Phase 2 argued the same order for a different reason; here is the audience evidence for it.

### A. The builder — *can I make this, and is it real?*

| | |
|---|---|
| **Who** | Writes firmware or backend code, runs a homelab or a 3D printer, owns a soldering iron and an ESP32 drawer. Reads the README before the landing page. Finds projects through HN, Hackaday, an ESP32 subreddit, a YouTube build video. |
| **Motivation** | An instrument they can understand and change. The pleasure is not owning a cassette; it is owning a *system*. This audience is the only one the current state fully satisfies — there is firmware, a browser flasher, three release binaries with sha256s, a host test suite and 96 ADRs. |
| **Objections** | "It doesn't make sound." · "There's no case, no BOM, no wiring diagram." · "It needs the exact Sunton board." · "One board, one build — has anyone else done it?" · "Why would I build this instead of flashing Volumio onto a Pi?" |
| **Where** | Hacker News (Show HN), r/esp32, r/selfhosted, r/homeassistant, Hackaday, YouTube hardware channels, ESP32 Discords, the GitHub repo itself. PROPOSAL: no size estimates — the prior research could not retrieve subreddit sizes (`docs/research/marketing/market-2026-11-03.md:89`), and I did not invent them either. |

**Minimum information to say yes**

1. **A BOM with real part numbers** and a supplier link per line — including which WS2812B, which
   card, which level shifter. *(Does not exist. `docs/research/marketing/opportunities.md:500`.)*
2. **The pin map** — *(exists, `README.md:187-192`, and it is good)*.
3. **A wiring diagram for the strip, the card and the 5 V feed**, with the 3.3 V/3.5 V threshold
   problem stated — *(the problem is documented at `docs/specs/led-strip.md:34-37`; the diagram is
   not)*.
4. **The firmware, flashable from a browser** — *(exists: `k7.xperiments.dev`, three released
   versions)*.
5. **What it does not do**, in a list, unhedged — *(exists, and it is the strongest trust asset in
   the project: `README.md:1249-1283`, once corrected for the drift in Phase 1 §2.6)*.
6. **One video of it working on someone's desk** — *(does not exist)*.
7. **Kit price and what is in the box** — *(does not exist; Phase 6)*.

### B. The library owner — *will it respect my collection?*

| | |
|---|---|
| **Who** | Owns 500–20,000 files, rips carefully, may DJ or have once DJ'd. Has a tagging workflow and opinions about it. Distrusts subscriptions for reasons they can articulate. |
| **Motivation** | The library as an archive: measured (tempo, key), listable, playable without an account, and not re-encoded by a service. The product's actual mechanics — trusted tags, a card that carries its own name, playlists as files, a Snapshot of measurements — are *made for this person* (`docs/adr/0009`; `docs/specs/card-mark.md`; `docs/specs/playlist-format.md`). |
| **Objections** | "It re-encodes to MP3 — so my FLACs are degraded." · "It writes to my files?" · "Key detection is unreliable." · "What happens to my tags?" · "Will it read my library without touching it?" · "If the card dies, is my library gone?" |
| **Where** | r/DataHoarder, r/DJs, r/Beatmatch, r/musichoarder, Discogs forums, the Mixxx/MusicBrainz/Rekordbox communities, self-hosted music forums (Navidrome, Plex, beets users). |

**Minimum information to say yes**

1. **The exact audio path, including what is lost.** Station format, that everything is re-encoded,
   that the encoder is not LAME — *(exists and is unusually well evidenced:
   `README.md:315-386,1274-1276`)*. **This is the segment where the honest quality answer is a
   conversion feature, not a risk.**
2. **Whether the source-bytes path exists.** It does not: there is no per-track GET, by design
   (`docs/specs/CONTEXT.md:9-30`). Say it before they discover it.
3. **What the app writes into their files, and what it never touches.** Tag writes plus a 402×188
   JPEG label, on a card opened read-write, in place as each track settles (`web/README.md:29-40`).
   A dropped folder is read-only and gets a *copy*. — This is a trust claim that needs a page, not a
   bullet.
4. **Accuracy, with the repo's own caveats.** Tagged fields are trusted and marked; key is "the
   weakest field"; mood models lose 10–30 points across collections (`web/README.md:229-238`).
5. **Card safety.** Atomic list writes (temp-then-rename), the card mark, and what happens when a
   card is removed while playing (it is detected — `app_services.c:341-420`).
6. **A demo of the measuring pass on a real library**, with timings — *(does not exist; the web
   README's screenshots predate the K7 look)*.

### C. The cassette person — *is this a costume?*

| | |
|---|---|
| **Who** | Owns tapes and a deck, dubs, buys NOS blanks, follows cassette culture. Small, loud, and the credibility gate for the whole product: if this group blesses it, everyone else's objection is answered in advance. |
| **Motivation** | The ritual and the object. If the kit arrives as an actual cassette shell with the electronics inside, this is the only audience for whom the sentence "a cassette that is the deck" is literally true. |
| **Objections** | "It's a screen pretending to be a tape." · "No transport, no heads, no wow and flutter." · "It's an SD card in a box." · "You're calling it K7 and it can't play a K7." |
| **Where** | r/cassetteculture, Tapeheads, cassette-focused Instagram and YouTube, local tape-label scenes. |

**Minimum information to say yes**

1. **The confession, first.** "We didn't bring back the tape; we brought back what you did with it"
   — the line already in the repo (`docs/research/marketing/campaign.md:91`).
2. **Physical proof**: what the shell is, how the board fits, what it feels like, whether a real
   cassette donor is used.
3. **The one thing that *is* tape-like**: reels at 47.625 mm/s, the 8-second wind on a track change,
   the label printed on the screen. These are the only claims this audience will accept.
4. **A video with the sound of the thing** — a card going in, the wind, the click of nothing
   (there are no buttons). Silence is a feature here, not a gap.
5. **No comparison to a Walkman that pretends to be one.** The prior plan's no-touch list is right
   (`campaign.md:850-852`).

### D. The design buyer — *do I want it in the room?*

| | |
|---|---|
| **Who** | Buys objects for how they look. Desk-setup and industrial-design feeds, Teenage Engineering adjacency, people who bought a Playdate or a Yoto. |
| **Motivation** | The object and the palette. This audience buys on one photograph and one four-second loop. |
| **Objections** | "There's no photo of the actual thing." · "Where do I buy it?" · "What does it even do?" · "Is it a toy?" |
| **Where** | Instagram, TikTok, YouTube reviews, design blogs, Pinterest. |

**Minimum information to say yes**

1. **A photograph of the real object, in a real room, with real light** — *(does not exist; the
   site's hero is the panel's own drawing in a slot built for a photo, `docs/specs/k7-site.md:103-106`)*.
2. **A price, or a range** — *(does not exist; Phase 6)*.
3. **One sentence they can repeat** — *(exists: "your music, on a cassette")*.
4. **A purchase path** — *(does not exist; there is a flash button, which this audience will never
   press)*.
5. **Reassurance that it will work with what they own** — a speaker, a phone, a turntable-less
   living room.

**INFERENCE:** this is the audience that makes the product *wanted* and the one that cannot be
served first. It needs the bridge, the case and a photographer. Do not spend on it in the first 90
days; do make the assets it will need, from the builder's shoot.

### Which segment to lead with

**A, then C, then B, then D.** Not because A is the biggest — it is the smallest — but because A is
the only segment whose "yes" can be earned with what exists this week, and the only one that will
*produce* the assets B, C and D need: a build log, a photograph, a video, a third-party write-up.

This is the reverse of the prior plan's order (`campaign.md:340-347`), which is the single biggest
disagreement in this document. Phase 2 §7 gives the reasoning; here is the schedule consequence:

| Month | Lead | Why |
|---|---|---|
| 1 | A | everything needed already exists, except a BOM and a video |
| 2 | A + C | the shell and the wind are the C proof; A's build log makes them |
| 3 | B + D | B needs the app's data story; D needs A's photographs |

---

## 2. Trust builders already in the repository

These are not "nice to have". They are the campaign's only real capital, and most of them are
already written.

| Asset | Why it is credible | Where |
|---|---|---|
| **MIT firmware, in a public repo, with 683 commits** | readable, forkable, no rug-pull possible | `LICENSE`; `github.com/xperiments/k7` |
| **96 ADRs that are superseded, not rewritten** | shows how decisions were made, including reversals | `docs/adr/` |
| **A rule that a phase is done only on host *and* board** | rare discipline, stated and followed | `README.md:470-474` |
| **Measured numbers with the log lines attached** | 41.4 % of a core over 59 minutes, internal RAM floor, scans, encoder cost | `README.md:533-568` |
| **A table of claims the product refuses to make** | the strongest possible anti-marketing asset | `docs/research/marketing/campaign.md:76-84` |
| **A list of known limitations, in the README** | nobody else in this niche publishes one | `README.md:1249-1283` |
| **Three signed firmware releases with sha256** | a stranger can flash without building | `web/firmware.json`; `web/public/firmware/` |
| **A live site that flashes a board from the browser** | a real capability, unusual in this niche | `https://k7.xperiments.dev/` |
| **An MCP endpoint on a cassette player** | the least expected, most shareable fact | `README.md:297` |
| **A working browser analyser, offline, no upload** | a free product the audience can try in 60 seconds | `web/README.md:3-4` |

INFERENCE: the trust story is not "open source". Everyone says that. It is **"this project tells you
what it does not do, and here is the appendix"** — and that is a story a technical audience will
retell.

## 3. Proof that is missing

Ranked by how much of the campaign it unlocks.

1. **One photograph of the physical object.** Without it, D is unreachable, C is suspicious and B
   is unconvinced it is a thing. The shell is the subject; the screen is the detail.
2. **One video, 60–90 seconds, of the whole loop**: card in → reels start → music from a speaker
   across the room → a list on the deck's screen → the tape winds on a change. This single asset is
   the campaign. Everything else is a derivative.
3. **A BOM and a wiring diagram**, so a builder can price the kit themselves. This is the difference
   between "interesting" and "I ordered the parts".
4. **A boot-to-music timing** (card in → sound out), measured on the bridge. Nobody has measured it.
5. **A second builder.** One stranger who built it and wrote about it is worth more than any asset
   the author can make. There is currently no evidence anyone else has.
6. **A price and a box.** Even a range, even a "not yet" with a date.
7. **A photograph of the label print on the panel.** The most surprising visual feature, and it has
   never been photographed (`docs/design/label-templates/` closes the ticket; the repo has no photo).
8. **The bridge itself**, working, in the same frame as the cassette.

---

## 4. The aha moment

**Ranked, with the dependencies stated.**

### 1. The one that sells it — "it's playing in the other room"

> A hand puts a microSD card into the cassette. The reels start turning. From across the room, a
> speaker starts playing that track. The camera does not move.

**Under ten seconds. No words needed.** It answers the three questions in order — *it's a cassette,
it's working, it's wireless* — and the third one is a surprise because of the first two.

*Depends on:* the bridge, a speaker, the card, one camera, one room. **It does not depend on a case
or a price.** This is the shot to build the campaign around and the reason the bridge is the
campaign's critical path.

### 2. The one that works today — "the tape winds"

> Change track on the deck. The reels spin down for eight seconds, the tape packs shift, and it
> lands on the new track at normal play.

*Depends on:* the board only. **This can be filmed this week**, and it is genuinely unusual: the
wind is bounded by tape geometry, not by an animation curve (`cassette.h:38-44,65-84`). But on its
own it says "a nice screen", not "a music machine". Use it as the teaser, not the argument.

### 3. The one nobody expects — "the label"

> Two fingers hold an invisible cassette against the screen; the track's cover becomes a printed
> cassette label, with the title, the artist and the band colours.

*Depends on:* the board, the label sheet, one good cover. It is the single most *shareable* thing the
software does, and the most likely to be reposted by someone who does not care about audio.

**Recommendation:** shoot #1 as the flagship. Film #2 and #3 on the same day, from the same set,
with the same light. Three assets, one shoot, and between them they cover A (build), C (ritual) and
D (desire).

---

## 5. The kit question, answered

The check-in asked: *"I plan to sell kits for pre-assembled assembly. This avoids certification
issues. It's a real product, selling it as a kit, correct?"*

**Short answer: it is a real product, and a kit is a legitimate way to sell it — but the certification
assumption is wrong, and the project's own research already says so.**

The repo's compliance brief assessed exactly this, in its own words:

> "**Sell a kit** — modules, panel, card, wires, printed guide. The kit is electrical equipment;
> **do not rely on the 'assembler becomes the manufacturer' argument** — as the seller of a kit
> intended to become this product, you are likely the manufacturer." — needs "CE (at minimum EMC),
> RoHS, WEEE; a legal read before the first sale"
> (`docs/research/marketing/compliance-2026-11-03.md:113-117`)

and, immediately after:

> "There is no fourth route where open source or 'it's a prototype' removes the obligation when money
> changes hands." — `compliance-2026-11-03.md:119-120`

What a kit *does* change, and it is not nothing:

- **The radio module is pre-certified.** RF testing scope shrinks to host integration — antenna,
  ground plane, shielding, the module's own label rules. That is cheaper than certifying a radio
  from scratch, and the repo already lists these as the tasks to do
  (`compliance-2026-11-03.md:136-139`).
- **The power supply and enclosure are the buyer's problem** if the kit ships without them, which
  removes some safety-file work — but not the EMC question for the assembled device.
- **WEEE/RoHS and packaging EPR still attach** to the seller of the kit in most EU readings, and if
  you ship to France, the AGEC/Triman and EPR registrations are per-market.

**The routes that genuinely avoid certification**, from the same file:

1. **Sell the plan** — the guide, the BOM, the firmware, the files. "You are selling a document, not
   equipment." No CE, no FCC. *(`compliance-2026-11-03.md:110-112`.)*
2. **Sell nothing** — publish, take donations, apply for a grant (the prior funding memo's route).

**PROPOSAL — the sequencing that keeps the plan alive without a false claim:**

| Stage | What is sold | Certification exposure | What it needs |
|---|---|---|---|
| 0 | nothing — the build is free | none | the BOM, the guide, the video |
| 1 | **the plan + a parts bundle sold as a "bill of materials kit" with no assembled claim** | low, but a lawyer must read the label | clear "components, not an apparatus" framing |
| 2 | **a kit that becomes the product** | CE/EMC, RoHS, WEEE, EPR | €3–10k, 6–10 weeks, a frozen design |
| 3 | finished units | full CE + FCC | the same, plus a factory |

The blunt version: **a kit is not a certification loophole; it is a smaller product with the same
duties.** Plan the price around that, and do not let the campaign say "kit, so no CE" in public —
that sentence is exactly what a competitor or a journalist would quote back.

Two more things the kit route adds, which need a decision before Phase 6:

- **If the bridge is a second ESP32 you also sell**, certification scope doubles, and Bluetooth
  brings its own qualification (Bluetooth SIG declaration) on top.
- **The Cyber Resilience Act** applies to products with digital elements from 11 Dec 2027
  (`compliance-2026-11-03.md:22-29`) and expects a written support/update policy. There is no OTA
  today (`README.md:1264`). A kit sold now with no update path is a 2027 problem, not a 2026 one —
  but it is the reason to build OTA before the second batch, not after.

---

## 6. What each segment needs, in one page

If only one table from this phase survives, this is the one.

| | A. Builder | B. Library owner | C. Cassette person | D. Design buyer |
|---|---|---|---|---|
| **Lead with** | the BOM and the build log | what it does to the files | the confession | one photograph |
| **Proof they check first** | it compiles and runs on a stranger's desk | the audio path, unhedged | the wind, the reels, the shell | the light on the plastic |
| **Killer objection** | "no sound, no case" | "it re-encodes my FLACs" | "it's a screen" | "where do I buy it" |
| **Answer that works** | the bridge + honest limitations | "it is radio, and here is the encoder" | "we didn't bring back the tape" | price + a photo + a purchase path |
| **Asset that converts** | 90-second build video | a measuring pass on a real library | a silent film of the wind | a still, in a room |
| **When** | now | month 3 | month 2 | month 3+ |
| **Can they buy today?** | yes — the plan is free | no | no | no |

<!-- Phase 4 follows in 04-claims.md; the models-and-licences appendix lives there. -->

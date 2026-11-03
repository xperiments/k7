# K7: a global campaign — what to do, and what to refuse

Researched 2026-11-03 against two kinds of primary source: **this repository** (the product's
own specifications, ADRs and measured board status) and **the platforms' own documentation**
(Meta, TikTok, Google, web.dev, the European Commission). Every claim below carries the file
and line it came from, or a URL. Where a number is a planning assumption rather than a
measurement, it says so in the sentence, not in a footnote.

The question was: *we have to market K7, thought globally, with objectives on the web,
Instagram, TikTok and YouTube — what should we do?*

---

## 1. The verdict, before the reasoning

1. **There is no product to sell yet, and no live web surface to sell it from.**
   `https://k7.xperiments.dev` did not resolve on 2026-11-03 (`curl: (6) Could not resolve
   host`) — the deployment in [`docs/specs/k7-deploy.md`](../../specs/k7-deploy.md) is a plan,
   and the card writer and Snapshot flash path are still unbuilt or board-pending
   ([`docs/specs/k7-app.md:331`](../../specs/k7-app.md#L331),
   [`README.md:1133`](../../../README.md#L1133)). A conversion campaign would be a
   promise with no checkout behind it.
2. **So the campaign's job in the first 90 days is not to sell units. It is to make the
   object wanted, and to make the build credible.** Conversion is a waitlist and a build
   guide, not a cart.
3. **The free product is the demo.** The analyser at the heart of the app measures tempo,
   key, mood and energy in the browser and never uploads a file
   ([`web/README.md:4`](../../../web/README.md#L4)), and the app is usable with no player in
   the room ([`web/CONTEXT.md:5`](../../../web/CONTEXT.md#L5)). That is a 60-second proof that
   costs a visitor nothing — the strongest asset the project owns and the one the campaign
   should be built around.
4. **The campaign is a twelve-week tape with two sides.** Side A (weeks 1–6) is the object:
   short vertical video, sound-on design, no ask. Side B (weeks 7–12) is the build: long-form
   YouTube, the demo, the waitlist. One shoot bank, four channels, one asset numbering
   system. §6 and §7.
5. **No paid media until an organic asset has earned it.** Media is a multiplier on a
   message that already works; at week 0 there is no message, only a build. §11 puts the
   first euro in week 6, behind a rule (§14) that can cancel it.
6. **The plan now runs at €0.** The first version of this document priced a Standard envelope
   and treated it as the reference; that was the wrong default. §17 is the route that runs on
   hours, and the envelopes in §11 are upgrades bought with evidence, not prerequisites.

The rest of this document is how that verdict is arrived at, and what to do on Monday.

---

## 2. The product, and the exact claims the campaign may make

Marketing is a set of claims. The repository is unusually strict about its own claims — it
separates what is measured from what is assumed, and it names what it has *not* built. The
campaign inherits that discipline: a claim that the product cannot back is refused at review,
not softened in the edit.

### 2.1 What it is

A cassette-shaped music player built on an ESP32-S3, with an 800×480 panel, that owns a music
library on a microSD card and **broadcasts** it over the local network: one continuous MP3
stream where every listener hears the same track at the same moment
([`README.md:1`](../../../README.md#L1), [`README.md:6`](../../../README.md#L6),
[`README.md:1180`](../../../README.md#L1180)). Beside it is a browser application — free,
installable, offline-capable — that measures the card's tracks, authors playlists, browses the
library, and writes the card's Update file and its Card mark
([`web/CONTEXT.md:1`](../../../web/CONTEXT.md#L1),
[`docs/specs/k7-app.md:1`](../../specs/k7-app.md#L1)). The name is not a style choice: *K7* is
what a cassette is called in French, and the thing is shaped like one
([`docs/adr/0062-xtape-becomes-k7.md:3`](../../adr/0062-xtape-becomes-k7.md#L3)). The rebus
works in Catalan too, because seven is *set* there: K7 reads *ca-set*, which is *casset* (§3.3).

### 2.2 What it is not — and the claims we will refuse

This table is the campaign's hardest working document. Each row is a sentence that would
convert well and is **false**, plus the true sentence to use instead.

| The tempting claim | Why it is false | Say instead |
|---|---|---|
| "The cassette is back" | It does not accept, play, record or reverse a cassette. The "tape" is a microSD card. | "A cassette for the music you already own." |
| "Hi-fi", "audiophile", "lossless playback" | The station is a continuous 256 kbps MP3 re-encode, and the encoder's psychoacoustic model is the limit — the repo says so itself. | "Your library, on the air, in the room." Never a fidelity claim. |
| "Pause, seek, skip back" | The broadcast cannot be paused or seeked; every client hears the same moment. | Show it as radio, because it is radio. |
| "Multi-room" | One device per LAN: the hostname is fixed and two devices collide. | "One tape, one room." |
| "Private and secure" | There is no authentication anywhere and no TLS, deliberately, on a LAN appliance. | "No account, no cloud, nothing leaves your network." That is true and checkable. |
| "Works anywhere, off-grid" | It needs a Wi-Fi network and a card prepared on a computer. | "It works on your network, not on someone else's." |
| "AI DJ", "AI discovers music" | The assistant writes search *filters* from a sentence and never writes results or describes a track — deliberately. | "Describe the set. It writes the filter. You keep the taste." |
| "Streaming integrations" | None exist, by design. | The absence *is* the product: nothing is licensed to you, so nothing can be revoked. |
| "Available now" / "Buy" | Nothing ships; the web app is not deployed; the card writer is unbuilt. | "Join the build list" — until a checkout and a compliance file exist (§13). |

Two of these are load-bearing for the campaign's credibility, and they point in opposite
directions:

- **Do not court cassette purists with a lie.** `r/cassetteculture` will identify a card
  reader in two seconds, and the comment section will do the campaign's reputational damage
  for free. The campaign's line is the honest one: *we did not bring back the tape; we brought
  back what you did with it.* That is also a better story.
- **Do not overclaim privacy.** The repo documents plain HTTP, no auth, credentials compiled
  in, and a warning not to expose it to the internet
  ([`README.md:23`](../../../README.md#L23)). The claim "no account, no cloud" is true and
  verifiable; "secure" is not, and a journalist will check.

### 2.3 Product facts the creative can rely on

| Fact | Where it is established |
|---|---|
| The screen paints a K7 mark on black at boot and holds it while the card is scanned | [`docs/adr/0045`](../../adr/0045-the-boot-mark-holds-until-the-card-is-scanned.md) |
| Reels follow tape geometry; tape winds on a track change; the VU fades through the reel change | [`docs/adr/0005`](../../adr/0005-cassette-reels-follow-tape-geometry.md), [`docs/adr/0011`](../../adr/0011-cassette-rewinds-on-a-track-change.md), [`docs/adr/0015`](../../adr/0015-tape-edge-and-vu-fade-through-the-reel-change.md) |
| A track's cover is printed onto a tinted cassette body, and the body's colour comes from the cover | [`docs/adr/0039`](../../adr/0039-the-cassette-is-a-card.md), [`docs/adr/0040`](../../adr/0040-the-cassettes-are-a-table.md) |
| Knobs are dragged, and a pad drives the knob | [`docs/adr/0027`](../../adr/0027-the-knobs-are-dragged.md), [`docs/adr/0028`](../../adr/0028-the-pad-drives-the-knob.md) |
| Shuffle is a mode of the broadcast, walked both ways | [`docs/adr/0023`](../../adr/0023-shuffle-is-a-broadcast-mode.md) |
| The app measures BPM, key (with Camelot code), mood and energy in the tab; files never leave the machine | [`web/README.md:3`](../../../web/README.md#L3) |
| The assistant turns a sentence into editable filters, never into results | [`web/CONTEXT.md:290`](../../../web/CONTEXT.md#L290), [`docs/adr/0053`](../../adr/0053-the-assistant-writes-filters.md) |
| An MCP endpoint lets other software read the library, and a control build can play/skip | [`README.md:127`](../../../README.md#L127) |
| Firmware is MIT; the analyser app is AGPL-3.0-only; the MP3 encoder stays LGPL in its own component | [`README.md:992`](../../../README.md#L992), [`web/package.json`](../../../web/package.json) |

**Consequence.** The creative material writes itself from the right-hand column, and almost
none of it needs an actor, a script or a claim. It needs a camera pointed at a screen.

---

## 3. The market: a minority inside a growing market

### 3.1 The numbers that exist

IFPI's own release for its 2026 report (covering 2025) is the primary source for the global
picture, and it is unambiguous about the shape of the market
([IFPI, 18 March 2026](https://www.ifpi.org/global-music-report-2026-global-recorded-music-revenues-grow-6-4-as-record-companies-drive-innovation/)).

| What | 2025 | Trend |
|---|---|---|
| Global recorded-music revenue | **US$31.7 bn** | +6.4%, an eleventh consecutive year of growth |
| Total streaming | over **US$22 bn** | 69.6% of global revenues |
| Paid subscription streaming | — | +8.8%, 52.4% of revenues, **837 million paid accounts** |
| Physical formats | **US$5.3 bn, 16.6%** of all revenue | **+8.0%**, +US$390 m, "returned to growth" — but **−3.0% in 2024** |
| Vinyl | — | **+13.7%**, a nineteenth consecutive year of growth |
| Asia | — | +10.9%; **45.1% of global physical revenues** |
| Europe | — | +5.6% (France +3.7%, UK +4.8%, Germany +1.7%) |
| Japan / China | — | +8.9% / +20.1% |

### 3.2 Three readings, and the one that matters

1. **The market is streaming, and the campaign is not fighting it.** 837 million paid accounts
   is the weather, not the enemy. K7 is addressed to the minority that still buys an object;
   the campaign's job is to be the exception worth making, not to convert the majority.
2. **Physical growth is vinyl's, and it is not monotonic.** Physical reached US$5.3 bn and
   +8.0%, but it **fell 3.0% in 2024**; vinyl is the engine at +13.7% and 19 consecutive years;
   and IFPI publishes **no cassette figure at all**. The only hard cassette counts are national
   and small: **164,000 units in the UK in 2025, +53%** — after "a slight dip in 2024" — and
   then **−25.3% in the first half of 2026**
   ([Official Charts](https://www.officialcharts.com/chart-news/best-selling-cassettes-singles-albums-2025/),
   [Music Week](https://www.musicweek.com/labels/read/physical-sales-growth-accelerates-in-first-half-of-2026-as-vinyl-closes-in-on-cd/094401)).
   The US publishes only an "Other Physical" bucket (cassettes, MiniDisc, SACD, video), and it
   grew **16.5% in revenue on 0.5% more units** — price and mix, not volume
   ([RIAA 2025](https://www.riaa.com/wp-content/uploads/2026/03/RIAA-Year-End-Revenue-2025.pdf)).
   So "the cassette is back" is a spike off a six-figure base, not a plateau. That is exactly why
   §2.2 refuses the claim: the honest position is **an object play that borrows the cassette's
   form**, and it is also the only position the numbers support.
3. **"Global" is not "English-first and US-heavy".** Asia is 45.1% of global physical revenue
   and grew 10.9%, with Japan at +8.9% and China at +20.1%. That does not mean launching in
   Japan in week 1 — it means the English-only assumption is a choice with a known cost, and
   that a later Japanese lane is the largest single expansion available. The language plan in
   §4.5 follows from this.

### 3.3 The word is an asset, and it is not a slogan

*K7* is not a model number invented for the product. It is a phonetic spelling — a rebus — of
*cassette*, introduced by Philips around 1966, pronounced [ka.sɛt] and feminine in French
([Wiktionnaire, *K7*](https://fr.wiktionary.org/wiki/K7)). That is a genuinely good story: the
name is a sixty-year-old pun that French speakers still use without hearing the pun.

Two consequences, one of them a caution:

- **It gives the campaign its origin line for free** — the word French has used for a cassette
  since Philips. In French, the name needs no explanation and carries the object's whole history
  with it. That is why the first localisation is French, not German.
- **In Catalan the rebus works by pronunciation, not by usage — and the owner is right about
  it.** Catalan for seven is **set**, pronounced /ˈsɛt/ ([Viccionari, *set*](https://ca.wiktionary.org/wiki/set)),
  and Catalan for a cassette is **casset**, /kəˈsɛt/ ([ca.wikipedia, *Casset*](https://ca.wikipedia.org/wiki/Casset)).
  So *K7* read aloud in Catalan is **ca-set** — the same pun as French, with the same result
  ([ADR 0062](../../adr/0062-xtape-becomes-k7.md)). What the corpus does **not** show is the
  written form *K7* used as a Catalan noun: Catalan writes *casset*. The distinction matters for
  the copy deck — say the name in Catalan, never present *K7* as the Catalan spelling.
- **A dictionary is not an authority, and neither is this document.** Le Robert returns nothing
  for *K7* and Larousse has no entry; Wiktionnaire is the citable source here, not the Académie.
  Before the origin line goes to print, one French editor confirms it — and the pan-francophone
  reach (Belgium, Quebec, Africa) is unproven, so do not write "everywhere French is spoken".

### 3.4 Who buys, and why — the campaign's pillars in the buyers' own words

| Finding | Figure | Source |
|---|---|---|
| Share of US vinyl buyers aged 13–24 | **26%** (46% are 13–34) | [Luminate, AIM London deck, 2026](https://luminatedata.com/wp-content/uploads/2026/09/2026-AIM-London-Presentation.pdf) |
| Share of US CD buyers under 34 | **38%** | same |
| Buyers at US independent record stores aged ≤34 | **46%** | same |
| Bought music on cassette | **5%** of respondents | [IFPI *Engaging with Music 2023*](https://www.ifpi.org/wp-content/uploads/2023/12/IFPI-Engaging-With-Music-2023_full-report.pdf) |
| Why they buy vinyl | collecting **24%**, "physically owning my own music" **22%**, ritual of playing **19%**, artwork to look at **19%** | same |

Two things follow, and the second one is the campaign's structure handed over by the audience:

1. **The audience is younger than the nostalgia framing assumes.** A quarter of US vinyl buyers
   are 13–24 and nearly half are under 35. That is a short-video audience, not a Facebook one:
   it justifies putting TikTok and Reels ahead of every other channel and refusing the
   "remember the nineties" register outright.
2. **The stated reasons are the three pillars, in the buyers' words.** *Collecting* and
   *owning* are the object and the tape; *ritual* and *artwork* are the room and the object.
   §4.3 did not invent a positioning; it transcribed a survey.

**And the thing the campaign must not say.** No primary survey measures "streaming fatigue", and
the measurable signal runs the other way — US paid subscription accounts rose by **6.5 million
in 2025 to 106.5 million**, the strongest net add since 2022
([RIAA data, via MBW](https://www.musicbusinessworldwide.com/us-paid-music-streaming-subscriptions-hit-106-5m-in-2025-adding-6-5m-accounts-yoy-the-strongest-growth-since-2022/)).
The enemy in §4.2 is a *business model* the audience has a relationship with, not a trend the
campaign may claim is collapsing. Write "you don't own it"; never write "nobody streams any
more".

### 3.5 Adjacent objects, and the price signal

| Object | Price as seen (2026-11-03) | What it is | What it is not |
|---|---|---|---|
| Nothing Headphone (1) | **£249** ([nothing.tech](https://nothing.tech)) | design-led ANC headphones | not a player, not an object you keep |
| Teenage Engineering EP–133 K.O. II | **$329** ([teenage.engineering](https://teenage.engineering)) | sampler / sequencer | not a music player |
| FiiO M21 | **$203.99** ([fiiohifi.com](https://fiiohifi.com)) | hi-res digital audio player | a phone-shaped DAP with no ritual |
| Crosley CR3045 cassette-shaped Bluetooth speaker | **€21.99** ([jpc.de](https://www.jpc.de)) | a novelty shaped like a tape | **no transport at all** — the exact thing K7 must not be confused with |
| We Are Rewind WE-001 | **€179** (press-reported; the brand's site did not resolve) | a real portable cassette player | a Walkman, not a cassette |

**The gap is the product.** Nothing in this set is a cassette-shaped object that plays the files
you already own: the €22 Crosley only looks like a tape, the €179 We Are Rewind actually plays
tapes but is a Walkman with a belt clip. K7's category is the empty space between them.

**The price signal, as a hypothesis rather than a decision.** The design-audio objects sit at
**€180–330**; the novelty sits at **€22**. A K7 unit priced under €100 would be read as the
Crosley, and the campaign would spend its credibility explaining that it is not a speaker. That
is an argument for a **€150–300** target *if* a finished unit ever ships, and for a build kit
far below it — but it is a hypothesis for the build-list survey to confirm, not a price. §13
gates any price at all.

### 3.6 What this does not prove

- **The revival is not monotonic, and the campaign must survive that.** Global physical fell 3.0%
  in 2024 and UK cassettes fell 25.3% in H1 2026. A "cassettes are back" line has to survive
  both data points, which is why the campaign does not use it.
- **No streaming fatigue.** Paid subscriptions grew faster in 2025 than in 2024; "people are
  tired of streaming" is an inference, not a finding, and the copy should never assert it.
- **The audience size is unknown.** Reddit's own endpoints returned 403 and the mirrors 429/503
  during this research, so no community is sized here; the plan sizes audiences by engagement,
  not by subscriber count, and the first 90 days exist partly to measure what nobody published.
- **Physical growth is not demand for this object.** The report measures spending on records,
  not on cassette-shaped players with a microSD slot. Nothing in it says anyone wants K7.
- **Revenue is not buyers.** A small number of high-spend collectors can move revenue without a
  mass audience — which suits a niche product, and also caps it.
- **No cassette unit count.** Any "cassette revival" number in the campaign's copy needs a
  source that counts units (a national trade body, not a headline figure about physical), and
  IFPI provides none. The UK figure is the only clean count found, and it is six figures.
- **A global 2025 figure is not a 90-day forecast.** The campaign's targets in §8 are planning
  numbers, not a market projection, and should be replaced by the campaign's own baseline after
  week 3.

---

## 4. Positioning

### 4.1 The statement

> For people who own their music and miss the object it came in, **K7** is a cassette-shaped
> player that turns a card into a tape and a room into a radio station. Unlike a streaming
> box, it cannot recommend anything. It only plays what you chose.

Every clause in that sentence is defensible from §2. The category it competes in is not
"cassette player" (there are real ones, and K7 is not one). It competes with **the streaming
subscription and the smart speaker** as the thing that plays music at home — and it wins on
the two axes the campaign can prove: *the object* and *the ownership*.

### 4.2 The enemy, named precisely

Not "technology", and not "the internet" — the product is a Wi-Fi device built by an engineer.
The enemy is **renting access to your own taste**: the account, the licence that can be
revoked, the recommendation that decides what you hear next, the queue that is different in
every room.

The campaign's tone follows from that: **a technician who loves music**, not a nostalgia
brand. No sunset filters, no "the good old days", no film grain for its own sake. The
nostalgia is in the object; the voice is dry, precise and warm. Specifications as poetry:
*800×480. 44.1 kHz. One tape per LAN.*

The claim is about ownership, not about a trend: **write "you don't own it", never "nobody
streams any more"** (§3.4 — subscriptions grew faster, not slower, in 2025).

### 4.3 The three pillars

| Pillar | The promise | The proof the campaign can show | The claim it must not make |
|---|---|---|---|
| **The object** | It exists, in the room, and it is worth looking at. | Boot mark, reels at tape geometry, cover printing onto a tinted body, VU, dragged knobs ([§2.3](#23-product-facts-the-creative-can-rely-on)). | "It feels premium" — say it with the hand and the light, do not say it. |
| **The tape** | You choose, name and keep a selection. It is yours and it fits in a pocket. | The Card mark, playlists authored in the app, the card as the tape ([`docs/specs/card-mark.md`](../../specs/card-mark.md), [`docs/adr/0039`](../../adr/0039-the-cassette-is-a-card.md)). | "Mixtape" as a product noun — the product's own glossary reserves *Playlist* and avoids *mixtape* ([`web/CONTEXT.md:263`](../../../web/CONTEXT.md#L263)). Use it culturally in copy, never as the feature name. |
| **The room** | Everyone in the room hears the same track at the same moment. Music becomes a shared event again. | The broadcast ([`README.md:6`](../../../README.md#L6)); the silence when four people stop talking. | "Multi-room", "whole-home", "sync" — one device, one LAN. |

### 4.4 Naming, spelling, search

- Always **K7**, never "K-7", "k7" in a headline or "K7 player" alone. The two usable noun
  phrases are **"K7 cassette"** (the object) and **"K7 tape"** (the thing you make). In French,
  **"la K7"**.
- **"K7" alone is a polluted query.** It collides with unrelated commercial uses and, in
  French, with the ordinary word. The campaign therefore never optimises or buys the bare
  term: every keyword, hashtag, title and tag pairs it — `K7 cassette`, `K7 tape player`,
  `K7 by xperiments`, `#K7tape`. This is also why the campaign has a name of its own (§6):
  a search for the campaign must not compete with a search for the product.
- **The domain is a strategic weakness, not a detail.** `k7.xperiments.dev` is a subdomain of
  a lab domain: it cannot be said aloud, it does not survive a business card, and it will
  never win a brand query. Before any paid media, the campaign needs a short, sayable origin
  that 301s to the app. This is a decision for the owner, not for the campaign, but it gates
  §11 and it should be made in week 1.

### 4.5 Languages and markets — what "global" actually means on day one

A global campaign is not one English campaign with a world map behind it. It is one production
with a language plan attached, and the plan is ordered by evidence rather than by market size.

| Lane | When | What is localised | Why this lane, this order |
|---|---|---|---|
| **English** | Day one | Everything. The master cut, the captions, the hub. | The default, and the only lane the shoot has to fund. |
| **French** | Day one (subtitles), week 5 (hub and paid) | Subtitles, carousel copy, the origin line. | The name is French (§3.3), and France is the market where the product needs no explanation. |
| **German** | Week 7 | Subtitles only. | One of Europe's three largest recorded-music markets (IFPI), with a cassette and techno culture that reads the object correctly. |
| **Spanish** | Week 10–12, if the funnel supports it | Subtitles; landing page later. | Spanish says *casete*, so *K7* is a brand there, not a word — but in Catalan it reads *casset*, which makes a Catalan lane cheaper than a Spanish one (§3.3). |
| **Japanese** | After the 90 days | Not in this plan. | Asia is 45.1% of global physical revenue; it is the largest expansion available and it deserves a plan of its own, not a subtitle track. |

Two rules hold across lanes: **the film is the same film** (the object does not need translating),
and **the caption is not** — a burned-in English caption with a French subtitle underneath is
what a global feed actually watches, and it costs one SRT file per lane, not a reshoot.

---

## 5. Who this is for

Three audiences. The campaign is addressed to A and C; B is the layer that makes the other two
believe it, and the reason the films can be as confident as they are.

| | **A. The collector** | **B. The builder** | **C. The design buyer** |
|---|---|---|---|
| **Who** | Owns 500–20,000 files and physical records; keeps a library deliberately; already distrusts the subscription. | Writes firmware, runs a homelab, owns a soldering iron and a 3D printer; reads the source before the brochure. | Buys objects for how they look and feel; follows desk-setup and industrial-design accounts; will pay for a thing that says something about them. |
| **What convinces them** | That the library is treated as an archive, not as content: measurements, keys, playlists, a card that carries its own mark. | That the source is real, the hardware is documented, and the author reports what does not work. | One photograph and one four-second loop. The screen, the plastic, the light. |
| **Where they are** | Cassette and vinyl communities, Discogs, collector forums, Instagram. | Hacker News, GitHub, ESP32 and self-hosting communities, YouTube build videos. | Instagram Reels, TikTok, YouTube reviews, design blogs. |
| **What loses them** | Anything that treats a cassette as a costume. A false "lossless" claim. | Marketing language, a closed build, an unmeasured claim. | A product that looks cheap in motion; a video with a stock-music bed and no object. |
| **Campaign role** | Demand and credibility; the people who will argue in the comments. | Proof and backlinks; the ones who build it and document it, which is content the campaign cannot buy. | Reach and desire; the ones who make the object a thing other people want. |

**The order is a schedule, not a ranking.** The campaign is *for* A and C: they are who the
films address from week 1, and they are who make the object wanted. But B has to exist
underneath them first, because the builder produces the two things a collector or a design
buyer will check — a documented build (screenshots, a BOM, a video of it working) and
third-party writing that is not the brand's own. So B is seeded quietly and early (the repo,
the guide, one honest Show HN in week 7), and never as the campaign's face. Running a
design-first launch without that layer produces desire with no proof under it: a beautiful film,
a collector's click, and a comment section that says "where do I buy it" with no answer.

---

## 6. The idea: a tape with two sides

**Campaign: K7 — SIDE A.** Twelve weeks, twelve tracks, two sides of six.

- **Side A, tracks 01–06 — the object.** Six short films about the thing itself. No call to
  action beyond *follow* and *join the build list*. This is where desire is made.
- **Side B, tracks 07–12 — the build.** The demo, the build guide, the flagship video, the
  argument. This is where credibility and conversion are made.

Why this is a system and not a theme:

1. **It numbers every asset.** `t07_hero_9x16_v2` is the same string in the edit, the file
   name, the ad's campaign field and the UTM. A twelve-track campaign with a strict ID is a
   campaign whose creative report can be read without a spreadsheet archaeology session (§10).
2. **It matches the product's own metaphor**, so the campaign explains the product by being
   shaped like it — the one kind of cleverness that earns its keep.
3. **It sequences the ask.** A tape has a side you play first and a side you turn over. The
   build list is open from day one, but it is not the campaign's *ask* until week 7 — by then
   there is proof to sign up for, and the ask costs the audience something it can afford.
4. **It survives a bad week.** A track that underperforms is one track, not the campaign: the
   next one ships on schedule, because the schedule is the format.

**Hashtag and handle discipline.** One campaign tag — `#K7tape` — carried on every post, plus
the product tag `#K7cassette` on the object films. No invented micro-tags: a tag with twelve
posts is a tag nobody follows.

---

## 7. The twelve tracks

Each track is one content unit, produced once and cut for four surfaces. The "long-form"
column is where the track earns the right to be watched for eight minutes; not every track
has one.

### Side A — the object

| # | Title | The 15-second hook | The proof in the frame | Long-form |
|---|---|---|---|---|
| 01 | **Boot** | Black glass. The K7 mark paints, the backlight rises, a relay clicks. No music, no voice. | The boot mark held until the card is scanned ([ADR 0045](../../adr/0045-the-boot-mark-holds-until-the-card-is-scanned.md)). | — |
| 02 | **Reels** | Macro on the reels: they turn at the track's speed, the tape winds when the track changes. | Reels follow tape geometry ([ADR 0005](../../adr/0005-cassette-reels-follow-tape-geometry.md)). | 4 min: "why the reels are not an animation" |
| 03 | **Label** | A cover prints onto a cassette body; the body's colour is sampled from the cover. | The Card and the print ([ADR 0039](../../adr/0039-the-cassette-is-a-card.md), [ADR 0040](../../adr/0040-the-cassettes-are-a-table.md)). | — |
| 04 | **Needle** | The VU in the dark, one lamp, a bass line. The meter moves like it is being played. | The VU fade through the reel change ([ADR 0015](../../adr/0015-tape-edge-and-vu-fade-through-the-reel-change.md)). | — |
| 05 | **Knobs** | A hand drags the low knob; the room's low end lifts. Sound on, no words. | Knobs are dragged; the pad drives the knob ([ADR 0027](../../adr/0027-the-knobs-are-dragged.md), [ADR 0028](../../adr/0028-the-pad-drives-the-knob.md)). | — |
| 06 | **The room** | The object on a shelf in a real home, morning light, a record sleeve beside it. Nothing happens for four seconds. | The object at rest, at scale, in a life. | — |

### Side B — the build and the tape

| # | Title | The 15-second hook | The proof in the frame | Long-form |
|---|---|---|---|---|
| 07 | **Drop a folder** | A folder of 400 tracks is dropped into the browser; BPM, key and mood fill in, one by one, offline. | The analyser runs in the tab; files never leave the machine ([`web/README.md:4`](../../../web/README.md#L4)). | 8 min: "measuring a library without uploading it" |
| 08 | **The build** | A bare devkit, a panel, four wires, a card. It boots on the first flash. | The board status page ([`README.md:473`](../../../README.md#L473)) — including what failed. | 10–12 min: the flagship |
| 09 | **No cloud** | A paper diagram of three boxes — device, router, phone — then the phone in airplane mode still playing. | The bridge and the stream ([`docs/specs/bridge-protocol.md`](../../specs/bridge-protocol.md)); no account exists anywhere. | — |
| 10 | **The assistant** | A sentence is typed: "warm-up, 118–124, minor, not sad". A filter appears, editable, with a count. No playlist is invented. | The Intent, not the result ([`web/CONTEXT.md:290`](../../../web/CONTEXT.md#L290)). | 5 min: "the model writes filters" |
| 11 | **One room** | Three people, one room, one track, phones face down. Nobody reaches for the queue. | The broadcast: one moment, every client. | 6 min: a short documentary of the listening session |
| 12 | **Side B** | The ask, plainly: the build list, the guide, the price signal. The tape turns over. | Everything the previous eleven tracks proved. | 4 min: "what we are building next" |

**The kit each track ships.** One hero cut (9:16, 15–30 s), one alternate cut (6–10 s loop, or
a 45–90 s story), one carousel (5–7 slides, technical-drawing style), one web section, and a
long-form where the table says so. That is ~36 short assets and 6 long ones from two shoot
days plus a screen-capture day — the smallest bank that can feed three short-video platforms
for twelve weeks.

**Production rules, non-negotiable.**

- The object is in frame one. No logo stings, no title cards before the thing.
- The device's own sounds — relay, motor, boot — are the brand's audio signature. Music is
  licensed or original; never a trending sound that buries the machine.
- Burned-in English captions; a French subtitle file from day one and German by week 7
  (§4.5). Every feed is watched muted first.
- One shoot bank, four exports: 9:16 clean (no platform watermark), 4:5 for the Instagram
  feed, 16:9 for YouTube, and a 1:1 for nothing — square is dead.

---

## 8. The four channels, and what each one is for

The channels are not four copies of the same plan. Each has one job, and a channel doing
another channel's job is a channel being wasted.

The targets below assume the **Standard** envelope (§11). At Lean, halve the reach numbers and
keep the conversion *rates*: rates are properties of the message, reach is a property of the
money, and confusing the two is how a campaign reports success while selling nothing.

### 8.1 What each surface physically accepts

Read from the platforms' own references on 2026-11-03; the full sheet, with every URL and the
doc's own "last updated" date, is [`facts-2026-11-03.md`](./facts-2026-11-03.md).

| Surface | Ratio | Max length | Max file | Ceiling that shapes the edit |
|---|---|---|---|---|
| Instagram Reels | 9:16 recommended (0.01:1–10:1 accepted) | **15 min** (min 3 s) | 300 MB | The 15 minutes is an API ceiling, not a reason to make 15 minutes. |
| Instagram Stories | 9:16 recommended (0.1:1–10:1) | **60 s** | 100 MB | The only surface where a 9:16 vertical is mandatory in practice. |
| TikTok | **not stated** in the Content Posting API docs | 3 min for all creators; up to 10 min via API | 4 GB | 3 minutes is the safe planning number. |
| YouTube long-form | 16:9 standard; 1080p or 2160p | not stated in Help; 256 GB via API | — | Nobody finishes 12 minutes for a product they cannot buy; 8–10 is the target. |
| YouTube Shorts | vertical, 1080p max | **3 min** | — | A 15-second object film is the fit; the 3 minutes are there for the build cutdowns. |

Three documented constraints change the plan more than any creative decision:

1. **TikTok's audit rule.** *"All content posted by unaudited clients will be restricted to
   private viewing mode… your API client must undergo an audit"*
   ([Content Posting API](https://developers.tiktok.com/doc/content-posting-api-get-started)).
   **Consequence:** the campaign posts by hand for the first twelve weeks. Scheduling tools that
   use the unaudited API will publish to a private audience, silently. Do not build a posting
   pipeline; build a publishing *calendar*.
2. **Instagram's publishing limit contradicts itself inside one document** — 100 posts per 24 h
   in the guide prose, 50 in its own carousel section
   ([Content Publishing](https://developers.facebook.com/docs/instagram-platform/content-publishing)).
   **Consequence:** plan to 50, and never let an automation own the account's ability to post
   the one thing that mattered that day.
3. **YouTube's automation is cheap but narrow.** 100 `search.list` calls and 100 uploads a day,
   at 1 unit each, plus 10,000 units for everything else
   ([Quota calculator](https://developers.google.com/youtube/v3/determine_quota_cost)); API
   uploads from unverified projects are no longer forced private
   ([videos.insert](https://developers.google.com/youtube/v3/docs/videos/insert)). **Consequence:**
   uploads can be automated, but the channel's own search-driven research cannot be — 100
   searches a day is enough for a human, not for a scraper.

One thing the plan must *not* pretend to know: **no official safe-zone numbers exist** in any
spec page that would load on 2026-11-03, and TikTok's ad-spec article currently 404s. Design
against the platform's own preview, keep the caption out of the bottom fifth, and re-capture
the spec sheet before a paid flight rather than quoting a margin nobody can source.

### 8.2 Web — proof, conversion, and the only additive measurement

**Job.** Turn a curious visitor into a person who has measured their own library, made a tape,
and left an address. Nothing else on the web matters in the first 90 days.

**What ships.**

- A campaign hub that opens with the object in motion, then the demo above the fold:
  *drop a folder, watch it get measured*. No sign-up wall, no email gate before the value.
- The analyser, deployed at last. Today the code exists (`web/`) and the domain does not
  resolve; the deployment spec is written ([`docs/specs/k7-deploy.md`](../../specs/k7-deploy.md))
  and this is the first engineering task of the campaign, not the last.
- A "make a tape" step: pick a dozen songs from the analysis, name it, choose a cassette body
  colour, and get a shareable label image. This is the campaign's only viral loop, and it is
  built from parts that already exist (the Card, the print, the Playlist).
- One long page per proof: the build guide (BOM, wiring, flash), the measurements page (what
  the analyser does and how it was validated), the privacy page (what leaves the machine:
  nothing), and the press kit.
- The build list: one field, double opt-in, a real promise about frequency.

**Targets, 90 days (Standard tier, §11).**

| Metric | Target | Why this one |
|---|---|---|
| Sessions | 12,000 | The only number that is not platform-reported. |
| Demo start rate | 25% of sessions | If the demo is not the first thing, this collapses. |
| Analysis completion | 55% of starts | Measures whether the demo actually runs on real libraries. |
| Tapes created | 500 | The shareable artifact; also the strongest intent signal short of an address. |
| Build-list signups | 600 (range 300–900) | The conversion the campaign is actually for. |
| Guide downloads | 1,000 | The builder audience's hand raised. |
| Qualified builders | 120 | Signup **and** guide **and** a library over 500 tracks — the number the owner should read. |
| Core Web Vitals, p75 | LCP ≤ 2.5 s, INP ≤ 200 ms, CLS ≤ 0.1 | The demo is heavy; if it is slow on a phone, the campaign has no product. |

**What the web will not do.** No newsletter modal on first paint, no exit-intent popup, no
cookie wall that blocks the demo, no "AI" chat bubble, no blog written for search volume. A
handful of deep pages beat fifty thin ones, and every thin page is a page a buyer will find
instead of the proof.

### 8.3 Instagram — desire, and the object's portfolio

**Job.** Make the object desirable in a feed that is watched muted, and give the collector
something to save. Instagram is where the campaign's *craft* is judged: if the object looks
cheap here, the design audience is gone.

**What ships.** Two Reels and one carousel a week, plus four Stories. Reels are the Side A
heroes, re-cut with a quieter grade and a stricter first frame. Carousels are the technical
drawings: the anatomy of the cassette body, the signal path from card to speaker, the twelve
tracks of the campaign as a tape sleeve. Stories carry the build-list link, polls that feed
next week's carousel, and the reposts of the best comments.

**Targets, 90 days.**

| Metric | Target |
|---|---|
| Reach | 700,000 (organic) |
| Non-follower share of reach | ≥ 60% — below this, the account is talking to itself |
| Saves + shares per reach | ≥ 3% — the only engagement that predicts distribution |
| Profile visits | 30,000 |
| Link clicks | 4,000 |
| Followers | 12,000 |

**What Instagram will not get.** TikTok exports with a watermark, engagement bait ("comment
TAPE for the link"), DM automations, or a grid of quotes about music. The grid is the product.

### 8.4 TikTok — discovery, and the fastest place to be wrong

**Job.** Find the hook that works, at the lowest cost per attempt. TikTok is the campaign's
test lab: a track's opening three seconds can be validated here in 48 hours and then reshot
for the other two platforms. It is also the only one of the three where a small account can
still reach a million people in a week.

**What ships.** Four to five posts a week: two new heroes, two re-cuts of what worked, one
native response to whatever the cassette/retro-audio conversation is doing that week. Original
sound is the default — the machine's own noises are the signature — and a trending sound is
used only when it does not cover them. Comment replies are content: the first hundred comments
on a build video are the campaign's best market research.

**Targets, 90 days.**

| Metric | Target |
|---|---|
| Views | 1,200,000 |
| Median views per post | 8,000 |
| Average watch time, posts under 20 s | ≥ 55% |
| Shares | 12,000 |
| Followers | 15,000 |
| Link clicks | 5,000 |

**What TikTok will not get.** Choreography the product has nothing to do with, fake "customer"
reactions, giveaway loops, or a founder talking to camera for 40 seconds before the object
appears. The first frame is the device.

### 8.5 YouTube — consideration, and the proof that outlives the campaign

**Job.** Be the answer to "does this actually work?" for the next two years. A short video is
consumed and forgotten; a build video is searched, cited, and linked from forums. YouTube is
where the campaign converts the builder and reassures the collector.

**What ships.** Six long-forms across the twelve weeks (§7), starting in week 2, with three
Shorts a week cut from the same bank and community posts between them. The flagship is track 08 (the build). Around it: the analyser explained,
the "no cloud" argument, the assistant's guardrail, and one documentary of a listening session.
Titles are written as questions people type, not as poetry: *"A cassette player that plays your
own files (no cloud, no account)"*, *"Measuring BPM and key in the browser, offline"*.

**Targets, 90 days.**

| Metric | Target |
|---|---|
| Views | 150,000 (long-form) + 200,000 (Shorts) |
| Impression CTR | ≥ 4.5% |
| Average view duration, 8–12 min | ≥ 45% |
| Views from search | ≥ 35% — the durability test |
| Subscribers | 2,000 |
| Sessions sent to the hub | 3,000 |

**What YouTube will not get.** A 6-second bumper treated as a strategy, thumbnails that
promise something the video does not deliver, or a 60-second "ad" uploaded as a video. If it is
paid, it is bought as media (§11), not disguised as content.

### 8.6 The communities — where the first 500 people actually come from

None of the four channels above produces a first audience on its own. That comes from the
places where these three audiences already argue, and the rule there is older than any
platform: **give more than you take.** Reddit's own guidance is explicit — read a community's
rules before posting, and a widely used rule of thumb is that only about one in ten of a
person's submissions should be their own content
([Reddiquette, updated 2026-10-05](https://support.reddithelp.com/hc/en-us/articles/205926439-Reddiquette)).

**The plan, in order.**

1. **Weeks 1–2: participate, do not launch.** Answer questions in cassette, vinyl, ESP32,
   self-hosting and homelab communities with no link in the post. The account that will
   launch in week 7 must already have a history.
2. **Week 7: the build guide, not the product.** Post the guide and the measurements to
   Hacker News (*Show HN*), the ESP32 communities, and the self-hosting ones. The frame is
   "here is how it was built and what does not work", which is the only frame those rooms
   accept.
3. **Week 9: the culture, not the hardware.** Approach cassette and vinyl communities with
   the honest line from §2.2 — *we did not bring back the tape* — and let them be the judges.
   If the room rejects it, the campaign learns something a paid test cannot buy.
4. **Everywhere: one account, one voice, disclosed.** If the owner is posting, the profile
   says so. Astroturfing is the one mistake this audience does not forgive, and it is
   detectable in a day.

---

## 9. The web surface: seven pages and one loop

There is no marketing site today. There is an application (`web/`) that produces a build, and a
deployment plan ([`docs/specs/k7-deploy.md`](../../specs/k7-deploy.md)) that has not been
executed — the domain does not resolve. The campaign needs a small, fast surface in front of it,
and it needs it before the first post, not after.

### 9.1 What each page is for

| Page | One job | What is on it |
|---|---|---|
| `/` | Make the visitor start the demo in under 15 seconds. | One looping object film, one sentence, the demo drop zone, three proof numbers (tracks measured, keys found, nothing uploaded), the build list. |
| `/build` | Turn a curious engineer into a builder. | BOM with real part numbers, the four wires, the flash, the first boot, and **the failures** — the board-status section that says what did not work. |
| `/measure` | Make the measurement believable. | What BPM, key, mood and energy mean here, how they were validated, and the honest note that a tagged file is trusted rather than re-measured ([`web/README.md`](../../../web/README.md)). |
| `/privacy` | Answer the only question a hostile reader has. | What leaves the machine: nothing. What the device exposes on a LAN, including the no-auth warning the repo already prints ([`README.md:23`](../../../README.md#L23)). |
| `/tape/:id` | Be the shareable artifact. | A generated cassette label from a real tape: title, artist list, BPM/key spread, the body colour derived from the cover, and a "make your own" link. |
| `/press` | Give a journalist everything in one zip. | Facts, figures that are measured, product photographs, the founder's one-paragraph bio, a contact that answers in 24 h. |
| `/list` | Collect the address. | One field, double opt-in, two questions (library size; would you build or buy), and the promise about frequency. |

### 9.2 The loop

**Drop a folder → watch it get measured → make a tape → share the label → someone else drops a
folder.** That is the only loop the campaign has, and every page above either feeds it or
supports it. The label is the key: it is a real artifact made from the visitor's own music,
it looks like the product, it carries a link, and it costs the campaign nothing to produce.

Two design constraints make or break it:

- **The first value must arrive before the models download.** The analyser's weights are about
  24 MB ([`web/README.md`](../../../web/README.md)); a visitor on a phone will not wait. The
  demo therefore starts with a bundled 20-second sample: the visitor sees a tempo and a key
  appear, then chooses to load the real thing on their own folder. Ship the sample path first —
  and time it on the **deployed** URL, which runs single-threaded (§17.2), not on `npm run dev`.
- **The label must be generated, not templated by hand.** One canvas, deterministic from the
  analysis, exported as a 1080×1350 image and as an OG image for the share URL. If a human has
  to make the image, the loop is dead by week 3.

### 9.3 Performance and search, as constraints rather than goals

- **Core Web Vitals are the demo's problem.** LCP ≤ 2.5 s, INP ≤ 200 ms, CLS ≤ 0.1 at the 75th
  percentile, mobile and desktop separately
  ([Web Vitals](https://web.dev/articles/vitals)). A hub with a hero video and a heavy analyser
  fails LCP by default: the hero is a 4-second loop of a still frame, not an autoplaying video,
  and the analyser is imported on interaction.
- **Installability no longer needs a service worker.** The current criteria are HTTPS, a
  manifest with `name`, 192px and 512px icons, `start_url`, a display mode, and an engagement
  heuristic (one click and 30 seconds on the page)
  ([Install criteria](https://web.dev/articles/install-criteria)). The PWA stays, but it is not
  the campaign's promise — *offline* is.
- **`.dev` forces HTTPS** (HSTS preload, [Google Registry](https://get.dev/)), which the app
  wants anyway. It is not an SEO constraint.
- **No product structured data until there is a product.** Google's Product snippet needs
  `name` plus `offers`, `review` or `aggregateRating`, on a page about a single product — not a
  catalogue ([Product snippet](https://developers.google.com/search/docs/appearance/structured-data/product-snippet)).
  Adding it to a waitlist page is a false signal and a review risk, not a shortcut.
- **Search strategy is four pages, not forty.** One page per question the audience actually
  types ("a cassette player that plays MP3 files", "measure BPM and key of a folder offline",
  "local music player without a cloud account", "ESP32 cassette player build"), each written by
  someone who knows the answer. Google's own framing — technical requirements, spam policies,
  key best practices, and no guarantee of indexing
  ([Search Essentials](https://developers.google.com/search/docs/essentials)) — is a reason to
  write four good pages, not fifty thin ones.

---

## 10. Measurement: one truth, and three diagnostic layers

The failure mode of a four-platform campaign is four dashboards and no number. The rule here:
**GA4 and the build list are the only additive truth; every platform number is a diagnostic.**

### 10.1 The event schema

Five events, no more. Every one of them has a human decision attached.

| Event | Fires when | The decision it informs |
|---|---|---|
| `demo_start` | The visitor drops a folder or presses *try a sample*. | Is the hero doing its job? |
| `analysis_complete` | The first track finishes measuring. | Does the demo survive real libraries and phones? |
| `tape_created` | A dozen or more songs are named and the label is generated. | Is the loop real, or is the demo a toy? |
| `tape_shared` | The label image or link is copied or shared. | Is the artifact worth passing on? |
| `build_list_submit` | The double opt-in is confirmed. | The only conversion that counts. |

Plus `guide_download` and `outbound_github` for the builder audience, and `install` for the PWA.
Volunteer one free-text field on `/list`: *"what would you play on it first?"* — it is worth
more than any dashboard and it writes next month's creative.

### 10.2 UTM grammar

A taxonomy is only useful if it is impossible to get wrong. Fixed slots, lowercase, hyphens:

```
?utm_source=instagram|tiktok|youtube|newsletter|creator
&utm_medium=organic|paid|creator|email
&utm_campaign=sidea
&utm_content=t07_hero_9x16_v2
&utm_term=collectors
```

`utm_content` is always the asset ID from §6, so a session in GA4 and a row in a platform's
creative report can be joined by eye. Campaign is one value for twelve weeks —
`sidea` — because a campaign that changes its own name cannot be compared to itself.

### 10.3 Attribution, honestly

- **Last-click will over-credit brand search and the newsletter.** Read the funnel by *first*
  touch for the story and by *last* touch for the budget; they will disagree, and both are
  wrong in a known direction.
- **Platform reach does not deduplicate.** Three platforms reporting 700k reach each is not
  2.1M people; it may be 900k. Only sessions and signups are additive.
- **Server-side events are a project, not a toggle.** Meta's Conversions API and TikTok's
  Events API both exist and both need a deduplication rule against the browser event; the
  official Meta page truncated before its dedup rule on 2026-11-03, and the plan should not
  assert a key it has not read. **Do this in week 8, not week 1**, and read the rule first.
- **Consent is a build step.** Consent Mode v2 adds `ad_user_data` and `ad_personalization` to
  the older `ad_storage` and `analytics_storage`, with defaults set before any tag fires
  ([Consent mode](https://developers.google.com/tag-platform/security/guides/consent)). No
  pixel before consent in the EU/UK; no exception for "just the retargeting pixel".

### 10.4 The weekly review, which is the actual operating system

Thirty minutes, same hour every Monday, four questions, one decision recorded:

1. Which single asset over-performed, and **why** — first frame, length, sound, subject?
2. Which under-performed, and is it the hook or the distribution?
3. What did the funnel do at its weakest step (demo start → completion → tape → list)?
4. What ships this week, and what is cancelled to make room?

The output is one line in a creative ledger: `t07 hero v2 → best saves of the campaign; reshoot
the 03 label film in the same grade`. A campaign without a ledger repeats itself and calls it
learning.

---

## 11. Money

Three envelopes, for the case where money exists. **None of them is required to start** — §17
runs the same idea at €0, and the Lean tier is something the first evidence buys, not the first
euro. The differences are not "more of the same"; each one buys a different kind of certainty.

| | **Lean — €12k** | **Standard — €45k** | **Full — €120k** |
|---|---|---|---|
| Production (2 shoot days, macro, edit, captions) | €5,000 | €12,000 | €30,000 |
| Creators (fees + product + usage rights) | €3,000 (6 micro) | €10,000 (12–15) | €25,000 (25+, incl. 2 mid-tier) |
| Media | €2,000 | €18,000 | €55,000 |
| Tools, domain, email, licensed music, ops | €2,000 | €5,000 | €10,000 |
| What it buys | Proof that the object films work. | A real audience and a measurable funnel. | A launch, not a campaign. |
| What it does not buy | Reach. | A product to sell. | A product to sell. |

**The media plan is three flights and a pause, not a budget line.**

| Flight | Week | What it is | The decision it must earn |
|---|---|---|---|
| 0 | 6 | €1,500: boost the two organic winners only (TikTok Spark Ads, Meta Reels). | Does paid beat organic on cost per `demo_start`? If not, stop. |
| 1 | 7–8 | 50% of the media budget: retargeting (50% video viewers, site visitors, demo abandoners) + lookalike from the build list. | Does retargeting convert at 3× cold? If not, cut it and move to flight 2. |
| Pause | 9 | Seven days with no paid media at all. | The organic baseline the whole plan is measured against — the cheapest incrementality test available. |
| 2 | 10–12 | The rest: YouTube in-feed against the flagship, high-intent search. | Does search produce qualified builders (signup + guide) rather than clicks? |

**What the money will not be spent on.** Bought followers, "PR placements" sold by the
placement, growth agencies paid on reach, TikTok automation tools that post through an
unaudited API (they publish to a private audience — §8.1), or a retainer before there is a
product to retain against. Each of those spends the campaign's credibility, which is the one
budget that does not refill.

**What is not in these numbers, and must be.** Hardware for the shoot (a second complete unit,
because one unit cannot be filmed and shipped), legal review (§13), and the web build (§9) —
an engineering cost, not a marketing one, and the largest single item in the plan if it is
honestly priced.

---

## 12. The calendar, from four weeks out

| Week | Ships | Channels | Decision at the end |
|---|---|---|---|
| **−4** | Hub, demo, build list built; shoot bank planned; compliance check started | — | Is there a sayable domain? If not, stop and fix it. |
| **−3** | Two shoot days: all six Side A heroes + the build footage | — | Is the object photogenic in motion? If not, the campaign is a written one. |
| **−2** | Edit bank 01–06 + carousels; the analyser demo hardened on a phone | — | Does the sample demo complete on a 4-year-old phone in under 20 s? |
| **−1** | Hub live; press kit; build list open; community accounts warmed | — | Is `demo_start` firing from a cold visit? |
| **1** | t01 Boot, t02 Reels | TikTok, IG, Shorts | First-frame retention |
| **2** | t03 Label, t04 Needle + first long-form (t02, 4 min) | + YouTube | Which platform is cheapest per view? |
| **3** | t05 Knobs, t06 The room + carousel (anatomy) | all | Does the design audience engage, or only the builders? |
| **4** | Re-cuts of the two best; first creator posts | all | Creator cost per session |
| **5** | t07 Drop a folder (demo push) | all + web | `demo_start` rate from social vs paid |
| **6** | Re-cuts; **flight 0 media** | TikTok, Meta | Paid vs organic cost per `demo_start` — the stop/go rule |
| **7** | t08 The build (flagship) + Show HN + guide live | YouTube, HN, communities | Builders arriving? Guide downloads per day |
| **8** | t09 No cloud + server-side events shipped | all | Consent-safe measurement verified |
| **9** | t10 The assistant + collector-community posts + **7-day paid pause** | all | Organic baseline without paid — the only incrementality test in the plan |
| **10** | t11 One room; flight 2 opens | all | Does search produce qualified builders, or just clicks? |
| **11** | t12 Side B (the ask); flight 2 continues | all | Qualified builders per €1,000 |
| **12** | The tape closes: a single page of what was learned, public | web, newsletter | Go / no-go for a launch campaign |

**Roles, at the Standard tier.** One owner (strategy, copy, community, the ledger), one editor
two to three days a week, one web engineer for three weeks, one designer for carousels and
thumbnails, one support inbox with a 48-hour promise. That is four people part-time, and the
plan is sized to it deliberately: a campaign that needs six people to ship a week of content is
a campaign that will miss week 3.

---

## 13. The things that can end this campaign, and the gates that stop them

A campaign that drives desire for a device nobody may legally sell is a liability with a
content calendar. These are gates, not risks: each one is either cleared or the campaign does
not make the claim attached to it.

### 13.1 Hardware, if a unit is ever sold

| Gate | Why it applies | What clears it |
|---|---|---|
| **CE marking / Radio Equipment Directive 2014/53/EU** | It is radio equipment placed on the EU market; CE "can only be affixed after testing the product and performing the conformity assessment procedure prescribed by the applicable EU harmonisation legislation" ([European Commission, RED](https://single-market-economy.ec.europa.eu/sectors/electrical-and-electronic-engineering-industries-eei/radio-equipment-directive-red_en)). | A conformity assessment and a technical file, before a price is shown to an EU buyer. |
| **Cyber Resilience Act** | A connected product with firmware: the CRA "entered into force" on 10 December 2024 and its "main obligations… will apply as of 11 December 2027" ([same page](https://single-market-economy.ec.europa.eu/sectors/electrical-and-electronic-engineering-industries-eei/radio-equipment-directive-red_en)). | A support and update policy that is written down, not implied — and a plan for the 2027 date. |
| **FCC equipment authorization (US)** | Radio-frequency devices are authorised under [47 CFR Part 15](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15) before marketing. | Test lab + authorization; do not ship a US waitlist a promise before it. |
| **WEEE, RoHS, packaging EPR** | Electronics sold into the EU carry take-back, substance and packaging duties, some per market ([WEEE 2012/19/EU](https://eur-lex.europa.eu/eli/dir/2012/19/oj), [RoHS 2011/65/EU](https://eur-lex.europa.eu/eli/dir/2011/65/oj), [packaging 94/62/EC](https://eur-lex.europa.eu/eli/dir/1994/62/oj)). | Registration per market; a take-back page. |
| **Battery question** | The build uses a USB-powered devkit today; a kit that ships a cell changes the shipping class (UN 38.3) and the safety file. | Decide "no battery" and say so, or budget the paperwork. |
| **Trademark clearance for "K7"** | "K7" is an ordinary word in French, which weakens it there and collides elsewhere. | A clearance search (EUIPO, INPI, USPTO) before paid media; if it fails, the campaign can still run under a distinct brand with K7 as a model name. |

The CE/FCC line has its own brief — what applies, what the certified module buys, the security
model that decides it, and the costs — in [`compliance-2026-11-03.md`](./compliance-2026-11-03.md).

**Consequence for the plan.** Until CE and FCC are cleared, there is no "buy" button — only the
build list, the guide and the price signal (*"we are aiming at €X"*), and even the price signal
is a claim a lawyer should see.

### 13.2 Music, marks and claims

- **Music in commercial content.** TikTok is explicit: *"Businesses cannot use the general music
  library for commercial usage"* — including organic posts, duets, stitches, branded content and
  ads — and must use the Commercial Music Library
  ([TikTok](https://ads.tiktok.com/resources/help/article/commercial-music-library?lang=en)).
  **No official Meta equivalent was found on 2026-11-03**, so the campaign licenses its music
  for every paid placement and uses platform libraries only where the platform says it may.
- **Marks we do not touch.** *Walkman*, *Sony*, *TDK*, *Maxell*, *Spotify*, *Apple Music* — no
  comparative packaging, no "Spotify for your files", no logo in frame. Descriptive use of
  "cassette" is fine; a claim of compatibility is not, because none exists.
- **Claims that must be substantiated on demand.** "No subscription" (true — nothing is
  licensed), "no account" (true), "nothing leaves your machine" (true for the analyser; false
  if written about the device, which serves its library on the LAN to anyone who can reach it).
  The privacy page in §9 must say both, in that order, or the first journalist who reads the
  README will write the correction for us.
- **Branded content disclosure.** Any creator post that is paid carries the platform's own
  disclosure toggle, not a hashtag alone.

### 13.3 Data

- **EU/UK:** consent before any ad or analytics tag, Consent Mode v2 defaults set first
  (§10.3), a privacy policy that names the sub-processors, double opt-in on the build list, and
  a one-click unsubscribe that works.
- **US:** a "Do Not Sell or Share" path if CCPA/CPRA applies at the scale the campaign reaches,
  and no sale of the build list — ever, and say so on the page.
- **The DSA's ad-transparency duties are not verified here.** The Commission's text is the
  source ([Regulation (EU) 2022/2065](https://eur-lex.europa.eu/eli/reg/2022/2065/oj)); a
  lawyer confirms who bears what before the first paid flight into the EU. Do not put a
  compliance claim in the plan that a doc has not been read for.

---

## 14. What would make this plan wrong

Written down before the campaign starts, so that stopping is a decision and not a mood. The
stop rules for the zero-budget route are in §17.7.

| By | If | Then |
|---|---|---|
| Week 3 | Median views under 1,500 and saves under 1% of reach on the object films | The films are the problem, not the algorithm: reshoot with hands, a room and a person. Do not post more of the same. |
| Week 6 | Paid cost per `demo_start` is higher than organic | Cancel flights 1 and 2. Spend nothing more; the campaign is a community campaign now. |
| Week 8 | Demo completion under 25% of starts | Freeze media. The demo is the funnel's broken step, and no ad fixes a broken demo. |
| Week 9 | The collector communities reject the honest framing | Stop addressing them as a market. Keep the builder and design audiences; the product's culture story changes, not the product. |
| Week 10 | Fewer than 40 qualified builders | The "buy" ask is premature. Replace t12's ask with a build-kit ask and extend by eight weeks. |
| Any week | A legal gate in §13 is unresolved | No price, no pre-order, no "available". The campaign keeps making the object wanted and says nothing about buying it. |
| Week 12 | The public retro page embarrasses the owner | The campaign was a performance. End it and write the honest one. |

The plan is also wrong if it produces a beautiful campaign with no addresses. That is why
`build_list_submit` is the only conversion the document optimises, and why the first thing
built is not a video but a page with a field on it.

---

## 15. Sources, and what could not be verified

All external pages were read on **2026-11-03**; the two evidence files beside this document keep
the raw rows, including each page's own "last updated" date:
[`facts-2026-11-03.md`](./facts-2026-11-03.md) (platforms, web, policy) and
[`market-2026-11-03.md`](./market-2026-11-03.md) (industry data, prices, the word).

### 15.1 This repository

- Product and constraints: [`README.md`](../../../README.md) — the broadcast, the no-auth
  warning, the board status, the known limitations.
- The application: [`web/CONTEXT.md`](../../../web/CONTEXT.md) (areas, the card, the
  measurements, the assistant, the glossary) and [`web/README.md`](../../../web/README.md) (the
  analyser, the models, the tagged-file behaviour).
- Specifications: [`docs/specs/k7-app.md`](../../specs/k7-app.md),
  [`docs/specs/k7-deploy.md`](../../specs/k7-deploy.md),
  [`docs/specs/bridge-protocol.md`](../../specs/bridge-protocol.md),
  [`docs/specs/card-mark.md`](../../specs/card-mark.md).
- Decisions: [ADR 0005](../../adr/0005-cassette-reels-follow-tape-geometry.md),
  [0015](../../adr/0015-tape-edge-and-vu-fade-through-the-reel-change.md),
  [0023](../../adr/0023-shuffle-is-a-broadcast-mode.md),
  [0027](../../adr/0027-the-knobs-are-dragged.md),
  [0028](../../adr/0028-the-pad-drives-the-knob.md),
  [0039](../../adr/0039-the-cassette-is-a-card.md),
  [0040](../../adr/0040-the-cassettes-are-a-table.md),
  [0045](../../adr/0045-the-boot-mark-holds-until-the-card-is-scanned.md),
  [0053](../../adr/0053-the-assistant-writes-filters.md),
  [0062](../../adr/0062-xtape-becomes-k7.md).

### 15.2 Platforms

- Instagram: [Content Publishing](https://developers.facebook.com/docs/instagram-platform/content-publishing),
  [IG User Media](https://developers.facebook.com/docs/instagram-platform/instagram-graph-api/reference/ig-user/media).
- TikTok: [Content Posting — get started](https://developers.tiktok.com/doc/content-posting-api-get-started),
  [Media Transfer Guide](https://developers.tiktok.com/doc/content-posting-api-media-transfer-guide),
  [Commercial Music Library](https://ads.tiktok.com/resources/help/article/commercial-music-library?lang=en).
- YouTube: [Quota calculator](https://developers.google.com/youtube/v3/determine_quota_cost),
  [videos.insert](https://developers.google.com/youtube/v3/docs/videos/insert),
  [quota and compliance audits](https://developers.google.com/youtube/v3/guides/quota_and_compliance_audits),
  [resolution and aspect ratios](https://support.google.com/youtube/answer/6375112),
  [Shorts](https://support.google.com/youtube/answer/10059070).
- Web: [Web Vitals](https://web.dev/articles/vitals),
  [install criteria](https://web.dev/articles/install-criteria),
  [Search Essentials](https://developers.google.com/search/docs/essentials),
  [Product snippet](https://developers.google.com/search/docs/appearance/structured-data/product-snippet),
  [Consent mode](https://developers.google.com/tag-platform/security/guides/consent),
  [.dev registry](https://get.dev/).
- Communities: [Reddiquette](https://support.reddithelp.com/hc/en-us/articles/205926439-Reddiquette).

### 15.3 Law and standards

- [Radio Equipment Directive 2014/53/EU — European Commission](https://single-market-economy.ec.europa.eu/sectors/electrical-and-electronic-engineering-industries-eei/radio-equipment-directive-red_en)
  (CE marking, the Cyber Resilience Act's 11 December 2027 date).
- [Regulation (EU) 2022/2065 (DSA)](https://eur-lex.europa.eu/eli/reg/2022/2065/oj) — cited as
  the text to check, not as a read obligation.
- Product duties: [WEEE 2012/19/EU](https://eur-lex.europa.eu/eli/dir/2012/19/oj),
  [RoHS 2011/65/EU](https://eur-lex.europa.eu/eli/dir/2011/65/oj),
  [packaging 94/62/EC](https://eur-lex.europa.eu/eli/dir/1994/62/oj),
  [47 CFR Part 15](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15).
  EUR-Lex serves a challenge page to automated fetches; the ELI is the citation, and a lawyer
  reads the article text, not this document.

### 15.4 Market

- [IFPI Global Music Report 2026 release](https://www.ifpi.org/global-music-report-2026-global-recorded-music-revenues-grow-6-4-as-record-companies-drive-innovation/)
  and the [report PDF, figures by format](https://www.ifpi.org/wp-content/uploads/2024/03/GMR2026_SOTI2.pdf).
- [RIAA Year-End 2025](https://www.riaa.com/wp-content/uploads/2026/03/RIAA-Year-End-Revenue-2025.pdf).
- [Official Charts — best-selling cassettes 2025](https://www.officialcharts.com/chart-news/best-selling-cassettes-singles-albums-2025/);
  [Music Week — UK physical, H1 2026](https://www.musicweek.com/labels/read/physical-sales-growth-accelerates-in-first-half-of-2026-as-vinyl-closes-in-on-cd/094401).
- [Luminate, AIM London 2026](https://luminatedata.com/wp-content/uploads/2026/09/2026-AIM-London-Presentation.pdf);
  [IFPI Engaging with Music 2023](https://www.ifpi.org/wp-content/uploads/2023/12/IFPI-Engaging-With-Music-2023_full-report.pdf).
- [Wiktionnaire — K7](https://fr.wiktionary.org/wiki/K7);
  [ca.wikipedia — Casset](https://ca.wikipedia.org/wiki/Casset).

### 15.5 What this document could not verify

Listed so that nobody downstream assumes the silence is agreement:

- **Meta's music-in-ads rule.** No official Meta page stating a limited business music library
  was found. TikTok's equivalent is explicit (§13.2); Meta's is not, so the campaign licenses
  its music for every paid placement.
- **Meta Conversions API and TikTok Events API deduplication rules.** The pages exist but did not
  yield the rule on 2026-11-03. §10.3 says to read them before implementing, and does not guess.
- **Ad safe zones and the TikTok ad-spec table** — TikTok's article 404s. Design against the
  platform preview.
- **The DSA's advertiser duties.** The Article 26 transparency duties are understood to sit on
  platforms rather than advertisers, but the text was not read here.
- **Community sizes** (subreddits, hashtag volumes) — Reddit returned 403 and its mirrors 429/503;
  no number is invented.
- **We Are Rewind's price** is press-reported, not brand-verified: its site did not resolve.
- **"K7" as Catalan** is contradicted by the sources read, and the repo's ADR says otherwise;
  the ADR stands as the record, and the copy deck must not repeat the Catalan half.
- **Anything about demand for this specific object.** No source measures it. The campaign's
  first 90 days are the measurement.

---

## 16. The one-page version

For the reader who will not read eighteen sections:

1. **Do not launch a product campaign.** Nothing is for sale, the web app is not deployed, and
   the hardware gate (CE, FCC, WEEE) is open. The campaign's conversion is a build list.
2. **Make one idea, not four campaigns:** *K7 — SIDE A*, a twelve-track tape with two sides —
   the object (weeks 1–6) and the build (weeks 7–12).
3. **Put the analyser at the centre.** Drop a folder, watch it measured, make a tape, share the
   label. It is free, it is the product's proof, and it is the only loop.
4. **TikTok discovers, Instagram desires, YouTube proves, the web converts.** One shoot bank,
   four exports, one asset ID, one campaign tag.
5. **Say only what is true:** not a cassette player, not hi-fi, not multi-room, not secure —
   an object you own, a tape you made, a room that hears the same thing.
6. **Spend nothing until an organic asset earns it**, and stop on the written rules in §14.
7. **Measure one number:** qualified builders. Everything else is a diagnostic.
8. **If the budget is zero, §17 replaces §11 and §12:** eight weeks, ~85 hours, six films and one
   build video, no paid media, and a different set of targets.
9. **If there is time instead of money, §18 replaces the cadence:** 200 hours buy the loop, the
   guide and a loaner programme — not more posts — and the campaign is pointed at one revenue
   route by week 8.

### The first seven days, with a budget

The rest can wait; these cannot, because everything else is built on them. Without a budget, the
same week is compressed into days — §17.8.

| Day | Action | Owner |
|---|---|---|
| 1 | **Decide the origin.** A short, sayable domain that 301s to the app, or an explicit acceptance that the campaign launches on `k7.xperiments.dev`. | Owner |
| 1 | **Publish the claim rules.** §2.2 becomes the copy deck's first page; anyone writing a caption signs it. | Owner |
| 2 | **Deploy the analyser** behind the hub skeleton, with the sample demo working on a phone and `demo_start` firing. | Engineer |
| 3 | **Shoot a test, not the campaign.** One afternoon, one phone, a macro clip: t01 and t02. Watch them twice, muted, on a phone. If the object is not photogenic, the plan changes now, cheaply. | Editor |
| 4 | **Open the compliance track.** Get two quotes for CE and FCC testing, and start the K7 clearance search. These have lead times; starting them in week 8 is starting them late. | Owner |
| 5 | **Stand up measurement.** GA4, the five events, the UTM sheet, the consent banner. Nothing to report yet — just the plumbing. | Engineer |
| 6 | **Name the ledger**, and put the first line in it: the week-3 decision (§14) and the date it is due. | Owner |
| 7 | **Do not post.** | — |

**What not to do in the first week:** no ads, no press pitch, no "coming soon" teaser, no
recruiting creators, no posting to communities. Every one of those spends an asset the campaign
has not built yet — attention, before there is anything to look at.

---

## 17. If the budget is zero

Then the campaign is not smaller — it is **narrower**. €0 removes reach and production value. It
does not remove the three things the plan actually runs on: the demo, the honesty, and one film
that is good enough to be sent to a friend. Everything else in §7 to §12 is a way to spend money;
this section is the way to spend hours.

The instruction is therefore not "do the €45k plan with less". It is: **run the same idea at the
scale one person can sustain, and let the first evidence buy the rest.**

### 17.1 What changes

| The €45k plan | At €0 | Why |
|---|---|---|
| 12 tracks, two sides | **6 object films + 1 build video** | Six is what one person can shoot in an afternoon and finish in eight weeks. |
| TikTok + IG + YouTube + web, each with its own cuts | **TikTok first; the same file reposted to Reels and Shorts; YouTube for the build video only** | Re-posting costs minutes; producing for four platforms costs weeks. |
| Two shoot days, an editor 2–3 days a week | **One afternoon, one phone, CapCut (free), evenings** | The object is 800×480 and the films are macro. A phone is enough. |
| 12–15 creators | **Zero creators; three builders get the guide and a 30-minute call** | A creator fee is money; a builder's documentation is content you cannot buy and do not pay for. |
| Seven-page hub | **Three pages: `/`, `/build`, `/list` — plus the demo** | `/measure` and `/privacy` can be sections of `/build` until traffic justifies pages. |
| Carousels, press kit, listening-session documentary | **Cut** | Time-expensive, reach-cheap. The build video and the demo carry the campaign. |
| Paid media, flights, pause week | **Zero paid. Two earned pushes instead: Show HN (week 6) and the communities (week 4 and 6)** | At €0, the only amplifier is other people's audiences. |
| FR/DE/ES subtitle tracks | **English master + a French caption pass** | One lane done properly beats four done late. |

The one thing that does **not** change: the claims discipline of §2.2. A false "lossless" in a
free video is as expensive as in a paid one.

### 17.2 The stack, at €0

| Need | What to use | The catch |
|---|---|---|
| Hosting the app and the hub | A free static host (Vercel, Netlify or Cloudflare Pages — the app builds to static files and does its work in the tab) | The deployed app **deliberately has no `COOP`** ([ADR 0058](../../adr/0058-the-bridge-is-a-typed-rpc-between-two-windows.md), [`k7-deploy.md:76`](../../specs/k7-deploy.md#L76)), so there is no `SharedArrayBuffer` and ONNX runs **single-threaded** in production, while `npm run dev` sets COOP/COEP and uses threads. Time the demo on the deployed URL, never on the dev server. |
| The build list | A free form service, or the free tier of an email tool | Most free tiers cap subscribers; migrate when you cross it, not before. |
| Analytics | GA4 + the platforms' own dashboards | Set the five events (§10.1) before the first post, or the first fortnight is unmeasurable. |
| Editing | CapCut or DaVinci Resolve (free), the phone's own camera | Export 1080×1920, 30 fps, no watermark, no platform music. |
| Captions | The platform's auto-captions, corrected by hand | Auto-captions are wrong on "K7" — check every one, or the product's name is misspelled in its own campaign. |
| Music | **The device's own sounds.** No licensed track at all | This also sidesteps the TikTok commercial-library rule (§13.2) entirely. |
| Design | The assets already in `assets/labels/` and `assets/fonts/` | They were made for the screen; they work as the campaign's furniture. |
| Domain | Keep `k7.xperiments.dev` for now | A short domain is a few euros a year, not a campaign prerequisite — but do not print the subdomain on anything permanent. |

### 17.3 Eight weeks, in hours

About **85 hours**, which is 10–11 hours a week for two months. That is the real budget.

| Week | What ships | Hours |
|---|---|---|
| 1 | The app deployed; `/` with the sample demo; `/list` with the form; events firing | 12 |
| 2 | The claim rules page; `/build` written from the README and the board status; **shoot 12 attempts in one afternoon** | 12 |
| 3 | Keep 3, edit 2; publish the first film | 9 |
| 4 | Publish the second; answer 3 community threads with no links | 6 |
| 5 | Shoot and edit **the build video** (screen capture + hands + first boot) | 12 |
| 6 | Publish the build video; **Show HN**; the guide download | 8 |
| 7 | Re-cut the best film with what the comments taught; publish | 6 |
| 8 | The ask: `/list` as the only call to action; a public page of what was learned | 6 |
| *every week* | Community (2 h) + review (1 h) | 24 |
| | | **≈ 85** |

### 17.4 What €0 can realistically produce

| Metric | Range | Why a range, not a number |
|---|---|---|
| Views across all assets | **30,000–150,000** | One outlier decides the campaign's reach; no one can promise which film it is. |
| Sessions | **1,000–3,000** | Seven assets send far less traffic than thirty-six. |
| Build-list signups | **60–200** | The funnel rates of §8.2 hold; the volume behind them does not. |
| Qualified builders | **15–40** | The number to read. Fifty people who care beats fifty thousand who scrolled. |
| Durable assets | **1** (the build video) | It keeps answering "does it work?" for two years, for free. |

### 17.5 What the zero route bets on

1. **One film, not six average ones.** Shoot twelve attempts, keep three, publish the best. At €0,
   the only thing that can compete with a funded competitor is a single piece that is genuinely
   good.
2. **The build video is the campaign's real asset.** It is searchable, linkable, and it proves the
   product in a way no 15-second film can. It is also the one thing a funded competitor cannot
   copy quickly.
3. **Communities are the only amplifier left.** A paid plan buys reach; at €0 the reach is other
   people's rooms, and the fee is participation, not money (§8.6).
4. **The demo costs nothing to film.** A screen recording of a folder being measured *is* the
   product demo — no set, no actor, no music.

### 17.6 What is lost, and accepted

- **Reach: an order of magnitude.** No paid amplification means the Standard plan's numbers are
  fantasy here; §17.4 replaces them.
- **Production value: the phone shows.** That is survivable for macro shots and screen captures,
  and not survivable for the room shot (t06) — replace it with the object in your own room, and
  accept the difference.
- **Margin for error: none.** With no budget for a reshoot, the first shoot has to be
  over-supplied: film three times what you need.
- **Languages: one lane.** French waits, or is one caption pass.
- **The shareable label (§9.2): later.** It costs development time, not money — so it waits for
  the week after week 8, not for a budget.

### 17.7 Stop rules at €0

| When | If | Then |
|---|---|---|
| After 3 films | Under 500 views each, and under 1% saves | The object is not reading in your hands: change the concept (hands, room, a person), not the platform. |
| Before anything is published | The demo does not finish on a phone | Fix the demo. No campaign survives a broken demo. |
| 2 weeks after the build video | Under 2,000 views and no guide downloads | The builder audience is not there yet; go back to the object films and stop writing guides. |
| After 3 weeks of demo traffic | Zero signups | The page, not the traffic, is the problem: one field, one promise, no gate. |
| Any week | The hours are not there | Cut a film, not the community. The community is the compounding asset. |

### 17.8 The first seven days, at €0

| Day | Action | Hours |
|---|---|---|
| 1 | Write §2.2 into a one-page claim sheet. Decide to keep the subdomain for now. | 1.5 |
| 2 | Deploy the app to a free host; confirm it loads and analyses on a phone. | 4 |
| 3 | Ship `/` with the sample demo and `/list` with a free form; wire the five events. | 4 |
| 4 | Shoot twelve attempts with the phone. Do not edit anything. | 2 |
| 5 | Keep three. Edit one. | 3 |
| 6 | Publish it, with the link in the bio and a pinned comment. Nothing else. | 1 |
| 7 | Answer three community threads. No links, no product mention. | 1 |

The rule that survives compression: **do not post before there is a page to send people to.**
At €0 the sequence is days, not weeks — the order of operations is what matters, not the clock.

---

## 18. When time is the resource: the 200-hour version

Several screens and full-time availability change the constraint again. Hours are no longer the
scarce thing — cash is, and so is attention. The mistake this section exists to prevent is
converting free time into **more content**. The marginal value of a thirtieth post is close to
zero; the marginal value of the loop, the guide and a working demo is the whole campaign.

**Time buys options, not reach.** What it should buy, in this order:

| Priority | What | Hours | Why it is first |
|---|---|---|---|
| 1 | The loop: deployed demo, the label generator, `/`, `/build`, `/list`, the five events | 60 | It is the only asset that compounds without an audience. |
| 2 | The build video, the guide, four technical explainers | 40 | The durable, searchable proof — the one a funded competitor cannot copy fast. |
| 3 | **Eight** films, with one reshoot pass | 30 | Better, not more. A reshoot is the cheapest quality upgrade that exists. |
| 4 | Community, Show HN, press emails | 30 | The only amplifier left at €0. |
| 5 | Measurement, copy, two compliance quotes | 20 | The quotes have lead times; week 8 is late. |
| 6 | Buffer | 20 | Something always eats a week. |
| | **Total** | **200** | 25 h/week for eight weeks. |

### 18.1 Several screens = the creator budget you do not have

The thing money would have bought is not ads — it is **third-party proof**. Hardware does that
better than a fee: a builder who documents the thing is worth more than a creator who is paid to
hold it, because the audience can tell the difference in four seconds.

- **Loan two units**, not give them: one to a cassette/vinyl account, one to an embedded/ESP32
  account, one to a design/desk account if a third unit exists. Written terms: loan, return or buy
  at cost, credit, raw-footage rights to the brand, **no script approval** (the credibility is in
  the absence of control).
- **Do the maths first.** Each unit is parts plus shipping plus the risk of a return; a loaner
  programme with three units is a real cost, and it is the only marketing line this budget has.
- **The output is not a review.** It is a photo set, a build note, a forum post and a quote you
  can put on `/build` for two years.

### 18.2 The shot you cannot take yet

The most desirable image this product has is a shelf of K7s, each showing a different tape. Today
that image is impossible on one network: the mDNS hostname is fixed and two devices collide
([`README.md:129`](../../../README.md#L129)). Two honest options:

1. **Film one unit at a time**, and never imply a fleet. This is the default.
2. **Make the hostname per-device** — a firmware change that unlocks the shelf shot *and* the
   multi-room story the campaign must not claim until it is true (§2.2).

Do not composite a shelf. This audience finds the reflection in the glass.

### 18.3 Three routes to money, and what each one needs

Being without income is not a detail to design around later; it decides what the campaign is
*for*. The campaign can serve any of these, but not all three at once.

| Route | What it proves | What it needs | The risk |
|---|---|---|---|
| **Reserve list** (no payment) | That people want it, in numbers | Nothing but the page and the traffic | None — and it converts no cash. |
| **Pre-order with a deposit** | That people will pay | Entity, terms, refund policy, **and CE/FCC cleared** (§13.1) | Taking money before compliance is the one mistake that ends a small hardware project. |
| **A small kit run** | That it can be assembled and shipped | A legal read on selling modules + panel as components rather than a finished apparatus; shipping and support | Thin margin, thick support load. |
| **A grant** | That the work is worth funding before it sells | A proposal, a budget, an eligible applicant (some funds take individuals), and months of patience | Slow; and the campaign is the evidence, so the campaign still has to exist first. The one fund that fits, verified on 2026-11-03, is [written up here](./funding-2026-11-03.md). |

The sequencing that makes sense at €0 with time: **reserve list now → compliance quote in hand by
week 8 → choose between the deposit and the grant with real numbers.** A refundable deposit is a
promise; a grant application is a plan; both are stronger with 60–120 qualified builders behind
them, and neither is available in week 1.

### 18.4 The stop rule for unpaid full-time work

Set the date before starting — week 8 — and check two things, not one:

1. **At least 40 qualified builders**, and
2. **One revenue route with a written next step**: a grant submitted, a compliance quote in hand,
   or a costed pre-order page.

If both are true, the campaign earned its continuation. If neither is, the campaign did not fail —
the product or the market did not answer — and the right move is to go back to building, not to
make more content. Unpaid full-time work with no checkpoint is how a good project becomes a
resentment.

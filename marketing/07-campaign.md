# Phase 7 — Campaign

**Read with:** [`00-decisions.md`](./00-decisions.md) (shell sold alone · seller is the owner · four
printers), [`05-website.md`](./05-website.md) (the page), [`06-ctas-and-what-to-sell.md`](./06-ctas-and-what-to-sell.md)
(the ask).

**The honest shape of this campaign:** it is not a twelve-week brand campaign. It is **one launch
with one asset, aimed at one room**, followed by a quiet, permanent build log. A solo maker with four
printers does not have a media plan; they have a Saturday, a camera and a link.

---

## 1. The plan in one paragraph

Spend three weeks making the thing **legible and photographable** — publish the STL and the BOM, fix
the README's drift, print and shoot the shell, film ten seconds of it working. Then spend one week
putting it in front of the only three places that can sell a printed cassette to builders: Hacker
News, three subreddits with three different arguments, and the 3D-printing sites where people
already look for objects to print. Then spend eight weeks answering the people who arrive, shipping
shells, and publishing what you learned — because for this product the build log *is* the marketing,
and it compounds in a way that a launch spike does not.

The prior plan's twelve-week, two-side, twelve-track campaign (`docs/research/marketing/campaign.md:351-375`)
is a good campaign for a funded team with a shoot budget. At four printers and one person it is a
plan to miss week three. This one is sized to the operator.

---

## 2. Phases, with hours

### Pre-launch — 3 weeks, ~12 h/week

| Week | Do | Why it is this week | Hours |
|---|---|---|---|
| **−3** | Publish the STL (with a licence). Write the BOM with real part numbers and links. | both are needed by the shell buyer *and* the builder, and both are free to publish | 10 |
| **−3** | Fix the README drift (Phase 1 §2.6): strip driver, hot card, setup page, Snapshot v5, port 80. | the README is the product's front door and it currently contradicts itself in eight places | 4 |
| **−2** | Print the final shell. Photograph it: six shots (§5). | nobody buys a printed object they have not seen. This is the highest-value hour in the plan. | 12 |
| **−2** | Film the ten-second loop and a 90-second cut (§5). | the one asset the whole launch points at | 10 |
| **−1** | Kit-list page live; shell order flow live; terms, returns and privacy written; packaging with the Triman mark. | the ask must work before the traffic arrives | 10 |
| **−1** | Print a small buffer stock; measure one shell's print time and failure rate; compute capacity (D7). | so the first order does not take three weeks | 4 |

**Nothing is announced until the shell can be bought and shipped.** A launch that lands on a page
with no price is a launch spent.

### Launch — 1 week, ~20 h

| Day | Do |
|---|---|
| **Mon** | The STL goes up on the print-sharing sites with a good photograph and a link to the shell. |
| **Tue** | **Show HN** — the one shot. Title and body in §4. |
| **Wed** | r/esp32 (the build), r/3Dprinting (the shell, print settings), r/cassetteculture (the confession — the riskiest and the most valuable). Three different posts, never the same text. |
| **Thu** | Hackaday tip line. One email, three sentences, the video link. |
| **Fri** | Answer everything. Ship the first shells. |
| **Weekend** | Post the build log: what broke, with numbers. |

### Post-launch — 8 weeks, ~5 h/week

- Ship shells. Every parcel is a potential photograph — ask for one, in the confirmation email.
- One post a week from the pillar list (§4), not more. The marginal value of a fourth post in a week
  is close to zero; the marginal value of a fourth *month* is not.
- Answer the repo. An issue answered well is a public record of how you work.
- Collect: build photos, a first testimonial, a second person who built one.
- Decide the kit question with real numbers, not before (D4, Phase 6 §4).

---

## 3. Channels

### The four that are chosen

**1. Hacker News — one Show HN, once.**
The audience is exactly the builder (Phase 3 §1A), the product is unusual enough to be interesting
(it encodes MP3 on an ESP32 and refuses to serve a file), and the project's honesty is the thing HN
rewards. It is also a single shot: you get one, and a bad title wastes it.
*Cost:* an evening. *Ceiling:* whatever the front page gives — and it decays in 48 hours.

**2. Reddit — three communities, three arguments.**
Not one post cross-posted. r/esp32 gets the firmware and the encoder bug. r/3Dprinting gets the
shell, the print settings and the fit. r/cassetteculture gets the confession and the wind. Each is a
different reader with a different reason to care, and cross-posting the same text is how a launch
gets removed by three moderators at once.

**3. The print-sharing sites (Printables, MakerWorld, Thingiverse).**
**This is the sleeper channel and possibly the best one.** A 3D-printed product belongs on the sites
where people already browse for objects to print. The STL is free, which is the discovery; the sold
shell is the convenience for the majority who own a printer and would rather not run a 14-hour
print, or who do not own one at all. It also puts the object in front of exactly the audience with a
printer, at zero cost, permanently.

**4. One video, hosted on YouTube, embedded nowhere else.**
Not a channel in the media sense — an asset that outlives the launch and can be linked from the
repo, the site, HN and the print pages. One video, two cuts (10 s and 90 s). A build video later,
only if the first one earns it.

### The five that are rejected, and why

| Channel | Why not |
|---|---|
| **Paid media (Meta/TikTok/YouTube ads)** | There is no product to scale and no budget. Ads on a page with one product and no reviews buy traffic that bounces. Revisit after 50 shells. |
| **TikTok / Instagram as a primary channel** | They sell *desire* for an object; the shell buyer is a builder who found you through a repo or a print page. Post the films there — as reposts, with no cadence promise. |
| **Crowdfunding** | Needs an entity, a reward structure, a fulfilment plan and a certification story. It converts a small, controllable launch into a public obligation with a date. |
| **PR / "placements"** | Nothing to place yet, and the audiences that matter do not read them. |
| **Discord / community** | An empty room is worse than no room. Open one after 20 owners exist, not before. |

### What "not all of them" means in practice

Four channels, one launch week, and then **one channel forever**: the build log on the repo and the
site. Everything else is an amplifier for a post that already exists.

---

## 4. Content pillars, and ten pieces that already exist in the repo

### The pillars

| Pillar | The promise | Who it is for |
|---|---|---|
| **The object** | it exists, it is printed, it is worth looking at | C, D |
| **The build** | every decision is written down, including the failures | A |
| **The constraint** | it has edges on purpose — no seek, one room, one list | B, D |
| **The library** | your files, measured and respected | B |

### Ten pieces, with their sources and effort

| # | Piece | Pillar | Source in the repo | Effort |
|---|---|---|---|---|
| 1 | **"Why there is no seek bar"** — the broadcast is the product | constraint | `README.md:1272-1273`; `docs/adr/0023` | 2 h |
| 2 | **"The bug in the MP3 encoder that no decoder could read"** — the padding bit, measured per format | build | `README.md:1022-1041`; `mi_encode.c:58-78,122-151` | 4 h |
| 3 | **"41.4 % of a core"** — how a 44.1 kHz encoder fits on an ESP32-S3, with the table | build | `README.md:333-386,1043-1052` | 3 h |
| 4 | **The wind** — 8 seconds of tape kinematics, silent | object | `cassette.h:38-44,65-84`; film it | 1 h |
| 5 | **The label** — two fingers, the cover becomes a cassette label | object | `docs/specs/label-templates.md`; `docs/design/label-templates/` | 2 h + shoot |
| 6 | **"Cards in the world"** — renaming a product without stranding the cards | build | `docs/adr/0062-xtape-becomes-k7.md` | 3 h |
| 7 | **"What it doesn't do"** — the refusals page, as a post | constraint | Phase 4 §5, §7 | 2 h |
| 8 | **The BOM** — exactly what to buy, with links | build | to be written (Phase 3 §1A) | 4 h |
| 9 | **Five faults only the board could show** — bring-up order, the 20-reset loop, the IRAM mistake | build | `README.md:813-851` | 3 h |
| 10 | **"One tape per LAN"** — the fixed hostname as a design decision, not a bug | constraint | `README.md:1264-1265`; ADR 0062 | 2 h |

Two more that are nearly free: the **analyser demo** ("drop a folder, it measures it") and the
**shell print settings** (layer height, material, tolerances, time) — the latter is what the
3D-printing audience actually wants and it takes one afternoon to write.

### The launch post (Show HN)

Subject, in the product's voice:

> **Show HN: K7 – a 3D-printed cassette that streams your music library over Wi-Fi**

Body, in three short paragraphs — the first says what it does, the second says what is unusual, the
third says what is missing. The third is the one that makes it work:

> It's an ESP32-S3 in a printed shell with a 5" touchscreen that draws a cassette. It reads a card,
> re-encodes whatever is on it to a 44.1 kHz MP3 stream, and broadcasts it to anything on the LAN.
> The screen's reels turn at the speed the tape would.
>
> The part I'm least sure anyone else wanted: it has no seek bar, no account, and no recommendations,
> because I wanted the constraints of a tape back. Firmware is MIT, the app is free, the shell is on
> Printables.
>
> What it doesn't do: it has no DAC, no amplifier and no speaker — the sound comes out of something
> else. It's not lossless, it can't pause or seek, and there's no authentication, so don't put it on
> the internet. The BOM, the STL and 96 decision records are in the repo. I sell printed shells for
> people who don't want to run the print.

INFERENCE: the third paragraph is not a risk to the launch, it is the launch. The audience this
product needs rewards the list of failures more than the list of features, and the failure list is
the only thing on the page a competitor cannot copy.

---

## 5. Launch assets checklist

### Photography — six shots, one afternoon

1. **Hero:** the shell on a plain surface, screen lit, three-quarter view, window light.
2. **In hand:** held, with the card going in. This is the shot that says "object".
3. **Detail:** the screen close, the reels, the orange on near-black.
4. **Print:** the shell open, the module inside, the parts laid out — the builder's shot.
5. **The room:** the shell on a shelf with a speaker in frame, both in focus, nothing precious.
6. **The label:** the label print on the panel, held with two fingers, cropped tight.

Rules: no stock cassette, no film grain, no prop styling, no logo on the wall. The palette is
near-black and orange; let the object be the only brand in frame.

### Video

- **10 seconds** — card in → reels turn → sound from across the room. No titles, no music, no
  voiceover. The product's own sound is the point.
- **90 seconds** — the same shot, then the four rungs, the wind, the label, the app measuring a
  folder, the BOM on screen. One voiceover, or captions only.
- **Build video** — only if the 90-second cut earns it.

### Press kit — one zip

Three photos, the ten board captures, a 120-word description, one paragraph on what it is not, the
licence facts (MIT firmware, AGPL app, LGPL encoder), the links (repo, site, prints), a contact, and
a line saying there are no review units yet.

### Repo and site

- README drift fixed (§2).
- OG image and `twitter:card` (Phase 5 §6).
- A **"built one?"** section, so the first builder has somewhere to send a photo.
- Shell product page with price, print time, materials, what is in the parcel and what is not.
- Terms, returns, privacy — three short pages, before the first sale.

### Packaging

A box that survives a postal system, the Triman mark, the parts list inside, and one card that says
*"if you build it, send me a photo"*.

---

## 6. Metrics, and the rules that change the course

**Nothing here is a forecast.** The numbers are *decision thresholds* proposed in advance, so that
stopping or changing is a rule and not a mood.

### Pre-launch

| Measure | What it tells you |
|---|---|
| STL published, BOM published, page live, order flow tested | readiness, binary |
| One shell printed and timed; failure rate known | capacity (D7) |
| Six photos and two cuts exist | the launch can happen |

### Launch week

| Measure | Proposed threshold | If it fails |
|---|---|---|
| HN front page in the first 2 hours | top 30 | the *title* is wrong, not the product — rewrite and try once more in a month, never twice in a week |
| Referrers from Reddit | any post above 50 upvotes | retitle with a concrete claim, repost to a different sub |
| **List signups** | 100 in the first week | under 30 → the page is not converting; check the hero photograph first |
| **Shell orders** | 5 in the first two weeks | 0 with >500 visits → price or photograph; 0 with <100 visits → distribution, not product |
| Repo stars | 50 | not a goal; a proxy for "people bookmarked it" |
| Questions that are *objections* rather than *bugs* | few | many objections means the refusals section is not being read — move it up |

### Post-launch — 8 weeks

| Measure | Proposed threshold | If it fails |
|---|---|---|
| List → order conversion | 5 % | under 2 % → the price is wrong, or the shell does not look like the photo |
| Shells shipped per week | whatever capacity allows | backorders > 3 weeks → stop taking orders, say so |
| Build photos received | 3 | none → the builders are not building; ask the list why |
| Issue quality | real bugs, reproducible | only feature requests → the audience is wrong |
| Second builder writes something public | 1 | none → the community story is not working; lead with the object instead |

### The two rules that would make me change the plan

1. **If the shell sells and the kit does not get asked for, do not build the kit.** Sell prints,
   publish firmware, keep the app free, and let the kit be a thing people assemble themselves. That
   is a smaller business and a more honest one.
2. **If the object films outperform the build posts by an order of magnitude, stop writing and shoot
   more.** The campaign should follow the evidence, not the pillars.

---

## 7. What I would cut from the prior plan

The prior campaign is 1,230 lines and it is good; this is what I would not do, at this size:

- **The twelve-week two-side structure.** It is a format for a funded team. Four weeks to a sale is
  the format for one person.
- **The three-tier media envelopes (€12k/€45k/€120k).** Not wrong, just not now.
- **The creator programme.** It needs units to send, and there are none.
- **The second and third languages at launch.** English and French, as Phase 5 says; the rest waits
  for demand.
- **The weekly cadence promise.** One post a week for eight weeks is the most a solo maker can
  sustain *and* ship shells. A missed week in a promised cadence costs more than a quiet week.

<!-- Phase 8 follows in 08-gaps-and-questions.md. -->

# Phase 8 — Gaps and questions

**Read with:** [`00-decisions.md`](./00-decisions.md). This is the last file and the shortest. It
assumes you have read Phase 4 (claims) and Phase 6 (what to sell).

---

## 1. The one gap that matters

Everything else on this list is downstream of one sentence:

> **There is no photograph of the object, and the object is what you are selling.**

The firmware works, the site is live, the app measures music in a browser, and none of it can be
sold until a printed cassette exists in a photograph. Not because marketing is shallow, but because
the shell is the only thing in the whole build that is yours — the modules are off-the-shelf
(`00-decisions.md` D2), the firmware is MIT, the app is free — and a buyer cannot want a printed part
they have never seen.

The second gap is its twin: **the STL is not in the repository.** It is the community's licence to
build, the shell buyer's reason to trust that the print is the print, and the thing that turns the
3D-printing sites into a distribution channel. It exists on your side of the screen and not on the
audience's.

Close those two and the campaign has a centre. Leave them and every other asset is decoration.

---

## 2. Prioritised gaps

**P0 — the launch cannot happen without it** · **P1 — the campaign is not credible without it** ·
**P2 — it makes the difference between good and excellent**

### Product

| Priority | Gap | Why it blocks | Effort | Owner |
|---|---|---|---|---|
| **P0** | **The STL** is not in the repo, not published, and has no licence | it is the shell's product, the community's build and the print sites' content | 2 h + a decision on licence | you |
| **P0** | **The BOM** with real part numbers and links | a builder cannot price the build, and you cannot price the kit | 4 h | you |
| **P0** | **The order flow**: invoice, payment, shipping, terms, returns, privacy | money cannot change hands without it | 1 day | you |
| **P1** | **The bridge** — the sink that makes sound | the 10-second aha, and the word "system"; without it the product is a source with no output | build from off-the-shelf parts | you |
| **P1** | **OTA** | not needed for the shell; needed before the second batch of kits, and named as a blocker for selling to strangers (`docs/research/marketing/opportunities.md:447-448`) | weeks | you |
| **P1** | **Touch proven with a finger**, recorded | "the board tier has not yet recorded a finger" (`README.md:732-733`); the whole UI is touch | an hour | you |
| **P2** | Second unit, so one can be filmed while one ships | a campaign with one unit cannot shoot and ship at once | — | you |

### Proof

| Priority | Gap | Why | Effort |
|---|---|---|---|
| **P0** | **Photograph of the printed shell** (six shots, Phase 7 §5) | §1 | one afternoon |
| **P0** | **The README's own drift fixed** — eight places where the prose contradicts the tree (Phase 1 §2.6) | the project's honesty is its brand; a README that says the strip driver does not exist while it runs is the one thing an critic will quote | half a day |
| **P1** | **The 10-second loop** | the campaign's only real asset | one day, needs the bridge |
| **P1** | **The label print, photographed** | the most surprising feature, never photographed | one hour |
| **P1** | **A second builder** | the only proof that is not your own voice | months — start asking at the first shipment |
| **P2** | A measuring pass on a real 1,000-track library, with timings | the library owner's conversion asset | one evening |

### Assets

| Priority | Gap | Why | Effort |
|---|---|---|---|
| **P0** | Kit-list page and the shell product page | the ask must work before traffic | included in the site work |
| **P0** | **Packaging**: box, insert, Triman/Info-tri mark | a legal requirement in France, and the first thing a customer photographs | a day |
| **P1** | OG/social image; `og:`/`twitter:` tags (absent, `web/index.html:1-18`) | every share is currently a bare link | 2 h |
| **P1** | Press kit in one zip | needed the moment anyone writes about it | 3 h |
| **P2** | Pre-rendered marketing page (today: 1.4 KB of HTML and a bundle) | SEO and first paint | 1–2 days |
| **P2** | Captions and a transcript for the video | accessibility, and it is nearly free | 1 h |

### Legal

| Priority | Gap | Why | Effort |
|---|---|---|---|
| **P0** | **Seller registration** (micro-entreprise or equivalent) and a VAT position | cannot invoice or take payment without it | days, plus waiting |
| **P0** | **Packaging EPR registration** and the Triman marking | required to ship in France, even for plastic only | days |
| **P0** | **Terms, returns and privacy** pages | three short pages; the cheapest legal spend in the plan | a day |
| **P1** | **Written answer on CE/RED scope** for the *kit*, and on the shell's framing | the entire "sell a kit" plan turns on it (`compliance-2026-11-03.md:113-120`) | one email + one call |
| **P1** | **MTG email** about the CC BY-NC weights | it makes the app question disappear, or forces a product decision | one email |
| **P1** | **Trademark watch on "K7"** (EUIPO/INPI) | before any paid media; the prior plan already says so (`campaign.md:833`) | a search, then a lawyer |
| **P2** | Jura and the icon font added to `THIRD_PARTY_NOTICES`; the root `package.json` ISC line corrected | three contradictory licence statements in a public repo | an hour |

---

## 3. The next seven days

Not a plan; a list you can do while the printers are running.

1. **Print one shell and time it.** Note grams, hours, failures. That single measurement unlocks
   capacity and price (`00-decisions.md` D7).
2. **Photograph that shell** — the six shots, window light, no props.
3. **Publish the STL** with a licence, and put it on one print-sharing site.
4. **Write the BOM** as a table with links.
5. **Fix the eight README drifts** (Phase 1 §2.6).
6. **Send two emails**: MTG (weights) and a lab or a lawyer (CE scope for a kit; and whether
   "printed shell + guide" is the framing you think it is).
7. **Put the kit-list form on the site** — before the shell page, because it costs an hour and it
   starts collecting today.

---

## 4. The ten questions, ordered by impact

Each one is here because the answer changes a decision, not because it would be nice to know.

**1. Will you publish the STL — and under what licence?**
It is the product, the community and the content. MIT or CC BY-SA for the printed parts; the design
stays yours either way, because the money is in the print and the guide, not the file.

**2. What does one shell cost to print — grams, hours, failure rate — and what will you charge?**
Everything in Phase 6 §4 collapses into this. Four printers give you capacity; only this number gives
you a price.

**3. Is the bridge being built now, and when will it make a sound?**
The 10-second aha, the word "system", and the difference between "a cassette that streams" and "a
cassette that plays". If it is months away, the launch leads with the object and the build, not with
the room.

**4. Who takes the photograph, and when?**
If it is you: this week, before anything else in §3. If it is someone else: book it now, because
every asset in the campaign waits on it.

**5. When do you register as a seller?**
Until you can invoice, the answer to every "can I buy one?" is no. It is also the gate on the
packaging EPR registration, which is the gate on shipping.

**6. How many hours a week do you actually have for the next six weeks?**
Phase 7 is sized at ~12 h/week for three weeks and ~20 h for launch week. If the real number is 5,
cut the launch to HN + one subreddit and skip the video, rather than missing every date.

**7. Will you write in public about the failures?**
The build log is the campaign's compounding asset, and it only works if it includes the 20-reset
loop, the encoder at 128 % of a core, and the eight README drifts. If the answer is no, the campaign
becomes a product launch, and it will be a smaller one.

**8. Domestic first, or EU from day one?**
It sets the packaging registrations, the shipping cost and the returns policy. Domestic-only is a
legitimate first batch; saying "worldwide" and then discovering per-country EPR is not.

**9. Is the name settled — and is `k7.xperiments.dev` the address you want on a business card?**
The prior plan flagged this in week one and it is still open (`campaign.md:296-306`). A sayable
origin is the cheapest brand decision available, and it must be made before any paid media or print.

**10. Do you want to be the manufacturer?**
Not the seller — the *manufacturer*, with the CE file, the technical documentation and the 2027
Cyber Resilience Act obligations that come with a connected product. If yes, the kit is a business.
If no, the shell-and-firmware model is the honest ceiling, and it is a good one.

---

## 5. If you read only one thing

The product is stronger than its marketing, and its marketing is stronger than its object. That is an
unusual order and it is fixable in a fortnight:

- **The firmware is genuinely unusual** — an MP3 encoder on an ESP32-S3 at 256 kbps in half a core,
  a reel model driven by tape geometry, a broadcast that cannot be paused because it is radio. None
  of that needs inventing; it needs photographing, filming and saying plainly.
- **The honesty is the differentiator, not a constraint on it.** A product that publishes what it
  does not do, and a README that lists its own failures, is the only kind of thing this audience
  still trusts. The refusals table is the best asset in the repository.
- **The object is one photograph and one STL away.** That is the whole distance between "impressive
  repo" and "I would like one of those".

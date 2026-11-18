# Phase 6 — Calls to action, and what to sell first

**Read with:** [`04-claims.md`](./04-claims.md) §5 (what may not be said), [`00-decisions.md`](./00-decisions.md)
(kit, printed shell, off-the-shelf).

**Constraint that governs this whole phase:** nothing may say *buy* until the certification question
is answered (Phase 4 row 43; `docs/research/marketing/compliance-2026-11-03.md:119-120`). So the
interesting question is not "how do we sell it" but **"what can be sold lawfully, this month, that is
still worth money"** — and there is a better answer than a waitlist.

---

## 1. The CTAs, with exact wording

### Primary — the one thing the page asks for

| Option | Wording | Notes |
|---|---|---|
| **A** | **Join the kit list** | Recommended. Names the thing they are waiting for; "kit" is the product; no commitment implied. |
| B | Get the build notes | Wider, but attracts people who will never buy, and commits you to a newsletter you may not want to write. |
| C | Tell me when there is a kit | Honest, warmer, longer; good as the button's *helper* line under A. |
| D | Reserve a kit | **Do not use.** It implies an allocation and an obligation you cannot yet honour. |

**Recommended pair:** button **Join the kit list**, helper line under it *"One email when there is a
kit. Nothing else."*

### Secondary — the proof, and the thing that already exists

| Option | Wording | Where |
|---|---|---|
| **A** | **Flash a board** | Hero, second position. Keep — it is the current site's best line and it is literally what happens. |
| B | Put it on a board | Warmer, less precise. |
| C | Build one now | Puts the burden first; better after the BOM exists. |

**Recommended:** keep **Flash a board**. Under it, one line: *"You need a board, a data cable and
Chrome. About a minute."* — that is true (34.0 s for the three parts, `README.md:536`) and it
removes the fear.

### Tertiary

| Option | Wording | Where |
|---|---|---|
| **A** | **Open K7** | Header and footer, for owners. Keep as is (`Site.tsx:32`). |
| B | Try the analyser | From the "measured music" section → `/app#/prepare`. |

### The refusal CTAs — the ones you will be tempted to add

| Do not use | Why |
|---|---|
| **Buy now** / **Preorder** | No entity, no terms, no refund policy, no certification. |
| **Notify me when available** | "Available" is a claim about a product that does not legally exist yet. |
| **Get the kit — €X** | A price is a commercial claim; see §4. |
| **Join the community** | There is no community yet — a Discord with three people is worse than none. |

---

## 2. CTA by funnel stage

| Stage | What they know | The CTA | Where | What it must prove |
|---|---|---|---|---|
| **Discover** | nothing; they saw a 9-second video | *Watch it work* (→ the 90 s film) · *See the build* | social bios, video descriptions, the post itself | that it exists and moves |
| **Evaluate** | it exists; is it for me? | *Join the kit list* · *Flash a board* · *Try the analyser* | the site, HN post, build log | that it is real, documented and honest |
| **Commit** | they want one | *Join the kit list* → (later) *Order the shell* → (later) *Order the kit* | the kit section, the confirmation email | that the price is fair and the thing will arrive |
| **Advocate** | they have one | *Publish your build* · *Send your photos* · *File an issue* | the repo, the confirmation email, a "built one?" page | that contributing is welcome and credited |

INFERENCE: the advocate stage is the one the project has never designed, and it is the one that
matters most for a kit business — every builder who documents their build is a photograph, a
testimonial and a search result you did not pay for. The cheapest CTA in the whole plan is **"send
us a photo of yours"**, and it should exist before the first kit ships.

---

## 3. What to sell first: seven options, compared

| # | Option | What it is | Pros | Cons | Risk | Verdict |
|---|---|---|---|---|---|---|
| 1 | **Waitlist / build list** | an email | free, lawful, immediate, measures demand | no revenue, and a list that never converts is a slow embarrassment | none | **do it now** — but not *instead of* §4 |
| 2 | **Preorder / deposit** | money before goods | validates willingness to pay, funds a batch | consumer-law obligations, refunds, a promise with a date | high — the repo's own brief says no | **not yet** |
| 3 | **The kit** | modules + printed shell + guide | the stated plan; the real product; a price that reflects assembly | CE/EMC, RoHS, WEEE, EPR, packaging, support, returns | medium-high, and it caps the first batch at whatever you can assemble | **the goal, in 2–3 months** |
| 4 | **The assembled unit** | turnkey | the design buyer's only real option; highest price | more certification, a test jig, warranty, a second pair of hands | high | **after the kit** |
| 5 | **Files / firmware** | free | the community's licence to build; the trust engine; zero obligation | no revenue | none | **already done** — keep it free |
| 6 | **Custom install / integration** | your time | highest hourly rate; uses what exists | public-performance licensing in venues; a device with no auth on someone else's LAN | medium | **no** — until there is an auth story |
| 7 | **The printed shell, sold alone** | plastic and a guide, no electronics | **lawful today**; the only part that is *yours*; ships worldwide; no EEE, no radio, no CE for the electrical apparatus; converts a follower into a builder holding your object | low ticket; needs a good STL and a printer; shipping time | low | **sell this first** |

### Recommendation: sell the shell, publish the BOM, keep the firmware free

**This month:** waitlist + **the printed shell**, sold as *"the shell and the guide"* — no
electronics in the parcel. The buyer sources the modules from the published BOM. That is not a kit
in the legal sense (no radio equipment is placed on the market), it is a printed part and a document,
and it is the only revenue that carries no certification question.

**Next:** a **kit**, once three things exist — the BOM, the guide, and a written answer on CE — and
once the shell has sold enough to prove people will pay for the object.

**Later:** an **assembled unit**, at a price that pays for assembly, because that is the only thing
the design buyer will ever buy.

Why this order, in one sentence: **the shell monetises the one asset that is genuinely yours, the
BOM builds the community that will debug the kit, and the kit arrives only when the paperwork can
follow it.**

Two caveats, both real:

- **INFERENCE, needs a lawyer:** that selling a printed enclosure plus a document is not "placing
  radio equipment on the market". It is the standard open-hardware posture and it is much safer than
  selling a bag of modules — but "much safer" is not "settled", and the sentence on the product page
  matters as much as the parcel's contents.
- **The shell has to be worth money.** A rough 3D print in PLA will not carry a price. If it is sold,
  it needs to look deliberate: the right filament, a finish, a fit that clicks, and a photograph that
  makes someone want it before they know what it does. That is a design task, and it is the same
  task as the hero image (Phase 5 §4).

---

## 4. Pricing logic — the questions I need answered before a number exists

I will not put a figure in copy until these are answered. They are ordered by how much they move the
number.

### The cost side

1. **What does one unit's BOM cost** at quantity 1, 10 and 50 — with the actual part numbers? (Every
   figure in the repo is a planning estimate: `docs/research/marketing/opportunities.md:273-301`.)
2. **What does the printed shell cost** — grams of filament, machine hours, failure rate, and your
   time to post-process?
3. **How long does assembly take** — from parts-in-hand to boxed, including flashing and a test?
4. **What is your hour worth**, and are you willing to be paid for it in year one?
5. **What do shipping and packaging cost**, domestically and to the EU?

### The demand side

6. **Which price band are you aiming at**, and what is the closest thing a buyer would compare it to
   — a WiiM, a We Are Rewind, a Teensy project, a Yoto? (The repo's market map has the anchors it
   could verify: `docs/research/marketing/campaign.md:216-233`.)
7. **Would you rather sell 20 at €X or 200 at €Y?** The answer decides the shell's finish, the
   guide's quality and whether certification is worth buying.
8. **Is the shell priced as a product or as a community good?** (At cost with a tip jar is a
   different business from at margin.)

### The structural ones

9. **Who is the seller** — you, a company, an association? This decides VAT, EPR registrations,
   returns and the refund policy, and it is the first question a payment processor asks.
10. **Kit only, assembled only, or both** — and in what order? (`00-decisions.md` Q2.)
11. **What is *not* in the box** — card, cable, speaker, tools, batteries? Saying it early is the
    difference between a good review and a bad one.
12. **What is the returns and warranty position** on a thing a customer soldered themselves?

### What I would do with those answers

Build a one-page unit economics sheet — COGS at 1/10/50, assembly hours at your rate, shipping,
payment fees, a returns reserve, and the certification cost amortised over the first batch — and
solve for the price that makes the *shell* break even at 20 units and the *kit* worth doing at 50.
No number goes public until that sheet exists, because the alternative is a price you have to raise
in public.

---

## 5. The CTA sequence, as it actually ships

| When | The page says | Because |
|---|---|---|
| **Now** | *Join the kit list* · *Flash a board* · *Order the shell* (once the STL is printable and photographed) | lawful, revenue-positive, and it builds the builder cohort |
| **+1–2 months** | *Order the shell* · *Kit — join the list* · *Flash a board* | the BOM and the guide exist; the list has names on it to convert |
| **+3 months** | *Order the kit* · *Order the shell* · *Flash a board* | CE position answered, packaging exists, first batch defined |
| **+6 months** | *Order the kit* · *Order an assembled unit* · *Flash a board* | assembly is priced and someone else can do it |

Every stage keeps **Flash a board** on the page. It is the proof, it is free, and it is the reason a
technical reader believes the rest.

<!-- Phase 7 (campaign) follows in 07-campaign.md after the Phase 6 check-in. -->

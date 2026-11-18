# Decisions log

Running record of what has been **confirmed by the owner**, what it changes, and what is still open.
When a later phase contradicts this file, this file wins until the owner changes it.

---

## Confirmed — 2026-11-16

### D1. The commercial route is a kit, and the shell is 3D printed

**Stated:** "3D printed" · "I also plan to sell kits for pre-assembled assembly."

**What it settles**

- The physical object is a **printed shell plus off-the-shelf modules**. There is no custom PCB to
  design, no tooling, no mould, no minimum order for a case.
- The kit is therefore a **bag of modules, a printed-part set and a guide** — which is the cheapest
  possible kit to start and the easiest to publish, because every part is buyable by anyone.
- "Pre-assembled assembly" reads as two SKUs: **the kit** (parts + printed shell + guide) and **the
  assembled unit**. They are different certification stories (see D4).

**What it changes in the deliverables**

- Phase 1's "no physical design" is now: *no physical design in the repository*. The STL exists
  outside it and is the **single most valuable missing asset** — it is required for the kit, it is
  the community's licence to build, and it is the subject of the first photograph.
- Phase 3's design buyer (D) becomes reachable as soon as the shell is printed and photographed —
  it no longer waits on industrial design.
- Phase 5 can show a real object in the hero, once the STL is printed and shot.

### D2. Everything is off-the-shelf

**Stated:** "All is from off the shelf components."

**What it settles**

- No carrier board, no schematic to publish, no EMC design of your own, no bill of materials with
  custom parts. The BOM is a shopping list, and a buyer can price it before you do.
- It **removes the engineering risk** from the kit and moves all of it to the commercial/legal
  question, which is now the only thing standing between the project and a sale.
- It weakens the moat in the ordinary sense — anyone with the BOM can build one — and that is
  consistent with the stated intent: firmware for the community, kits for people who would rather
  not source parts.

**Consequences for the campaign**

- The differentiator is not the hardware. It is the **firmware, the screen, the app and the
  documentation** — which is exactly what Phase 1 §3 identified as the genuinely unusual part.
- The BOM becomes a **marketing asset**, not a secret. Publishing it is the builder's proof (Phase 3
  §2) and it costs nothing, because the parts are not defensible anyway.

### D3. The bridge is also off-the-shelf

**Stated:** in response to "do you build and sell the bridge, or recommend a purchased board?" —
"All is from off the shelf components."

**What it settles**

- The sink is a **module plus a DAC board** (and Bluetooth, if the module has it), not a product you
  manufacture. It can ship in the kit as parts, or be a "bring your own" list.
- The aha shot (Phase 3 §4) is achievable **without** designing a second device — which removes the
  biggest schedule risk in the plan.
- Certification scope does **not** double in the way a custom second device would, but a kit that
  contains a radio module and a DAC is still one assembled apparatus for CE purposes (D4).

### D4. Certification: the kit is not an exemption — carried forward

**Not a decision, a constraint** the owner has not contradicted, and which the repo's own brief
states: a kit intended to become this product makes the seller **likely the manufacturer** → CE
(at minimum EMC), RoHS, WEEE, packaging EPR
(`docs/research/marketing/compliance-2026-11-03.md:113-117`).

**Status:** open, owned by the owner, blocking Phase 6's price and any "buy" word.

### D5. The printed shell is sold on its own — confirmed

**Stated:** "yes" to *will you sell the shell alone (printed parts + guide, buyer sources the
electronics)?*

**What it settles**

- The first revenue is **plastic and a document**, not electrical equipment. No radio module leaves
  the seller, so the CE/RED question does not attach to the parcel (INFERENCE — a lawyer should
  confirm the framing and the product-page wording; see D4).
- The shell is the **only defensible thing** in the whole build: the modules are off-the-shelf, the
  firmware is MIT, the app is free. The printed part and the guide are what someone pays for.
- It converts the audience the campaign can actually reach (builders) into customers, at a low
  ticket, before any certification spend.
- **It makes the photograph non-optional.** Nobody buys a printed object they have not seen.

**Consequences**

- The STL must be published for the community and sold as a print for everyone else — the open
  version and the paid version are the same design, and that is the point (the buyer pays for the
  print, the fit and the guide, not for secrecy).
- Packaging, shipping and a **packaging EPR** obligation now apply even though the electronics do
  not (see D6).
- The kit becomes a later, separate decision (D4), not the launch.

### D6. The seller is the owner, personally

**Stated:** "me" to *who is the seller?*

**What it settles**

- There is no company, no VAT number, no terms of service and no returns policy yet — and every one
  of those is asked for before a payment processor will pay out.
- Selling as an individual in France means, at minimum, a **micro-entreprise** (or equivalent)
  registration to invoice, a VAT position (the *franchise en base* regime has a turnover ceiling),
  and **packaging EPR** registration with the *Info-tri*/Triman marking on every parcel.
  **INFERENCE — confirm with an accountant, not with me.**
- It keeps the risk small and the paperwork proportionate: a shell business can start as a
  micro-entreprise; a certified electronics business cannot.

**Consequences for the deliverables**

- Phase 6's CTA sequence holds, but *Order the shell* cannot go live before the seller can issue an
  invoice and take a payment.
- The site needs a **terms, returns and privacy** page before the first sale — three short pages, and
  they are the cheapest legal spend in the plan.
- The campaign's credibility line changes slightly: it is not "a company", it is *one person with
  four printers*, which for this audience is a feature.

### D7. Four printers

**Stated:** "have 4 printers."

**What it settles**

- Printing is **not the bottleneck** — assembly, packing, support and shipping are.
- Capacity is a *formula*, not a number I can fill in: `shells/week = 4 × (168 eligible hours / hours
  per print) × success rate`. Everything depends on the shell's print time and failure rate, which
  are not in the repository. **Measure one shell, then compute.**
- It makes a **small batch** genuinely viable (tens, not hundreds), which is the right size for a
  first sale and the right size for a solo operator.

---

## Open questions — awaiting the owner

| # | Question | Blocks | Default if unanswered |
|---|---|---|---|
| Q1 | Does the STL get published, and under what licence? | the kit page, the community build, the first photo | publish it; MIT or CC BY-SA for the printed parts |
| Q2 | Kit only, assembled only, or both? | Phase 6's comparison and the certification scope | kit first; assembled later |
| Q3 | Is the app permanently free and unbundled from the kit? (asked 2026-11-16) | the CC BY-NC position, the claims, the terms | yes — free, unbundled, source offered |
| Q4 | Who is the seller — a person, a company, an association? | price, VAT, EPR registrations, returns | none assumed |
| Q5 | Is there a budget for a photographer, or does the owner shoot it? | Phase 5's hero and Phase 7's asset list | owner shoots, phone + window light |
| Q6 | Volume: how many kits in the first batch? | price, BOM discounts, certification per-unit cost | 10–25, hand-assembled |

---

## The owner's last question, answered here so it is not lost

> "If software is free we can maintain it not?"

**Yes — and it is the correct structure, with one correction to the reasoning.**

- **Maintenance is not the issue.** CC BY-NC restricts *commercial use*, not maintenance, forking or
  distribution. You can keep working on the app for as long as you like.
- **"Free" is doing the work, not "maintained".** A free, ungated, publicly hosted app that a buyer
  happens to open for their own listening sits far from the licence's target. What the licence does
  reach is *your* commercial use of it: selling the app, gating it, hosting it as a paid feature, or
  selling a bundle whose value depends on it.
- **So the structure to hold is:** the app is free, unbundled, never described as part of the kit,
  source offered over the network (AGPL, `web/src/config.ts`), and the kit is sold as hardware +
  firmware. Say that in the terms, not only in the copy.
- **It does not remove the AGPL** — Essentia.js is AGPL-3.0, so the app is AGPL regardless of ONNX.
  Free software is compatible with that; it is not compatible with closing it.
- **Still worth one email to MTG**, because a written "non-commercial use is fine here" makes the
  whole question disappear, and they offer a proprietary licence if it does not.

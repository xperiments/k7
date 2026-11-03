# CE and FCC for K7: what it costs, what blocks it, and what to do first

Researched 2026-11-03. Costs are **indicative market ranges**, not quotes — a laboratory offer is
the only number that binds. Legal statements are read on the European Commission's own page where
cited; the rest is engineering judgement, and it says so.

## 1. The verdict

1. **The Wi-Fi is the easy part.** The ESP32-S3 module already carries radio certification, so the
   RF campaign is a host-integration check, not a from-scratch design.
2. **The hard part is the security model.** A device that ships with no authentication, no TLS and
   no update path is the one place where this product and European law point in opposite
   directions — and that is a design decision, not a test result.
3. **Budget €3,000–€10,000 for EU + US on a simple device**, plus 6–10 weeks, plus your own time on
   the technical file. The CE mark itself is free; the testing and the file are not.
4. **It is not a marketing cost, and probably not a grant cost.** It is the price of the first
   sale. Until it is paid, the campaign has a reserve list, not a checkout (§18.3 of the campaign
   plan).

## 2. What actually applies

| Regime | Applies because | What it requires |
|---|---|---|
| **Radio Equipment Directive 2014/53/EU** (RED) | It is radio equipment placed on the EU market: *"CE marking can only be affixed after testing the product and performing the conformity assessment procedure"* ([European Commission](https://single-market-economy.ec.europa.eu/sectors/electrical-and-electronic-engineering-industries-eei/radio-equipment-directive-red_en)) | RF (EN 300 328), EMC (EN 301 489-1/-17), safety (EN 62368-1), RF exposure (EN 62311), plus the cybersecurity essential requirements where they apply (EN 18031) |
| **RoHS 2011/65/EU** | Any electrical equipment | Substance limits; a test report is cheap (indicatively €250–€700 with REACH, per [LCAS](https://lcas.info/packages/rohs-reach-electronics-testing-cost/)) |
| **WEEE 2012/19/EU** | Selling electrical equipment in the EU | Producer registration through an eco-organisation, per country; in Spain under Royal Decree 110/2015 (regional explainer: [Euskadi](https://www.euskadi.eus/contenidos/noticia/jornada_raee_presentacion_rd/es_def/adjuntos/2015-06-25%20RD%20110-2015%20Productores%20y%20distribuidores.pdf)) |
| **Packaging EPR** (94/62/EC, national schemes) | It ships in a box | Registration and fees per market |
| **FCC Part 15** | Selling in the US | Unintentional radiator (Subpart B) compliance; if the transmitter is a certified module used per its grant, the module's certification covers the radio and the host needs its own EMC compliance (SDoC). The modular-transmitter rule is [47 CFR 15.212](https://www.ecfr.gov/current/title-47/chapter-I/subchapter-A/part-15/subpart-C/section-15.212) |
| **Cyber Resilience Act** | A connected product with software | *"Main obligations… will apply as of 11 December 2027"* ([European Commission](https://single-market-economy.ec.europa.eu/sectors/electrical-and-electronic-engineering-industries-eei/radio-equipment-directive-red_en)) — an update and support policy, not a sticker |

Two notes that decide scope:

- **No LVD, no battery.** A 5 V USB-powered device is outside the Low Voltage Directive, and with no
  cell there is no UN 38.3 shipping class. Both simplify the file.
- **Open source is not an exemption.** MIT/AGPL licences change nothing about market access.

## 3. What the certified module buys you

Espressif publishes CE-RED and FCC documentation for its modules
([certificate library](https://documentation.espressif.com/en/documentList?qr=CE-RED&qr=FCC)). That
gets you:

- **The radio campaign is a formality** — the module's own test reports cover the transmitter, if
  you keep the module's antenna, layout constraints and integration instructions.
- **A shorter, cheaper EU programme** — the "each additional radio band and each antenna variant"
  cost driver in the lab's own breakdown
  ([LCAS, RED](https://lcas.info/packages/red-wireless-en-300-328-testing-cost/)) is a cost you are
  not paying, because there is one band and a certified antenna.

And it does **not** get you:

- **The host's EMC.** The panel, the SD bus at 20 MHz, the wires, the enclosure: all of that is
  your unintentional radiator, on both sides of the Atlantic.
- **The safety and RF-exposure assessment.** The module is not the product.
- **The technical file, the declaration of conformity, or the labels.**

## 4. The real blocker: the security model

The repo is explicit and deliberate: **no authentication anywhere, no TLS, Wi-Fi credentials
compiled in, no OTA** ([`README.md:23`](../../../README.md#L23),
[`README.md:1172`](../../../README.md#L1172)). That is defensible for a LAN appliance the author
owns. It is much harder to defend for a product sold to strangers, for three reasons:

1. **RED cybersecurity.** The Commission's delegated regulation on cybersecurity under RED
   Art. 3(3)(d)(e)(f) *"entered into application in August 2025"*
   ([European Commission](https://single-market-economy.ec.europa.eu/sectors/electrical-and-electronic-engineering-industries-eei/radio-equipment-directive-red_en)),
   with the harmonised standards in the EN 18031 family. Whether a **LAN-only** device that never
   touches the internet falls inside that scope is a question for a laboratory or notified body —
   not for this document. **Ask it in the first email.** If it does, a device with no access
   control and no secure update is not certifiable as designed.
2. **The CRA, from 11 December 2027.** Even if RED's cybersecurity requirements are repealed into
   the CRA (the Commission says the repeal takes effect on that date), the direction of travel is
   the same: a sold device needs a support and update policy.
3. **Liability.** A device that serves a stranger's music library to anyone on their Wi-Fi, with no
   way to patch it, is a claim waiting to be tested by the first security researcher who buys one.

**Consequence for the roadmap.** The work that makes the product certifiable is the same work that
makes it deployable by someone who is not its author: a per-device identity, an optional pairing
step, an update path, and a documented threat model. That is exactly the list in the funding memo
([`funding-2026-11-03.md`](./funding-2026-11-03.md) §3) — which is the strongest argument for doing
that work first, before any certification money is spent.

## 5. Indicative costs

| Line | Indicative | Source |
|---|---|---|
| RED: EN 300 328 + EN 301 489 (2.4 GHz, one band) | **€2,500–€8,000** | [LCAS package](https://lcas.info/packages/red-wireless-en-300-328-testing-cost/) |
| RoHS + REACH SVHC report | **€250–€700** | [LCAS package](https://lcas.info/packages/rohs-reach-electronics-testing-cost/) |
| Packaging compliance (PPWR) | **€250–€900** | [LCAS package](https://lcas.info/packages/ppwr-packaging-compliance-testing-cost/) |
| FCC (lab EMC work; no government fee for SDoC) | not verified — get it quoted with the EU programme | — |
| WEEE / packaging registrations | annual, scaled to weight; small at a few kg, not zero | eco-organisation quotes |
| Technical file + DoC | your time; a consultant if you buy one | — |
| **Realistic total, EU + US, first product** | **€3,000–€10,000** | sum of the above, plus the FCC quote |

The cheapest useful spend before any of that: **an EMC pre-compliance scan** at a local lab, and a
30-minute call with an accredited lab about the RED cybersecurity scope. Both cost little and both
can cancel a redesign.

## 6. How long

| Step | Realistic |
|---|---|
| Quotes from two labs | 1–2 weeks |
| Pre-scan (optional but advised) | days, once booked |
| Lab booking + test programme | 2–6 weeks of queue, 1–2 weeks of testing |
| Report, technical file, DoC, labels | 1–2 weeks of your time |
| **Total** | **6–10 weeks**, if the design is frozen |

Nothing here parallelises with a redesign. Freeze the hardware and the security model first.

## 7. The three ways to ship with no money

| Route | Legal position | What it needs |
|---|---|---|
| **Sell the plan** — the guide, the BOM, the firmware, the files | No CE, no FCC: you are selling a document, not equipment | Nothing. This is the honest €0 route today. |
| **Sell a kit** — modules, panel, card, wires, printed guide | The kit is electrical equipment; **do not rely on the "assembler becomes the manufacturer" argument** — as the seller of a kit intended to become this product, you are likely the manufacturer | CE (at minimum EMC), RoHS, WEEE; a legal read before the first sale |
| **Sell a finished unit** | Full CE + FCC + WEEE + packaging | €3,000–€10,000 and 6–10 weeks, plus a security model that survives review |

There is no fourth route where open source or "it's a prototype" removes the obligation when money
changes hands.

## 8. What to do this week, for free

1. **Ask a lab the scope question in writing.** "LAN-only Wi-Fi appliance, no internet connection,
   no account: does RED Art. 3(3)(d)(e)(f) / EN 18031 apply?" Keep the answer; it is worth a
   paragraph in the funding proposal and in the risk section of the campaign.
2. **Request two quotes** for RED (one band) + RoHS + FCC, using the module's certificates as input.
3. **Download the module's CE-RED and FCC documents** and check that your hardware respects the
   integration instructions — antenna, ground plane, shielding, and the module's own label
   requirements.
4. **Decide the security model** — device identity, optional pairing, update path — before the
   enclosure and the PCB are final.
5. **Do not put certification in the grant budget** unless the fund's eligibility list covers it;
   keep it as the first-sale cost it is.

## 9. What this does not settle

- **Whether EN 18031 applies to a LAN-only device.** That is a legal/technical determination, and
  the lab's written answer is the only one worth having.
- **The cost of a first batch.** Certification is one line of a manufacturing quote; tooling,
  assembly, test jig and returns are others.
- **Whether the product is worth certifying at all.** That is what the reserve list and the 40
  qualified builders in the campaign plan are for. Certification turns a build into a business;
  it does not tell you whether anyone wants one.

# Phase 5 — Website strategy

**Read with:** [`00-decisions.md`](./00-decisions.md) (kit, printed shell, off-the-shelf),
[`02-soul-and-positioning.md`](./02-soul-and-positioning.md) (the sentence),
[`04-claims.md`](./04-claims.md) (what may be said).

**What exists today:** `k7.xperiments.dev` is live and it is a *utility page* — a hero, a flash
section, an install section, a licence footer (`web/src/site/Site.tsx:36-97`). It is well built, it
is honest, and it is not trying to sell anything. That was right for the state of the project in
November. It is now the wrong page for a product with a kit in its future, because **it captures
nothing**: there is no address, no list, no interest, and no way to tell a visitor from a crawler.

---

## 1. The goal, and the one conversion

**Goal:** in one screen, make a stranger understand what this is; in one scroll, make them believe
it exists; in one action, keep them.

**The single primary conversion: join the kit list.**

One email field, one button, nothing else. Not a preorder — there is no entity, no terms, no refund
policy and no cleared certification (Phase 4 §5). Not a download — the repo is already free and a
download captures nobody. The list is the only honest ask that produces an address, and the address
is the only thing the campaign cannot otherwise get.

**Why not "Flash a board" as the primary?** Because it is the *proof*, not the *ask*. It is
performed by a minority (a board, a data cable, Chrome on a computer) and it converts a visitor into
a user who owes you nothing and leaves no trace. It belongs on the page, high, as evidence that the
thing is real and free — not as the page's purpose.

**Why not "Open the app"?** Same reason, and it is the weaker of the two: a visitor who installs the
app has not seen the deck at all. Keep it as a tertiary link for people who already own a K7.

So the page carries three actions, in a deliberate hierarchy:

| Rank | Action | Where | What it means |
|---|---|---|---|
| 1 | **Join the kit list** | hero, and again after "what it doesn't do" | the conversion |
| 2 | **Flash a board** | hero (secondary), and its own section | the proof |
| 3 | **Open K7** | header, footer | for people who already have one |

INFERENCE: this is the difference between a campaign and a brochure. The current page is a very good
brochure.

---

## 2. The page, in order

Nine sections. Every one has a job, and a claim it is not allowed to exceed.

| # | Section | Purpose | Claim or proof it carries | Source of its material |
|---|---|---|---|---|
| 1 | **Hero** | say what it is in five seconds, and ask | "your music, on a cassette"; it is real | headline, subhead, one still or the 10 s loop, primary CTA |
| 2 | **The ten seconds** | the aha, with no words | card in → reels turn → sound from across the room | Phase 3 §4, shot 1 (needs the bridge) |
| 3 | **Three nouns** | kill the confusion: cassette / tape / room | what the object is, what the card is, where the sound comes from | Phase 2 §2; the "speakers already in the room" clause |
| 4 | **The screen** | desire, and the thing no competitor has | four rungs, the wind, the label print | the live SVG panel, plus three board captures |
| 5 | **Measured music** | give something away, now | tempo, key, mood, energy — in the tab, nothing uploaded | the app, linked; the analyser is the free product |
| 6 | **The build** | trust, for the audience that reads | BOM, STL, firmware, three releases, the repo, the ADRs | `README.md`, `web/firmware.json`, the STL (to publish) |
| 7 | **What it doesn't do** | trust, for everyone else | six refusals, unhedged | Phase 4 §5 and §7 |
| 8 | **The kit** | the ask, informed | what is in the box; price or "not yet" | Phase 6, once priced |
| 9 | **Footer** | the licence trail | MIT firmware, AGPL app, notices, source | `Site.tsx:75-97` |

**Why this order.** Desire before proof, proof before refusal, refusal before the ask. A reader who
reaches §8 has been shown the object, told what it does, given something free, shown the source, and
told what is missing — which is exactly the state of mind in which a technical buyer says yes. The
refusals section is placed *after* the build and *before* the kit on purpose: it is the last thing
they read before deciding, and it is the most persuasive thing on the page.

### The hero

- **H1:** *Your music, on a cassette.* — keep. It is the page title, the meta description's first
  clause and the best line in the set (`web/index.html:10,15`).
- **Sub:** *For people who own their music and miss the object it came in: a 3D-printed cassette
  that turns a card into a tape and your network into a radio station. No account, no cloud, no
  recommendations — and it plays to the speakers already in the room.*
- **Primary button:** *Join the kit list* → the form.
- **Secondary button:** *Flash a board* → `#build`.
- **Hero media:** the 10-second loop, muted, looping, with a still as poster. Until the bridge is
  built, the still is **a photograph of the printed shell** — not the panel drawing. The current hero
  is `resources/cassette-drawer-800x480.png`, the panel's own drawing, in a slot built for a photo
  (`docs/specs/k7-site.md:103-106`). Replace it at the first opportunity; it is the highest-value
  single change to the page.
- **Fallback:** if there is no photograph yet, the live SVG panel (§4) is better than a static PNG,
  because it moves. But it is not as good as a photograph, and it should be replaced, not kept.

---

## 3. Interactive and experiential ideas

Ranked by payoff per hour. The first two are worth building; the rest are not, yet.

### 1. The panel, live, in the browser — *build this*

The cassette screen already exists as resolution-independent SVG in React
(`web/src/ui/cassette-screen.tsx`), with the reels driven by tape geometry and the whole look
tokenised (`docs/specs/k7-look.md:540-547`). Put it on the page at the panel's real 800×480, and let
the visitor **swipe the four rungs**: Initial → Art → Real → Peek, and drag a knob.

*Why it works:* it is the product, not a picture of the product; it costs almost nothing because the
component exists; and it lets a visitor feel the one thing that is hard to photograph — that the
screen behaves like a machine.

*Constraint:* the site must also work at 800×480 (`docs/specs/k7-look.md:608`), which is exactly the
right size for this. Extracting the component from the app's context (device state, covers) is the
real cost — budget a day.

### 2. Make a label — *build this after the STL is published*

The Label Editor runs real LVGL 9.6 in WASM and exports a `label-template v1` with a PNG preview
(`tools/LabelEditor/README.md`). Embedding a cut-down version — pick a template, type a title, see
it on a cassette — is the single most *shareable* thing this product can put on the web, and it is
the only interactive that shows the deck's most surprising feature without a board.

*Risk:* it is a second app to maintain. If time is short, ship a gallery of the four finished
templates instead (`assets/labels/templates/`, `docs/label-*.png`) and link to the editor.

### 3. The BOM calculator — *build it when the BOM is real*

Tick the parts you already own; see what the kit would cost you. It respects the builder's
intelligence, it is a genuine reason to return to the page, and it needs no backend. It cannot be
built before the BOM exists (Phase 3 §1A, item 1).

### 4. Drop a file, see it measured — *link, do not embed*

The analyser is already the free product, and it is a 41 MB-weight app. Do not put that in the
marketing page's bundle. One button: *Try the analyser* → `/app#/prepare`.

### 5. A 3D viewer of the printed shell — *not yet*

A glTF turntable is a lovely thing and it is not a photograph. It costs a week and it answers a
question ("what does it look like?") worse than one good photo does. Revisit only if the kit sells
before a photo exists.

---

## 4. Assets: reuse, and create

### Reuse — already in the repository

| Asset | Use on the page | Where |
|---|---|---|
| The live cassette SVG | hero, or the interactive panel | `web/src/ui/cassette-screen.tsx` |
| The panel drawing (800×480) | social card, press kit, a section image | `resources/cassette-drawer-800x480.png` |
| Ten board captures + contact sheet | the screen section, the press kit | regenerate with `npm run screens` into `docs/images/screens/` (not checked in) |
| The coverflow (CSS 3D) | the tape/library section, if a second interactive is wanted | `web/src/ui/album-shelf.tsx`; `docs/specs/k7-look.md:435-460` |
| Four finished label designs | the label section | `assets/labels/templates/*.json`, `docs/label-*.png` |
| The label renderer (LVGL in WASM) | "make a label" | `tools/LabelEditor/` |
| The flash flow | the build section, unchanged | `web/src/site/Flash.tsx` |
| The install flow | the "Open K7" path, unchanged | `web/src/site/Install.tsx` |
| Jura + the palette tokens | everything | `web/src/index.css:111-136`; `docs/specs/k7-look.md:98-136` |
| The firmware manifest | "three releases, sha256, flash from the browser" | `web/firmware.json` |
| The rotor frames | a small looping hub animation | `assets/hub/001-036.png` |

### Create — in priority order

1. **A photograph of the printed shell**, in a room, with the screen lit. One image. It is the
   difference between a project and a product.
2. **The 10-second loop** (Phase 3 §4), and a 60–90 second cut for the build section.
3. **The STL**, published, with a licence — the kit cannot exist without it, and neither can the
   community build.
4. **The BOM**, as a table on the page (and a CSV).
5. **An OG/social image** — currently absent (`web/index.html:1-18` has no `og:` or `twitter:` tag).
6. **The "what it doesn't do" copy**, written once, from Phase 4.
7. **A press kit** in one zip: three photos, the ten captures, a 120-word description, the
   licence facts, contact.

---

## 5. Copy skeleton

Draft, in the product's voice. Not final; not to be published without the Phase 4 review.

### Hero

> **Your music, on a cassette.**
>
> For people who own their music and miss the object it came in: a 3D-printed cassette that turns a
> card into a tape and your network into a radio station. No account, no cloud, no recommendations —
> and it plays to the speakers already in the room.
>
> **[ Join the kit list ]**  [ Flash a board ]

### Three nouns

> **The cassette.** A printed shell around an ESP32-S3 and a five-inch touch panel. A card slot. A
> strip of eight lights. That is the whole object.
>
> **The tape.** A microSD card. Your library in `music/`, your lists as files beside it, and a small
> mark that says which card this is. Put it in a reader, prepare it, put it in the deck.
>
> **The room.** One stream, one moment, every listener. It plays to anything on your network that
> can open an MP3 URL — a phone, a laptop, the hi-fi — so the sound comes out of the speakers you
> already chose.

### The screen

> **It behaves like a tape.**
>
> The reels turn at the speed the tape would — 47.625 mm per second, at each pack's own radius, so
> the supply and take-up reels counter-rotate the way they should. Change track and it winds for
> eight seconds and lands on normal play. Hold two fingers to the glass and the track's cover
> becomes a printed cassette label.
>
> *[ the interactive panel, or three captures ]*

### Measured music

> **Your library, measured in the tab.**
>
> Drop a folder. K7 reads tempo, key, mood, energy and danceability in your browser — the files
> never leave the machine. It trusts the tags you already wrote and says which values it trusted.
> Then it writes what it measured onto the card, so the deck can answer "something at 128" without
> touching a model at all.
>
> **[ Try the analyser ]**

### The build

> **Everything is in the repository.**
>
> MIT firmware. 96 decision records that are superseded rather than rewritten. A host test suite and
> a rule that a phase is only finished when it passes on the host *and* on the board. Three firmware
> releases, with checksums, that the site can flash to a bare board over USB — no toolchain, no
> ESP-IDF, no terminal.
>
> **[ Flash a board ]**  [ Source ]  [ BOM ]  [ STL ]

### What it doesn't do

> **Six things it will not do, so you can decide before you build one.**
>
> - **It has no speaker, no DAC and no amplifier.** Audio leaves as a 256 kbps MP3 stream. Something
>   else plays it.
> - **It is not lossless.** Whatever the card holds is resampled and re-encoded. The card is the
>   master; the stream never is.
> - **It cannot pause or seek.** It is radio: one stream, the same moment for everyone.
> - **It has no account, and no authentication either.** Plain HTTP on your LAN, a default hotspot
>   password. Do not put it on the internet.
> - **One device per network.** The hostname is fixed; two K7s on one LAN collide.
> - **It will not be sold until it is certified.** A kit is electrical equipment — the guide, the
>   parts and a legal read come first.

### The kit

> **The kit.**
>
> The printed shell, the module, the panel, the card, the strip, the wires and the guide. You bring
> a USB-C cable and a soldering iron. *[ price or: not yet — join the list and you will hear first ]*
>
> **[ Join the kit list ]** — one email, when there is something to say.

### Footer

Keep `Site.tsx:75-97` almost as it is, and fix one sentence: the firmware is MIT and the app is
AGPL; the footer currently says AGPL for both (`Site.tsx:76`; Phase 4 row 7).

---

## 6. Technical notes

### Static, not dynamic

The marketing page is a document. It should be **prerendered to HTML at build time**, with the app
staying a client-side route at `/app`. Today `/` ships 1.4 KB of HTML and a JavaScript bundle
(`curl` of the live site), which means a crawler that does not run JS sees a title tag and nothing
else, and a reader on a slow connection waits for a bundle to see a headline.

- **Route split:** `/` → prerendered marketing page; `/app` → the existing SPA; `/sw.js` unchanged.
  The Site spec already keeps the worker from standing in front of the Site (`docs/specs/k7-site.md:56-62`);
  prerendering is the natural next step in the same direction.
- **Bundle:** the marketing route must not import the analyser, ONNX runtime, taglib or the device
  client. Target **< 60 KB gzip of JS** for the marketing route, or none at all if the panel
  interactive is lazily loaded on first interaction.
- **The one dynamic thing** is the kit-list form. Post it to the email provider server-side or to
  their hosted endpoint; no client-side SDK.

### Performance budget

| Metric | Target | Why |
|---|---|---|
| LCP | < 2.5 s on a throttled 4G profile | Core Web Vitals' own threshold, and the hero is one image |
| CLS | 0 | every image has width/height; the video has a poster and an aspect ratio |
| Total JS (marketing route) | < 60 KB gzip | the app's 41 MB of weights are a separate route |
| Requests before first paint | ≤ 5 | self-hosted Jura, one CSS file, one image |

Images: AVIF with a JPEG fallback (the app already has an AVIF encoder, `@jsquash/avif`), served
from `/site/`. Video: **self-hosted**, `<video muted loop playsinline preload="metadata">` with a
poster; the CSP already forbids third-party scripts and allows only `youtube-nocookie` frames
(`curl` of the live headers) — a YouTube embed would contradict the site's own "no third-party
script" sentence and cost a megabyte before the first frame.

### SEO

- **The name is a polluted query.** "K7" alone collides with unrelated products and with the French
  common noun (`docs/research/marketing/campaign.md:296-306`). Every page pairs it: *K7 cassette*,
  *K7 tape player*, *K7 by xperiments*. Target the phrases a person would actually type:
  *cassette-shaped music player*, *ESP32 music streamer*, *SD card music player*, *DIY cassette
  player*.
- **Missing today:** `og:title`, `og:description`, `og:image`, `og:type`, `twitter:card`,
  `<link rel="canonical">`, a sitemap, and structured data (`web/index.html:1-18`).
- **Structured data:** `Organization` (xperiments) and `SoftwareApplication` (the app, free) now.
  **`Product` only when a real price and availability exist** — marking up a product that cannot be
  bought is the kind of claim Phase 4 refuses.
- **One page or several?** One page for launch. Add `/kit` only when the kit has a price, and
  `/build` only if the guide outgrows a section. Every extra page is a page to keep true.

### Analytics — keep the promise, and still learn something

The site promises **no analytics, no cookies, no third-party script**
(`docs/specs/k7-site.md:117-120,190-191`). That is a brand asset and I would not spend it on a
dashboard. Measure without breaking it:

1. **The list is the truth.** Signups per week, and which page/UTM they came from (a hidden field).
2. **Server-side aggregate.** Vercel's request logs give page views and referrers without a cookie
   and without client code.
3. **UTM discipline on outbound links**, not on-site tracking (`utm_source=hn|reddit|yt`,
   `utm_campaign=kit`).
4. If a funnel is genuinely needed later, add a **self-hosted, cookieless** counter behind a consent
   banner — and change the spec's sentence in the same commit, or the site starts lying.

### i18n

| Language | When | Why |
|---|---|---|
| **English** | day one | the default, and the language of the repo |
| **French** | day one | the name is French, the product is in France, and it is the one market where *K7* needs no explanation (`docs/adr/0062:3`) |
| **German** | after the first kits | Europe's largest recorded-music market; cassette and techno culture read the object correctly |
| **Spanish** | not yet | *casete*, so the name does not carry the pun; no evidence yet that the effort pays |
| **Catalan** | **no** | the campaign asserts a Catalan rebus; its own market file says "Not supported" (`docs/research/marketing/market-2026-11-03.md:70`). Do not build a lane on it |

Implementation: two static routes (`/fr/`), not a runtime switcher. The page is short; two copies of
the copy is cheaper than an i18n framework, and it keeps the bundle tiny.

### Accessibility

The look spec already sets measured contrast (accent 5.3:1, ink 7.4:1, dim 4.9:1,
`docs/specs/k7-look.md:125-128`) and `prefers-reduced-motion` → motion **none**
(`k7-look.md:533-535`). Three additions the page needs:

- The interactive panel must have a keyboard path (arrow keys between rungs) and a text alternative
  that says what the four rungs are.
- The video needs captions and a transcript — it has almost no speech, so this is cheap.
- The kit-list form: a real `<label>`, an error message tied by `aria-describedby`, and a success
  state that is announced.

### The 800×480 constraint

The look spec requires the Site to work at both desktop and 800×480 (`docs/specs/k7-look.md:608`) —
the panel's own resolution. Treat it as a design brief rather than a test: the page should be
*readable on the device it is about*. It is also the cheapest possible way to check the typographic
scale.

### Consent and the list

One field, double opt-in, an EU-hosted provider, a privacy page that names the sub-processor, no
sale of the list ever, and a one-click unsubscribe. The site has no cookie banner today because it
sets no cookies — **keep it that way**: a hosted form endpoint needs no cookie, and the moment the
page sets one, the banner and the promise both arrive.

---

## 7. What the site must not become

1. **A landing page with a countdown.** There is no launch date. A countdown invents one.
2. **A page that needs JavaScript to say what the product is.** It is a document first.
3. **A page with a stock photo of a cassette.** The object is printed; photograph *that*. A stock
   tape in a hero would be the exact lie Phase 2 §6 refuses.
4. **A page that hides the limitations below the fold.** They go *before* the ask, deliberately.
5. **A page with a chat widget, a cookie banner or a heat-map script.**
6. **A page that says "buy" before there is a certified thing to sell** (Phase 4 row 43).

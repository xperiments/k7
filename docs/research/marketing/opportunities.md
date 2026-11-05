# Where the money is (or is not) for xtape

Research date: **2026-10-30**. All prices are as fetched on that date; anything not fetched
is marked *unverified*. Every number below has a link. Where a source is a vendor's own
pricing or licence page I say so; where it is a lab/molder's published guide I label it
"vendor estimate"; where it is a market report I label it "estimate".

The question is narrow: given a cassette-shaped ESP32-S3 LAN music appliance with an MCP
endpoint, a companion analysis web app, and a 1–3 person team, **who pays for what, and how
much of it is real**.

---

## 1. Executive summary

- **Hardware is where the money is; the one software subscription in the category is paired with
  hardware.** WiiM Pro Plus $219 and Ultra $329, Arylic $69–$2,499, Volumio Rivo €989, Sonos
  Port $499, HiFiBerry boards €25–€50. Volumio *does* charge DIY users a Premium subscription —
  and then gives it away on its own devices and licenses it to brands; Roon's $14.99/mo comes
  with its own server line.
  → **Decision: price a device, not a service. Any recurring-revenue plan for a LAN-only box
  is a distraction until there are hundreds of owners.**
- **A 100-unit run is a hobby, not a business.** With ~$10k of certification and setup cost and
  ~$75/unit contribution at $299, break-even is roughly **100–150 units**; at 1,000 units and
  $329 the same design yields ~$230/unit. The variable that matters is *units sold*, and the
  binding constraint is founder assembly labour, not BOM.
  → **Decision: design for a 500–1,000 unit/year line from day one (moulded or
  vendor-supplied enclosure, panelised PCBA), or accept that this is a limited edition.**
- **MCP is real infrastructure and worth roughly zero as a purchase reason.** The spec went
  stateless in 2025-11-25 and finished the job in 2026-07-28; there are 10,000+ public
  servers and 87M weekly npm SDK downloads — and **Espressif now ships an official MCP C SDK
  for ESP32**, which means an MCP endpoint on a chip is a commodity, not a differentiator.
  → **Decision: keep `/mcp` free, ship it, and never let it gate a release or a price.**
- **The legal floor is cheap and already mostly met.** MP3 royalties ended with Fraunhofer's
  programme termination on 2017-04-23; the only real obligation is LGPL-2.0 §6 for `shine`,
  satisfiable with a source+object offer and a relink path; AAC is the one codec to keep out
  because Via LA licenses it per unit and does not publish the rate.
  → **Decision: budget a day of packaging work (NOTICE, source tarball, build script,
  EULA carve-out), not a retainer — unless you enable secure boot, which changes the answer.**
- **The plausible business is a design object at $299–$399, 300–1,000 units/year, possibly
  followed by an OEM/module deal.** The cassette form factor is the only defensible asset;
  everything else (streaming, library index, MCP) is reproducible by a competitor in weeks.
  → **Decision: spend the next $500 testing whether the *look* sells, not on features.**

---

## 2. The market map

Prices fetched 2026-10-30. "What the customer pays for" is the part that actually generates
the vendor's revenue.

| Product | What it costs | What the customer pays for | Source |
|---|---|---|---|
| **WiiM Pro Plus** | **$219** (US) | Hardware only; app, AirPlay 2, Chromecast, DLNA free, no subscription | [Amazon US listing](https://us.amazon.com/dp/B0CC2HWC7N) |
| **WiiM Ultra** | **$329** (US) | Hardware only (3.5" touchscreen, HDMI ARC, phono in) | [Amazon US listing](https://us.amazon.com/dp/B0D41YCLW8) |
| **Arylic S10+** | **$69** | Hardware only | [Arylic catalogue](https://www.arylic.com/products.json) |
| **Arylic LP10** | **$99** | Hardware; AirPlay 2 + Cast | [Arylic catalogue](https://www.arylic.com/products.json) |
| **Arylic LP20** | **$199** | Hardware | [Arylic catalogue](https://www.arylic.com/products.json) |
| **Arylic LP100** | **$399** | Hardware (HiFi streamer) | [Arylic catalogue](https://www.arylic.com/products.json) |
| **Arylic HA400** (4-zone amp) | **$2,499** | Hardware + dealer/integrator channel — the CI market pays 10× consumer prices for zone count | [Arylic catalogue](https://www.arylic.com/products.json) |
| **HiFiBerry DAC+ Light / Digi2 Pro** | **€24.90 / €49.90** | Hardware (Raspberry Pi HAT); HiFiBerryOS free | [HiFiBerry shop](https://www.hifiberry.com/shop/boards/dac2-pro/) |
| **HiFiBerry DAC2 Pro** | **€44.90** | Hardware | [HiFiBerry shop](https://www.hifiberry.com/shop/boards/dac2-pro/) |
| **Volumio Rivo** (transport) | **€989** | Hardware + "all Volumio Premium features included" — software bundled into the price | [Volumio shop](https://shop.volumio.com/product/rivo) |
| **Volumio Primo** | **€799–€849** | Hardware + bundled Premium | [Volumio shop](https://shop.volumio.com/product/rivo) |
| **Volumio OS** (software) | Free download; **Volumio Premium** subscription "monthly or yearly" for DIY (price not in static HTML — *unverified*) | **Three tiers at once:** subscription from DIY users, Premium given away free on Volumio's own hardware, and **OEM licensing** to brands (a form that qualifies leads by annual unit volume) | [Get started](https://volumio.com/get-started/), [Volumio OEM](https://volumio.com/business-space/oem/) |
| **Sonos Port** | **$499** (B&H) | Hardware + app/cloud ecosystem; no separate subscription | [B&H](https://www.bhphotovideo.com/c/product/1502022-REG/sonos_port1us1blk_port.html) |
| **Bluesound NODE NANO / NODE / NODE ICON** | **CAD 499 / 849 / 1,599** (≈USD 360 / 610 / 1,150 at ~0.72) | Hardware + BluOS ecosystem | [Bluesound product page](https://www.bluesound.com/products/node-icon/) (store currency CAD, confirmed via `/cart.js`) |
| **Roon** | **$14.99/mo**, **$12.49/mo** billed annually, **$829.99 lifetime** | Subscription for metadata, DSP, multi-zone — the only real software subscription in the category | [Roon pricing](https://roon.app/en/pricing) |
| **Roon Nucleus One** | **$644.96** incl. 1 year of Roon | Hardware *and* subscription bundled; the box is the subscription's delivery vehicle | [Roon Store](https://store.roonlabs.com/products/nucleus-one-with-roon-subscription/) |
| **Plex / Plexamp** | Free server+client; **Plex Pass** subscription | Subscription buys features (transcoding, downloads, hardware accel.), not the library; Remote Watch Pass listed at **$2.99/mo / $29.99/yr** | [Plex plans](https://www.plex.tv/plans/) (prices partly JS-rendered; Plex Pass lifetime reported rising to $750 from July 2026 — *secondary*, [notebookcheck](https://www.notebookcheck.net/Plex-lifetime-pass-price-triples-to-750-as-monthly-subscriptions-cost-unchanged.1303078.0.html)) |
| **Navidrome** | **Free**, open source | Nothing — 24,449 GitHub stars, funded by users, not licences | [navidrome.org](https://www.navidrome.org/), [repo](https://github.com/navidrome/navidrome) |
| **moOde audio** | **Free** | Nothing; no price appears on the project site at all | [moodeaudio.org](https://moodeaudio.org/) |
| **Asset UPnP** | Paid, one-time (price not extractable from the vendor page — *unverified*, historic era $30–40) | PC software licence for a UPnP server with library features | [dbpoweramp](https://dbpoweramp.com/purchase.htm) |
| **Yoto Player (4th gen)** | **$129.99** + cards **$6.99–$15.99** (most at **$12.99**) | Hardware *and* content cards — the razor, then the blades | [Yoto catalogue](https://www.yotoplay.com/products.json) |
| **Tonies** | Content figures observed at **$11.99 / $16.99 / $19.99** | Hardware + content figures; the box is cheap, the characters recur | [tonies.com US](https://tonies.com/en-us/) |
| **We Are Rewind** (cassette-shaped player) | **£159** | Hardware + nostalgia; Bluetooth, no streaming | [Richer Sounds](https://www.richersounds.com/we-are-rewind-amy-black-yellow/) |

**Structural read of the table.**

1. **The price band is set by WiiM.** $219–$329 buys an honest streamer with AirPlay 2,
   Chromecast, a real DAC and a mature app. Anything xtape sells above $250 must sell on
   *form*, not function, because on function it loses.
2. **Software is bundled or given away, never the main line — except where it has a hardware
   or database moat.** Volumio runs a three-tier model: a Premium subscription for DIY users,
   Premium free on its own devices ("On a Volumio device, the Premium features are built in,
   with no subscription"), and OEM licences for brands
   ([get started](https://volumio.com/get-started/)). Roon sells a standalone subscription but
   its value is a metadata database plus its own server line. Navidrome and moOde are free with
   no paid tier at all. **Nobody in this category has built a subscription business on a LAN
   appliance alone.**
3. **The OEM/module layer is where the boring money is.** Arylic's products trace back to
   Linkplay modules ([openHAB LinkPlay binding](https://www.openhab.org/addons/bindings/linkplay/)),
   and Volumio runs an explicit OEM licensing programme with a volume-qualification form.
   That is proof a firmware+board licence market exists for exactly this category.
4. **Content is the highest-margin adjacency, and it is not available to xtape.** Yoto and
   Tonies monetise repeat content purchases; xtape's users already own their music, so there
   is no equivalent recurring SKU.

---

## 3. What the MCP surface is worth

### 3.1 The spec moved toward xtape, then commoditised it

- Current revision is **2026-07-28**; the revision list runs 2024-11-05 → 2025-03-26 →
  2025-06-18 → 2025-11-25 → 2026-07-28 ([versioning page](https://modelcontextprotocol.io/specification/versioning)).
- **2026-07-28 made MCP stateless**: it removed protocol-level sessions and the
  `Mcp-Session-Id` header, removed the `initialize`/`notifications/initialized` handshake
  (every request now carries protocol version and client capabilities in `_meta`), replaced
  the HTTP GET endpoint and `resources/subscribe` with `subscriptions/listen`, added
  `server/discover`, and **reclassified the old HTTP+SSE transport as legacy** — "remove SSE
  stream resumability and message redelivery… A broken response stream loses the in-flight
  request" ([2026-07-28 changelog](https://modelcontextprotocol.io/specification/2026-07-28/changelog)).
- The direction of travel started earlier: Anthropic describes the 2025-11-25 release as
  introducing "asynchronous operations, **statelessness**, server identity, and official
  extensions" ([Anthropic](https://www.anthropic.com/news/donating-the-model-context-protocol-and-establishing-of-the-agentic-ai-foundation)).

**This is genuinely good news for xtape's design.** A POST-only, no-SSE, sessionless
JSON-RPC endpoint was an odd choice in 2025 and is now the shape of the standard. xtape
implements revision 2025-11-25 while the world has moved to 2026-07-28 — one revision behind,
architecturally aligned, and cheap to update (the diff that matters is `server/discover` and
carrying version/capabilities in `_meta`).

### 3.2 …and then the chip vendor shipped an SDK for it

**Espressif publishes `espressif/mcp-c-sdk` (v2.0.1, Apache-2.0)** on its own component
registry: "MCP SDK for ESP32 devices. Streamable HTTP, tools, resources, prompts, completions,
and tasks (2025-11-05 target; legacy 2024-11-05 transport optional)", default protocol target
**2025-11-25**, with the same transport caveat xtape has — "POST is supported; GET is
currently 405 when SSE is not enabled"
([component page](https://components.espressif.com/components/espressif/mcp-c-sdk)).

Read that carefully: **the exact architectural choice that makes xtape's MCP endpoint
interesting is now a checkbox in the vendor's SDK.** The implementation work was not wasted
(it is done, it is smaller than the SDK, and it has no dependency), but it is not an asset.
A competitor with an ESP32-S3 and a weekend can match it.

### 3.3 Adoption is real, and it is server-side, not device-side

| Signal | Number | Source |
|---|---|---|
| Public MCP servers | **10,000+ "active public MCP servers"** | [Anthropic, MCP donation post](https://www.anthropic.com/news/donating-the-model-context-protocol-and-establishing-of-the-agentic-ai-foundation) |
| SDK downloads | **97M+/month** across Python and TypeScript (Anthropic's figure); npm `@modelcontextprotocol/sdk` alone at **87,026,138 downloads for the week ending 2026-10-29** — note this exceeds Anthropic's monthly figure, so npm is counting transitive/CI installs; treat both as order-of-magnitude | [Anthropic](https://www.anthropic.com/news/donating-the-model-context-protocol-and-establishing-of-the-agentic-ai-foundation), [npm API](https://api.npmjs.org/downloads/point/last-week/@modelcontextprotocol/sdk) |
| Registry | Live, with a versioned server schema (observed `2025-12-11`) and `streamable-http` remotes | [registry API](https://registry.modelcontextprotocol.io/v0/servers?limit=2), [repo](https://github.com/modelcontextprotocol/registry) (7,422 stars) |
| Reference servers repo | **91,702 stars** | [github.com/modelcontextprotocol/servers](https://github.com/modelcontextprotocol/servers) |
| Governance | Donated by Anthropic to the **Agentic AI Foundation**, a Linux Foundation directed fund co-founded with Block and OpenAI, supported by Google, Microsoft, AWS, Cloudflare, Bloomberg | [Anthropic](https://www.anthropic.com/news/donating-the-model-context-protocol-and-establishing-of-the-agentic-ai-foundation) |
| Client support | Adopted by ChatGPT, Cursor, Gemini, Microsoft Copilot, VS Code (per Anthropic); Claude has 75+ MCP connectors | same |

### 3.4 The killer detail: the obvious consumer doesn't speak xtape's dialect

Home Assistant is the single most likely consumer of an MCP endpoint on a LAN appliance. It
has **both directions**:

- **HA as MCP server:** the official `mcp_server` integration exposes an endpoint at
  `/api/mcp`, with OAuth 2.0, and "exposes all LLM APIs" to an agent
  ([docs](https://www.home-assistant.io/integrations/mcp_server/)).
- **HA as MCP client:** the `mcp` integration consumes external servers — but its
  configuration is literally a field named **"SSE Server URL"**, and the docs tell you to put
  a proxy in front of anything that only speaks stdio
  ([docs](https://www.home-assistant.io/integrations/mcp/)).

So the most plausible buyer of xtape's MCP surface runs a client that wants **SSE**, while
xtape serves **stateless POST** and returns 405 on GET. Unless HA's client has since grown
streamable-HTTP support (the docs I fetched on 2026-10-30 still describe SSE), **xtape's
`/mcp` does not connect to Home Assistant without an adapter.** That is a concrete, fixable,
and currently unfixed integration gap — and it is a better use of a week than another tool.

Home Assistant's install base is the relevant denominator for any integration-led angle:
**715,424 active installations** reporting analytics
([analytics.home-assistant.io](https://analytics.home-assistant.io/) `data.json`, fetched
2026-10-30). That is a large, technical, self-hosting audience — and one that already has a
universal remote.

### 3.5 Verdict

**MCP is a distribution hook and a demo, not a purchase driver.**

- Evidence *for* commercial value: none found. There is no survey, no price premium, no
  vendor case study showing a device sold because it had MCP. The 10,000-server ecosystem is
  overwhelmingly cloud/developer tooling, not consumer hardware.
- Evidence *against* differentiation: the silicon vendor ships the SDK; the transport that
  makes it cheap is now the default; 87M weekly npm downloads mean the client side is free and
  universal, i.e. it is table stakes.
- The honest framing for a landing page: "your LLM can DJ" is a **demo that gets a post
  shared**, which is worth something in marketing and nothing in margin.

**Decision:** ship `/mcp` as a free affordance, spend one day adding `server/discover` and
`_meta` version negotiation to reach 2026-07-28, add a streamable-HTTP/SSE bridge so HA can
actually connect, and then stop investing until an owner asks for a specific tool.

---

## 4. The self-hosted library segment

### 4.1 The denominator: streaming is the market

- Global recorded-music revenue reached **US$31.7 billion in 2025**, +6.4%, eleventh
  consecutive year of growth ([IFPI Global Music Report 2026](https://www.ifpi.org/global-music-report-2026/)).
- **837 million users of paid streaming subscription accounts**; streaming revenue passed
  **US$22 billion = 69.6%** of global recorded music income; paid subscription alone is
  **52.4%** of revenues; **vinyl grew 13.7%**, its 19th consecutive year of growth (same
  source).

The last two numbers are the interesting pair: subscription streaming owns the mass market,
while **physical ownership grew for nineteen straight years**. "Own your music" is not a
counter-trend to streaming — it is a durable minority taste that has outlived every
prediction of its death. That is exactly the taste xtape sells to.

### 4.2 The niche is measurable only by proxy

There is **no published market-size figure for self-hosted music servers**. Anything quoted
is an estimate. The best available proxies:

| Proxy | Number | Source |
|---|---|---|
| Navidrome GitHub stars | **24,449** | [repo](https://github.com/navidrome/navidrome) |
| Jellyfin GitHub stars (broader self-hosted media) | **58,834** | [repo](https://github.com/jellyfin/jellyfin) |
| Home Assistant active installs (self-hosted automation) | **715,424** | [analytics](https://analytics.home-assistant.io/) |
| Plex users | not verified in this pass | — |

Navidrome's own pitch is "free of charge", "plays well with gigantic music collections
(tested with ~900K songs)", and "OpenSubsonic API — compatible with the de facto standard
Music API, supporting all its clients and ecosystem"
([navidrome.org](https://www.navidrome.org/)). Translation: **the software layer is a solved,
free commodity with a standard API and dozens of clients.** Nobody in this segment pays for
the server; they pay for the box, the DAC, the screen, or the convenience.

### 4.3 Who actually pays

Three distinct wallets, in descending order of size:

1. **Audiophiles buying hardware.** HiFiBerry sells €25–€50 boards into this market and
   Volumio sells €799–€989 streamers into it. The willingness to pay is for *analogue
   quality and build*, not features.
2. **Home Assistant / homelab people buying convenience.** 715k installs, technical, allergic
   to subscriptions, and already accustomed to spending on Raspberry Pi-class hardware.
3. **Integrators and small venues buying zone count.** Arylic's HA400 4-zone amp at $2,499
   versus its $199 consumer streamer is the clearest evidence of where margin lives.

**Estimate (labelled as such):** a realistic serviceable niche for a €250–€400
cassette-styled streamer is on the order of **low thousands of units per year across all
vendors**, not tens of thousands. Plan for hundreds, celebrate thousands.

---

## 5. Unit economics of a small-batch device

### 5.1 Anchors that are actually verified

| Input | Verified figure | Source |
|---|---|---|
| Comparable electronics, retail, single unit | **$39.99** for an ESP32-S3 board with a **5" 800×480** display + touch (also a 4.3" 800×480 at $32.99) | [Waveshare ESP32-S3-Touch-LCD-5](https://www.waveshare.com/esp32-s3-touch-lcd-5.htm) |
| PCBA setup + joint | **"$8 setup + $0.0016 per solder joint"**, no setup fee promos, 24h assembly | [JLCPCB PCBA](https://jlcpcb.com/pcb-assembly) |
| Low-volume assembly alternative | Seeed Fusion turnkey assembly, published quote-based | [Seeed Fusion](https://www.seeedstudio.com/fusion_pcb.html) |
| Aluminium bridge tooling (SPI 105) | **$2,000–$5,000+** | [RapidDirect 2026 cost guide](https://www.rapiddirect.com/blog/injection-molding-costs/) (molder estimate) |
| P20 pre-hardened steel tool | **$5,000–$25,000+** | same |
| Hardened H13/420SS tool | **$50,000–$100,000+** | same |
| Resin cost | PP **$1.50–$3.50/kg**, ABS **$2.00–$4.50/kg**, PC **$3.00–$6.50/kg** | same |
| FCC certification path | SDoC for unintentional radiators (self-test, no TCB review) vs Certification (TCB review **~$2,000–$8,000** + test + filing fee "several tens of USD") | [Blue Asia Labs 2026 FCC cost breakdown](https://www.blueasialabs.com/cn/shouyehuandeng/fcc-certification-costs-2026-detailed-price-breakdown) (lab estimate) |
| CE / RED | **no verified 2026 price found.** Plan **$3,000–$8,000** per radio variant for EMC + RED testing and documentation; **unverified** | — |
| Payment processing | ~**3%** of revenue (standard card rate); verify current [Stripe pricing](https://stripe.com/pricing) for your entity | *unverified in this pass* |

Note the first row: **$39.99 retail, one-off, includes the panel, the touch controller, the
module, the PCB and connectors.** That single datapoint brackets xtape's electronics more
usefully than any BOM spreadsheet: at 500–1,000 units, a buyer with a real design would
expect to land the same electronics at **$25–$35**.

### 5.2 Bottom-up cost stack (planning figures)

Per-unit, USD. "100" assumes 3D-printed enclosure and hand assembly; "1,000" assumes
aluminium tooling and vendor PCBA.

| Line | @100 | @1,000 |
|---|---|---|
| SOC board (ESP32-S3 + 800×480 panel + touch), volume | 38 | 26 |
| Carrier PCB + DAC + jacks + encoder + microSD + USB-C + passives | 16 | 10 |
| PCBA setup/joints (JLCPCB) | 4 | 1.5 |
| Enclosure | 14 (printed) | 5 (moulded, tool amortised) |
| Final assembly, test, flashing | 12 | 7 |
| Packaging + printed insert (NOTICE text) | 3 | 1.5 |
| Inbound freight/duty | 2 | 1 |
| **COGS** | **89** | **52** |
| One-time: tooling | 0 | 4,000 |
| One-time: FCC SDoC + CE/RED (single variant) | 8,000 | 8,000 |
| One-time: fixtures, compliance/legal review | 2,300 | 3,000 |
| **One-time total** | **10,300** | **15,000** |

### 5.3 P&L at two plausible price points

| | 100 units @ $299 | 1,000 units @ $329 |
|---|---|---|
| Revenue | 29,900 | 329,000 |
| COGS | (8,900) | (52,000) |
| One-time, amortised in year 1 | (10,300) | (15,000) |
| Payment fees ~3.2% | (960) | (10,530) |
| Returns + warranty reserve 4% | (1,200) | (13,160) |
| Outbound freight (subsidised) | (1,000) | (10,000) |
| **Contribution before founder labour** | **7,540** | **228,310** |
| Contribution per unit | **$75** | **$228** |
| Founder labour (3h/unit @100; 1.5h/unit @1,000, at $50/h) | (15,000) | (75,000) |
| **Result** | **(7,460)** | **+153,310** |

**Three conclusions, and they are the whole section:**

1. **Break-even units ≈ one-time cost ÷ contribution ≈ 10,300 ÷ 75 ≈ 140 units** at a
   $299/100-unit cost structure. Below that, the project is a hobby that consumes cash.
2. **The 100-unit run loses money on a labour basis even when it "makes" $7.5k of
   contribution.** This is the trap boutique hardware founders fall into: gross margin looks
   like 25%, and the founders' 300 hours are invisible.
3. **Multiplier reality check.** The common "retail = 3–5× BOM" rule would put a $52 COGS
   device at $156–$260 — which is *below* where it must sit to pay for certification,
   returns, and support. Small-batch hardware needs **5–6× COGS**, or a much cheaper COGS via
   volume. The Waveshare datapoint (a $39.99 retail board, one-off) shows how thin the
   electronics margin is for a company that buys in millions: xtape cannot win on
   electronics cost, only on industrial design and story.

**Certification reality:** using a pre-certified ESP32-S3 module does **not** exempt the
finished product — it is still a radio product placed on the EU market (RED) and an
unintentional radiator in the US. What the module buys you is a *much* easier EMC story and a
smaller test matrix, not zero testing. (Espressif's module certification statements were not
fetched in this pass — *unverified* on the exact scope of what a module certificate covers.)

---

## 6. Legal floor

### 6.1 MP3: settled, no royalty

Fraunhofer IIS states plainly: **"On April 23, 2017, Technicolor's mp3 licensing program for
certain mp3 related patents and software of Technicolor and Fraunhofer IIS has been
terminated."** ([Fraunhofer IIS, mp3 consumer electronics page](https://www.iis.fraunhofer.de/en/ff/amm/consumer-electronics/mp3.html)).

The MPEG-1 Layer III standard dates to 1993; a 20-year patent term from the last relevant
filings lands around 2017, which is exactly what the programme termination reflects. **There
is no MP3 royalty to pay in 2026 and no licence to sign.** What remains is ordinary
attribution hygiene (do not imply Fraunhofer endorsement; do not use the Fraunhofer/Thomson
"MP3" logo — trademark status *unverified*, so avoid the logo).

### 6.2 `shine` (LGPL-2.0-only): one obligation, and it is documentable

`toots/shine` is LGPL-2.0-only — the 1991 "GNU Library General Public License, Version 2",
with no "or later" clause. The project already says so correctly in
`THIRD_PARTY_NOTICES` and `LICENSES/LGPL-2.0.txt`.

LGPL-2.0 §6 is the operative clause for static linking
([full text](https://www.gnu.org/licenses/old-licenses/lgpl-2.0.txt)):

> "…you may also compile or link a 'work that uses the Library' with the Library to produce a
> work containing portions of the Library, and distribute that work under terms of your
> choice, provided that the terms permit modification of the work for the customer's own use
> and reverse engineering for debugging such modifications. You must give prominent notice
> with each copy of the work that the Library is used in it and that the Library and its use
> are covered by this License. You must supply a copy of this License. […] Also, you must do
> one of these things: a) Accompany the work with the complete corresponding machine-readable
> source code for the Library […] and, if the work is an executable linked with the Library,
> with the complete machine-readable 'work that uses the Library', as object code and/or
> source code, so that the user can modify the Library and then relink […]; b) Accompany the
> work with a written offer, valid for at least three years, to give the same user the
> materials specified in Subsection 6a […] for a charge no more than the cost of performing
> this distribution; c) If distribution […] is made by offering access to copy from a
> designated place, offer equivalent access to copy the above specified materials from the
> same place; d) Verify that the user has already received a copy of these materials…"

**Concrete compliance checklist for a shipped device:**

1. **Notice** — in the box (printed insert) *and* on the device (About screen) *and* in the
   downloadable source bundle: that `shine` is used, that it and its use are covered by
   LGPL-2.0, and where to get the licence text. The project's `THIRD_PARTY_NOTICES` is the
   right artefact; it must reach the *customer*, not just the repo.
2. **Licence text** — ship `LICENSES/LGPL-2.0.txt` in the bundle and on a URL.
3. **Source of the library** — the exact `shine` tree plus the toolchain/sdkconfig needed to
   build it (unmodified vendored source is already the project's posture; that helps).
4. **Relink path** — either §6(a) object code of "the work that uses the Library" (i.e. the
   `.o` files of xtape's own firmware, or its source — MIT, so this is free to give) or a
   §6(b) three-year written offer. **The cheapest compliant answer is: publish the firmware
   source and the build recipe.** It costs nothing because the project is MIT anyway.
5. **Terms must permit modification and reverse engineering for debugging** — your sale terms
   cannot forbid reverse engineering. Write this into the EULA/ToS explicitly.
6. **Secure boot / flash encryption changes the answer.** If you lock the device so it can
   only run signed images, you must still provide a way for the user to run a relinked
   binary. On ESP32-S3 without secure boot, arbitrary images can be flashed over UART, so
   shipping build materials is sufficient. **If you enable secure boot, get advice.**

Also note §6's scope limit: it is the *library* that stays LGPL; xtape's own code stays MIT
and can be sold. There is no obligation to open-source the application.

### 6.3 The rest of the stack

| Component | Licence | What selling requires |
|---|---|---|
| **ESP-IDF** | **Apache-2.0** — verified by reading the repo `LICENSE` ("Apache License Version 2.0, January 2004") ([file](https://github.com/espressif/esp-idf/blob/master/LICENSE)) | Keep the NOTICE file, preserve attribution, comply with Apache §4 (state changes). Patent grant included. |
| **LVGL 9.6** | MIT ([repo](https://github.com/lvgl/lvgl)) | Attribution only. |
| **dr_libs** | public domain / MIT-0 | No notice required. |
| **Espressif audio components** (`esp_audio_codec`, `esp_hls_stream`, …) | "Espressif Modified MIT": **"for use EXCLUSIVELY with Espressif Systems products"**, redistribution "FOR USE WITH NON-ESPRESSIF PRODUCTS is strictly prohibited" | Fine on ESP32-S3; **blocks reuse of that code on any non-Espressif target**, including a host/simulation build. Keep them external registry deps, never vendored. (Already documented in `docs/research/02-packaging-and-licensing.md`.) |

**FSF compatibility caveat worth knowing:** the FSF lists Apache-2.0 as "compatible with
version 3 of the GNU GPL… not compatible with GPL version 2, because it has some requirements
that are not in that GPL version" — patent termination and indemnification
([gnu.org license list](https://www.gnu.org/licenses/license-list.html#apache2)).
LGPL-2.0 is a GPLv2-family licence. The mainstream reading is that §6 exists precisely to
permit linking a non-LGPL "work that uses the Library", so keeping `shine` unmodified,
separately built, and *not* merged with Apache-2.0 sources is the safe posture — which is
what the project does. **Flag for counsel if you ever restructure the build** (e.g. compile
`shine` into the same translation unit or object as ESP-IDF code).

### 6.4 AAC: the one codec to leave out

- Via LA administers an **Advanced Audio Coding (AAC)** patent pool covering MPEG-2 AAC,
  MPEG-4 AAC, HE-AAC, HE-AAC v2, xHE-AAC, AAC-LD/ELD
  ([Via LA AAC programme](https://www.via-la.com/licensing-programs/aac/)).
- The page states: **"An AAC patent license is needed by manufacturers or developers of
  end-user encoder and/or decoder products."**
- The fee schedule is behind a "License Fees" tab that did not yield numbers in the rendered
  page; **the per-unit rate is not published**. Historical MPEG-LA AAC rates quoted around
  the web (~$1/unit) are **stale and unverified** — plan as if the number is non-zero and
  requires a negotiation.

**Conclusion: do not ship AAC encode or decode.** MP3, Opus, Vorbis, FLAC and WAV are
unencumbered; AAC drags in a per-unit licence with an unpublished rate, on top of Espressif's
"Espressif products only" component licence. The existing `docs/research/server/s12-mp3-only-plan.md`
direction is correct on legal grounds.

### 6.5 GPL landmines

`fknrdcls/mp3_shine_esp32` is **GPL-3.0**. Compiling it into xtape would make the combined
firmware GPL-3.0 and force source disclosure of everything linked. The project deliberately
avoids it (`THIRD_PARTY_NOTICES`); keep it that way. Any future "let's just use this library
from GitHub" decision must check the licence first — the ESP32 audio ecosystem is full of
GPL-3.0 and "Custom" licences.

---

## 7. Ranked opportunities

Ranked by realistic **cumulative gross profit over 12–24 months for a 1–3 person team**, not
by ceiling. Revenue bands are my estimates from the unit economics in §5 and the price bands
in §2; treat them as planning numbers, not forecasts.

### 7.1 — Rank 1: the boutique device (sell the finished object)

- **What is sold:** one physical cassette-deck streamer, $299–$399, in a small batch.
- **Who pays:** the "own your music" audiophile/design-conscious buyer — the person who buys
  vinyl (19 years of growth, [IFPI](https://www.ifpi.org/global-music-report-2026/)) and a
  £159 cassette player ([We Are Rewind](https://www.richersounds.com/we-are-rewind-amy-black-yellow/)).
- **What has to be built:** the enclosure (the actual product), a manufacturable carrier PCB,
  a provisioning story for Wi-Fi credentials (currently compiled in — a blocker for shipping
  to strangers), an update path, and a returns process.
- **What blocks it:** no OTA and no provisioning are **hard blockers** for selling to people
  who are not you. Also: certification (~$8–11k), and the fact that a $299 box is compared
  against a $219 WiiM Pro Plus that has AirPlay 2.
- **Evidence someone pays:** the entire market map in §2; specifically We Are Rewind at £159
  for a cassette-shaped *non-streaming* player, and Yoto at $129.99 for a screen-less kids'
  box with a strong design identity.
- **Realistic gross profit:** **$25k–$120k** cumulative at 150–600 units/yr (see §5.3:
  $75–$228 contribution per unit before founder labour).

### 7.2 — Rank 2: integration, installation, and small-venue work

- **What is sold:** time — Home Assistant integration, multi-room setup, custom installs,
  "background music that isn't a Spotify account".
- **Who pays:** 715k HA households (a fraction will pay for help), small venues, galleries,
  cafés, barbershops.
- **What has to be built:** almost nothing new — a repeatable install playbook, a remote
  management story (a device with no auth on a venue LAN is a liability), and an invoice.
- **What blocks it:** **public performance licensing.** In the UK, "if you use, play or listen
  to recorded music in your business, the chances are you need a music licence"
  ([PPL PRS](https://www.pplprs.co.uk/)) — the same exists as GEMA/ASCAP/BMI elsewhere. You
  would be selling hardware into a compliance obligation, and venue owners often discover the
  licence cost after the hardware.
- **Evidence someone pays:** Arylic's $2,499 four-zone amplifier exists precisely for this
  channel ([catalogue](https://www.arylic.com/products.json)); Volumio's OEM/sales-network
  programme targets dealers ([Volumio OEM](https://volumio.com/business-space/oem/)).
- **Realistic gross profit:** **$10k–$60k**, heavily dependent on local presence — this is a
  services business wearing a hardware hat.

### 7.3 — Rank 3: the companion analysis app as a paid add-on

- **What is sold:** the `web/` tempo/key/mood/energy analysis, as a one-time unlock
  ($19–$39) or a bundled "Pro" firmware feature.
- **Who pays:** DJs and collectors with large local libraries — the same people who already
  pay for Mixed In Key-class tools (*price unverified in this pass*).
- **What has to be built:** a billing path, a licence key that the device can check **offline**
  (critical: a LAN appliance cannot phone home reliably), and a reason the analysis must live
  in the browser rather than in an existing free tool.
- **What blocks it:** Tunebat and similar free analysers; the feature is a commodity; and
  charging for it invites the "why isn't this free like Navidrome" reaction that the
  self-hosted audience specialises in.
- **Evidence someone pays:** the strongest in-category datapoint is **Volumio Premium** — a
  paid subscription for software features (TIDAL/Qobuz, multiroom, CD playback) that DIY users
  buy ([get started](https://volumio.com/get-started/)); the existence of a paid DJ-analysis
  category is weaker, indirect evidence.
- **Realistic gross profit:** **$5k–$40k**, mostly as an attach rate on Rank 1 (5–15% of
  owners at $29).

### 7.4 — Rank 4: kit / open hardware with paid "pro" firmware

- **What is sold:** a bare board or full kit ($99–$149) to the ESP32/homelab crowd, with the
  assembled device or pro features ($29–$59) as the upsell.
- **Who pays:** makers who would never spend $329 but will spend $129 and a Saturday.
- **What has to be built:** documentation, a BOM with available parts, a support forum, and a
  licensing scheme that does not leak the pro binary.
- **What blocks it:** HiFiBerry already occupies "cheap board + free OS" at €25–€50
  ([shop](https://www.hifiberry.com/shop/boards/dac2-pro/)), and the buyer who wants a kit is
  the buyer least likely to pay for firmware. Also: LGPL §6 gets *harder* when you ship a
  binary-only "pro" image on top of `shine` (you must still ship a relink path).
- **Evidence someone pays:** HiFiBerry's continued existence; Tindie/Pi Supply as channels.
- **Realistic gross profit:** **$5k–$30k**, but it is the fastest way to find out whether
  anyone wants the thing at all.

### 7.5 — Rank 5: OEM / white-label firmware licence

- **What is sold:** the firmware + UI + reference design, licensed to a small audio brand.
- **Who pays:** a brand with a case, a channel and no software team — Volumio's OEM form asks
  exactly this ("Whether you have your own hardware or software team",
  [Volumio OEM](https://volumio.com/business-space/oem/)).
- **What has to be built:** a second hardware target, a licence-key mechanism, QA processes,
  certification support, and a support SLA. Essentially a company, not a feature.
- **What blocks it:** no track record, no certification, no manufacturing partner, and a
  9–18 month sales cycle to a buyer who will compare you to Linkplay modules
  ([openHAB LinkPlay binding](https://www.openhab.org/addons/bindings/linkplay/)) already
  shipping in millions of units.
- **Evidence someone pays:** Volumio's OEM programme exists and qualifies leads by volume;
  Arylic/OEM module market exists. **But no evidence a 1–3 person team wins one without a
  shipped product first.**
- **Realistic gross profit:** **$0–$100k**, extremely long-tailed. Do this *after* Rank 1, as
  an inbound consequence of it.

### 7.6 — Rank 6: metadata and curation services

- **What is sold:** enriched metadata (MusicBrainz/AcoustID), artwork, curated "stations",
  maybe a printed booklet of the owner's library.
- **Who pays:** the same audiophile who pays for Roon's metadata layer — Roon's $14.99/mo is
  mostly a metadata and library-management product ([Roon pricing](https://roon.app/en/pricing)).
- **What has to be built:** an enrichment pipeline, disambiguation UX, and an offline cache
  (a LAN device cannot depend on a cloud call for its core screen).
- **What blocks it:** MusicBrainz and AcoustID are free and already good; the incremental
  value is in *curation taste*, which does not scale past one person.
- **Evidence someone pays:** Roon's pricing is the only hard evidence in the whole report that
  consumers pay recurring money for library intelligence.
- **Realistic gross profit:** **$2k–$15k**, best bundled into Rank 1's price (+$30) rather
  than sold standalone.

### 7.7 — Rank 7: sponsorship / open-source funding

- **What is sold:** nothing; you ask.
- **Who pays:** hobbyists and a few companies, at $5–$50/month.
- **What has to be built:** a credible public repo history and a demo video.
- **What blocks it:** the audiences that sponsor OSS (Navidrome, Jellyfin) are the audiences
  that expect hardware to be free too. `toots/shine` itself has just 423 stars
  ([repo](https://github.com/toots/shine)) — the shine ecosystem is not a funding well.
- **Evidence someone pays:** Navidrome and Jellyfin are donation-funded and free
  ([navidrome.org](https://www.navidrome.org/), 24k and 59k stars) — evidence the *model*
  works, not that it pays a hardware team.
- **Realistic gross profit:** **$1k–$8k**. Nice signal, not a business.

### 7.8 — Rank 8: the MCP-unique angle ("let your LLM DJ")

- **What is sold:** nothing directly — an affordance on the device, a demo, a viral post.
- **Who pays:** nobody, yet. See §3.5. The silicon vendor ships the SDK
  ([espressif/mcp-c-sdk](https://components.espressif.com/components/espressif/mcp-c-sdk)),
  the obvious consumer (Home Assistant) wants SSE, and there is no purchase-intent evidence
  anywhere.
- **What has to be built (if you do it anyway, cheaply):** `server/discover` + `_meta` version
  negotiation for 2026-07-28, an SSE/streamable bridge for HA, and one genuinely delightful
  tool ("play the 1997 mixtape").
- **What blocks it:** the aggregate remote-control role is already taken by Home Assistant's
  own MCP server (`/api/mcp`, [docs](https://www.home-assistant.io/integrations/mcp_server/)).
  An agent talks to HA; HA talks to devices. Being an MCP endpoint adds a hop, not a feature.
- **Evidence someone pays:** none found.
- **Realistic gross profit:** **~$0**. Strategic option value only.

---

## 8. Dead ends

| Dead end | Why it fails, with evidence |
|---|---|
| **DRM / copy protection for the library** | The user already owns the files; DRM adds nothing to the buyer and everything to support load. No competitor in the price band does it. |
| **Cloud transcoding / a hosted library service** | Streaming is a $22B business run at scale by Spotify/Apple/Amazon ([IFPI](https://www.ifpi.org/global-music-report-2026/)); bandwidth and storage costs scale linearly with users, and a 1–3 person team has no CDN. The project's own research already rules out the cloud-service direction. |
| **Competing on catalogue** | You cannot license 100M tracks. Plex/Navidrome succeed by *not* having a catalogue. |
| **Subscription for a LAN-only device** | Nothing on the device stops working when a subscription lapses, and the buyer knows it. The only subscription in the category (Roon) sells a continuously updated metadata database ([Roon](https://roon.app/en/pricing)), not access. Charge once. |
| **Ads in a hi-fi product** | The buyer paid $299 to avoid advertising. Ad revenue at 300 units is ~$0 anyway. |
| **AAC support** | Per-unit patent licence with an unpublished rate ([Via LA](https://www.via-la.com/licensing-programs/aac/)), plus Espressif's "Espressif products only" component licence. Zero upside, real cost. |
| **GPL audio components** | Turns the firmware GPL-3.0 and forces disclosure of everything. |
| **Charging for MCP** | Commodity SDK from the silicon vendor; no purchase-intent evidence; would be the first thing reviewers mock. |
| **Building a mobile app** | The device has a screen and a browser UI; a native app is a maintenance tax with no revenue line. |

---

## 9. The three cheapest falsifying experiments

Each is designed to *fail fast and cheap*, with an explicit kill threshold.

**E1 — Landing page + real preorder at $299 (cost ≈ $200, 2 weeks).**
One page, three product renders, a $20 deposit button (Stripe), $150 of ads targeted at
r/WiimStreamer-adjacent audiences, Home Assistant forums, and cassette/lo-fi communities.
- *Falsifies Rank 1 if:* fewer than **15 deposits in 14 days** at a $20 refundable deposit
  against ~2,000 qualified visitors (<0.75% conversion). Consumer hardware preorders at
  $300 typically need 1.5–3% to be interesting — **that benchmark is my estimate, not a
  verified figure.**
- *What it measures:* whether the *look* sells, which is the only defensible asset.

**E2 — Ship `/mcp` to ten Home Assistant power users (cost ≈ $0, 1 week).**
Post the endpoint in the HA community and r/selfhosted, add a `/mcp` hit counter that reports
distinct client IPs, and count how many people connect *twice* (a second connection is the
only honest signal of value).
- *Falsifies Rank 8 if:* fewer than **5 of 10** who ask for access actually connect a second
  time, or if nobody asks within a week of the post.
- *Bonus:* this experiment also tells you whether the SSE gap in §3.4 is real, because the
  first three respondents will hit it.

**E3 — Ask ten owners, and list one unit on Tindie at $349 (cost ≈ $80, 3 weeks).**
Ten structured conversations with WiiM/Volumio/Bluesound owners (what did you pay, what would
make you replace it, what would you pay for a cassette-shaped one), plus a single assembled
prototype listed at $349 with a 30-day lead time.
- *Falsifies Rank 1 and Rank 4 if:* **zero of ten** owners name "looks/feels" as a purchase
  reason (kills the design-object thesis), or the $349 listing gets **zero** orders in 21 days
  with >200 views (kills the price thesis).
- *Why $349:* it is the price at which §5.3 shows a 100-unit run can pay founder labour.

**Run them in parallel; total spend < $400 and < 4 weeks.** If all three fail, the honest
answer is that the project is a beautiful portfolio piece — which is a legitimate outcome, and
much cheaper to learn now than after a $10k tooling bill.

---

## 10. Sources

**Market and pricing** (all fetched 2026-10-30)
- [WiiM Pro Plus, Amazon US](https://us.amazon.com/dp/B0CC2HWC7N) — $219
- [WiiM Ultra, Amazon US](https://us.amazon.com/dp/B0D41YCLW8) — $329
- [Arylic product catalogue JSON](https://www.arylic.com/products.json) — S10+ $69 … HA400 $2,499
- [Volumio Rivo, €989](https://shop.volumio.com/product/rivo), [Volumio shop](https://shop.volumio.com/)
- [Volumio OEM licensing](https://volumio.com/business-space/oem/)
- [HiFiBerry DAC2 Pro, €44.90 (and related €24.90/€49.90 boards)](https://www.hifiberry.com/shop/boards/dac2-pro/)
- [Sonos Port, $499 at B&H](https://www.bhphotovideo.com/c/product/1502022-REG/sonos_port1us1blk_port.html)
- [Bluesound NODE ICON (store currency CAD)](https://www.bluesound.com/products/node-icon/)
- [Roon pricing](https://roon.app/en/pricing), [Nucleus One bundle $644.96](https://store.roonlabs.com/products/nucleus-one-with-roon-subscription/)
- [Plex plans](https://www.plex.tv/plans/); lifetime price change reported by [notebookcheck](https://www.notebookcheck.net/Plex-lifetime-pass-price-triples-to-750-as-monthly-subscriptions-cost-unchanged.1303078.0.html) (*secondary*)
- [Navidrome](https://www.navidrome.org/), [moOde](https://moodeaudio.org/), [dbPoweramp/Asset UPnP](https://dbpoweramp.com/purchase.htm)
- [Yoto catalogue JSON](https://www.yotoplay.com/products.json), [tonies.com US](https://tonies.com/en-us/)
- [We Are Rewind at £159, Richer Sounds](https://www.richersounds.com/we-are-rewind-amy-black-yellow/)
- [openHAB LinkPlay binding](https://www.openhab.org/addons/bindings/linkplay/)

**MCP**
- [Spec versioning / revision list](https://modelcontextprotocol.io/specification/versioning)
- [2026-07-28 changelog (statelessness, SSE removal)](https://modelcontextprotocol.io/specification/2026-07-28/changelog)
- [Anthropic: donating MCP to the Agentic AI Foundation](https://www.anthropic.com/news/donating-the-model-context-protocol-and-establishing-of-the-agentic-ai-foundation)
- [Official registry](https://registry.modelcontextprotocol.io/), [registry repo](https://github.com/modelcontextprotocol/registry), [servers repo](https://github.com/modelcontextprotocol/servers)
- [npm `@modelcontextprotocol/sdk` download stats](https://api.npmjs.org/downloads/point/last-week/@modelcontextprotocol/sdk)
- [Espressif `mcp-c-sdk` component](https://components.espressif.com/components/espressif/mcp-c-sdk)
- [Home Assistant MCP **server** integration](https://www.home-assistant.io/integrations/mcp_server/)
- [Home Assistant MCP **client** integration (SSE)](https://www.home-assistant.io/integrations/mcp/)
- [Home Assistant analytics data](https://analytics.home-assistant.io/)

**Market size**
- [IFPI Global Music Report 2026](https://www.ifpi.org/global-music-report-2026/) — $31.7B, 837M paid subs, streaming 69.6%, vinyl +13.7%
- GitHub stars via `https://api.github.com/repos/<owner>/<repo>`: navidrome 24,449; jellyfin 58,834; home-assistant/core 91,855; lvgl 25,056; esp-idf 19,392; toots/shine 423

**Manufacturing and certification**
- [JLCPCB PCBA pricing](https://jlcpcb.com/pcb-assembly) — "$8 setup + $0.0016 per solder joint"
- [Seeed Fusion](https://www.seeedstudio.com/fusion_pcb.html)
- [Waveshare ESP32-S3 5" 800×480 board, $39.99](https://www.waveshare.com/esp32-s3-touch-lcd-5.htm)
- [RapidDirect 2026 injection-moulding cost guide](https://www.rapiddirect.com/blog/injection-molding-costs/) (*vendor estimate*)
- [Blue Asia Labs 2026 FCC cost breakdown](https://www.blueasialabs.com/cn/shouyehuandeng/fcc-certification-costs-2026-detailed-price-breakdown) (*lab estimate*)
- [PPL PRS — music licensing for businesses](https://www.pplprs.co.uk/)

**Legal**
- [Fraunhofer IIS mp3 page — licensing programme terminated 2017-04-23](https://www.iis.fraunhofer.de/en/ff/amm/consumer-electronics/mp3.html)
- [LGPL-2.0 full text](https://www.gnu.org/licenses/old-licenses/lgpl-2.0.txt)
- [GNU license list — Apache-2.0 vs GPLv2 note](https://www.gnu.org/licenses/license-list.html#apache2)
- [Via LA AAC patent programme](https://www.via-la.com/licensing-programs/aac/)
- [ESP-IDF LICENSE (Apache-2.0)](https://github.com/espressif/esp-idf/blob/master/LICENSE)
- [LVGL](https://github.com/lvgl/lvgl), [toots/shine](https://github.com/toots/shine)
- Project-local: `THIRD_PARTY_NOTICES`, `LICENSES/LGPL-2.0.txt`, `docs/research/02-packaging-and-licensing.md`, `docs/research/server/s11-licensing-verified-and-packaging.md`

**Explicitly unverified in this pass** (do not quote these as fact): CE/RED test pricing; the
AAC per-unit rate; Asset UPnP's current price; Espressif module-certification scope; Stripe's
current US rate; any figure for the size of the self-hosted music-server market.

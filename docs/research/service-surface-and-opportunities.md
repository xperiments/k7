# What the appliance serves today, and what those services could be sold for

**Status:** research only; no source changed. Written 2026-10-30 against `12c83c5` plus the
working-tree UI changes.

> **Amended 2026-10-31.** The `/music/<rel>` download row in Part A is gone: the global
> premise in `docs/specs/CONTEXT.md` forbids serving or accepting a Track, so WebDAV and
> every track-download-shaped opportunity below are rejected. The rest of the survey (the
> library arena, MCP, the economics) is unaffected. Labels: **VERIFIED** = read in this repository at the cited line;
**EXTERNAL** = fetched from the cited URL on 2026-10-30; **ESTIMATE** = arithmetic with the
assumption stated; **UNVERIFIED** = could not confirm, and says so.

**The question:** what services does this firmware actually expose — MCP, HTTP, the library
"scan database", and the rest — and which of them is worth money?

**The answer in eight lines.**

1. **Eight real HTTP services on ten route slots** — the MCP endpoint takes three slots because
   GET and DELETE must answer 405 — plus two optional screen diagnostics, all on one port with no
   authentication (`README.md:232-242`). Icecast-compatible live MP3, byte-exact downloads, a
   prebuilt JSON and binary library view, play/rescan control, a stateless MCP endpoint.
2. **The library is the strongest engineering asset**: an immutable tree built once at boot into
   one PSRAM arena, served as JSON *and* as a versioned binary blob with a CRC, with atomic
   generations and reader leases (`docs/adr/0012-boot-built-library-arena.md`). A 64-track /
   2.28 GB card scans in **96 ms** on the board (`README.md:499`).
3. **MCP is architected right and commercially worth ~zero.** The protocol moved to stateless
   POST in 2025-11-25 / 2026-07-28 — exactly this design — but Espressif now ships an official
   Apache-2.0 `mcp-c-sdk`, so the endpoint is a commodity ([EXTERNAL][espressif-mcp]).
4. **It has no audio output.** There is no I2S, DAC or analog stage anywhere in the firmware; it
   is a *server*, not a deck. A DAC is researched (a note added to `docs/research/server/` today)
   but not built.
5. **It cannot be sold to strangers yet**: Wi-Fi credentials are compiled in, and there is no
   provisioning, no OTA and no auth (verified by absence: no OTA, SNTP or AP-mode calls anywhere,
   `WIFI_MODE_STA` only). That is an engineering gap, not a legal one.
6. **Hardware margin is the only real revenue line.** Prices: WiiM Pro Plus $219, Volumio Rivo
   €989, Sonos Port $499, We Are Rewind cassette player £159 ([EXTERNAL][wiim] [volumio] [sonos]
   [rewind]). A 100-unit run contributes ~$7.5k before founder labour — i.e. it loses money;
   break-even is ~140 units; 1,000 units at $329 is worth ~$153k after labour (**ESTIMATE**).
7. **The legal floor is cheap and mostly met.** MP3 royalties ended 2017-04-23
   ([EXTERNAL][fraunhofer]); the only obligation is LGPL-2.0 §6 for `shine`, satisfied by
   publishing this MIT firmware and its build recipe (`THIRD_PARTY_NOTICES:8-29`). AAC must stay
   out ([EXTERNAL][viala]).
8. **The analyser cannot be sold as it stands**: its ONNX weights are CC BY-NC (non-commercial)
   and the app is AGPL-3.0 (`web/NOTICE:19-24`, `web/LICENSE:1-3`).

---

# Part A — what the device serves today

## A1. The surfaces, at a glance

| Surface | Transport | Default? | What it is for | Source |
|---|---|---|---|---|
| `GET /` | HTTP/1.0, :8000 | yes | HTML index, Playlist order, now-playing; diagnostic page with no card | `mini_files.c:477`, `:179-255` |
| `GET /stream` | HTTP, upgraded to a continuous body | yes | Live MP3 broadcast, optional ICY metadata | `mini_icecast.c:417-430`, `:466-472` |
| `GET /music/<rel>` | HTTP/1.0 | yes | The original, untranscoded bytes | `mini_files.c:310-432` |
| `GET /api/library` | HTTP/1.0, JSON | yes | Prebuilt tree + counts + generation ETag | `library_http.c:80-83` |
| `GET /api/library.bin` | HTTP/1.0, octet-stream | yes | The arena itself: versioned header, CRC | `library_http.c:85-88` |
| `POST /api/play` | HTTP/1.0, `application/json` | yes | `{"path":"…"}`; switches at the next chunk | `mini_icecast.c:401-415` |
| `POST /api/rescan` | HTTP/1.0, `application/json` | yes | Starts a background rescan: 202 / 409 | `app_services.c:108-132` |
| `POST /mcp` | HTTP/1.0, JSON-RPC 2.0 | yes | MCP 2025-11-25, five read-only tools | `mcp.c:965-1210` |
| `GET`/`DELETE /mcp` | HTTP/1.0 | yes | 405 with `Allow: POST` — a slot each, not a service | `mcp.c:1171-1181` |
| `GET /api/screen.bmp` | HTTP/1.0 | **opt-in** | One 800×480 frame as BMP, 1,152,054 B | `screen_frame.c:113-154` |
| `POST /api/scene?n=0..9` | HTTP/1.0 | **opt-in** | Re-aims the diagnostic scene driver | `screen_frame.c:160-195` |
| mDNS `_mcp._tcp` | UDP 5353 | board only | Advertises `xtape.local:8000`, `path=/mcp` | `board_esp32.c:137-162` |

Route budget: `HTTP_MAX_ROUTES 12` — "the appliance's ten, plus the screen's two diagnostics"
(`http_server.h:37`). With the screen API enabled the table is **full**; a thirteenth
`http_server_register_route()` returns `ESP_ERR_NO_MEM` (`http_server.c:159`). In a default
build, **two slots are free**.

## A2. The HTTP core, and why it bounds everything that comes next

| Property | Value | Consequence |
|---|---|---|
| One listener, port 8000, `INADDR_ANY` | `http_server.c:404-424` | No admin port, no RTSP/ICY port, no IPv6 |
| `HTTP/1.0` + `Connection: close`, no keep-alive | `http_server.c:102-108` | One request per TCP connection; clients reconnect |
| No chunked encoding | `http_server.c:105` | Every body is `Content-Length` or close-delimited (the index and the screen use the latter) |
| Connection workers: 4 (cap 6), 10 KB stack each, pinned to core 1 | `http_server.c:454-459`; `appliance.c:156` | A 5th simultaneous request gets **503** (`http_server.c:435-440`) |
| Header block 1 KB, 2 s timeout | `http_server.h:31-36` | |
| POST body 8 KB; missing length → 411, wrong type → 415 | `http_server.h:34`; `http_server.c:292-341` | |
| Request target 512 B; method buffer 8 chars | `http_server.h:33,42` | An 8-character method (`PROPFIND`) is a **400 before routing** |
| `reason_phrase()` knows 200/202/400/404/405/411/413/414/415/500/503 | `http_server.c:46-62` | 403 and 409 print as `HTTP/1.0 403 Error` / `409 Error` (`mcp.c:968`, `app_services.c:121`) |
| No TLS, no CORS, no `HEAD`, no `Range`, no gzip | verified by absence | A browser cannot read any endpoint cross-origin; no resumable download |

## A3. The library is a boot-built database, not a file walker

This is the part that most resembles a "scan database", and it is deliberately more than one.

- **Scan once, at boot**, synchronously, before the listener starts (`app_services.c:217-256`).
  Children are sorted case-insensitively; directories and tracks interleave; the Playlist is the
  depth-first pre-order of tracks (`library.c:236-262`, `:747-759`). Board measurement: 64 tracks
  / 2.28 GB in 96 ms (`README.md:499`).
- **One arena, two views.** The tree lives in PSRAM as a versioned blob; `/api/library.bin`
  serves those bytes verbatim and `/api/library` serves a prebuilt JSON of the same generation
  (`library.c:658-723`, `:727-831`). Neither touches the card per request.
- **Generations and leases.** A rescan builds the next generation off to the side and publishes
  it atomically; readers keep the generation they started with until they release it
  (`library.c:148-218`). A slow 2 MB JSON read and a rescan coexist without a global lock.
- **Caps, and they are visible, not fatal:** 4 MiB of arena+JSON headroom, 20 000 tracks,
  128-byte names, 1025-byte absolute paths; hitting a cap sets `truncated` in both views and
  says so on the index (`library.h:28-31`; `library.c:935-972`; `mini_files.c:231-233`).
- **JSON schema:** `root, generation, count, bytes, state, truncated, error, tree` where a dir is
  `{id,type,name,path,children[]}` and a track adds `size,format`
  (`library.c:658-723`). `state` is one of `ok|empty|no_dir|no_card|error`.
- **What it is not:** no ID3/Vorbis metadata in any network surface, no artwork field, no
  last-modified time, no dedupe, no persisted playlists, no play counts. Tags and cover art exist
  only for the screen (`artwork/`; `README.md:1082-1086`). `library_search` is a **case-insensitive
  substring over the relative path**, not over metadata (`mcp.c:691-723`).

## A4. MCP, in detail

| Property | Value |
|---|---|
| Revision | `2025-11-25`, echoing `2025-06-18` / `2025-03-26`; anything else → 400 `-32602` |
| Transport | One endpoint, POST only, JSON only; no SSE, no sessions, no batching (`mcp.c:994-997`) |
| Lifecycle | `initialize`, `notifications/initialized` (202, empty), `ping`, `tools/list`, `tools/call` |
| Result envelope | `content:[{type:"text",text:"<JSON as a string>"}]`, `isError`; **no `structuredContent`** (`mcp.c:898-917`) |
| Access control | `Origin`/`Host` pin to the local IP or `xtape.local:8000`; absent `Origin` accepted (`mcp.c:940-963`) |
| Limits | 8 KB body, 128 JSON tokens, depth 16, 128 KiB of result text |

**Five tools by default** (`mcp.c:759-779`):

| Tool | Input | Returns |
|---|---|---|
| `library_stats` | — | state, tracks, directories, bytes, generation, truncated, scan_ms |
| `library_list` | `path?`, `depth? 1-8`, `limit? 1-200` | entries in Playlist order, recursively to depth |
| `library_search` | `query`, `limit? 1-50` | path-substring matches with size and format |
| `library_track` | `path` | one track object |
| `player_status` | — | title, file, path, elapsed/duration, L/R levels, generation, track_index |

**Four more with `XTAPE_MCP_CONTROL=1`** (`mcp.c:781-815`): `player_play` (path, or id+generation),
`player_skip`, `player_prev`, `library_rescan`.

**Which clients actually connect** (`docs/research/server/s13-library-tree-and-mcp.md:966-976`):
MCP Inspector and Claude Code yes; **Claude Desktop no** — its local config is stdio-only, so it
needs a bridge; Cursor unverified. And the Home Assistant client is **SSE-only** (see B2).

## A5. Three sharp edges found while verifying

- **Prefix routing is literal, and some handlers never compare the path.** `GET /stream/anything`
  starts a stream, `POST /api/playXYZ` plays, `POST /api/sceneX` re-aims the scene; the other
  handlers check exact equality. Harmless today, but any new route inherits the hazard.
- **There is no transport on the device in a default build.** Next and previous exist as
  `mini_icecast_skip/prev`, called only by the MCP control tools and the transport test; the
  screen says so itself ("there is no transport on the chrome at all", `screen_input.c:14-15`).
  The only way a default build changes track is `POST /api/play`, or the Menu row that resolves to
  `mini_icecast_play`. **A deck with no next button is a demo, not a product.**
- **Error bodies are inconsistent by route.** `/mcp` renders transport errors as JSON-RPC
  (`mcp.c:1183-1194`); `/api/play` and `/api/rescan` return the default HTML error page for the
  same 411/413/415. A client parsing JSON from the control API must handle both.

## A6. What the audio path exposes to the network

Very little, and that is a deliberate boundary: the stream is a **broadcast** — every client hears
the same moment, and there is no seek, pause or per-client queue (`README.md:1154`).

| Externally addressable | Not addressable |
|---|---|
| Which track plays (`/api/play`; play/skip/prev in control builds) | EQ bands and trim (screen knobs only) |
| Read-only status and levels (`player_status`) | Station rate/bitrate (compile-time) |
| — | Shuffle (screen Menu only) |
| — | Anything about the decoder, resampler or encoder |

## A7. What is NOT there

Each row is a code-verified absence, not a judgement — this is the list a reader of "ESP32 music
server with JSON and MCP" would wrongly assume exists.

| Assumed | Reality |
|---|---|
| Auth, API keys, users | None anywhere, by decision (`README.md:18-31`; `CONTEXT.md:412-416`) |
| TLS/HTTPS | Absent; no mbedtls/esp_tls use in `components/` |
| Range / resumable download / seek | Absent; always 200 + whole file |
| Keep-alive, chunking, gzip, `HEAD` | Absent |
| WebSocket, SSE | Absent, including for MCP |
| WebDAV, upload/write API | **Researched only** — a full note with a verdict, no code |
| UPnP/DLNA, AirPlay, Spotify Connect, Chromecast | Absent; only `_mcp._tcp` is advertised |
| Bluetooth, MQTT, Home Assistant integration | Absent (`CONFIG_BT_ENABLED` unset) |
| OTA updates | Absent; the partition table has no OTA slot (`partitions.csv`) |
| Wi-Fi provisioning / AP mode / captive portal | Absent; credentials compiled in |
| NTP / wall-clock time | Absent; nothing needs it |
| ID3/artist/album search, artwork over HTTP | Absent; path search only |
| Persisted playlists, queue, play counts, resume | Absent |
| Multi-room / zone grouping | Absent (`components/portable/groups/` is **UI** groups) |
| `/status`, `/metrics`, `/health` | Absent; telemetry is a 60 s log line |
| Audio output (DAC/analog) | Absent — no I2S code at all |

Also worth knowing: `/api/play` and `/api/rescan` change state with no auth and no CSRF
protection. A page on the LAN can fire them as `no-cors` POSTs; it cannot read the response, but
the side effect happens. MCP is the only surface that validates `Origin`.

## A8. What the next service would cost

The serving layer is closer to its ceiling than it looks:

- **Two free route slots** in a default build, none with the screen API on. WebDAV needs two and
  a wider method buffer.
- **No chunking** means every new response must be measured before it is streamed, or
  close-delimited like the index.
- **Four connection workers**, one of which a download holds for its whole duration. A long-poll
  or streaming service (SSE, WebDAV copy, metrics push) spends a worker permanently.
- **8 KB body cap and an 8-character method buffer** rule out large uploads and most WebDAV.
- **No auth seam.** Anything added is open to the LAN by construction; a token would have to be
  invented, and per the spec `/stream` must stay open anyway (`sd-stream-server.md:477-489`).

---

# Part B — what those services are worth

## B1. The market it would compete in

Prices fetched 2026-10-30; every figure is a vendor page, not a report.

| Product | Price | What the customer actually pays for |
|---|---|---|
| WiiM Pro Plus / Ultra | **$219 / $329** [EXTERNAL][wiim] | Hardware; app, AirPlay 2, Chromecast, DLNA are free |
| Arylic S10+ / LP100 / HA400 | **$69 / $399 / $2,499** [EXTERNAL][arylic] | Hardware; the 4-zone amp shows the integrator channel pays ~10× |
| Volumio Rivo | **€989** [EXTERNAL][volumio] | Hardware *with Premium bundled*; also OEM licensing to brands |
| Sonos Port | **$499** [EXTERNAL][sonos] | Hardware + ecosystem |
| HiFiBerry DAC boards | **€24.90–€49.90** [EXTERNAL][hifiberry] | Hardware; software (HiFiBerryOS) free |
| Roon | **$14.99/mo, $829.99 lifetime** [EXTERNAL][roon] | A metadata/DSP database that keeps updating |
| Navidrome / moOde | **free** [EXTERNAL][navidrome] | Nothing — 24k GitHub stars, donation-funded |
| Yoto / Tonies | **$129.99** + cards, figures $12–20 [EXTERNAL][yoto] | Hardware + recurring *content* |
| We Are Rewind cassette player | **£159** [EXTERNAL][rewind] | Hardware + nostalgia; no streaming at all |

**Structural read.** The price band is set by WiiM: $219 buys AirPlay 2, Chromecast, a real DAC
and a mature app. Above that, a device has to sell on *form*. Nobody in this category runs a
subscription on a LAN appliance alone — Roon's subscription buys a continuously updated database,
and Volumio's Premium is bundled free on Volumio hardware. **Software is given away; hardware and
brand channels are where revenue lives.**

## B2. MCP: architecturally right, commercially zero

- **The protocol moved to this design.** 2025-11-25 introduced statelessness; 2026-07-28 removed
  `initialize`, sessions and the SSE GET entirely ([EXTERNAL][mcp-changelog]). A POST-only,
  sessionless, tools-only endpoint is now the shape of the standard rather than an oddity.
- **…and the silicon vendor shipped the SDK.** `espressif/mcp-c-sdk` v2.0.1~1, Apache-2.0,
  target 2025-11-25, POST with `GET → 405`, ~27.6k downloads
  ([EXTERNAL][espressif-mcp], re-fetched for this note). A competitor with an ESP32-S3 and a
  weekend reproduces this surface. The work was not wasted — it is smaller and dependency-free —
  but it is not a moat.
- **Adoption is real and server-side.** 10,000+ public MCP servers and 87M+ weekly downloads of
  the TypeScript SDK ([EXTERNAL][anthropic-mcp]) — i.e. the *client* side is free and universal,
  which makes MCP table stakes, not differentiation.
- **The most likely consumer cannot connect.** Home Assistant's MCP client integration takes an
  "SSE Server URL" and polls an SSE endpoint ([EXTERNAL][ha-mcp]); this server returns 405 on GET
  and speaks no SSE. HA reports 715,424 active installations and 3% of them run that integration
  ([EXTERNAL][ha-analytics]) — so the integration gap is concrete, and fixing it is a week of
  bridge work, not a research project.
- **Claude Desktop cannot connect either**, for a different reason: its local config is stdio-only.

**Verdict: MCP is a distribution hook and a demo, not a purchase driver.** There is no survey, no
price premium and no vendor case study showing a device sold because it spoke MCP. "Your LLM can
DJ" is a post that gets shared; it is worth something in attention and nothing in margin.

## B3. Unit economics of the object

Anchors: a Waveshare ESP32-S3 board with a 5" 800×480 panel and touch retails at **$39.99**,
one-off ([EXTERNAL][waveshare]); JLCPCB PCBA quotes "$8 setup + $0.0016 per solder joint"
([EXTERNAL][jlcpcb]); FCC TCB review runs roughly $2,000–$8,000
([EXTERNAL][fcc], lab estimate); CE/RED has no published 2026 price — plan $3–8k per radio
variant (**UNVERIFIED**).

| | @100 units | @1,000 units |
|---|---|---|
| COGS (**ESTIMATE**) | $89 | $52 |
| One-time (tooling, certification, fixtures) | $10,300 | $15,000 |
| Price | $299 | $329 |
| Contribution per unit | **$75** | **$228** |
| After founder labour ($50/h) | **–$7,460** | **+$153,310** |

**Break-even ≈ 140 units.** A 100-unit run is a hobby that consumes cash; the binding constraint
is assembly and support labour, not the BOM. The common "retail = 3–5× BOM" rule under-prices
small-batch hardware, which needs 5–6× to pay for certification, returns and support.

**The missing product, not just the missing feature.** This box has no DAC, no amplifier and no
speaker: it serves audio over the network and draws a cassette on its screen. As a $299 object it
is therefore a *source* — and a source without an analog output is a hard sell next to a $499
Sonos Port that has one. A DAC is researched in a note added to `docs/research/server/` today: the
PCM seam is `emit_chunk()` (`mini_icecast.c:677-685`), one call before the encoder, so a sink there
would inherit the EQ and the level meter. Nothing is built, and pins are the constraint.

## B4. The legal floor

| Item | Position | What selling requires |
|---|---|---|
| MP3 | Programme terminated 2017-04-23 ([EXTERNAL][fraunhofer]); no pool lists it | Nothing on the encode side; avoid the MP3 logo |
| `shine` | **LGPL-2.0-only**, vendored unmodified (`THIRD_PARTY_NOTICES:8-29`) | Notice + licence text + source + relink path. Publishing this MIT firmware and its build recipe satisfies §6(a) and costs nothing |
| AAC | Via LA pool; the schedule in this repo's own research is **$0.98/unit for the first 500k plus a $15,000 initial fee** (`s1-aac-licensing.md:134-148`), while Via LA's public page publishes no numbers ([EXTERNAL][viala]) | **Do not ship it** — as `s12-mp3-only-plan.md` already concluded |
| ESP-IDF | Apache-2.0 | Keep NOTICE; state changes |
| `fknrdcls/mp3_shine_esp32` | GPL-3.0 | Never link it; it would GPL the firmware |
| `web/` analyser | **AGPL-3.0** code + **CC BY-NC** ONNX weights (`web/NOTICE:19-24`) | Not sellable as-is; requires an MTG commercial licence or different models |

One caveat beyond the licence texts: **enabling secure boot complicates LGPL-2.0 §6** — the user
must still be able to run a relinked library. Without secure boot, flashing over UART exists, so
shipping build materials is sufficient. Flag for counsel if that changes.

## B5. Ranked opportunities

Cumulative gross profit over 12–24 months for a 1–3 person team. Revenue bands are planning
figures from B1/B3, not forecasts.

| # | What is sold | Who pays | What blocks it | Gross profit |
|---|---|---|---|---|
| **1** | **The object** — a $299–399 cassette-deck *source*, small batch | Design-conscious "own your music" buyers — the vinyl buyer (19 years of growth, [EXTERNAL][ifpi]) and the £159 cassette buyer | **No provisioning, no OTA, no auth**; no DAC; no next/previous outside a control build; certification ~$8–11k; a 100-unit run loses money | **$25k–$120k** |
| **2** | **Installs and small-venue work** — HA integration, multi-room, background music | 715k HA households, cafés, galleries, barbershops | Public-performance licensing sits on the *customer* (PPL PRS and equivalents, [EXTERNAL][pplprs]); a no-auth box on a venue LAN is a liability | **$10k–$60k** |
| **3** | **The analyser as an attach** — $29 unlock, 5–15% of owners | DJs and collectors with local libraries | CC BY-NC models + AGPL; Tunebat-class free alternatives; "why isn't this free like Navidrome" | **$5k–$40k** |
| **4** | **Kit / open hardware** at $99–149 with a paid assembled upsell | Makers who will not spend $329 | HiFiBerry owns "cheap board + free OS"; kit buyers are the least likely to pay for firmware | **$5k–$30k** |
| **5** | **OEM/white-label firmware licence** | A brand with a case, a channel and no software team | No track record, no certification, 9–18 month cycles, Linkplay modules already ship in volume | **$0–$100k**, long tail |
| **6** | **Metadata/curation** (MusicBrainz/AcoustID, artwork) | The Roon buyer | Free, good, already available; curation does not scale past one person | **$2k–$15k**, better as a +$30 bundle |
| **7** | **Sponsorship** of the open repo | Hobbyists, a few companies | The OSS audience expects free hardware | **$1k–$8k** |
| **8** | **MCP/AI-DJ as a product** | Nobody, yet | Commodity SDK, no purchase intent, HA wants SSE | **~$0** |

**Two assets that are not products.** The `portable/` + `host/` split is genuinely rare: one set of
C sources compiled both for the ESP32-S3 and for macOS, with the whole HTTP surface, the library
arena and the encoder pinned by host tests (`host/Makefile:1-9,130-149`). That is an engineering
credential — it sells consulting and OEM conversations, not licences. And the mini MCP
implementation, however clean, is now a checkbox on Espressif's component registry; do not
package it as a product.

## B6. Dead ends

DRM for files the owner already has. Cloud transcoding (streaming is a $22B, 69.6%-of-revenue
business run at CDN scale, [EXTERNAL][ifpi]). Competing on catalogue. A subscription for a
LAN-only box — nothing stops working when it lapses and the buyer knows it. Ads in a hi-fi
product. AAC. GPL audio components. Charging for MCP. A native mobile app for a device that
already has a screen and a browser UI.

## B7. The three experiments that would settle it (<$400, <4 weeks)

1. **Landing page + a real $20 refundable deposit at $299.** Kill if <15 deposits from ~2,000
   qualified visitors in 14 days (that conversion benchmark is an **ESTIMATE**).
2. **Ship `/mcp` to ten Home Assistant power users and count *second* connections.** Kill if <5
   of 10 reconnect within a week. This also surfaces the SSE gap, because the first three will
   hit it.
3. **Ten owner interviews + one prototype on Tindie at $349.** Kill if zero of ten name
   "looks/feels" as a purchase reason, or the listing gets no orders in 21 days above 200 views.

---

# Part C — what I would do next, in order

1. **Decide what is being sold.** If the goal is revenue, it is the object or an integration
   service; software attached to this device has no standalone market (B1). If the goal is a
   portfolio and a demo, stop here and keep the firmware free.
2. **Close the "saleable to strangers" gap before anything else**: provisioning (SmartConfig or a
   tiny captive portal), an OTA partition with rollback, and a bearer-token option for
   `/api/play`, `/api/rescan` and `/mcp`. **ESTIMATE:** 2–3 weeks for a careful version; this
   matters more than any feature on the list.
3. **Decide on the DAC and the transport.** Without analog output the product is a server with a
   screen; without a next button it is a demo. The I2S research note says the DAC seam is one
   function deep and pins are the constraint; the transport needs one of: a physical control on
   the chrome, an HTTP `POST /api/next`, or `XTAPE_MCP_CONTROL` shipped by default. Build both
   only if opportunity 1 is real — but until then, the device cannot be demonstrated as a deck.
4. **Spend at most a week on MCP, and make it connect**: the 2026-07-28 era
   (`server/discover`, `_meta`) plus an SSE/streamable bridge so Home Assistant works. Do it in
   the host bridge (`tools/xtape-mcp.mjs`, as sketched in
   `docs/research/server/s13-library-tree-and-mcp.md:949-976`), not in firmware, so protocol
   churn stays out of the device.
5. **Keep MCP and the library API free.** They are distribution, not product. Never gate them,
   never charge for them.
6. **Before any sale**: ship `THIRD_PARTY_NOTICES`, `LICENSES/LGPL-2.0.txt`, the firmware source
   and the build recipe; keep `shine` separate and unmodified; write terms that permit
   modification and reverse-engineering for debugging.
7. **Leave the analyser alone commercially** until either the MTG licence is bought or the models
   are replaced. It is the one asset in the repository that cannot be monetised as it stands.

---

## Sources and where the detail lives

**This repository.** Route contracts and limits: `components/portable/http_server/`,
`mini_icecast/`, `mini_files/`, `library_http/`, `mcp/`, `app_services/`. Library model:
`docs/adr/0012-boot-built-library-arena.md` and `docs/research/server/s13-library-tree-and-mcp.md`.
Deployment posture and limitations: `README.md:18-31`, `:232-242`, `:1137-1165`. Access model and
licensing requirements: `docs/specs/sd-stream-server.md:470-512`. Codec and patent matrix:
`docs/research/server/s6-codec-licensing-matrix.md`. WebDAV (research only; the note was
renumbered during this session and currently sits at `docs/research/server/s15-webdav.md`).
The longer code-verified inventory this note condenses — 1,100 lines, one `file:line` per claim —
is in `.scratch/service-surface/current-services.md`; that directory is gitignored, so fold it
into `docs/research/` if it is worth keeping.

**External** (fetched 2026-10-30; prices are the vendors' own pages):

[espressif-mcp]: https://components.espressif.com/components/espressif/mcp-c-sdk
[ha-mcp]: https://www.home-assistant.io/integrations/mcp/
[ha-analytics]: https://analytics.home-assistant.io/
[mcp-changelog]: https://modelcontextprotocol.io/specification/2026-07-28/changelog
[anthropic-mcp]: https://www.anthropic.com/news/donating-the-model-context-protocol-and-establishing-of-the-agentic-ai-foundation
[wiim]: https://us.amazon.com/dp/B0CC2HWC7N
[arylic]: https://www.arylic.com/products.json
[volumio]: https://shop.volumio.com/product/rivo
[sonos]: https://www.bhphotovideo.com/c/product/1502022-REG/sonos_port1us1blk_port.html
[hifiberry]: https://www.hifiberry.com/shop/boards/dac2-pro/
[roon]: https://roon.app/en/pricing
[navidrome]: https://www.navidrome.org/
[yoto]: https://www.yotoplay.com/products.json
[rewind]: https://www.richersounds.com/we-are-rewind-amy-black-yellow/
[fraunhofer]: https://www.iis.fraunhofer.de/en/ff/amm/consumer-electronics/mp3.html
[viala]: https://www.via-la.com/licensing-programs/aac/
[waveshare]: https://www.waveshare.com/esp32-s3-touch-lcd-5.htm
[jlcpcb]: https://jlcpcb.com/pcb-assembly
[fcc]: https://www.blueasialabs.com/cn/shouyehuandeng/fcc-certification-costs-2026-detailed-price-breakdown
[pplprs]: https://www.pplprs.co.uk/
[ifpi]: https://www.ifpi.org/global-music-report-2026/

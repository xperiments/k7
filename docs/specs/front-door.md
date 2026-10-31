# The front door: the analysis app, served by the device

**Goal.** `https://xtape.local` is the one address. With a card in the player it shows the
**Player view**; with the card out it shows the **Prepare view** — the analysis page that
measures the card and writes `xtape.db` back to it. The app's own code ships in the firmware;
the 38 MB of models and runtimes are fetched once and cached on the computer that runs it.

The decision, its evidence and its rejected alternatives are
[ADR 0048](../adr/0048-the-device-serves-the-analysis-app.md). Vocabulary is
[`CONTEXT.md`](./CONTEXT.md): **Front door**, **Player view**, **Prepare view**, **Card state**,
**Shell**, **Weights**.

**Status:** decided, not built. Nothing below exists in the firmware yet.

---

## 1. What is served, and from where

| Piece | Bytes | Lives |
|---|---:|---|
| `index.html`, app bundle, `wasi-adapter`, `ort.mjs`, `sw.js`, manifest, icons | **≈ 0.47 MB** | 1 MB SPIFFS partition at `0x610000` |
| Essentia (`essentia-wasm.es.js`, 2.56 MB) | 2.6 MB | jsDelivr, pinned tag, sha256-checked |
| TagLib wasm ×2 | 1.4 MB | jsDelivr |
| ONNX Runtime wasm | 14 MB | jsDelivr |
| Models ×8 | 24 MB | jsDelivr |

**F1.** `web/dist` is packed by `spiffs_create_partition_image` into the `frontdoor` partition,
so `idf.py flash` writes the firmware and the page together. The build logs the app commit it
packed.

**F2.** The **Shell** is our code and nothing else. The worker is built with Essentia
external: it loads the module from the CDN, not from the bundle.

**F3.** The **Weights** are pinned to a tag and verified against the manifest's `sha256`
before use. JavaScript modules are fetched as text, checked, and imported from a blob URL;
`ArrayBuffer` artefacts are checked before they reach ONNX Runtime. Verification happens on
every cold load, not only the first.

---

## 2. Origins and routes

**R1.** `https://xtape.local` (port 443) serves the app at `/`, the JSON API at `/api/*`, MCP at
`/mcp`, and the index at `/library`.

**R2.** `http://xtape.local` (port 80) serves `/stream`, plain and unchanging, and answers every
other path with `301` to the same path on HTTPS. No TLS on 80, ever: hardware players and car
head units cannot trust a private CA.

**R3.** `/` stops being the index. The index moves to `/library` unchanged — the diagnostic
that needs no JavaScript and no certificate.

**T1.** TLS is `esp_https_server`, behind the existing `http_server.h` public API. The device
serves a certificate and key compiled in from a gitignored header
(`components/app/board_esp32/tls_credentials.h`), issued by a private CA for `xtape.local`.
The device needs no clock: the client validates the dates.

**T2.** Two `httpd` instances, one per listener, each with its own handle table. A route is
registered against a listener, and every existing route needs its listener named.

**T3.** The host tier keeps the hand-written loop as the other backend of the same
`http_server.h`, so the route handlers and the integration checks are shared. TLS and keep-alive
are board-only behaviour and are not asserted by the host suite.

---

## 3. The page

**P1.** A service worker precaches the Shell on install, so the app opens with the device
switched off and says the card is unreachable.

**P2.** The Weights are cache-first, in a cache whose name carries the app version and the
manifest hash. A model swapped at the same URL is a cache miss, not a stale hit.

**P3.** `index.html` and `sw.js` are served `no-cache`; hashed `assets/*` are `immutable`.
`/api/*` is never cached.

**P4.** The page is installable: a manifest with the product's name and icons, served from the
same partition.

**P5.** An update is offered, never imposed: a new service worker waits for a reload rather
than replacing the page under a running analysis.

---

## 4. The two views

**S1.** `GET /api/status` returns the **Card state**, the library's track count, and what the
broadcast is playing. The page polls it every 5 s. It is `no-store`.

**S2.** Card in → **Player view**. Card out → **Prepare view**. The view is chosen by the Card
state at load.

**S3.** A change of Card state switches the view only when the queue is idle. Mid-analysis the
page stays and shows a banner offering the switch. A card that comes back does not throw away a
running analysis.

**S4.** The Player view is TBD; until it exists it is a placeholder that says the card is in and
names what is playing.

---

## 5. What this does not change

- The cassette on the panel, the Snapshot flow, the MCP tools and `/api/library`.
- The global premise: the device still serves no Track. The page it serves is not a Track, and
  the card still leaves only as the live stream.
- The host build's composition: it runs the same services, on the hand-written backend.

---

## 6. Failure modes

| Condition | Behaviour |
|---|---|
| No certificate in the build | The TLS listener refuses to start; the device still serves the stream on 80 and logs why |
| Client does not trust the CA | The browser warns; the page cannot run (no secure context) — the CA install is documented as a prerequisite |
| CDN unreachable, cache cold | The Prepare view says the analyser cannot start without a first download that needs internet |
| CDN unreachable, cache warm | The app runs from Cache Storage |
| A model's hash does not match | It is refused, named in the log and on the page, and the analysis does not start |
| Card pulled mid-analysis | The queue's current file fails; the page keeps the view and says why |
| Device unreachable on an installed app | The shell opens from cache; the Player view says the device is off |

---

## 7. Build and flash

**B1.** `cd web && npm run build`, then `idf.py build flash`. The SPIFFS image is generated by
CMake from `web/dist`; a missing `web/dist` fails the firmware build rather than shipping an
empty page.

**B2.** The certificate and key are produced by `mkcert` for `xtape.local`, written to the
gitignored header, and rebuilt into the firmware when they change.

---

## 8. Tests

| # | Test | Tier |
|---|---|---|
| T1 | The two backends answer the same routes with the same bodies | host |
| T2 | `/` serves the app, `/library` serves the index, unknown paths 404 | host |
| T3 | The SPIFFS image contains `index.html`, `sw.js` and the hashed assets, and `sw.js` is served `no-cache` | board |
| T4 | A client that trusts the CA loads the page over HTTPS and the page reports a secure context | board |
| T5 | `/stream` is reachable over plain HTTP on 80 while the app is on 443, and 80 redirects everything else | board |
| T6 | `/api/status` reports the card state within one poll of a removal and a reinsertion | board |
| T7 | A cold cache loads 38 MB once; a warm cache starts the analyser with the network off | board |
| T8 | A tampered model is refused, by hash | host (unit) |

---

## 9. Open measurements

1. TLS handshake cost and heap on the S3, beside the encoder and the panel.
2. Two listeners' stacks and buffers against the 35 KB of internal RAM that is free during
   playback.
3. First-load time of 38 MB over the LAN, and whether the PWA's cache makes the second load
   invisible.
4. SPIFFS mount time and the read cost of a 0.47 MB shell.
5. Whether `showDirectoryPicker` and the service worker behave identically on the device's
   origin as they do on `localhost` — the CA install is the variable.

# The device serves the analysis app, and the weights come from elsewhere

The owner asked for one address: `xtape.local` should be the thing you open, and it should
know whether the card is in the player. Card in → the player view. Card out → the analysis
page that measures the card and writes `xtape.db` back to it.

The analysis page is `web/`, and it does not fit. Built, it is **43 MB**: 24 MB of ONNX
models, 14 MB of ONNX Runtime, 1.4 MB of TagLib, 2.56 MB of Essentia with its wasm inlined as
base64 — and **0.47 MB that is ours** (`index.html`, the app bundle, the WASI adapter, the ORT
module glue). The device has **9.9 MB** of free flash. What looked like a weight problem was
two problems: the weights, and an origin.

The origin one is fatal if ignored. `http://xtape.local` is not a **secure context**, and the
page needs three things that are secure-context only: `crypto.subtle` for the content hash
every analysis is keyed by, `showDirectoryPicker` for reading and writing the card, and
Service Workers for the cache that makes 38 MB of weights tolerable. Serving the app over
plain HTTP would load a page that fails on its first file.

## Decision

- **The device serves the app's own code; the weights are fetched and cached.** A 1 MB SPIFFS
  partition at `0x610000` holds `web/dist` minus `models/` and `ort/` — 0.47 MB. Essentia,
  TagLib, ONNX Runtime and the models come from jsDelivr at a pinned tag, verified against the
  manifest's `sha256` before use, and cached by a service worker.
- **HTTPS on 443, with a certificate from a private CA.** `mkcert` issues for `xtape.local`;
  the certificate and key are compiled in from a gitignored header, the way
  `wifi_credentials.h` is. The device needs no clock — the browser checks the dates. Port **80
  stays plain** for `/stream`, because a car head unit will never trust a private CA; every
  other path on 80 redirects to HTTPS.
- **`esp_https_server` on the device, behind the existing `http_server.h`.** TLS, keep-alive and
  chunked bodies come from IDF; the handle-based route table, the handler code and the 102
  integration checks stay where they are. The host tier keeps the hand-written loop as the
  second backend of the same API, so neither tier forks the handlers.
- **The page is a PWA.** The worker precaches the shell so the app opens offline; the weights
  are cache-first under a cache name carrying the version and the manifest hash; `sw.js` and
  `index.html` are `no-cache`, hashed assets `immutable`; `/api/*` is never cached.
- **Two views, one fact.** `GET /api/status` reports the **card state** (and the library and
  what is playing), polled every 5 s. Card in → **Player view**; card out → **Prepare view**.
  A change of card state switches the view only when nothing is running; mid-analysis the page
  stays and a banner offers the switch.
- **`/` stops being the index.** The app is `/`; the index moves, unchanged, to `/library` —
  the device's diagnostic surface that works without JavaScript and without the CA.

## Why the weights are not in flash

Even if the models were the only thing to place, 24 MB does not fit beside a 4 MB app and a
2 MB snapshot on a 16 MB part. Shrinking the shell is worth doing on its own terms — 0.47 MB
instead of 4.6 MB, because the worker's 2.6 MB is `essentia-wasm.es.js` with the wasm inlined,
not our code — but it is the models that force the network.

The cost is honest and named: **a cold cache without internet means no analysis.** The shell
opens, says so, and offers the player view if the card is in. Every later load is local.

## Why the weights are verified, not just pinned

A pinned URL is not integrity. The three JavaScript modules are fetched as text, checked
against the manifest, and imported from a blob URL — so the 2.6 MB of Essentia that would
otherwise be frozen into flash is checked *on every cold load*, which is stricter than
shipping it. ONNX Runtime and the models take `ArrayBuffer`s and are checked the same way
before they reach the runtime.

## Considered options

- **A thin front door, analyser stays on the Mac.** The least work and no certificate. Rejected
  by the owner: the app is meant to live on the device, and one address is the point.
- **Serve everything from the device.** 43 MB against 9.9 MB. Impossible, not a trade-off.
- **Serve the weights from the card.** The card is out of the device exactly when the page
  needs the weights.
- **Plain HTTP with a download fallback.** The page cannot hash a file, so it cannot analyse at
  all. A non-starter rather than a degradation.
- **One origin for everything, stream included.** Simpler server, and it breaks every hardware
  player on the network at the moment the certificate arrives.
- **Embed the shell in the app image.** Fits, and every web tweak re-flashes the 4 MB app; no
  cache headers by filename, and the app grows for a page it only serves.
- **A custom mmap'd pack instead of SPIFFS.** Faster and smaller, for a bespoke format, a
  packer, and a reader to get wrong — over 0.47 MB.
- **Keep a service worker out of it and cache in IndexedDB.** Works, and loses the offline
  shell that a PWA gives for the same effort.

## Consequences

- **The analyser is AGPL-3.0-only and now ships inside a MIT firmware.** It already ships as
  source in this repository; the SPIFFS image carries its licence and the notice with it, and
  the boundary is the partition, not the binary.
- **Two listeners, two route tables.** `http_server.h` grows a notion of which listener a route
  belongs to; the broadcast's stream route lives on the plain one, the app and MCP on TLS.
- **The certificate is a per-client install.** Any machine that will open the page needs the
  CA. That is the price of a secure origin on a LAN appliance, and the alternative is a real
  domain and a public certificate.
- **The firmware build depends on `npm run build`.** `spiffs_create_partition_image` packs
  `web/dist`; a stale image is a stale page, so the build says which commit of the app it
  packed.
- **`library_stats` and the index stay.** The device's own views are unaffected; `/library`
  is the index at its new address.
- **Not yet built.** This records the decision; no firmware serves the app yet, and the board
  tier — TLS memory, the handshake's cost beside the encoder, the first-load time of 38 MB over
  the LAN — is unmeasured.

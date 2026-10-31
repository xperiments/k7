# The front door: the analysis app, served by the device

**Goal.** `http://xtape.local` is the one address. With a card in the player it shows the
**Player view**; with the card out it shows the **Prepare view** — the analysis page that
measures the card and writes `xtape.db` back to it. The app's own code ships in the firmware;
the 38 MB of models and runtimes are fetched once and cached on the computer that runs it.

The decision, its evidence and its rejected alternatives are
[ADR 0048](../adr/0048-the-device-serves-the-analysis-app.md). Vocabulary is
[`CONTEXT.md`](./CONTEXT.md): **Front door**, **Player view**, **Prepare view**, **Card state**,
**Shell**, **Weights**.

**Status:** built and running on the board, over **plain HTTP** — no certificate, no trust
step, superseding the TLS sections of this document (see
[ADR 0049](../adr/0049-the-front-door-is-plain-http.md)). `http://xtape.local/` serves the
Shell from its own flash, `/api/status` answers, `/library` is the index, `/stream` is beside
them, and `POST /api/snapshot` takes the image the browser built and flashes it. The card stays
in the computer. What is not proven: the browser flows end to end (no automated browser), the
Player view is a placeholder, the cold/warm weight cache, and §9's measurements. The Shell is
816 KiB.

---

## 1. What is served, and from where

| Piece | Bytes | Lives |
|---|---:|---|
| `index.html`, app bundle, worker, `wasi-adapter`, `ort.mjs`, `sw.js`, manifest, icons, `shell.json`, `weights.lock.json`, `setup.html`, `ca.pem` | **816 KiB** | 1 MB SPIFFS partition at `0x610000` |
| Essentia (`essentia-wasm.es.js`, 2.56 MB) | 2.6 MB | jsDelivr, pinned tag, sha256-checked |
| TagLib's one browser wasm | 0.7 MB | jsDelivr |
| ONNX Runtime wasm | 14 MB | jsDelivr |
| Models, nine of them | 23.9 MB | jsDelivr |
| **Weights together** | **≈ 41 MB** | fetched once, cached |

**F1.** `web/dist-shell` is packed by `spiffs_create_partition_image` into the `frontdoor`
partition, so `idf.py flash` writes the firmware and the page together, and the build logs the
app commit it packed. The pack is a **configure-time** decision: after building the Shell the
build directory must be reconfigured. A clean checkout without a Shell still builds — the device
is useful without a page, with `/` answering `503` naming `/library` — and a build that is
meant to ship one passes `-DXTAPE_REQUIRE_SHELL=1`, which turns a missing or empty Shell into a
configure error.

**F2.** The **Shell** is our code and nothing else. The worker is built with Essentia
external: it loads the module from the CDN, not from the bundle.

**F3.** The **Weights** are pinned to a tag and verified against the manifest's `sha256`
before use. JavaScript modules are fetched as text, checked, and imported from a blob URL;
`ArrayBuffer` artefacts are checked before they reach ONNX Runtime. Verification happens on
every cold load, not only the first.

---

## 2. Origins and routes

**R1.** `http://xtape.local` is the device's only address and its only port. One listener
serves the app at `/`, the JSON API at `/api/*`, MCP at `/mcp`, and the index at `/library`.
Unknown paths are `404`: the page is one screen with no client-side routes.

**R2.** `/stream` is on the same port, byte-identical to what a radio has always received. It
was on 80 alone while the app was on 443; with one listener it is simply another route.

**R3.** `/` stops being the index. The index moves to `/library` unchanged — the diagnostic
page when no card is mounted, and the listing of a mounted one.

**U1.** `POST /api/snapshot` takes an `application/octet-stream` body — the `xtape.db` image —
validates it in full before erasing anything, writes the snapshot partition, reads it back, and
restarts in 1.5 s so the reply lands first. A refusal leaves the running snapshot untouched. The
content type is also the CSRF guard: it is not CORS-safelisted, so another origin cannot send it
without a preflight the device never answers, and a form cannot produce it.

**U2.** There is **no TLS**. No certificate is compiled in, no trust step exists, and a browser
shows "Not secure" and no interstitial. The consequences are accepted, not hidden: no service
worker and no installable PWA, no `crypto.subtle` (a pure-JS SHA-256 replaces it), and no
directory picker (replaced by the upload). See ADR 0049.

## 3. The page

**P1.** A service worker precaches the Shell on install, so the app opens with the device
switched off. It opens in the **Prepare view** and stays there: a cold offline load has no
status, and a missing device reads as "no card", not as an error banner.

**P2.** The Weights are cache-first, in a cache whose name carries the app version and the
manifest hash. A model swapped at the same URL is a cache miss, not a stale hit.

**P3.** `index.html` and `sw.js` are served `no-cache`; hashed `assets/*` are
`public, max-age=31536000, immutable`; anything else is revalidated. One `Cache-Control` per
response — `http_send_headers` defaults to `no-cache` and a route's own header replaces it, never
joins it. `/api/status` is `no-store`; the rest of `/api/*` keeps the default `no-cache`, which
revalidates the library views and the control commands.

**P4.** The page is installable: a manifest with the product's name and icons, served from the
same partition.

**P5.** An update is never imposed: a new service worker waits rather than replacing the page
under a running analysis. Offering it — a button that reloads — is **not built**.

---

## 4. The two views

**S1.** `GET /api/status` returns the **Card state**, the library's track count and generation,
and what the broadcast is playing: `title`, `path`, `elapsed_ms`, `duration_ms`. The page polls
it every 5 s and maps the two times to `elapsed`/`duration` for the view. It is `no-store`, and
it answers only on its own path: `/api/statuses` is a 404.

**S2.** Card in → **Player view**. Card out → **Prepare view**. The view is chosen by the Card
state at load.

**S3.** A change of Card state switches the view only when the queue is idle. Mid-analysis the
page stays and shows a banner offering the switch. A card that comes back does not throw away a
running analysis.

**S4.** The Player view is TBD; until it exists it is a placeholder that says the card is in and
names what is playing, with a link to the stream. The link is absolute and points at port 80,
because the stream is plain HTTP on a different origin from the page.

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
| The firmware was built without a Shell | `/` answers `503` naming `/library`; every other route is unchanged |
| The Shell directory exists but has no `index.html` | `/` answers `503` — a page that was packed and is not there is a device fault, not a typo |
| An unregistered `/api/…` path | `404`, never the page |
| An upload that is not a snapshot | `400` with the reader's reason; the partition is untouched |
| An upload with the wrong content type | `415`: a form or another origin cannot reach the write path |
| An upload larger than the route allows | `413` before the body is read |
| An upload when the build has no snapshot partition | `503` |
| The device restarts after an upload | The reply is written first; the reader re-opens the new image at boot |
| CDN unreachable, cache cold | The Prepare view says the analyser cannot start without a first download that needs internet |
| CDN unreachable, cache warm | The app runs from IndexedDB (there is no service worker on plain HTTP) |
| A model's hash does not match | It is refused, named in the log and on the page, and the analysis does not start |
| Card pulled mid-analysis | The queue's current file fails; the page keeps the view and says why |
| Device unreachable | The page still opens (it was served by the device, so this means the device went away); the Prepare view keeps what it has and there is no error banner |

---

## 7. Build and flash

**B1.** `cd web && npm run build:shell`, then `idf.py build flash` — `build` writes `dist/` for
local development, and `build:shell` writes the `dist-shell/` that is packed. A shipping build
adds `-DXTAPE_REQUIRE_SHELL=1`, which fails the configure when the Shell is missing or empty;
without it the firmware builds and `/` answers `503`.

**B2.** Nothing to install and no certificate to ship: see ADR 0049. `tls/` and
`tools/make-cert.sh` stay in the tree, unused, for a device that serves TLS again.

---

## 8. Tests

| # | Test | Tier | Status |
|---|---|---|---|
| T1 | `/` serves the app, `/library` serves the index, unknown paths 404 | host | **proven** — `host/tests/front_door_tests.py` |
| T2 | A valid image posted to `/api/snapshot` lands byte for byte and is served after a restart | host | **proven** — `host/tests/upload_tests.py` |
| T3 | A corrupt image, an empty one, a JSON body and an oversized one are each refused, with the partition untouched | host | **proven** — the same file |
| T4 | The same upload over the board's own listener | board | **proven by hand**: `POST /api/snapshot` → 200, and `library_stats` reports `snapshot.state=ok, rows=1` after the restart |
| T5 | `/stream` and the app share port 80, and nothing listens on 443 | board | **proven by hand**: `/` 200, `/stream` 200, `https://` refuses the connection |
| T6 | `/api/status` reports the card state within one poll of a removal and a reinsertion | board | **not proven** — the card has not been through a removal/reinsertion cycle since the front door landed |
| T7 | A cold cache loads the weights once; a warm cache starts the analyser with the network off | browser | **not proven**: manual Chrome smoke tests passed before the HTTP move, nothing automated |
| T8 | A tampered model is refused, by hash | host | **proven at the hash level**, and the JS SHA-256 is pinned to the standard vectors; no test yet that the loader refuses a bad body |

## 9. Open measurements

1. The upload's write time on the board, and how long the PSRAM cache is off while the
   snapshot partition is erased and written.
2. First-load time of the 41 MB of weights over the LAN, and how much the IndexedDB cache
   saves on the second load.
3. SPIFFS mount time and the read cost of the 816 KiB shell.
4. The JS SHA-256's cost against `crypto.subtle` on a real weights download — it is the one
   capability the move to plain HTTP gives up, and it is on the critical path once.
5. Whether a browser shows anything worse than the "Not secure" chip for a page that posts a
   binary body to its own origin (no interstitial is expected; unmeasured).

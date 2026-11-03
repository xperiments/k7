# K7: a secure application, and a Launcher that stays behind

**Goal.** K7 is the App — one secure origin, **installable from Chrome in one click as an
app**, offline-capable, and useful with no player in the room. The device keeps a small page of
its own at `http://k7.local`, the
**Launcher**, whose job is to be reachable on plain HTTP forever: it shows the device's state,
opens the App, carries the stream's audio, and executes the bridge's operations
([`bridge-protocol.md`](./bridge-protocol.md)). The device is a gateway, not the runtime.

The page the device used to serve is gone. **Front door** is retired as a word: it is now the
**Launcher**, the **App**, and — when the device serves the App's own bytes as a fallback —
the **Device copy**.

**Status:** designed, not built. The App runs today only as the device-served page at
`http://k7.local/` (formerly `http://xtape.local/`), which is a secure context on nothing, so
the card writer, the service worker and WebGPU are all dead there by construction.

**Vocabulary:** [`web/CONTEXT.md`](../../web/CONTEXT.md). The device's own API is
[`front-door.md`](./front-door.md) §2 — the spec as it stood when the device served the app
there; its routes and contracts still hold, its "one page" does not — and
[`sd-stream-server.md`](./sd-stream-server.md); the Image is
[`snapshot-format.md`](./snapshot-format.md); the card's mark is
[`card-mark.md`](./card-mark.md). The origin decision is
[ADR 0057](../adr/0057-the-app-lives-at-a-public-origin.md); the bridge's shape is
[ADR 0058](../adr/0058-the-bridge-is-a-typed-rpc-between-two-windows.md).

---

## 1. The pieces

| | **Launcher** | **App** | **Device copy** |
|---|---|---|---|
| Origin | `http://k7.local/` | `https://k7.xperiments.dev` | `http://k7.local/app` |
| Served by | the device, from the Shell image | Vercel, with headers | the device, from the Shell image |
| Secure context | no | yes | no |
| Owns | device facts, stream audio, Launch, the bridge's agent side | library processing, card writes, the PWA, WebGPU, every view | the same App, degraded by its origin |
| Never | serves a Track, writes the card, runs the analysis | fetches the device directly | writes the card, installs, or uses WebGPU |

**A1. One codebase, two packagings.** The App's code is the same in both artifacts; only the
entry HTML, the compression and one build flag differ (§9).

**A2. The Device copy exists because the device may be reachable when the internet is not.**
It is the App minus everything its origin forbids: no service worker, no directory handles, no
WebGPU, no OPFS, and `crypto.subtle` absent, so its hashes run in JavaScript. Its weights live
in the same IndexedDB entries — that store is not secure-context-gated — so it too works offline
once a weight has been fetched; only the first fetch needs the network. That is the fallback's
honest limit.

**A3. The Launcher has no routes, no analysis and no weights.** It is a few kilobytes of
hand-written HTML and JS. Everything it shows it reads from `/api/status` and `/api/playlists`;
everything it does it does on behalf of the App.

---

## 2. Origins and trust

**L1. The Launcher trusts an app-origin list compiled into it at build time:**
`https://k7.xperiments.dev`, plus a development origin only when the build is told one. It
opens the first entry and accepts messages from any entry. No URL parameter, query string,
referrer or stored setting can add one.

**L2. The App trusts exactly one Launcher origin: `http://k7.local`.** Not an IP, not another
port, not another host. Reached at `http://192.168.0.13`, the Launcher still shows status and
the stream, and its Launch button says connected mode needs `k7.local`; the App shows the same
sentence in reverse.

**L3. The device's name is `k7.local`.** The mDNS hostname is one string in
[`appliance.c`](../../components/app/appliance/appliance.c). The App's allowlist gets no
`xtape.local` entry: an unrenamed board serves the *old app* at that name and no Launcher at
all, so there is nothing there to trust. The cost of the rename is a flash, not a trust entry.

**L4. A development origin is a build input, never a runtime fact.**
`https://k7-dev.xperiments.dev:8000` — the port included, because an origin includes the port —
is compiled into a development Shell only. A shipping build refuses it (§9, [`k7-deploy.md`](./k7-deploy.md)).

**L5. The App is a public page.** Anyone may load it. That is safe because the App holds no
credential and can reach nothing: it can only *ask a Launcher it was opened by*, and only for
the operations in the table.

---

## 3. How the App describes itself

Three observed facts. There is no mode enum, and no precedence table.

**A4. `link`** — `bridge` (a session with a Launcher), `direct` (the App is the Device copy, so
same-origin `fetch` works), or `none`.

**A5. `network`** — `online` or `offline`, from the browser's own event.

**A6. `capabilities`** — `isSecureContext`, service worker, `navigator.gpu`, directory handles,
OPFS, all read at boot and shown in Settings.

**A7. One chip composes them** — "Connected · k7.local", "Connected · player unreachable",
"No player — Connect", "Offline · cached". A feature is gated on the capability it needs, never
on the chip.

| Feature | Needs |
|---|---|
| Scan a dropped folder, measure, build an Image, download it | File API — works on the Device copy |
| Pick a card, write `k7.db`, write the card mark | secure context + directory handles |
| Install, offline shell, weight prefetch | secure context + service worker |
| Local Provider (WebLLM) | secure context + WebGPU + ~1 GB of patience |
| OPFS | secure context |
| Media, Playlists, Ask over the Last image | nothing but the store |
| Device facts, playlists, send, play, stream | `link !== none` |
| Ask over a remote Provider | `network === online` + Settings |

**A8. `window.isSecureContext` is a readout, not an assertion.** The Device copy shows `false`
and the features it forbids, instead of failing at the first call.

### Install

**A9. The App must install from Chrome as an app, not as a bookmark.** Chrome's own criteria,
all of which the deployment must satisfy on `https://k7.xperiments.dev` and nowhere else: a
manifest served from the App's origin with `id: "/"`, `name` and `short_name` "K7",
`start_url: "/"` within `scope: "/"`, `display: "standalone"`, `theme_color`,
`background_color`, a 192 and a 512 icon, and a 512 icon with `purpose: "maskable"` so an
Android launcher does not crop the cassette; and a registered service worker with a `fetch`
handler — which the App has for its own reasons.

**A10. Installing must be one click, offered where it can work.** `beforeinstallprompt` is
captured and replayed from an explicit **Install K7** control; `appinstalled` and
`display-mode: standalone` hide it once it is done; Safari gets the Share → Add to Home Screen
sentence and Firefox an honest "this browser does not install apps". The **Device copy never
offers it**: an insecure origin cannot be installed, and a dead button is worse than no button.
An optional wide `screenshot` in the manifest buys the richer install dialog.

**A11. The installed window is the normal case, not a downgrade.** An installed App has no
opener, ever — which is exactly why the Bridge opens from the App's side as well as the
Launcher's ([`bridge-protocol.md`](./bridge-protocol.md) W1). Installing must be presented as
the recommended way to use K7, and **Connect** as what makes it work with a player.

**A12. The manifest is revalidated, never inferred.** It is served `no-cache` with the worker
and the entry ([`k7-deploy.md`](./k7-deploy.md) D4): a manifest is how Chrome decides what is
installed, so a stale one is an install that never sees new icons. `id` stays `/` at the new
origin — changing it is a different app to Chrome, and leaves the old install behind.

**A13. Installability is a deployment acceptance test.** Checked on the real origin, by hand,
once per release, with the Device copy and a Vercel preview both expected to refuse: Chrome's
omnibox install icon appears, the app opens in its own window with no address bar, and the
Icon on the launcher is not cropped.

---

## 4. The library has two sources

The App can browse and search a library from the **device** or from the **Last image**. Both
answer the same four questions — search, list, stats, one track — so the surface does not know
which it is talking to.

**S1. `LibrarySource`** is that seam: `search(args)`, `list(path, depth, limit)`, `stats()`,
`track(path)`. The **device** implementation is MCP over the bridge or direct; the **image**
implementation is the Last image's rows joined to `k7-media` for titles, artists and covers.

**S2. Local-first resolution.** `link` present → device, because it reflects the card as it is
now. No link → image, if one exists for a scope. Neither → an empty state that says which is
missing, never a spinner.

**S3. The surface says which source it shows.** "The player · 4 120 tracks" and "Last image ·
11 February · 4 118 tracks" are different facts and must not look alike.

**S4. One conformance corpus, two implementations.** Filter semantics — BPM range, key, mood,
energy, text, sort, offset, limit — are pinned as a JSON fixture of rows, queries and expected
results, run by the App's vitest and by the device's host tests. Two implementations that agree
by accident will disagree on purpose within a month.

**S5. Covers always come from the Last image's scope.** The device serves paths and
measurements and never a picture; the media store is scoped by a card mark
([`card-mark.md`](./card-mark.md) §2). The Image is what the device may be walking, so it is
also the scope covers are looked up in. A *different* card in the player can therefore borrow
the last image's covers until the device reports its own mark in `status`
([`card-mark.md`](./card-mark.md) §6) — a known gap with a specified end, not a hidden one.

---

## 5. The card

Card, Folder, the mark, the fingerprint and the write policy are
[`card-mark.md`](./card-mark.md). What this document adds is where they sit in the flow:

**C1. Pick → mark → scan → measure → build → deliver.** The writable pick creates or reads the
mark before anything else, so the whole run has a scope. The scan walks `music/`, the run
measures, the Image is built, and delivery is one of: write `k7.db` to the card, post it to the
device (`snapshot`), or keep it as the Last image. The card write and the post are independent.

**C2. The Last image belongs to a card.** It records the scope it was built for. Changing the
card in the computer does not silently retarget it.

**C3. Drafts are the user's, not the card's.** They live in the drafts store and survive every
card.

---

## 6. Offline

**O1. The Shell is the worker's.** The service worker precaches the built shell from
`/shell.json`; it does not `skipWaiting`, so a new version waits for a reload rather than
replacing the page under a running analysis.

**O2. The weights are not the worker's.** They are content-addressed in IndexedDB by
`url + sha256` and re-verified on every read ([`loader.ts`](../../web/src/weights/loader.ts)).
The worker's separate weights cache and the `MANIFEST_HASH` in its cache name go away — there
is one offline store for a weight, not two, and a model that changes costs one file rather than
41 MB.

**O3. Prefetch is 41 MB of `loadWeightBytes()` calls**, driven by the App, resumable, with
progress — never `cache.addAll` inside the worker's `install` event, where a CDN hiccup would
fail the install and leave no worker at all.

**O4. Prefetch runs when the App is installed** (`display-mode: standalone`), and on a button
in Settings for anyone who has not installed. A visitor who follows a link never spends 41 MB
of jsDelivr's donated bandwidth on someone else's behalf.

**O5. `navigator.storage.persist()` is requested after the first successful analysis**, when
the value is demonstrated; the quota and the cache's size are shown in Settings.

**O6. Update detection is a banner, not a takeover**: "a new version is ready — reload", shown
when a waiting worker exists.

**O7. Offline cannot cover two features and says so:** a **Remote Provider**, and the Invidious
**Instance**. Both are network features wearing a local app's clothes.

---

## 7. The local Provider

**P1. `Provider` is a union**: `{kind: 'remote', baseUrl, model, key}` or
`{kind: 'local', model}`. Everything above it sees one thing: a sentence becomes filters.

**P2. The contract does not change.** The model produces an **Intent** and never results
(ADR 0053); the same validator judges a local model's output, with one repair attempt and then
an error.

**P3. WebLLM, on WebGPU, gated.** Without `navigator.gpu` the local option is absent from
Settings rather than present and broken.

**P4. Two curated models, not a text field**: `Qwen2.5-1.5B-Instruct-q4f16_1` by default and
`Qwen2.5-0.5B-Instruct-q4f16_1` for small machines. A free-text model id is a 404 generator.

**P5. Downloaded only when chosen**, with progress, cancel and **Delete** — it is roughly 1 GB
and it is never part of O3's prefetch.

**P6. Pinned, not hash-verified.** The model files come from an immutable Hugging Face commit
recorded in a lock file; they are not hashed chunk by chunk like the 41 MB of ONNX weights.
They are **data, not code**: a tampered model cannot execute, and the worst it does is produce
a wrong filter that is shown and editable before anything is searched. The difference is
recorded in [ADR 0061](../adr/0061-the-local-provider-is-pinned-not-verified.md) so the next
person does not "fix" it into a minute of first-run hashing for no gain.

**P7. Nothing leaves the tab.** With a local Provider, the Ask's sentence, the Intent and the
library stay on the machine; that is the only configuration in which the App's privacy claim is
unconditional.

---

## 8. Security posture

The channel's rules are [`bridge-protocol.md`](./bridge-protocol.md) §7. What the App adds:

**X1.** Every device call goes through the transport seam. No view calls `fetch('/api/…')` and
no component knows an origin.

**X2.** The App never fetches `http://k7.local`. It could not, and the design does not pretend
otherwise: if a feature needs the device, it needs a Launcher.

**X3.** No `COOP: same-origin` on any App response. It would sever `window.opener` and kill
connected mode silently, and it is the one header that looks like a hardening win and is a
regression here ([ADR 0058](../adr/0058-the-bridge-is-a-typed-rpc-between-two-windows.md)). The
consequence — no `SharedArrayBuffer`, so ONNX Runtime stays single-threaded — is accepted, and
is already true on the Device copy.

**X4.** The Provider's key is the only secret the App holds. It lives in this origin's storage,
never crosses the bridge, and a CSP on the deployed App is what protects it from injected
script.

**X5.** The App is AGPL-3.0-only and ships its source offer at `SOURCE_URL`; a public origin
makes that a deployment requirement, not a nicety.

---

## 9. Packaging

| | HTTPS artifact | Device artifact |
|---|---|---|
| Entry | `index.html` is the App; `start_url: "/"` | `index.html` is the Launcher; the App is `app.html`, served at `/app` |
| Compression | plain | text as gzip, originals dropped, served with `Content-Encoding: gzip` |
| Weights | fetched from jsDelivr | the same |
| Local Provider | included, lazy chunk | `VITE_LOCAL_PROVIDER=0` — no WebGPU on that origin, so the chunk would be dead bytes |

**K1. The Launcher is a static file copied into the shell build**, not a Vite entry: it needs
no bundle, and a multi-page Vite config to serve 5 KB would be a cost with no return.

**K2. `/app` is one line** in `logical_for` ([`front_door.c`](../../components/portable/front_door/front_door.c)).
Every other path stays an exact filename.

**K3. The Shell image grows by the Launcher and loses nothing.** The crash of a page load on
the panel — the black frame ADR 0055 chose — now covers the Launcher and, on the Device copy
only, the App's own bytes. Measured: 392 783 bytes packed, against 377 KiB before the Launcher
existed — about 15 KB, the maskable icon included.

**K4. The build refuses a shipping Shell carrying a development origin**, and the deployed
artifact carries no firmware path at all. `k7-deploy.md`.

---

## 10. What this costs

- **No cross-origin isolation.** No `SharedArrayBuffer`, so ORT is single-threaded. WebGPU is
  unaffected, which is why the capability that motivated the move is not the one that pays.
- **Connected mode needs the new firmware**, because the Launcher *is* firmware. A board with
  the old image serves the old app and cannot bridge.
- **Two origins, two stores.** Work done in the Device copy cannot be handed to the Hosted App;
  the card is the only bridge between them.
- **A public origin is a permanent dependency.** A fresh browser needs the internet once; after
  that the worker is the app.
- **The fallback is thin.** No service worker, no picker, no WebGPU, and the App's code only
  because the device carries it. It is the App for a LAN with no internet, not the App for a new
  machine with no internet.
- **The weights are still 41 MB from a public CDN.** No design decision here changes that; O4
  only decides who pays for it.

---

## 11. Measurements to take before they are asserted

| What | Why it is not known |
|---|---|
| A `library.list` / `library.search` response for 5 000 tracks | the bridge's 1 MiB cap is a guess until measured |
| ~~Shell image size with the Launcher and `app.html`~~ | measured: 392 783 bytes packed (377 KiB before), so ~15 KB |
| A page load's black frame with the Launcher | ADR 0055 measured 187 KB; the Launcher is 30× smaller |
| Writing `k7.card` on a real FAT32 card through the picker | the writer is unbuilt |
| Cold 41 MB prefetch over the LAN and over a WAN | the offline promise's first-run cost |
| WebLLM's first load and its VRAM on the M2 | P4's choice is a claim until then |

---

## 12. Build order

**M1 — the channel, and an App that installs.** The manifest and its three icons (the rename,
the maskable pass), the Install control with its four states, the Launcher page (gateway and
bridge modes), the `/app` alias, the transport seam with `direct` and `bridge`, `status` and one
round trip, the capability readout. The manifest and icons belong here rather than in M3
because they are rename work touched regardless, and because a worker that installs is the same
worker that serves offline. Acceptance: the App opens from the Launcher; both origins'
allowlists hold; `window.isSecureContext` and `navigator.gpu` are shown; it installs from
Chrome's omnibox and from its own button and opens in its own window with the 512 maskable icon
uncropped (A9–A13); the CI manifest check is green; the Playwright COOP test passes.

**M2 — the card.** Directory pick with write, the mark, the scope, the fingerprint fallback,
the card writer for `k7.db`. Acceptance: a card is marked once and recognised next time; a
second card does not inherit the first's covers; a read-only card falls back to a fingerprint.

**M3 — offline.** Prefetch on standalone, persist, the update banner, the weights cache
collapse. Acceptance: a reload with the network off works; a model bump downloads one file
rather than 41 MB; a waiting worker produces the banner and not a takeover.

**M4 — connected, standalone, and the local Provider.** All fifteen operations, the Library's
second source, the Device copy's gating, the local Provider. Acceptance: every area works with
no device; with a device, send/play/stream work; a local model produces an Intent offline.

---

## 13. Tests

- **vitest** — protocol state machines (§T1 of the bridge spec), the card mark's policy table,
  the fingerprint, the Image's `LibrarySource` over the conformance corpus, the Intent validator
  against a local model's malformed output.
- **Playwright** — two origins: handshake each way, one round trip, COOP/opener regression,
  disconnect and reconnect, offline reload, a blocked popup. Plus an installability assertion
  where the test browser fires `beforeinstallprompt` — desktop Chrome and Edge do, the headless
  shell may not — with the manifest shape check in CI as the half that always runs.
- **host / Python** — the Shell image holds `index.html` (Launcher) and `app.html`, `/app` maps,
  `/k7.db` and `/xtape.db` both update.

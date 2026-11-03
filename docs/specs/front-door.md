# The front door: one shell, four areas

**Goal.** `http://xtape.local` is the one address and the one page. It is a small app with a
sidebar: **Card**, **Playlists**, **Media**, **Player**. The Card area measures a card in the
computer and sends `xtape.db` back; Playlists authors the lists that image carries; Media browses
what the device holds and looks a track up on YouTube; Player shows what the broadcast is
carrying. The app's own code ships in the firmware; the 38 MB of models and runtimes are fetched
once and cached on the computer that runs it.

The decision to serve a page at all is [ADR 0048](../adr/0048-the-device-serves-the-analysis-app.md);
plain HTTP is [ADR 0049](../adr/0049-the-front-door-is-plain-http.md); a shell of areas with hash
routes is [ADR 0052](../adr/0052-the-front-door-is-a-shell-of-areas.md); a Playlist in the Snapshot,
walked by the broadcast, is [ADR 0051](../adr/0051-a-playlist-lives-in-the-snapshot.md); the
assistant writing filters rather than results is
[ADR 0053](../adr/0053-the-assistant-writes-filters.md). The page's
vocabulary is [`web/CONTEXT.md`](../../web/CONTEXT.md); the device's is
[`CONTEXT.md`](./CONTEXT.md). The byte contract for the image is
[`snapshot-format.md`](./snapshot-format.md).

**Status:** built, flashed and running on the board over plain HTTP — the Shell (377 KiB packed),
the four areas, the hash routes, `format_version 2` with the list area, `GET /api/playlists`,
`GET /api/playlists/<id>`, `POST /api/playlists/select`, the selection remembered in NVS, and the
broadcast walking a selected order. The page is served and its assets download from the device.
What is not proven: a **selection restored across a reboot with a card mounted** (no card has been
in the board since the feature landed, so nothing could resolve), the browser flows end to end (no
automated browser), the cold/warm weight cache, and §9's measurements.

---

## 1. What is served, and from where

| Piece | Bytes | Lives |
|---|---:|---|
| `index.html`, app bundle, worker, `wasi-adapter`, `ort.mjs`, manifest, icons, `shell.json`, `weights.lock.json`, `setup.html` | **377 KiB** of bytes (measured; 381 KiB of image) | 1.25 MB `frontdoor` partition at `0x610000`, read into PSRAM once at boot (ADR 0055) |
| Essentia (`essentia-wasm.es.js`, 2.56 MB) | 2.6 MB | jsDelivr, pinned tag, sha256-checked |
| TagLib's one browser wasm | 0.7 MB | jsDelivr |
| ONNX Runtime wasm | 14 MB | jsDelivr |
| Models, nine of them | 23.9 MB | jsDelivr |
| **Weights together** | **≈ 41 MB** | fetched once, cached |

**F1.** `web/dist-shell` is packed by `tools/pack_shell.py` into ONE image - a table of
contents, then the files' bytes - and flashed to the `frontdoor` partition by
`esptool_py_flash_to_partition`, so `idf.py flash` writes the firmware and the page together,
and the build logs the app commit it packed. The board reads that partition **once**, before
the panel exists, into PSRAM, and serves the page from there; a page load never touches flash,
because any flash access takes the bus the panel's scan-out draws from (ADR 0055). Flash is the constraint, so the build
stores every text file as gzip and drops the original: the front door sends those bytes with
`Content-Encoding: gzip`, and a client that did not offer gzip for a compressed-only file gets
`406`. `index.html` and the icons stay plain. Nothing on the board decompresses anything. The
pack is a **configure-time** decision: after building the Shell the build directory must be
reconfigured. A clean checkout without a Shell
still builds — the device is useful without a page, with `/` answering `503` naming `/library`.
A shipping build passes `-DXTAPE_REQUIRE_SHELL=1`, which turns a missing or empty Shell into a
configure error.

**F2.** The **Shell** is our code and nothing else. The worker is built with Essentia external:
it loads the module from the CDN, not from the bundle.

**F3.** The **Weights** are pinned to a tag and verified against the manifest's `sha256` before
use. JavaScript modules are fetched as text, checked, and imported from a blob URL;
`ArrayBuffer` artefacts are checked before they reach ONNX Runtime. Verification happens on
every cold load, not only the first.

**F0.** Anything that touches flash at runtime runs on one task with an internal stack: the
Snapshot write, the NVS selection, and serving a page. The HTTP workers' stacks are in PSRAM, and
a flash access disables the cache they live behind — the page's own reads reset the device until
this was found ([ADR 0054](../adr/0054-flash-io-runs-on-an-internal-stack.md)).

**F4.** The Shell is **small on purpose**. Every component it installs is JavaScript the board
has to store and the browser has to parse: `sidebar` plus its `dropdown-menu`, `breadcrumb`,
`avatar` and `sheet` parts are the largest single addition the front door has taken. A component
that is not on a route the user can reach is not installed. `web/README.md` carries the measured
size and the budget.

---

## 2. Origins and routes

**R1.** `http://xtape.local` is the device's only address and its only port. One listener serves
the page at `/`, the JSON API at `/api/*`, MCP at `/mcp`, and the index at `/library`. Unknown
paths are `404`: the device serves one page and has no catch-all.

**R2.** Inside that one page, the **routes** are hashes — `#/process`, `#/playlists/:id` — so the
address bar has a location for every place without asking anything of the firmware. A hash
change is a client event: the device never sees it, no request is made, and the back button
works.

| Route | Area | What it shows |
|---|---|---|
| `#/process` | Card | intake (drop or pick a card), the Queue, the run's progress |
| `#/playlists` | Playlists | the device's Playlists and this browser's Drafts |
| `#/playlists/:id` | Playlists | one Playlist's rows, order and actions |
| `#/playlists/ask` | Playlists | the prompt, the Intent, and the search that becomes a Draft |
| `#/media` | Media | albums, from the device's Snapshot rows |
| `#/media/album/:album` | Media | one album's tracks |
| `#/media/artist/:artist` | Media | one artist's albums |
| `#/media/video/:id` | Media | one Video, click to play |
| `#/player` | Player | cover, what is Playing, the device's facts |
| `#/settings` | — | the Provider and the Invidious Instance |

**R3.** `/stream` is on the same port, byte-identical to what a radio has always received.

**R4.** `/library` is the Index: every Track in the Library tree in **broadcast order**, with its
size, what is Playing and a link to the stream. It is the device's diagnostic page when no card is
mounted and the page cannot run.

**U1.** The App no longer posts to this route: the image travels on the card and arrives as
the Update file ([ADR 0065](../adr/0065-the-card-is-the-only-way-to-the-player.md)). The route
itself is unchanged and still validates what it is given — removing it from the firmware is a
separate, flash-sized decision — so what follows describes the device's behaviour, not the
App's. `POST /api/snapshot` takes an `application/octet-stream` body — the `xtape.db` image —
validates it in full before erasing anything, writes the snapshot partition, reads it back, and
restarts in 1.5 s so the reply lands first. A refusal leaves the running snapshot untouched. The
image may carry **Playlists** (`format_version 2`, §10 of the format); an accepted image replaces
the previous lists entirely. The content type is also the CSRF guard: it is not CORS-safelisted,
so another origin cannot send it without a preflight the device never answers.

**U2.** `GET /api/playlists` reports the lists the Snapshot holds — id, name, created, row count —
and which one is selected. `GET /api/playlists/<id>` reports one list's rows as paths. Both are
`no-cache`, like the rest of `/api/*` but `/api/status`.

**U3.** `POST /api/playlists/select` takes `{"id": "<hex16>"}` or `{"id": null}` and tells the
broadcast which Playlist to walk. `404` for an id the Snapshot does not hold, `409` for a list
with no rows — or whose rows the Library cannot resolve at all — `503` when there is no Snapshot,
`400` for a body that is not that object. The choice is remembered in NVS and survives a restart;
on boot a stored id the current image no longer holds falls back to the tree's order and the
stored id is cleared. A refusal leaves the working selection alone.

**U4.** `POST /api/play` keeps its meaning: the broadcast moves to that Track. If a Playlist is
selected and the Track is not in it, the selection is **dropped** — the walk is over — and the
reply's `order` and the next `/api/status` say so. This is the one implicit mode change, and it
is the honest reading of "play this now".

**S1.** `GET /api/status` reports the **Card state**, the Library's count and generation, what is
Playing (`title`, `path`, `elapsed_ms`, `duration_ms`), and the order: `order` is `"tree"` or
`"list"`, and `playlist` is `{"id","name"}` or `null`. It is `no-store` and answers only on its
own path.

**U4.** There is **no TLS**. No certificate is compiled in, no trust step exists, and a browser
shows "Not secure" and no interstitial. The consequences are accepted, not hidden: no service
worker and no installable PWA, no `crypto.subtle` (a pure-JS SHA-256 replaces it), and no
directory picker. See ADR 0049.

---

## 3. The page

**P1.** There is no service worker, because a plain-HTTP origin cannot register one. The device
serves the Shell from its own flash, so the page is always the firmware's own build and there is
nothing to precache. The Weights are the only thing that outlives a load, cached in IndexedDB
and verified by hash on every cold use (F3).

**P2.** `index.html` is served `no-cache`; hashed `assets/*` are
`public, max-age=31536000, immutable`; anything else is revalidated. One `Cache-Control` per
response — `http_send_headers` defaults to `no-cache` and a route's own header replaces it, never
joins it. `/api/status` is `no-store`; the rest of `/api/*` keeps the default `no-cache`.

**P3.** The manifest is still served, and no service worker means no install prompt. The page is
a page, not an installed app.

**P4.** The app is **dark only**: `index.html` carries `class="dark"`, the palette is the app's own
(warm charcoal, an amber accent, a teal "measured"), and it is mapped onto semantic tokens in
`src/index.css` so installed components keep their identity.

**P5.** The page is **dense**: it is a tool worked in, not a page read once. Controls take
`size="sm"`, body text is `text-sm`, meta is `text-xs`, and a Process row is about 32 px tall.

---

## 4. The four areas

The shell is `c-sidebar-1` — the ReUI example — installed from the registry and adapted, not
rebuilt: `SidebarProvider`, an inset, a header with the trigger and a breadcrumb, and the
template's own collapse and mobile `Sheet` behaviour. What changes is data and destination, never
the structure:

- **Header.** The workspace switcher becomes the device: the name, whether a card is in, and a
  menu of device actions (open the stream, open `/library`, copy the MCP endpoint, rescan).
- **Groups.** Card, Playlists, Media, Player, each a `SidebarGroup` with count badges the
  template already supports. The template's "Projects" group and account block are removed: there
  are no projects and no accounts.
- **Footer.** Settings (`#/settings`) and the licence line that used to sit under every page.
- **Bar.** A player at the bottom of the inset, under every area: the cover, what is sounding,
  play/pause, a stop, and a volume — plus, for a card track, previous and next and a **seek that
  spans the bar**. A card play is the **album**: the bar is handed its tracks in order and one
  loader, so a track that ends asks for the next (the last one stops) and next/previous move
  through it. A stream has neither, because there is nothing to be next to. It has two sources and
  says which — the player's **stream** (`/stream`, live, no seek) or a **file off the card in
  the computer**, read through the card's handle (`card/read.ts`) — the one the page holds, or
  the one the browser remembers for that Scope, re-granted read-only on the click that plays —
  and played from a blob URL. Its one element lives in `PlaybackProvider`, not in the bar, so re-rendering an area
  never interrupts the sound and any page can start it. With no card in the player, `play` on an
  album row means this file; with one, it means the player, and the bar follows its broadcast.

**The card state chooses the first area, once.** A cold load with a card in the player opens
`#/player`; with no card, `#/process`. After that the sidebar is the user's. The S3 banner is
gone; what it protected is structural — **the Process area stays mounted while another route is
shown**, so navigating can never destroy a running Queue nor stop a worker. Every other area
mounts on demand.

### 4.1 Card — Process

The Queue, measured, written to the card by the run itself.

- **Header:** title, the run's state (`14 of 32 analysed`), the card the run belongs to (its
  mark, or a fingerprint when it has none), and the one fact that matters after it — *k7.db
  written 12:34*. There is nothing to send: the image is written to the card when the run
  finishes, and again when a Playlist changes while the card is still open (ADR 0065). What the
  header does offer is intake, because a card changes under the page: **Rescan card** walks the
  card this page holds open again — the tracks it already has are recognised, not measured
  twice — and **Change card** opens the picker, replacing the queue with the new card's and the
  Scope with it. Clear, asked in place, is the last action. The handle is remembered per Scope
  (`card/handles.ts`), so a reload does not lose the card: Rescan asks for the read-write grant
  again on the click, which is the only moment a browser will show the prompt.
- **Progress:** one bar for the whole run, the count, and what is in flight. It is a run, not a
  page: it stays at the top while the rows scroll.
- **Rows:** gathered by album — *Album / Artist* as the heading, with its track count, in the
  order the card was walked — and one line each, ~32 px: name, the genre from the file's own
  tags, BPM, key, a mood word, and the format. The heading names the artist only when the
  album has one: a compilation heads with the album alone and each of its rows says
  `Artist - name`. The name drops the extension (the format column carries it) and a ripper's
  track number (`01 - `, `02. `), while a title that merely starts with a number keeps it. **Provenance is paint, not text:** a value the file's tags carried is written in
  the foreground colour, a value this App measured in the warning colour, and an absent one as
  a muted dash. A mixed row shows it at a glance, and no row carries a provenance word. A row
  that is working shows its stage; a row that failed shows the failure and a Retry. **There is no drill-in**: the row does not open,
  and the analysis explaining itself — the old `ResultCard` behind an accordion — is not a list
  of five thousand rows. Its two actions, *measure the tempo* and *save a tagged copy*, live in
  the row's own menu.
- **Intake:** the folder picker and the drop zone (`DropZone`) are here, where the run is — there
  is no separate Scan page. The choice is checked before anything is measured: only the card
  root — the directory holding `music/` — is accepted, and a `music` directory, a parent folder
  or a folder with no `music` in it is refused by name
  ([`card/shape.ts`](../../web/src/card/shape.ts)). A picker is the only path that can write the
  image back; a dropped card is measured read-only.
- **States:** empty (no Queue: the drop zone *is* the empty state), loading (rows appear as they
  are found; `Skeleton` only for the manifest), error (models missing or a weight refused,
  inline, with what to do).
- **Device work:** the run builds `k7.db` — rows from the Queue, lists from the Playlists
  Drafts — keeps it as the **last image**, and writes it to the card (ADR 0065).

### 4.2 Playlists

- `#/playlists` lists what the device holds (from `GET /api/playlists`, with the selected one
  marked) and this browser's Drafts. Creating one is a name; the lists reach the card with the
  next run's image, and an edit made while the card is still open rewrites it (ADR 0065).
- `#/playlists/:id` is the editor: rows in order, drag to reorder, a "Sort by" menu that applies
  a rule **once** — BPM rising, Camelot wheel, energy arc, artist/album, name — and an add
  affordance that opens Media's tracks. Removing a row is a row action; nothing here is
  destructive enough to need a dialog.
- `#/playlists/ask` is **Ask**: a sentence, an **Intent** (the `library_search` filters the model
  produced), shown and editable, and then a search — run against the device's own MCP endpoint —
  whose rows become a new Draft. With no Provider configured, the filter fields stand alone. The
  model never sees a track (ADR 0053).
- Rows come from the device: a Draft's rows are paths and Track fingerprints, resolved against
  the Library for names and measurements, against this browser's cache for covers. A row the
  Library no longer has is shown as missing and skipped by the broadcast, not deleted here.
- **The card is the only delivery.** A list reaches the player inside the next run's image; an
  edit made while the card is still open rewrites that image on the card, and there is nothing
  to send (ADR 0065).

### 4.3 Media

- `#/media` lists albums from **this browser's `xtape-media` cache** — the tags and
  pictures the analyser kept when it read the card — because the device's search cannot be paged
  to the end of a library (`offset` is capped at 199, `limit` at 100). The device supplies what
  exists (the status count and generation, and a walk of `library_list`) and the measurements of
  an opened album (`library_track` per row, batched). Library tracks with no cached row are
  grouped under **Not measured** and offer Process; a cover measured elsewhere shows a
  placeholder tile — ordinary, not an error.
- Presence counts only what the App can open — MP3 and FLAC, the same judgement intake makes
  (`measureableName`). A track the device lists in a format it also plays (WAV) is named once
  as **cannot open**, never as unmeasured: no run will ever read it, so listing it as work left
  undone would be a lie. Paths are joined without regard to case, since a FAT32 card has none
  and the device folds them too (`library_find`).
- A track whose analysis is already known by content hash is not measured again, but it still
  leaves its tags and picture in this browser's cache under the card it was found on. The
  analysis is keyed by content, the shelf by card; joining on the card's row is what keeps a
  recognised track from appearing as unmeasured.
- Measurements are not in the library: `library_track` answers from the image the player last
  applied, which is the card's `k7.db` taken when the card arrived (the device reboots once to
  apply it, then deletes the file). A card measured since then is answered from an older image,
  so an album page names the image (`snapshot.generation`, `rows` in `library_stats`) beside the
  image this browser wrote, instead of printing "not measured" per row. The two silences are
  named apart: **no answer** (the device did not reply for that path) and **not in the image**
  (it replied, and the image has no row).
- The browser keeps every image it writes (`k7-snapshots`, keyed by Scope), so an album is
  readable with the card in the computer and the player empty. The page asks the player first —
  with a card in it, its answer is what it plays — and falls back to its own rows
  (`readImageMeasurements`), keyed as the player keys them: the path's fingerprint and the
  file's size. With no card in the player the page does not ask at all, and an album answered
  entirely from the image says so once rather than per row.
- One album opens its tracks with BPM, key and energy, and a way to add them to a Draft. A
  row's one button says what the press will do: **Play** when the card is in the computer (the
  track is read off it and sounds in the page's bar), **Queue** when the card is in the player
  (the track is sent to the player and arrives through the stream a moment later). There
  is no artist list beside the grid: an artist's page is reached from the album it made.
- An album is its **title**. Two artists on one title are one tile, not one each — a
  compilation is one shelf, the same rule Process groups its queue by — and its page says how
  many names it carries, with each track naming its own. The route therefore carries the title
  alone (`#/media/album/:album`).
- A track offers **Look up on YouTube**: an Invidious search seeded with artist and title and
  editable, 3–5 thumbnail results, each opening `#/media/video/:id` — a **click-to-play** page:
  the poster is the thumbnail the search already had, and the player is created only when
  someone presses play. The frame is YouTube's no-cookie embed and nothing else; `frame-src`
  names exactly that host, because a static header cannot name an Instance chosen in Settings.
  It carries `referrerpolicy="origin"` because the page is served `no-referrer` and the player
  answers a bare frame with error 153.
  "Open on YouTube" sits beside it as the escape hatch. Nothing is downloaded, and no Video can
  become a Playlist row.
- **States:** empty (no Snapshot: "measure the card once"), loading (skeleton tiles), error
  (device unreachable → cached names and covers, with a note; Invidious unreachable → the
  Instance can be changed from here).

### 4.4 Player

- The cover, the Track's name, artist and album (joined from the Snapshot row and this browser's
  cache), its path, an elapsed/duration bar from `/api/status`, and the device's facts: card,
  tracks, Library generation, Snapshot coverage and when it was built.
- **No transport.** A live stream cannot pause for one listener, and the only HTTP verb is
  `POST /api/play {path}`; offering next/previous here would promise what the device does not have
  outside an opt-in MCP build.
- **Rescan** (`POST /api/rescan`) sits with the facts it changes, because it is the device
  rebuilding its Library from the card it is holding — a different act from reading a card in
  this computer, which is Process's intake. Disabled with the reason when the device is
  unreachable or the slot is empty, and a `409` keeps the device's own sentence.
- **States:** device unreachable (the last thing seen, clearly marked, with the stream link),
  nothing playing, no cover, no Snapshot.

### 4.5 Settings

`#/settings`, reachable from the sidebar footer: the **Provider** (base URL, model, key, and a
Test that proves the model returns a parseable Intent) and the Invidious **Instance**. Stored in
`localStorage`, never sent to the device, lost with site data. The key is never rendered after it
is saved, only indicated as set.

A **Start from** row fills the base URL and the model for Ollama, Gemini or OpenRouter; it fills
nothing else, and no endpoint or credential ships with the page. With no preset chosen the card
is the three fields it always was.

A Provider on this computer — Ollama at `http://localhost:11434/v1` — is a different origin from
the page, so it has to allow `http://xtape.local`; until it does, the browser discards a reply
the server did send. Test says that when it is the reason, names the origin to add and the knob
that adds it, and never calls a running Provider unreachable (ADR 0056). The recipe is in
`web/README.md`.

---

## 5. What the page reads, and from where

| Fact | Source | Absent means |
|---|---|---|
| Card state, tracks, generation, Playing | `GET /api/status` | no device: the last answer, marked unreachable |
| The Library's tree | `library_list` over `/mcp` | no device: Media says so |
| Titles, artists, albums | this browser's `xtape-media` cache | "not measured", with Process |
| One track's measurements | `library_track` over `/mcp` | a dash, not an error |
| Playlists, the selection | `GET /api/playlists` | no Snapshot: none, and a Draft is still authorable |
| Covers | this browser's `xtape-media` store | placeholder tile, never an error |
| Videos | the Invidious **Instance** | a note and the setting |
| A sentence's filters | the **Provider** | the filter form alone |

`/api/library` is not read by the page: it carries paths and sizes and no tags, so the MCP rows
are the only source of names.

---

## 6. What this does not change

- The cassette on the panel, `/stream`, `/library`, the Snapshot flow and the existing MCP tools.
- The global premise: the device still serves no Track and accepts none. A page is not a Track,
  and the card still leaves only as the live stream.
- The host build's composition: it runs the same services, on the hand-written backend.
- The panel's Menu still browses the library tree; nothing on the cassette screen names the
  selected Playlist (ADR 0051).

---

## 7. Failure modes

| Condition | Behaviour |
|---|---|
| The firmware was built without a Shell | `/` answers `503` naming `/library`; every other route is unchanged |
| An unregistered `/api/…` path | `404`, never the page |
| A hash route the shell does not know | The shell's own not-found view, with the areas listed |
| An upload that is not a snapshot, or is v1/v2-invalid | The reader's reason, `400`, partition untouched |
| An upload larger than the route allows | `413` before the body is read |
| A v2 image on a firmware that knows only v1 | Refused as an unknown version; the running snapshot is untouched |
| The device restarts after an upload | The reply is written first; the reader re-opens the new image at boot |
| A selected Playlist disappears from a new image | Falls back to the tree's order, reported in status |
| A Playlist row's file is gone or resized | Skipped at playback; shown as missing in the editor |
| CDN unreachable, cache cold | Process says the analyser cannot start without a first download |
| A model's hash does not match | Refused, named in the log and on the page, and the analysis does not start |
| Card pulled mid-analysis | The queue's current file fails; the page keeps the route and says why |
| Device unreachable | Player and Media keep what they had and mark it stale; Process keeps its Queue; no error banner |
| The Provider rejects the key | The Intent is empty, the form stands alone, and the failure names the provider |
| The Provider does not allow this page's origin | Test and Ask say the endpoint answered, name the origin to allow and — for a Provider on this machine — the setting that allows it; a Provider that is up is never called unreachable |
| The Invidious Instance is down | The search says so and points at Settings |

---

## 8. Tests

| # | Test | Tier | Status |
|---|---|---|---|
| T1 | `/` serves the app, `/library` serves the index, unknown paths 404 | host | **proven** — `host/tests/front_door_tests.py` |
| T2 | A valid image posted to `/api/snapshot` lands byte for byte and is served after a restart | host | **proven** — `host/tests/upload_tests.py` |
| T3 | A corrupt, empty, JSON or oversized image is refused, partition untouched | host | **proven** — the same file |
| T4 | The same upload over the board's own listener | board | **proven by hand** |
| T5 | `/stream` and the app share port 80, and nothing listens on 443 | board | **proven by hand** |
| T6 | `/api/status` reports the card state within one poll of a removal and a reinsertion | board | **not proven** |
| T7 | A cold cache loads the weights once; a warm cache starts with the network off | browser | **not proven** |
| T8 | A tampered model is refused, by hash | host | **proven at the hash level** |
| T9 | A v2 image with lists is accepted; one with a list row out of bounds, unsorted ids, a reserved byte or a bad CRC is refused | host | **proven** — `t29_snapshot` (v2 fixtures and every refusal) and `playlist_tests.py` |
| T10 | `select` applies the list, `404`s an unknown id, `409`s an empty one, and a pick outside the list drops it | host | **proven** — `playlist_tests.py` |
| T10b | The choice is remembered across a reboot with a card mounted, and a stored id the new image lacks falls back | board | **not proven** — the board has had no card since the feature landed (the NVS write itself is proven: `select {"id":null}` answered 200 on the board, where the PSRAM-stack handler used to reset) |
| T11 | A v1 image still applies on a v2 reader | host + board | **proven** — `playlist_tests.py` (v1, no lists) and the board served its previous v1 image after the v2 firmware was flashed |
| T12 | Each hash route renders on a reload | browser | **proven** — headless Chrome loaded `#/process`, `#/playlists`, `#/media`, `#/player` and `#/settings` from the running board and each rendered its area (the shell, nav and page); `router.test.ts` covers the parse/format inverse |
| T12b | Process keeps its Queue across a navigation | browser | **not proven** — needs a measuring queue, which needs a card |
| T12c | A page load leaves the panel alone: no tearing while the shell is fetched, and no offset left behind | board | **not proven** — the image path is proven on the host (`t31_shell_image`, `front_door_tests.py`, and all 45 files of the real shell served byte-exactly), but the panel is the one thing only the board can answer (ADR 0055) |
| T13 | A Provider that answers without CORS is named as that, with the origin to allow; a dead one is named as dead | unit | **proven** — `src/provider.test.ts` (the probe and the two sentences); the CORS refusal itself was seen by hand against Ollama 0.40.13 at `http://localhost:11434`, `403` and no `Access-Control-Allow-Origin` for `Origin: http://xtape.local` |

---

## 9. Open measurements

1. The upload's write time on the board with a list section, and how long the PSRAM cache is off
   while the partition is erased and written.
2. The Shell's packed size after the sidebar, and whether the 1.25 MB partition still holds it
   with room for the Card area's code.
3. ~~`library_search` paging across a 5,000-track library~~ — resolved: the cap makes a full
   album index ~50 partial requests, so the index is the browser's cache and the device is asked
   only per opened album. A bulk album tool remains the alternative if a card is ever measured
   elsewhere and its albums must still appear.
4. The boot cost of resolving a 500-row Playlist against the library by path.

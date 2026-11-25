# The front door: one shell, four areas

> **The page half of this document is retired** by
> [ADR 0067](../adr/0067-chrome-only-direct-to-the-device.md): the device serves no page and
> packs no Shell, and `/`, `/index.html`, `/bridge` and `/app` now answer 302 to the App at
> `https://k7.xperiments.dev/app#/player` ([ADR 0078](../adr/0078-the-site-owns-the-root-and-the-app-moves-to-app.md)).
> The routes and contracts in §2 are the device's API and still hold; everything below about
> serving a page does not. The **one** page the device serves is `GET /setup`, which exists so a
> device with no network can be given one — specified in
> [`wifi-provisioning.md`](./wifi-provisioning.md) and decided by
> [ADR 0076](../adr/0076-the-network-is-chosen-on-the-devices-setup-network.md). `/` still
> redirects to the App on every interface, the setup network included.

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
the four areas, the hash routes, the Snapshot's list area, `GET /api/playlists`,
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
| `#/playlists` | Playlists | the selected card's Playlists and this browser's Drafts, one row per list |
| `#/playlists/:id` | Playlists | one Playlist's rows, order and actions, with the card's files beside them |
| `#/playlists/:id/ask` | Playlists | the same editor: the address of the retired Ask tab, which no longer names a tab |
| `#/media` | Media | albums, from the device's Snapshot rows |
| `#/media/album/:album` | Media | one album's tracks |
| `#/media/artist/:artist` | Media | one artist's albums |
| `#/media/video/:id` | Media | one Video, click to play |
| `#/player` | Player | cover, what is Playing, the device's facts (no sidebar entry) |
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
image carries measurements and nothing else (`format_version 5`; a **Playlist** is a file on the
card, [`playlist-format.md`](./playlist-format.md)). The content type is also the CSRF guard: it
is not CORS-safelisted, so another origin cannot send it without a preflight the device never
answers.

**U2.** `GET /api/playlists` reports the lists the card holds — id, name, created, row count and a
fingerprint of the file — and which one is selected. `GET /api/playlists/<id>` reports one list's
name and rows as paths. Both are `no-cache`, like the rest of `/api/*` but `/api/status`; both
read `k7.playlists/` and nothing else, and neither reads the card on a request: the device builds
its index when a card mounts and updates it on its own writes ([ADR 0094](../adr/0094-the-device-writes-the-lists-it-is-given.md)).

**U2a.** `PUT /api/playlists/<id>` takes the list's **file** as its body — the text of
[`playlist-format.md`](./playlist-format.md), `text/plain; charset=utf-8` — and creates or
replaces that file; `DELETE /api/playlists/<id>` removes it. The PUT writes the body to a
temporary file, validates it there with the reader the card is read by (the id inside must equal
the id in the path), and renames it onto the final name on the same volume; it answers
`{"ok":true,"id","rows","fingerprint"}` only once the file is on the card. `400` for a body that
is not a list or an id that disagrees, `503` when there is no card, `507` when the card is full,
`413` for a body over 64 KB. The body is the file rather than a JSON document so the device
stores exactly what it validated, with no second encoding between them. Deleting the selected list
clears the selection and the broadcast returns to the tree's order.

**U3.** `POST /api/playlists/select` takes `{"id": "<hex16>"}` or `{"id": null}` and tells the
broadcast which Playlist to walk. `404` for an id the card does not hold, `409` for a list
with no rows — or whose rows the Library cannot resolve at all — `503` when there is no card,
`400` for a body that is not that object. The choice is remembered in `k7.select` and survives a
restart; on boot a stored id the card no longer holds falls back to the tree's order and the
record is cleared. A refusal leaves the working selection alone.

**U4.** `POST /api/play` keeps its meaning: the broadcast moves to that Track. If a Playlist is
selected and the Track is not in it, the selection is **dropped** — the walk is over — and the
reply's `order` and the next `/api/status` say so. This is the one implicit mode change, and it
is the honest reading of "play this now".

**S1.** `GET /api/status` reports the **Card state**, the card's mark (`mark`: the `k7.card`
id, or `null`), the Library's count and generation, what is Playing (`title`, `path`,
`elapsed_ms`, `duration_ms`), and the order: `order` is `"tree"` or `"list"`, and `playlist` is
`{"id","name"}` or `null`. `rescanning` is `true` while a Library rescan is running, so the
App's Device card can say the card is being re-read and show the new generation when it
publishes. `listeners` is `{"used","slots"}`, the broadcast's stream slots taken and total: it
is how the page tells a listener the station refused from one whose link dropped, and waits
for the slot instead of reporting a dead stream. It is `no-store` and answers only on its own
path.

The deck also names itself. `id` is the board's Wi-Fi station MAC as twelve lowercase hex
characters, `fw` the running firmware's version and `board` the board's short name. `mark`
names the card and `id` names the deck: a home automation keys its entity on `id`, because it
is the same on every boot and every re-flash that keeps the settings, two boards never share
it, and `erase_flash` does not take it away. `fw` and `board` are there so a device page is
filled without a second request. All three are additive — a reader that does not know them
ignores them — and a board that cannot read its radio leaves `id` out rather than inventing
one.

**S2.** Every reply carries the device's address on the network as
`X-K7-Net: 192.168.1.42:80`, when it has one, and the CORS reply exposes it so a page can read
it. `k7.local` is a name the browser resolves over mDNS — or `k7-<name>.local` when a person has
named the deck, in which case `k7.local` keeps answering as an alias (ADR 0118) — and the App asks
the status on a timer:
resolving it every five seconds is a heartbeat that spends its time resolving. The header rides
on a reply the App was already receiving - the page load included - so the App learns the
address without asking for it and sends the rest of its requests, the stream included, there.
There is no route for this: an address that has to be requested separately is one more request
that can fail. The address is a lease, not a fact, so a request that fails to reach it forgets
it and the next one goes back to the name.

**S3.** `GET /api/snapshot` serves the image the device is answering from, verbatim, or `404`
with the reason when it has none — whether that image came over `POST /api/snapshot` or off the
card at its first boot. Its rows hold each Track's title, artist, album and genre beside its
measurements (snapshot-format.md), so a browser that has never met the card (a cleared store,
another machine) can join them to the library arena's paths and rebuild its albums. That is what
makes naming a card in the player possible without putting it in a computer; the pictures are
the cover archive's, restored separately. The device never parses the file it sends.


**U5.** There is **no TLS**. No certificate is compiled in, no trust step exists, and a browser
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
- **Groups.** Card, Playlists, Media, each a `SidebarGroup` with count badges the template
  already supports. There is no "Now playing" entry: the bar is the player. The template's
  "Projects" group and account block are removed: there are no projects and no accounts.
- **Footer.** Settings (`#/settings`) and the licence line that used to sit under every page.
- **Bar.** A player at the bottom of the inset, under every area: the cover, what is sounding,
  play/pause, a stop, a volume, and — while a card track is sounding and the player is emitting
  — a **go live** icon back to the broadcast; plus, for a card track, previous and next and a
  **seek that spans the bar**. A card play is the **album**: the bar is handed its tracks in
  order and one loader, so a track that ends asks for the next (the last one stops) and
  next/previous move through it. A stream has neither, because there is nothing to be next to.
  It has two sources and says which — the player's **stream** (`/stream`, live, no seek) or a
  **file off the card in the computer**, read through the card's handle (`card/read.ts`) — the
  one the page holds, or the one the browser remembers for that Scope, re-granted read-only on
  the click that plays — and played from a blob URL. Its one element lives in
  `PlaybackProvider`, not in the bar, so re-rendering an area never interrupts the sound and any
  page can start it. With no card in the player, `play` on an album row means this file; with
  one, it means the player, and the bar follows its broadcast.

**The card state chooses the first area, once.** A cold load with a card in the player opens
`#/media` — the bar already says what is playing; with no card, `#/process`. After that the sidebar is the user's. The S3 banner is
gone; what it protected is structural — **the Process area stays mounted while another route is
shown**, so navigating can never destroy a running Queue nor stop a worker. Every other area
mounts on demand.

### 4.1 Card — Process

The Queue, measured, written to the card by the run itself.

- **Header:** title, the run's state (`14 of 32 analysed`), the card the run belongs to (its
  mark, or a fingerprint when it has none), and the one fact that matters after it — *k7.db
  written 12:34*. There is nothing to send: the image is written to the card when the run
  finishes, and again when a Playlist changes while the card is still open (ADR 0065). What the
  header does offer is intake, because a card changes under the page: while the Queue has rows,
  **Rescan card** walks the card this page holds open again — the tracks it already has are
  recognised, not measured twice — and **Change card** opens the picker, replacing the queue with
  the new card's and the Scope with it. Clear, asked in place, is the last action. The handle is
  remembered per Scope (`card/handles.ts`), so a reload does not lose the card: Rescan asks for
  the read-write grant again on the click, which is the only moment a browser will show the
  prompt.
- **A reload does not read the card.** A remembered handle is not a reason to walk the card: that
  is a run, and a run is asked for. The empty Queue carries **Rescan card** beside *Choose card*
  (the drop zone is that state), so the first click after a reload asks for the grant and starts
  the read; most rows are recognised from the card's own record in milliseconds, so a resumed run
  continues rather than repeats. A **Pause** in the header holds a run in flight (the rows
  already handed out finish, nothing new is claimed) and becomes **Resume**; a new queue always
  measures again.
- **Progress:** one bar for the whole run, the count, and what is in flight. It is a run, not a
  page: it stays at the top while the rows scroll.
- **The measurement goes into the files.** A card opened read-write gets each settled track's
  standard tags — BPM, key, mood, energy — and the XTAPE manifest written into the file, with
  the label, before the image is written. A later run, on this card or on those files copied to
  another SD, reads BPM and key from the tags and skips the models; the manifest carries the
  rest. A dropped folder is read-only, so its row menu still offers *save a tagged copy*.
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
  image and the file tags back; a dropped card is measured read-only.
- **States:** empty (no Queue: the drop zone *is* the empty state, carrying **Choose card** and,
  when this browser remembers one, **Rescan card**), loading (rows appear as they are found;
  `Skeleton` only for the manifest), error (models missing or a weight refused, inline, with what
  to do).
- **Device work:** the run builds `k7.db` — the rows from the Queue, and no lists, which are files
  on the card — keeps it as the **last image**, and writes it to the card (ADR 0065,
  [ADR 0095](../adr/0095-a-playlist-is-a-file-on-its-card.md)).

### 4.2 Playlists

- **The area follows the card switcher.** With the **Device card** selected the player answers for
  its lists — `GET /api/playlists`, and `/api/playlists/<id>` for one list's rows; with an SD or
  virtual card selected, the `k7.playlists/` directory that card carries does. A card that is open
  in this session is read directly, and **Read the card** asks for the grant when someone wants its
  current bytes; a card that is open in neither place shows only this browser's pending edits, and
  says the card is not open — there is no mirror of a card's lists
  ([ADR 0095](../adr/0095-a-playlist-is-a-file-on-its-card.md)).
- `#/playlists` is **one list of lists**. Rows are joined by the list's id, so a list appears once;
  each row says how far it has **reached** — only here, on this card, on the player — and whether
  that copy is **current** (*edited — the card has the older copy*), or that a **pending removal**
  is waiting for the card. The actions follow the state: **Open** and **Delete** for what this
  browser holds, **Walk this** for the Device card's lists, and **Read the card** when the card has
  not been read. A card's list this browser has no draft for opens by being adopted: a draft with
  the same id, name, creation time and rows.
- **New playlist** is one action — a name in a dialog, then the editor. The empty state's one action
  is the same, and there is no second way to start a list.
- **Any card the player holds is editable.** A list there is opened from the player's rows, edited,
  and written back through the device; this browser does not need to have opened the card. The
  Device card's lists are no longer read-only, and *Walk this* is one action among them
  ([ADR 0094](../adr/0094-the-device-writes-the-lists-it-is-given.md)).
- **A list is the card's, and either side may write it.** With the card in the computer the editor
  writes the file through the card's handle; with the card in the player it sends the same list to
  the device, which writes it and answers once it is on the card
  ([ADR 0095](../adr/0095-a-playlist-is-a-file-on-its-card.md),
  [ADR 0094](../adr/0094-the-device-writes-the-lists-it-is-given.md)). The write is debounced after
  an edit, a refusal is shown and retried, and the page says where each list has got to. The image
  carries measurements still, and Prepare is its only door
  ([ADR 0065](../adr/0065-the-card-is-the-only-way-to-the-player.md)).
- `#/playlists/:id` is the editor, in two columns: the draft's blocks are on the left, and the
  card's folders as a **source tree** on the right, with two round icon views at that column's top
  — **Files** and **Results**, the second dead until a search or an answer has filled it — and the
  **IA Agent**'s bar at the tree's foot. The bar is one field and three icons: **Filters** for the
  exact arguments, the magnifier for the card's tree searched by the field's words, and **Ask** for
  the same words sent to the model — one field either way. A file adds
  one row; a folder adds one block that carries every file under it and
  moves as a unit. A drag from the tree lands exactly where it is dropped, a block reorders by drag
  (with arrow-key alternatives), and a folder block opens to show its rows. The order is the
  author's: nothing re-sorts it. **Clear** empties the list behind an inline confirmation, and
  removing a block is a block action; nothing here is destructive enough to need a dialog.
- **A row's facts come from the selected source**, not from the player: an SD or virtual card's
  rows are read from this browser's rows for that card and from the card's own image, so a row the
  card does not have reads *not in this card's library*. The Device card's rows come from the
  player (`library_track`), which is what the editor used for every card before this. A row the
  source no longer has is shown as missing and skipped by the broadcast, never deleted here.
- The assistant is that bar, and an answer fills the **Results** view: the rows it found, drawn as
  the list's own rows a size smaller and in the answer's order, with only the offer to add them
  under them. A search's results are the card's tree filtered by the field's words instead.
  Nothing on the card
  changes until the person acts: one action adds the answer's rows to the open list — appended at
  the end, in the answer's order — with **Save as a new list** as the secondary, which creates a
  list of its own and offers to open it. The model writes filters and never results, and never sees the list
  ([ADR 0050](../adr/0050-the-assistant-waits-for-a-secure-origin.md),
  [ADR 0053](../adr/0053-the-assistant-writes-filters.md),
  [ADR 0093](../adr/0093-ask-edits-the-list-it-is-beside.md)). The five states of the built-in
  model, the **Filters** sheet and the one-retry error live in the column, `#/playlists/:id/ask`
  lands on the editor because there is no tab to name any more, and `#/playlists/ask` redirects to
  the list of lists from the retired page.

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
- Album tiles are **cassette-shaped**: a case stands on its short end — 100 mm of tape, 64 mm
  tall — so a cover is `16:25`, taller than wide, and the grid is denser than a square one
  (`ALBUM_GRID`) because each tile is narrower than it is tall. Art is cropped to fill, never
  letterboxed.
- One album is **two columns**: the album itself on the left — cover, title, the artist as a
  link, tracks and genre — and what one does with it on the right, the track list with BPM, key
  and energy and the way to add a row to a Draft. Below `lg` they stack, cover first. A
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

Reachable at `#/player` and not in the sidebar: the bar is the player, so a page for it would be
a second place to look at the same fact. It stays as the place the device's own numbers live.

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

**Art packs.** The page also installs a user's **K7Art pack**: the file picker takes a
`.k7artpack`, the page shows its designs, the upload goes to `POST /api/art`, and **Reset to
factory** clears it. The device needs the `local-network` grant and the player's art route to
answer; with the card in the reader, or no player, the page says so and offers no upload. The
pack's format, its checks and the pool are [`art-packs.md`](./art-packs.md) §4.

---

## 5. What the page reads, and from where

| Fact | Source | Absent means |
|---|---|---|
| Card state, tracks, generation, Playing | `GET /api/status` | no device: the last answer, marked unreachable |
| The Library's tree | `library_list` over `/mcp` | no device: Media says so |
| Titles, artists, albums | this browser's `xtape-media` cache | "not measured", with Process |
| One track's measurements | `library_track` over `/mcp` | a dash, not an error |
| Playlists, the selection | the selected card: the player's `GET /api/playlists` for the Device card, the card's own `k7.playlists/` otherwise | this browser's pending edits, and the page says the card is not open |
| Covers | this browser's `xtape-media` store, else the active pool's J-card | the factory J-card, never an error |
| The active K7Art pack, the factory count | `GET /api/art` | no device: the factory J-cards only, and the page says the pack is unreachable |
| The active pack's J-cards | `GET /api/art/pack`, cached once per pack | the factory's bundled J-cards |
| Videos | the Invidious **Instance** | a note and the setting |
| A sentence's filters | the **Provider** | the filter form alone |

`/api/library` is not read by the page: it carries paths and sizes and no tags, so the MCP rows
are the only source of names.

---

## 6. What this does not change

- The cassette on the panel, `/stream`, `/library`, the Snapshot flow and the existing MCP tools.
- The global premise: the device still serves no Track and accepts none. A page is not a Track,
  and the card still leaves only as the live stream. An art pack is not a Track either:
  `POST /api/art` writes a bounded, CRC-checked art blob to flash, the device reads it in place
  and serves it read-only ([`art-packs.md`](./art-packs.md) §7, ADR 0111). It never touches the
  SD card.
- The host build's composition: it runs the same services, on the hand-written backend.
- The panel's Menu browses the library tree and walks the card's Playlists; nothing on the
  cassette screen names the selected Playlist
  ([ADR 0096](../adr/0096-the-menu-browses-the-library-and-walks-its-lists.md)).

---

## 7. Failure modes

| Condition | Behaviour |
|---|---|
| The firmware was built without a Shell | `/` answers `503` naming `/library`; every other route is unchanged |
| An unregistered `/api/…` path | `404`, never the page |
| A hash route the shell does not know | The shell's own not-found view, with the areas listed |
| An upload that is not a snapshot, or is a version this reader refuses | The reader's reason, `400`, partition untouched |
| An upload larger than the route allows | `413` before the body is read |
| An image from an older format version | Refused as an unknown version; the running snapshot is untouched |
| An art upload larger than the pack's 1 MB bound | `413` before the body is read |
| An art pack whose header, CRC or sheet fails | `422` with the reason; nothing is written; the factory stays |
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
| T9 | A v4 image with lists is accepted; one with a list row out of bounds, unsorted ids, a reserved byte or a bad CRC is refused | host | **proven** — `t29_snapshot` (v4 fixtures and every refusal) and `playlist_tests.py` |
| T10 | `select` applies the list, `404`s an unknown id, `409`s an empty one, and a pick outside the list drops it | host | **proven** — `playlist_tests.py` |
| T10b | The choice is remembered across a reboot with a card mounted, and a stored id the new image lacks falls back | board | **not proven** — the board has had no card since the feature landed (the NVS write itself is proven: `select {"id":null}` answered 200 on the board, where the PSRAM-stack handler used to reset) |
| T11 | A v1, v2 or v3 image is refused by a v4 reader; nothing applies across a version break | host | **proven** — `t29_snapshot` refuses all three |
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

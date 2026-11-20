# K7: a secure application that reaches the player directly

**Goal.** K7 is the App at `/app` on one secure origin — **installable from Chrome in one click
as an app**, offline-capable, and useful with no player in the room. The origin's root is the
**Site**, which offers the App's install and flashes the player's firmware
([`k7-site.md`](./k7-site.md)). The App reaches the device at
`http://k7.local` with its own `fetch`, gated on the browser's `local-network` permission: no
second window, no second document of its own, and no page it needs the device to serve — the
device's one page is the setup page, which exists only for a device that has no network yet
([ADR 0067](../adr/0067-chrome-only-direct-to-the-device.md),
[ADR 0076](../adr/0076-the-network-is-chosen-on-the-devices-setup-network.md)). The device is a
gateway, not the runtime.

The page the device used to serve is gone: **Front door**, **Launcher**, **Device copy**,
**Shell** and **Bridge** are retired words.

**Status:** M1–M3 are built and tested in this repository — the card's mark and the scoped
stores, installability, and the one offline weights store with a gated prefetch. The App is
deployed at `/app` on the origin ([`k7-deploy.md`](./k7-deploy.md) records what it answers), and
the device serves no page of it, so there is nothing of it to flash (ADR 0067). The `/app` split
is built; the Site at `/` is its first shape
([`k7-site.md`](./k7-site.md)). M4 is partly built: the direct transport exists; the
Library's image source answers Ask's search, the Playlists area reads and writes the selected
card's own list files, and the image is format 5 with no list area
([ADR 0095](../adr/0095-a-playlist-is-a-file-on-its-card.md)). Ask
is answered by the built-in model
([ADR 0084](../adr/0084-the-assistant-is-the-browsers-built-in-model.md)). The K7 look is
specified in [`k7-look.md`](./k7-look.md); LK-02 to LK-07 apply it to the App.

**Vocabulary:** [`web/CONTEXT.md`](../../web/CONTEXT.md). The device's own API is
[`front-door.md`](./front-door.md) §2 — its routes and contracts still hold, the page it once
served does not — and [`sd-stream-server.md`](./sd-stream-server.md); the Image is
[`snapshot-format.md`](./snapshot-format.md); the card's mark is
[`card-mark.md`](./card-mark.md). Its look is [`k7-look.md`](./k7-look.md). The origin decision
is [ADR 0057](../adr/0057-the-app-lives-at-a-public-origin.md); the direct transport is
[ADR 0067](../adr/0067-chrome-only-direct-to-the-device.md).

---

## 1. The pieces

| | **App** |
|---|---|
| Address | `https://k7.xperiments.dev/app` — the Site owns `/` ([`k7-site.md`](./k7-site.md)) |
| Served by | Vercel, with headers; the device serves no page of the App (`/setup` is the one
page it serves, and only for provisioning) |
| Secure context | yes |
| Owns | library processing, card writes, the PWA, every view, and the **Art packs** page that installs a K7Art pack and the cover pool it feeds |
| Never | serves a Track, accepts a Track upload |

**A1. One artifact, three documents.** `npm run build` produces the Site and the App from one
build, and builds the K7Art Editor (`tools/LabelEditor/`) into `/k7art/`; there is no device
packaging to keep in step with it. The editor's pack is a download the App installs
([`art-packs.md`](./art-packs.md)).

**A2. The App reaches the device over the local network.** `fetch('http://k7.local/api/…')` from
the HTTPS origin, allowed because LNA §2.5 exempts a `.local` host from mixed content once the
`local-network` permission is granted. Measured in Chrome 156 on the deployed origin
([`local.ts`](../../web/src/device/local.ts)).

**A3. The permission is asked for, and the App is refused until it is answered.**
`ui/LocalNetworkGate.tsx` blocks every area until
`navigator.permissions.query({name: 'local-network'})` is `granted`; a denial, or a browser with
no such API, gets a wall with instructions, never a fallback. The wall also points at the Site's
flash section for a person who has no player yet
([`k7-site.md`](./k7-site.md) S7).

A development build is the one exception: it is served from the LAN, where the permission is
not what gates a device request (LNA §2.2), so it mounts without the gate rather than waiting
on an answer Chrome will never ask for. The shipping build always gates (ADR 0082).

---

## 2. Origins and trust

**L1. The App trusts exactly one device origin: `http://k7.local`.** Not an IP, not another
port, not another host. Reached at `http://192.168.0.13`, the App's gate says the device needs
`k7.local`.

**L2. The permission is per origin.** `https://k7.xperiments.dev` is the origin that holds the
grant; a preview deployment is another origin and is never given one. The development origin is
served from the LAN, where the permission does not apply: its build mounts without it (ADR
0082).

**L3. The device's name is `k7.local`.** The mDNS hostname is one string in
[`appliance.c`](../../components/app/appliance/appliance.c). An unrenamed board serves nothing at
`xtape.local`, so there is nothing there to trust. The cost of the rename is a flash.

**L4. A development origin is a build input, never a runtime fact.**
`https://k7-dev.xperiments.dev:8000` — the port included, because an origin includes the port —
is what a development build is served from; a shipping build refuses it (§9, [`k7-deploy.md`](./k7-deploy.md)).
A development build also compiles the permission gate off, and a development firmware trusts
that origin beside the deployed one (ADR 0082); neither is selected at runtime.

**L5. The App is a public page.** Anyone may load it. That is safe because the App holds no
credential and can reach nothing on its own: a device request needs the `local-network` grant,
and the device's own CORS and routes decide what a granted origin may ask.

---

## 3. How the App describes itself

Three observed facts. There is no mode enum, and no precedence table.

**A4. `link`** — `local` (the device answered over the local network) or `none`. It is
observed, not chosen: while it is `none` the status poll tries the local path again, so a
player that was slow or off when the page loaded is picked up without a reload.

**A5. `network`** — `online` or `offline`, from the browser's own event.

**A6. `capabilities`** — `isSecureContext`, service worker, directory handles, OPFS, the
`local-network` permission, all read at boot and shown in Settings.

**A7. One chip composes them** — "Connected · k7.local", "Connected · player unreachable",
"No player — Connect", "Offline · cached". A feature is gated on the capability it needs, never
on the chip.

| Feature | Needs |
|---|---|
| Compose a virtual card (drop, measure, keep `k7.db` current), write it to an SD | secure context + directory handles |
| Pick a card, write `k7.db`, write the card mark | secure context + directory handles |
| Install, offline shell, weight prefetch | secure context + service worker |
| OPFS | secure context |
| Media and Playlists over the Last image | nothing but the store |
| Device facts, playlists, send, play, stream | `link !== none` |
| Ask | Chrome's built-in model, available (§7) |

**A8. `window.isSecureContext` is a readout, not an assertion.** The App reads it and gates the
features it forbids, instead of failing at the first call.

### Install

**A9. The App must install from Chrome as an app, not as a bookmark.** Chrome's own criteria,
all of which the deployment must satisfy on the App's origin and nowhere else: a manifest served
from that origin with `id: "/"`, `name` and `short_name` "K7", `start_url: "/app"` within
`scope: "/app"`, `display: "standalone"`, `theme_color`, `background_color`, a 192 and a 512
icon, and a 512 icon with `purpose: "maskable"` so an Android launcher does not crop the
cassette; and a registered service worker with a `fetch` handler — which the App has for its own
reasons.

**A10. Installing must be one click, offered where it can work.** `beforeinstallprompt` is
captured and replayed from an explicit **Install K7** control; `appinstalled` and
`display-mode: standalone` hide it once it is done. It is offered only where the App can run —
`navigator.permissions.query({name: 'local-network'})` is anything but `unsupported` — so Safari,
Firefox and every iOS browser get a sentence naming Chrome instead of an icon that opens a wall.
An optional wide `screenshot` in the manifest buys the richer install dialog.

**A11. The installed window is the normal case, not a downgrade.** An installed App has no
opener, and it does not need one: it reaches the device with its own `fetch`. Installing must be
presented as the recommended way to use K7, and **Connect** as what makes it work with a player.

**A12. The manifest is revalidated, never inferred.** It is served `no-cache` with the worker
and both entries ([`k7-deploy.md`](./k7-deploy.md) D4), and linked by the Site and the App alike:
a manifest is how Chrome decides what is installed, so a stale one is an install that never sees
new icons or the new start. `id` stays `/` — changing it is a different app to Chrome, and leaves
the old install behind; `start_url` and `scope` are `/app`, so the installed window is the App and
the Site stays a page of the website ([ADR 0078](../adr/0078-the-site-owns-the-root-and-the-app-moves-to-app.md)).

**A13. Installability is a deployment acceptance test.** Checked on the real origin, by hand,
once per release, with a Vercel preview expected to refuse: Chrome's
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

**S2. The selected card decides.** The **Device card** tile is the player's card: when it is
selected, the player is the source, because the player reflects the card as it is now. Otherwise
the player is the source when it reports the selected card's mark (`status.mark` matches the
scope). Otherwise the
**Last image**, if one exists for the scope; otherwise a linked player is the only source there
is; with neither, an empty state that says which is missing, never a spinner. A **Virtual card**
is always the image: the player cannot hold it.

**S3. The surface says which source it shows, and how much it measures.** "The player · 4 120
tracks · 3 460 analysed" and "Last image · 11 February · 4 118 tracks" are different facts and
must not look alike. A range only sees a track whose row carries the value (P9), so the player's
label counts the analysed tracks.

**S4. One conformance corpus, two implementations.** Filter semantics — BPM range, track
length, key, mood, energy, text, sort, offset, limit — are pinned as one JSON fixture of rows,
queries and expected results (`host/tests/snapshot-conformance.json`), read by the App's vitest
and by the device's host tests. Two implementations that agree by accident will disagree on
purpose within a month.

**S5. Covers always come from the Last image's scope.** The device serves paths and
measurements and never a picture; the media store is scoped by a card mark
([`card-mark.md`](./card-mark.md) §2). The Image is what the device may be walking, so it is
also the scope covers are looked up in. A *different* card in the player can therefore borrow
the last image's covers until the device reports its own mark in `status`
([`card-mark.md`](./card-mark.md) §6) — a known gap with a specified end, not a hidden one.

**S6. The selected card answers for its Playlists too.** A Playlist row is a path below one card's
`music/`, so the same selection that picks a library source picks the lists: the **Device card**'s
come from the player (`GET /api/playlists`, `/api/playlists/<id>`); an SD or virtual card's come
from its own `k7.playlists/`; and a card that is in neither place has no readable lists, so the
Playlists page shows only this browser's **Drafts** and says the card is not open. The page shows
each list once, joined by its id, with how far it has **reached** — only here, on this card, on the
player — and whether that copy is current, which the player answers with a fingerprint
([ADR 0092](../adr/0092-a-playlist-belongs-to-its-card.md)). Editing a list writes it: the file
when the card is in the computer, a request to the player when it is in the player
([ADR 0094](../adr/0094-the-device-writes-the-lists-it-is-given.md)).

---

## 5. The card

Card, Folder, the mark, the fingerprint and the write policy are
[`card-mark.md`](./card-mark.md). What this document adds is where they sit in the flow:

**C1. Pick → mark → scan → measure → build → deliver.** The writable pick creates or reads the
mark before anything else, so the whole run has a scope. The scan walks `music/`, the run
measures, the Image is built, and delivery is one of: write `k7.db` to the card, post it to the
device (`snapshot`), keep it as the Last image, or — for a virtual card — write the whole card
onto an SD ([ADR 0083](../adr/0083-a-virtual-card-is-a-staged-folder-under-a-working-directory.md)).
The card write and the post are independent.

**C2. The Last image belongs to a card.** It records the scope it was built for. Changing the
card in the computer does not silently retarget it.

**C3. A Playlist belongs to a card, and the card is its store.** A Playlist row is a path below a
card's `music/`, and the list itself is one file at `k7.playlists/<id>.k7list`
([`playlist-format.md`](./playlist-format.md)), written by the App when the card is in the computer
and by the player when it is in the player
([ADR 0095](../adr/0095-a-playlist-is-a-file-on-its-card.md),
[ADR 0094](../adr/0094-the-device-writes-the-lists-it-is-given.md)). The Image carries
measurements only. The browser's copy — the **Draft** — is the edit that has not reached the card
yet, not a home, and it is filed under the list's own card; a deletion is a **pending removal**
until the card takes it ([ADR 0092](../adr/0092-a-playlist-belongs-to-its-card.md)).

**C4. The current card is named in the header, and the header switches it.** The stage header's
right is the **card switcher**. The player's card — when it reports a marked one — and the SD in
this computer are two buttons; the virtual cards are one button when there is one and a
**Virtual cards** menu when there are several. The current unit takes the accent outline, and the
menu's trigger names the current virtual card, so every area says which card it is working on
without a visit to Prepare. Choosing a unit switches the card in place, without navigating; the
areas that follow the selected card follow it. A refusal to reopen a virtual card's working
directory leaves the previous card selected and says so. With nothing selected the switcher marks
no unit. The Media header names only the page — Albums, an album, an artist, Drive, Video — because
the switcher already names the card and the rail is the way back. The shelf in Prepare remains the
place that creates, renames, removes, writes and ejects.

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

**O7. Offline cannot cover one feature and says so:** the Invidious **Instance**, a network
feature wearing a local app's clothes. Ask is offline once the built-in model is installed; the
model's own download is its one network moment.

---

## 7. The assistant

**P1. One provider: the built-in model.** Chrome's Gemini Nano, reached through the Prompt API.
There is no endpoint, no key and no setting: a sentence becomes an **Intent** without a request.
[ADR 0084](../adr/0084-the-assistant-is-the-browsers-built-in-model.md) records the move.

**P2. The contract does not change.** The model produces an **Intent** and never results
(ADR 0050, ADR 0053); `parseIntent` judges the answer: unknown keys are dropped, a query that only
restates a key is dropped, and every number is clamped to the range the Snapshot stores. A
malformed answer gets one repair. A query that is not a name the sentence used is first read
from the sentence when a marker names one ("by X", "like X", "called X", "de X"); if no name is
there and another filter remains, the query is dropped; only when the query was the only filter is
the model asked once more with the sentence, and a word that is still not a name is dropped. A key
the model spells in words or symbols is written
the device's way before the search — "B flat major" and "B♭" become "Bb", "F sharp minor" and
"F♯m" become "F#m" — and a bare note takes its mode from the sentence when the sentence states
one; a key that cannot be read is left as written. The sentence's own count, listening-session
length, track length, BPM, offset, key or a genre it names wins over the model's: a pure extractor
reads it, and a field it finds replaces the model's field, while a field it does not find keeps the
model's value. A genre is matched as a whole word and read for its casing: a lowercase genre
always counts, a capitalised one counts at the start of the sentence or after a lowercase word
("Ambient music", "three hours of Ambient"), and a capitalised genre after a capitalised word is
read as the rest of a name, so the artist "Daft Punk" is not the punk genre and "the album
Discovery" is not disco; without a profile the static genre words apply, and with one the profile's
genres and the static words apply together. A genre the sentence never names is dropped:
a party mood is not the party genre, and a name is not a genre.
It runs only over a runnable Intent, so it never turns a refusal into a search, and it never writes
a name or a mood ([ADR 0088](../adr/0088-the-sentence-wins-on-the-numbers-it-states.md),
[ADR 0090](../adr/0090-a-genre-the-sentence-names-wins-like-its-numbers.md)).
A model answer with no filters is rebuilt from the sentence's own filters — a stated BPM, track
length, key, offset, count or listening session — and the purpose table when it names one; an
instruction to change the library ("add 5 songs to the playlist") is read like any other sentence,
and the rows it finds are added to the list the editor has open, because the App applies the Intent
and the model still writes no result
([ADR 0093](../adr/0093-ask-edits-the-list-it-is-beside.md)); a sentence with neither a purpose nor
a stated filter stays a refusal.
A sentence that names a purpose keeps only the filters its WORD→FILTER row prescribes; a kept bound
the table says is too loose is snapped to the table's value, and when no measurement filter is left
the row's filters and the count fill the Intent. The table's value is the App's, not the model's.
A mood the library does not have is left out: a prescribed mood is kept only when the profile has
it, keeping the row's numeric side, and a mood the model gave that the library lacks is dropped.
Without a profile the table stands.
When the exact search finds nothing, Ask retries once
without the numeric ranges: with a mood left to search on, as before; and — only when the Intent
carries three or more measurement ranges — with the pair the sentence's purpose word prescribes,
or with every range dropped when the sentence names no purpose. A hand-run Intent keeps the mood
rule only. Every key is
`library_search`'s own argument, and each is the data the row has: BPM is the folded 70–180, a
track's length is in seconds (the row keeps milliseconds), and `query` and `genre` are cut to the
device's byte ceilings (255 and 63), because a longer string would refuse the whole search. A
*track's* length is `duration_min`/`duration_max` in seconds; a *listening session's* length
("two hours of…") is `limit`, at 15 tracks an hour. The two are never the same key. The shape is
constrained by a JSON Schema passed as
`responseConstraint` — the spike showed Chrome accepts ranges, enums and `additionalProperties`,
measured on Chrome 157 — but the schema is a request and the validator is the judge. The schema is
not echoed into the model's input (`omitResponseConstraintInput`): the prompt's rules and turns
teach the shape, and echoing it made the model over-fill — the session screen measured 19 → 37
rows passing and 1850 → 1130 ms without the echo. When this
browser built the image the player is running (the same Snapshot generation), the prompt and
schema carry that library's aggregates instead: the genres become a closed enum, each axis's
ceiling becomes the library's, and each axis's floor is the library's too — except a track's
length, whose floor is the device's 0, not the library's shortest, so "under 30 seconds" is
still representable. The schema's floor for a track's length is therefore the device's, not the
library's. The other floors (BPM, energy, danceability, valence, arousal) and every ceiling stay
the library's; a sentence below one is refused by the schema — a known narrowness, reported
rather than widened silently. The schema leaves `key` unconstrained — the grammar is the judge —
so a key the library lacks is still sent and the search answers empty. The WORD→FILTER thresholds
its quartiles. Without a profile the
static ranges apply. The profile is aggregates only — genres, count, ranges and quartiles — and
no track, artist, path or per-track measurement is described either way.

**P3. The editor's sources column is two views — the card's folders, and the results — over one
bar that searches the card and asks the model.** The views are two round icon buttons at the
column's top: **Files** is the card's own tree, **Results** is what the last search or the last
answer found, and Results is dead until there is something in it — a direct search that ran, or an
answer that came back. An editor opens on Files; a new ask or a new search moves to Results and
stays there until the person moves away. One bar at the tree's foot serves both jobs: one field and
three icon buttons — **Filters** for the sheet of exact arguments, the **magnifier** for a search of
the card, which is local and instant, and **Ask** for the same words sent to the model, drawn only
where this browser has it. Enter does what the browser can do: Ask where there is a model, the
search where there is not. An empty field pressed with the magnifier is the way back to the whole
card.

The results view is not one shape for both. A search's results are the card's tree filtered by the
field's words, because a search is a way through the card. An answer's results are the rows
themselves, drawn as the list's own rows a size smaller — a number, the file, its BPM and key, a
plus — in the answer's order, because an answer is a set to choose from: pressing a row adds that
track at the end, and dragging one carries it to a place in the list the way any row does. Nothing
narrates either view: the tree's own header counts what
it holds — folders and tracks — and no heading, count line or source line repeats it. Over an
answer's tree sits only the offer to add its rows to the list beside, or what happened instead, and
the one thing the rows cannot say: that the ranges asked for found nothing and the retry dropped
them. The tree's shape is the card's, not the answer's order — that order is what **Add** appends,
and it is never re-sorted. A failure leaves the views where they are, with the sentence in place of
an answer, because there is nothing to show in Results. The model writes no prose (ADR 0050's shape
rule; ADR 0086), and [ADR 0093](../adr/0093-ask-edits-the-list-it-is-beside.md) still holds: Ask is
beside the list it edits, with no page and no rail item. **Filters** opens the exact
`library_search` arguments, seeded with the Intent that ran, for a misparse. There is no Intent
panel and no separate **Run search**. An answer that
names no group or artist is drawn from across the matched window, not from the search's first page:
Ask reads up to the player's 200-row window and keeps one track per artist while artists remain,
and when the window has fewer artists than the count it divides the rows between them as evenly as
the count divides — ten tracks from two artists are five and five, never three and seven — taking
each artist's share from across its own tracks rather than its first album. The answer is ordered
artist by artist in turn, so two rows side by side do not share an artist while another artist has
rows left; a run of one artist happens only when every other artist has run out. A sentence that
names a group or artist, and a stated offset or sort, keep their page. The selection is the App's, computed from the search; the model still writes no result, and
the Last image is drawn the same way from its own rows.
A model call that has not answered within thirty seconds is abandoned: the answer is an error
with the one retry, not a spinner that never ends.

**P4. Ask owns the model's setup.** Five states: no Prompt API (not Chrome 148+ on desktop),
`unavailable` (below the hardware floor, or built-in AI turned off in Chrome — the App cannot tell
which), `downloadable`, `downloading`, `available`. Ask shows one sentence for the first two, a
sentence and **Set up the assistant** for `downloadable`, progress for `downloading`, and the
composer for `available`. Nothing downloads on boot and nothing is prefetched; the **Filters**
sheet stands in every state. After the first ask the page holds one base session for the current
prompt and clones it for each question; the base is never prompted, and it is destroyed when the
prompt changes or the editor closes
([ADR 0089](../adr/0089-the-assistant-keeps-one-base-session-and-clones-it.md)).

**P5. The App can offer the model and cannot remove it.** The download needs a user gesture — the
setup's button is that gesture — and it is Chrome's component in Chrome's profile. The App can
start it and watch it; removal is in Chrome's settings, and the setup says so.

**P6. One session per Ask.** The session is created with the system prompt, prompts once and is
destroyed. No context carries between asks, and no session is held while the person browses. The
transcript is this tab's and is not sent back: each ask stands alone, it is not stored, and it dies
with the editor rather than with a page.

**P7. Nothing leaves the machine.** The sentence, the Intent, the profile and the library stay
where they are; the request is a local call, not a fetch. The profile is aggregate — genres,
count, ranges and quartiles — never a track, artist, path or per-track measurement.

**P8. The model's language set is Chrome's.** English, Japanese, Spanish, German and French at the
time of writing; `web/README.md` names the set. A misparse is visible in the rows that come back —
not in a sentence about them — and is fixable by asking again or opening **Filters**, which is the
rule (ADR 0053, ADR 0086).

**P9. A range sees only measured tracks.** Every measurement filter — BPM, a track's length,
energy, danceability, valence, arousal — needs a Snapshot row, so a track with no row still
matches a name search and an unknown value never matches a range. The
result count is the player's; the Player reports coverage. A filter the library has no values for
returns an empty result, not an error.

**P10. An answer adds to the list it is beside, and a set can still become its own list.** The
answer's one primary action appends the tracks it found to the draft the editor has open — at the
end, in the answer's order, repeats allowed, as the editor already allows them.
**Save as a new list** is the secondary: it creates a draft named from the sentence, and the
answer then offers to open it.
Neither is a separate act afterwards: the edit reaches the open list's card the moment it settles —
the file when the card is in the computer, a write through the player when it is in the player
([ADR 0095](../adr/0095-a-playlist-is-a-file-on-its-card.md)). A selected source with nothing to search — no card in the player and no image for
this scope — gets the reason and no rows. Nothing is added and nothing is saved without the
person's click.

---

## 8. Security posture

The device's trust rules are in §2 (`k7-deploy.md` D3, D14). What the App adds:

**X1.** Every device call goes through the transport seam. No view calls `fetch('/api/…')` and
no component knows an origin.

**X2.** The App fetches `http://k7.local` and nothing else. The `local-network` grant is the
only thing that lets it, and the device's CORS headers are what make the answer readable. A
development firmware also names the development origin in those headers; the deployed App
never asks for it and never sees it (ADR 0082).

**X3.** No `COOP: same-origin` on any App response. It would make the document cross-origin
isolated and change what the CDN's weights may load — a separate decision (`k7-deploy.md` D7),
not a hardening sweep. ORT stays single-threaded, and that is accepted.

**X4.** The App holds no secret. Nothing in this origin's storage is a credential — the Invidious
Instance, the worker count and the rail's state are not — and the built-in model needs none. The
deployed App's CSP remains a hardening layer, not a secret's keeper.

**X5.** The App is AGPL-3.0-only and ships its source offer at `SOURCE_URL`; a public origin
makes that a deployment requirement, not a nicety.

---

## 9. Packaging

| | The App |
|---|---|
| Entries | `index.html` is the Site, `app.html` is the App; the manifest's `start_url` is `/app` within `scope: "/app"` |
| Compression | plain; the host gzips |
| Weights | fetched from jsDelivr at the pinned tag |

**K1. There is no second packaging.** ADR 0067 removed the Shell: the build has one mode,
`vite build`, and produces `dist`.

**K2. The artifact carries the App, not the weights.** `pruneAppArtifact` drops `models/`,
`weights/` and `ort/*.wasm` from `dist` — the App fetches them from the CDN — keeping
`ort/ort-wasm-simd-threaded.mjs`, which `browser-runtime.ts` loads from `/ort/` on this origin.

**K3. The build refuses a shipping artifact carrying a development origin.** `k7-deploy.md`.

---

## 10. What this costs

- **No cross-origin isolation.** No `SharedArrayBuffer`, so ORT is single-threaded.
- **A public origin is a permanent dependency.** A fresh browser needs the internet once; after
  that the worker is the app.
- **The fallback is thin.** No service worker, no picker, and the App's code only
  because the device carries it. It is the App for a LAN with no internet, not the App for a new
  machine with no internet.
- **The weights are still 41 MB from a public CDN.** No design decision here changes that; O4
  only decides who pays for it.
- **The assistant is Chrome's, not the App's.** A machine below Chrome's floor has no Ask — the
  **Filters** sheet stands — and the App can offer the model but not remove it. The first ask was
  12.6 s on the spike's Mac; later answers were 1–5 s.
- **Assistant quality is measured, not promised.** Ten of twelve sentences were semantically right
  on the spike's rubric and all twelve were schema-valid; the duration rule and an artist name
  missed, and the model over-fills mood asks and invents `query` words. A tightened prompt scored
  8/12 and is not in the build. The answer says what it understood and **Filters** opens the exact
  arguments, so a miss is visible and fixable (ADR 0053, ADR 0086).
- **The conversation has no memory.** The transcript is a record, not context: "make it faster"
  after a set is a new ask. Carrying the previous Intent would change the one-shot prompt the
  10/12 describes, so it needs its own measurement before it ships. A reload clears the transcript;
  drafts survive.

---

## 11. Measurements to take before they are asserted

| What | Why it is not known |
|---|---|
| A `library.list` / `library.search` response for 5 000 tracks | no library that large has been measured: the Snapshot is **~280 KB at 5 000 tracks** (README), and the response beside it is not |
| ~~Shell image size with the Launcher and `app.html`~~ | measured: 392 783 bytes packed with the App copy; the Launcher alone is **14 613 bytes** (index.html + `launcher.js.gz` + `launcher-origins.js`) |
| A page load's black frame, now that the worker serves the shell | ADR 0055 measured 187 KB with the Launcher, which is retired ([ADR 0067](../adr/0067-chrome-only-direct-to-the-device.md)); the shell's own first paint is unmeasured |
| Writing `k7.card` on a real FAT32 card through the picker | the writer exists (`web/src/card/`, with `web/tests/card-write.test.ts`); no run against a real FAT32 card is recorded |
| Cold 41 MB prefetch over the LAN and over a WAN | the offline promise's first-run cost |
| The built-in model's first ask on a machine at Chrome's floor | the spike measured 12.6 s on one Mac; the floor is not measured |

---

## 12. Build order

> The milestones below predate [ADR 0067](../adr/0067-chrome-only-direct-to-the-device.md) where
> they name the Launcher, the Device copy or the Shell: read those words as retired.

**M1 — the channel, and an App that installs.** The manifest and its three icons (the rename,
the maskable pass), the Install control with its four states, the Launcher page (gateway and
bridge modes), the `/app` alias, the transport seam with `direct` and `bridge`, `status` and one
round trip, the capability readout. The manifest and icons belong here rather than in M3
because they are rename work touched regardless, and because a worker that installs is the same
worker that serves offline. Acceptance: the App opens from the Launcher; both origins'
allowlists hold; `window.isSecureContext` is shown; it installs from
Chrome's omnibox and from its own button and opens in its own window with the 512 maskable icon
uncropped (A9–A13); the CI manifest check is green; the Playwright COOP test passes.

**M2 — the card.** Directory pick with write, the mark, the scope, the fingerprint fallback,
the card writer for `k7.db`. Acceptance: a card is marked once and recognised next time; a
second card does not inherit the first's covers; a read-only card falls back to a fingerprint.

**M3 — offline.** Prefetch on standalone, persist, the update banner, the weights cache
collapse. Acceptance: a reload with the network off works; a model bump downloads one file
rather than 41 MB; a waiting worker produces the banner and not a takeover.

**M4 — connected, standalone, and the assistant.** All fifteen operations, the Library's second
source, the Device copy's gating, the built-in model. Acceptance: every area works with no device;
with a device, send/play/stream work; the built-in model produces an Intent with no key and no
network; with the model unavailable, Ask says what is missing and the **Filters** sheet works.

**M5 — the K7Art covers and the packs.** The App draws an album's no-cover face from the active
pool's J-card ([`art-packs.md`](./art-packs.md) §6), and the Art packs page imports a
`.k7artpack`, uploads it to `POST /api/art`, caches its J-cards and resets to the factory
([`art-packs.md`](./art-packs.md) §4). Acceptance: an album with no picture wears the pool's
J-card; a valid pack's designs join the covers after its upload; a bad pack changes nothing;
reset returns the factory; the card-in-reader mode offers no upload and keeps the factory covers.

---

## 13. Tests

- **vitest** — protocol state machines (§T1 of the bridge spec), the card mark's policy table,
  the fingerprint, the Image's `LibrarySource` over the conformance corpus, the Intent validator
  against malformed built-in model output (fixtures, not the model), and the packs and the
  covers: a `.k7artpack`'s header and CRC, the import/upload/reset, the J-card cache, the pool's
  modulo and the three fields, and the J-card renderer's roles, sizes, rotation and wrap
  ([`art-packs.md`](./art-packs.md) §8).
- **Playwright** — two origins: handshake each way, one round trip, COOP/opener regression,
  disconnect and reconnect, offline reload, a blocked popup. Plus an installability assertion
  where the test browser fires `beforeinstallprompt` — desktop Chrome and Edge do, the headless
  shell may not — with the manifest shape check in CI as the half that always runs.
- **host / Python** — `/`, `/bridge` and `/app` are 404; `/k7.db` and `/xtape.db` both update.

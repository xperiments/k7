# K7

The application at `/app` on `https://k7.xperiments.dev`, and the Site at `/` that offers it. It
measures a card's tracks, authors the Playlists a Snapshot carries, browses the library from the
player or from its own last run, and reaches the player directly over the local network. The
device is a different context — see [`CONTEXT-MAP.md`](../CONTEXT-MAP.md).

## Language

### The site

**Site**:
The public page at the origin's root: what K7 is, the player's firmware **Flash**, and the App's
**Install**. It never talks to the player — the board is written over USB, not the network.
_Avoid_: website, landing page, front page, home, portal, front door (that is the device's HTTP
surface)

**Flash**:
Writing the player's firmware over its USB connection, from the Site. A first install and a
re-flash are the same act; only an **Erase** differs, and an erase is its own explicit choice.
_Avoid_: install, upload, burn, update (the card's **Update file** is a different thing)

**Install**:
Adding the App to a launcher or home screen, where it opens in its own window. Offered only where
the App can run, which is Chrome.
_Avoid_: flash, download, save, add to home screen (that is the gesture, not the act)

### The application

**App**:
The application at `/app` on the HTTPS origin: its areas, its stores, its worker, and everything
it computes. Opened from its installed icon or by hand.
_Avoid_: website, frontend, web app, launcher

**Weights**:
Everything the analysis needs that is too big to ship: the models and the runtimes. Verified by
hash, cached in this browser.
_Avoid_: models, assets, blobs

**Area**:
One of the App's destinations: **Media Sources**, **Playlists** and **Player**. Media Sources
holds the **Sources** and **Albums** pages, with the device's facts when the **Device card** is
selected on Sources; Playlists holds the list of lists and the **Editor**, whose sources column
holds the **IA Agent**; Player holds **Now playing**. An area owns routes and nothing else; what
it shows it reads from the player, from this browser, or from both.
_Avoid_: page, tab, section

**Route**:
A hash path in the address bar — `#/prepare`, `#/playlists/:id`, `#/media/album/:album` — naming an
area and, sometimes, one thing in it. The App serves one document and knows no path; a route
exists only inside the App.
_Avoid_: URL, link, path

**Carousel**:
The **Albums** page's first view: the album shelf, the cases on their short ends with the selected one turned
to face the reader. The name is the walk along the row; there is nothing that rotates, swipes or
advances on a timer. The shelf has the whole stage; clicking a case opens that album's tracks
beside it — the shelf is centred under the header and grows to fill a tall stage while the rail's
first letter or genre starts at the stage's top — walking the
row with the arrows keeps them open, and a click outside, Escape or
the floating button at the panel's top-left closes them. The left/right arrow keys walk the row from anywhere in the Albums view — a text
field keeps its own arrows — and the pointer drags the row through the cases, each crossed
case turning the 3D with it; the release lands on the nearest case — the end is caught even when the pointer leaves
the shelf or the window, so the row cannot be left in hand. A press that never moved is still
a click, and that click opens the case's tracks. A letter in the index walks every case of the row to that artist's first album rather than
teleporting; the walk takes about a second within a letter and up to three across the
alphabet. The index can instead file the shelf by **genre** — an album's most common genre
tag — and then the rail's rows are the genre names. Choosing a genre narrows the shelf to
its albums; choosing it again shows them all, and the rail keeps every genre either way.
Coming to it from another area turns the case of the album the player is carrying, when that
album is in the shelf; a search, a letter or a click then chooses the case.
_Avoid_: slider, gallery, carousel widget

**Drive**:
The **Albums** page's second view: the card's folders as the tree their paths make, with the files the card
holds under them. An album is a title its rows share; a folder is where the card put the files,
and the two are not the same shape. `#/media/drive` is the card's `music` directory.
_Avoid_: file browser, explorer, file tree

### The link

**Link**:
Whether the App can command a player right now: **local** when the device answers over the local
network, or **none**. An observed fact, never a mode the user selects: while it is **none**, the
status poll tries the local path again, so a player that was slow or off at load is picked up
without a reload.
_Avoid_: mode, connection state, online

**Local network**:
The `local-network` permission a person grants this origin, which is what lets the App fetch
`http://k7.local` from its HTTPS origin (LNA §2.5). Without it the App is gated: no area renders.
A development build is served from the LAN, where the permission does not apply to a local→local
request, so it mounts without it (ADR 0082).
_Avoid_: LAN mode, device access

**Capabilities**:
What this browser and this origin can actually do — secure context, service worker, directory
access, OPFS, local network. Observed at boot, shown in Settings, and what every feature is gated
on.
_Avoid_: mode, support matrix, features

**Network**:
Whether this machine has internet. Independent of **Link**: a linked App can be offline, and an
unlinked one can be online.
_Avoid_: mode, offline mode, connectivity

### The card

**Card**:
A removable volume whose `music` directory is the player's Library, and which this browser can
name for itself. A card out of the player is still the card. A **Virtual card** is a Card the
App prepares in a **Working directory** before an SD is in the machine.
_Avoid_: SD card (except for the object), volume, library, drive

**Device card**:
The **Card** in the ESP32-S3. The browser never has its directory, so the **Sources** page can
only view it: selecting its tile or the switcher's Device button shows the device's facts there and
points the **Albums** page at that card's scope. It appears only while the device reports a card,
and only a marked one can be named.
_Avoid_: remote card, player card, S3 card

**Virtual card**:
A **Card** the App creates under a granted **Working directory**: the same `music/` plus
`k7.card`, `k7.db` and `k7.covers`, in a plain folder, with a chosen soft **capacity**. Its
finish act is **Write to SD**.
_Avoid_: image, container, virtual drive

**Working directory**:
The one read-write directory the user grants so the App can keep **Virtual cards** as folders
under it. Its write grant does not survive a browser restart, so it is re-asked once per
session.
_Avoid_: workspace, root, base folder

**Folder**:
A directory the user drops or picks that is not a Card. Scanned recursively for Tracks, measured
like any other, and never written to.
_Avoid_: directory, library, collection

**Card root**:
The directory of a Card that holds `music` and the card's own files. The only place the App
writes to a card.
_Avoid_: root, mount point, top level

**Card mark**:
A name a Card carries for itself, written by the App at its root, so this browser can tell one
card from another across sessions. It is a name, not a credential.
_Avoid_: CID, card id, signature, uuid, fingerprint

**Card fingerprint**:
A hash of a card's contents, used as a **Scope** only when no mark is usable. It identifies
contents, not the card: adding a Track changes it.
_Avoid_: card mark, checksum, hash

**Scope**:
Which card a stored thing belongs to — a mark's id or a fingerprint. What covers, a Draft's
resolution and the Last image are kept under. The App remembers the last scope it worked on,
because the pick cannot be repeated once the card is in the player
([ADR 0063](../docs/adr/0063-the-browser-remembers-the-card-it-last-worked-on.md)).
_Avoid_: namespace, partition, key

**Card switcher**:
The header's right: the **Device card** and the SD in this computer as two buttons, and the
**Virtual cards** as one button when there is one or a menu when there are several. It names the
current **Card** and changes it; the **Sources** page's shelf is where a card is created and
managed. The card last chosen is remembered across reloads; if the player no longer holds a
card it can name, the app falls back to the card this browser last worked on.
_Avoid_: card bar, card picker, workspace switcher, tab bar

**Update file**:
The image at a Card's root that carries a **Snapshot** into the player. The App writes it, the
player consumes it once and deletes it; nothing else writes a card's files.
_Avoid_: snapshot file, image file, config

**Scan**:
Reading a card that is in this computer: a picked directory walked for MP3 and FLAC, producing
**Queue** entries. Never writes. It happens on the **Sources** page — the drop zone is that page's intake,
not a page of its own.
_Avoid_: import, add files, load

**Rescan**:
The player rebuilding its **Library** from the card it holds, at `POST /api/rescan`. A different
act, on a different machine, than a **Scan**.
_Avoid_: refresh, reload, reindex

**Prepare**:
The work that prepares a **Card** — real or virtual — on the **Sources** page. It composes a
**Virtual card**'s music, measures the **Queue**, keeps a virtual card's `k7.db` current, and
writes a virtual card onto an SD.
_Avoid_: process, analyse, run, batch

**Compose**:
Copying dropped folders of music into a **Virtual card**'s `music/`, preserving the folder
structure, and measuring each file as its copy lands.
_Avoid_: import, ingest, add

**Write to SD**:
The finish act: copying a **Virtual card**'s contents into a picked SD's root — only the files
that changed since the last write — then verifying and reporting **safe to eject**.
_Avoid_: flash, burn, export

**Last image**:
The **Snapshot** this browser last wrote to a card, kept here so the next write costs no
re-measurement and so a Playlist edit can be written without re-running anything. It belongs to
one Card, and it is a **Library source** of its own.
_Avoid_: cache, backup, export

### The material

**Track**:
One MP3 or FLAC file submitted for analysis.
_Avoid_: song, item, audio file

**Relative path**:
A track's location inside the folder it was added from, including the file name. The content
hash, not the path, identifies the track.
_Avoid_: full path, filepath

**Snapshot path**:
A track's location below the card's `music` directory: the volume name and the `music` segment
dropped, `/`-separated, no leading slash. The string the Track fingerprint hashes.
_Avoid_: relative path (that one keeps the folder it was added from), absolute path

**Embedded metadata**:
Tags stored in the file itself — ID3 for MP3, Vorbis comments for FLAC.
_Avoid_: file info, tags (alone)

**Tagged value**:
A field taken from embedded metadata instead of computed. Marked as tagged and never silently
replaced by the analyser.
_Avoid_: cached value, existing field

**Tagged mood**:
A free-text mood label taken from embedded metadata, used instead of the six model probabilities.
_Avoid_: mood tag (use for the model output)

**Content hash**:
The SHA-256 of a track's original bytes. Identifies the file independently of its name, its path,
and which card it is on.
_Avoid_: checksum, file id

**Model set**:
The exact models used for one analysis, identified as a set by their hashes.
_Avoid_: models, weights

**Analyzer version**:
The version of the analysis pipeline. With the content hash and the model set, it decides whether
a cached analysis is still valid.
_Avoid_: app version

### The measurements

**Analysis**:
The complete set of measurements for one track.
_Avoid_: result, metadata, features

**Raw tempo**:
The detector's BPM before octave folding.

**Folded tempo**:
The raw tempo moved into 70–180 BPM by doubling or halving — the tempo presented to the user.
_Avoid_: corrected tempo, bpm (reserve "BPM" for the number itself)

**Tempo confidence**:
A 0–1 heuristic from beat-interval consistency, used to decide whether deep analysis is needed.
Not a calibrated probability.
_Avoid_: accuracy, score

**Deep analysis**:
A re-analysis with the accurate tempo detector, triggered automatically when tempo confidence is
low, or requested for one track.
_Avoid_: retry, re-analysis

**Key**:
A tonic and a mode, for example A minor.

**Camelot code**:
The harmonic-mixing notation for a key, for example 8A. Derived from the key, not an independent
measurement.
_Avoid_: open key, wheel code

**Key strength**:
The key detector's correlation strength. Not a probability.
_Avoid_: confidence

**Mood tag**:
One of six binary classifier outputs — happy, sad, aggressive, relaxed, party, danceable. A
probability, not a label.
_Avoid_: mood, emotion

**Valence**:
Pleasantness, 0–1, from the mood model.

**Arousal**:
Intensity, 0–1, from the mood model.

**Energy**:
The name the interface gives to arousal — or the value of an energy tag when one is present. It is
not an independent model output when tagged.
_Avoid_: intensity (unless explaining the term)

### The work

**Queue**:
The tracks waiting to be analysed, in submission order.
_Avoid_: batch, library

**Tagged copy**:
A new file produced by writing an analysis into a copy of the original; the original is left
untouched.
_Avoid_: saved file, overwrite

**Stored analysis**:
The lossless analysis this app writes into a file's own tags — automatically when a card is
opened read-write — and reads back instead of measuring again. Files tagged before the rename
carry it under the old name and are still read.
_Avoid_: cache, saved result

**Cached analysis**:
An analysis reused because its content hash, analyzer version and model set all match.
_Avoid_: saved result

**Worker pool**:
The analysis threads **Prepare** runs, each with its own copy of the models. Its size is a
**Setting**: auto by default — half the logical cores, capped by the memory the browser
reports — or a pinned count. At boot the App starts that many workers and keeps the ones that
answer a small probe.
_Avoid_: thread pool, concurrency, parallelism

### The library

**Playlist**:
An ordered selection of the Library's Tracks that a person authored and named, reorderable here
and stored as one file on the card it belongs to, at `k7.playlists/<id>.k7list`. It belongs to the
card whose Tracks it names, and that card is where it lives: the App reads it from the card when
the card is in the computer, from the player when it is in the player, and says how far it has got.
The player owns its playback; the App owns its authoring.
_Avoid_: setlist, crate, mixtape, queue (that one is the analyser's)

**Playlist row**:
One Track's place in a **Playlist**, named by its path below the card's `music` directory. A row
whose path is no longer on the card is skipped, never silently dropped.
_Avoid_: entry, item, slot

**Draft**:
A **Playlist** whose last edit has not reached its own card yet — because the card is out, the
player is not answering, or the write was refused. It is this browser's pending edit, filed under
the **Card** whose Tracks it names, and it stops being a draft the moment the card takes it. A
list on a card or on the player is read into one while it is being edited, keeping its id, name,
creation time and rows.
_Avoid_: unsaved, local copy, working set

**Reach**:
How far a **Playlist** has got: only here, on its card, on the player. Where it is and whether the
copy there is current are separate facts — a list can be on the player and edited here since.
_Avoid_: state, status, sync state

**Block**:
One row of the Playlist editor's list: a Track, or a Folder carrying the Tracks it was added
with. Moving a block moves everything it holds; the **Snapshot** still receives the flattened
rows, so a block is an authoring idea and not a device one.
_Avoid_: group, folder row, item

**Cover**:
The picture a Track carries, kept in this browser because the player serves paths and
measurements and never a picture. A Track with no cover is ordinary, not an error.
_Avoid_: artwork, album art, thumbnail

**Library source**:
Which of the two answers a library question — search, list, stats, one Track: the **player**, which
reflects the card in it now, or the **Last image**, which reflects the last run this browser made.
_Avoid_: backend, data source, database

**List source**:
Which answer carries a card's **Playlists**: the player for the **Device card**, the card's own
`k7.playlists/` while it is in the computer, and nothing else — a card that is in neither place
shows only this browser's **Drafts**, because there is no copy of its lists here. The selected card
decides, as it does for a **Library source**.
_Avoid_: backend, data source, database

**Pending removal**:
A **Playlist** deleted here that its card still carries. The row says so until the card is
written, and the write ends it.
_Avoid_: queued delete, tombstone, soft delete

**Playing**:
The Track the live stream is carrying at this moment, as the player reports it. A fact about the
broadcast, not a control.
_Avoid_: now playing, current song, track

### The assistant

**IA Agent**:
The assistant in the **Editor**'s sources column: the composer at the foot of the card's tree, and
the last answer drawn over that tree. It is not a page, has no item of its own in the rail, and is
no longer a tab — the tree stays where it is. The conversation on it is **Ask**.
_Avoid_: Ask page, assistant page, AI Agent, Ask tab

**Ask**:
The conversation on the **IA Agent**: the composer at the foot of the tree, what the person said,
and the App's answer — the rows it found, drawn in the tree, and the offer to add them to the list
beside it. The model writes the **Intent**; the App writes what Ask says.
_Avoid_: chatbot, AI assistant, generate

**Intent**:
The `library_search` filters a sentence was parsed into — BPM range, key, mood, energy, text, sort
and a count. It is not a panel on the page: the answer says what it understood, and **Filters**
opens the exact arguments. The model writes filters, never results: a wrong filter is visible and
correctable, a confident paragraph about music that does not exist is not (ADR 0050's rule,
adopted in ADR 0053; the conversation is ADR 0086).
_Avoid_: query, prompt, answer

**Built-in model**:
The language model Chrome ships and runs on this machine, which turns an **Ask** into an
**Intent** with no key and no request. It is not the **Model set**, which belongs to the analysis.
_Avoid_: provider, remote provider, local provider, local model, LLM, AI, cloud

**Settings**:
What this browser remembers for itself: the Invidious **Instance**, the analysis worker count, and
whether the rail is collapsed. Lost with site data, never written to the player.
_Avoid_: preferences, config, options

### The videos

**Video**:
A YouTube search result: something to watch about a Track, never a Track. It plays inline on
its own route, in YouTube's no-cookie player, created only when someone presses play. It cannot
become a **Playlist** row and the player never sees it.
_Avoid_: track, song, media

**Instance**:
The Invidious server the video search talks to, chosen in **Settings** because public instances come
and go.
_Avoid_: server, mirror, proxy

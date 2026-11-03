# K7

The application at `https://k7.xperiments.dev`, and the small page the player serves beside it.
The App measures a card's tracks, authors the Playlists a Snapshot carries, browses the library
from the player or from its own last run, and is usable with no player in the room. The Launcher
is the player's own page: it shows what the device is doing, carries the broadcast's audio, and
does what the App asks it to. The device is a different context — see
[`CONTEXT-MAP.md`](../CONTEXT-MAP.md).

## Language

### The two pages

**Front door** is retired: it named one page where there are now two, and it is not a synonym for
either.

**Launcher**:
The page the player serves on plain HTTP. At its root it only sends the browser to the App —
the player's address is a doorbell. At `/bridge` it is the window that executes the App's
requests and carries the stream's audio, for a browser that cannot reach the device directly.
It holds no analysis and no library.
_Avoid_: front door, device UI, landing page, dashboard

**App**:
The application at the HTTPS origin: its areas, its stores, its worker, and everything it
computes. Opened from the Launcher, from its own installed icon, or by hand.
_Avoid_: front door, website, frontend, web app

**Device copy**:
The App's own bytes served by the player, for a network with no internet. The same code under
its origin's limits: no worker, no card write, no WebGPU, never installable.
_Avoid_: fallback app, offline mode, backup copy

**Shell**:
The App's code as one packed image in the player's flash — the Launcher and the Device copy.
What the player serves that is ours, and nothing else.
_Avoid_: bundle, frontend, page

**Weights**:
Everything the analysis and the Local Provider need that is too big to ship: the models and the
runtimes. Verified by hash, cached in this browser.
_Avoid_: models, assets, blobs

**Area**:
One of the App's destinations: **Card**, **Playlists**, **Media** and **Player**. An area owns
routes and nothing else; what it shows it reads from the player, from this browser, or from both.
_Avoid_: page, tab, section

**Route**:
A hash path in the address bar — `#/process`, `#/playlists/:id`, `#/media/album/:artist/:album` — naming an
area and, sometimes, one thing in it. The player serves one page and knows no path; a route
exists only inside the App.
_Avoid_: URL, link, path

### The link

**Link**:
Whether the App can command a player right now: **bridged** through a Launcher, **direct** because
it is the Device copy, or **none**. An observed fact, never a mode the user selects.
_Avoid_: mode, connection state, online

**Bridge**:
The channel between the App and the Launcher: the App's requests and the Launcher's answers, in a
closed table of operations. It carries no stream, no push and no arbitrary request.
_Avoid_: pipe, proxy, socket, API, tunnel

**Gateway**:
The Launcher as it is when no trusted App has opened it: it reports the device and offers to open
the App. One of its two faces; the other is the **Bridge window**.
_Avoid_: home page, menu

**Bridge window**:
The Launcher once a trusted App has opened it: it executes the App's requests and carries the
stream's audio, and closing it disconnects the App.
_Avoid_: worker, agent, service

**Capabilities**:
What this browser and this origin can actually do — secure context, service worker, WebGPU,
directory access, OPFS. Observed at boot, shown in Settings, and what every feature is gated on.
_Avoid_: mode, support matrix, features

**Network**:
Whether this machine has internet. Independent of **Link**: a bridged App can be offline, and a
standalone one can be online.
_Avoid_: mode, offline mode, connectivity

### The card

**Card**:
A removable volume whose `music` directory is the player's Library, and which this browser can
name for itself. A card out of the player is still the card.
_Avoid_: SD card (except for the object), volume, library, drive

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

**Update file**:
The image at a Card's root that carries a **Snapshot** into the player. The App writes it, the
player consumes it once and deletes it; nothing else writes a card's files.
_Avoid_: snapshot file, image file, config

**Scan**:
Reading a card that is in this computer: a picked directory walked for MP3 and FLAC, producing
**Queue** entries. Never writes. It happens in **Process** — the drop zone is that area's intake,
not a page of its own.
_Avoid_: import, add files, load

**Rescan**:
The player rebuilding its **Library** from the card it holds, at `POST /api/rescan`. A different
act, on a different machine, than a **Scan**.
_Avoid_: refresh, reload, reindex

**Process**:
The area that measures the **Queue** and builds the image carrying measurements and **Playlists**
to the player.
_Avoid_: analyse, run, batch

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
The lossless analysis this app writes into a file's own tag and reads back instead of measuring
again. Files tagged before the rename carry it under the old name and are still read.
_Avoid_: cache, saved result

**Cached analysis**:
An analysis reused because its content hash, analyzer version and model set all match.
_Avoid_: saved result

### The library

**Playlist**:
An ordered selection of the Library's Tracks that a person authored and named, reorderable here
and sent to the player inside a **Snapshot**. The player owns its storage and its playback; the
App owns its authoring.
_Avoid_: setlist, crate, mixtape, queue (that one is the analyser's)

**Playlist row**:
One Track's place in a **Playlist**, named by its path below the card's `music` directory. A row
whose path is no longer on the card is skipped, never silently dropped.
_Avoid_: entry, item, slot

**Draft**:
A **Playlist** as this browser holds it, before it has been sent. It belongs to the user, not to a
card.
_Avoid_: unsaved, local copy, working set

**Cover**:
The picture a Track carries, kept in this browser because the player serves paths and
measurements and never a picture. A Track with no cover is ordinary, not an error.
_Avoid_: artwork, album art, thumbnail

**Library source**:
Which of the two answers a library question — search, list, stats, one Track: the **player**, which
reflects the card in it now, or the **Last image**, which reflects the last run this browser made.
_Avoid_: backend, data source, database

**Playing**:
The Track the live stream is carrying at this moment, as the player reports it. A fact about the
broadcast, not a control.
_Avoid_: now playing, current song, track

### The assistant

**Ask**:
The prompt box in Playlists: the user's words, sent to the **Provider**, answered with an
**Intent** and nothing else.
_Avoid_: chat, assistant, generate

**Intent**:
The `library_search` filters a sentence was parsed into — BPM range, key, mood, energy, text, sort
and a count — shown on screen and editable before anything is searched. The model writes filters,
never results: a wrong filter is visible and correctable, a confident paragraph about music that
does not exist is not (ADR 0050's rule, adopted in ADR 0053).
_Avoid_: query, prompt, answer

**Remote Provider**:
The OpenAI-compatible endpoint — base URL, model and key — the **Intent** is asked of, held in this
browser's settings and never sent to the player. No track is ever described to it.
_Avoid_: API, backend, model, cloud

**Local Provider**:
A model running in this browser, with no key and no network: the same **Intent**, asked of a
machine that cannot tell anyone. Needs WebGPU, so the **Device copy** can never have one.
_Avoid_: local model, offline model, embedded AI

**Settings**:
What this browser remembers for itself: the **Provider**, the model, and the Invidious
**Instance**. Lost with site data, never written to the player.
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

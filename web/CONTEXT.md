# Front door

The page the device serves at `http://xtape.local` and the app behind it: one shell with four
areas — the card, playlists, the library's albums and artists, and the player. It measures
tempo, key, mood and energy for a user's own audio files, authors Playlists, and shows what the
broadcast is doing. It stores nothing on the card and serves no Track; the device is a different
context — see [`CONTEXT-MAP.md`](../CONTEXT-MAP.md).

## Language

### The page

**Front door**:
The page the device serves at its only address, and the app behind it. The device's network way
in, as the **Cassette screen** is the panel's.
_Avoid_: web UI, dashboard, admin page

**Area**:
One of the front door's four destinations: **Card**, **Playlists**, **Media** and **Player**. An
area owns routes and nothing else; what it shows it reads from the device, from this browser, or
from both.
_Avoid_: page, tab, section

**Route**:
A hash path in the address bar — `#/process`, `#/playlists/:id`, `#/media/video/:id` — naming an
area and, sometimes, one thing in it. The device serves one page and knows no path; a route
exists only inside the shell.
_Avoid_: URL, link, path

**Shell**:
The front door's own code, served by the device from its flash partition: the page, the bundle,
the manifest and the weights manifest. What `web/` builds that belongs on the device.
_Avoid_: app, frontend (alone)

**Weights**:
Everything the analysis needs that is too big to ship: the models and the runtimes. Fetched from
a pinned release, verified by hash, and cached in this browser.
_Avoid_: models, assets, blobs

### The card

**Scan**:
Reading a card that is in this computer: a dropped folder or the folder picker, walked for MP3
and FLAC, producing **Queue** entries. A host act that never writes to the card.
_Avoid_: import, add files, load

**Rescan**:
The device rebuilding its **Library** from the card it holds, at `POST /api/rescan`. A different
act, on a different machine, than a **Scan**; it shares the area only because it shares the card.
_Avoid_: refresh, reload, reindex

**Process**:
The area that measures the **Queue** and builds the image carrying measurements and **Playlists**
to the player.
_Avoid_: analyse, run, batch

**Last image**:
The **Snapshot** this browser built — and, once sent, the one the device accepted — kept in this
browser so sending again costs no re-measurement.
_Avoid_: cache, backup, export

### The material

**Track**:
One MP3 or FLAC file submitted for analysis.
_Avoid_: song, item, audio file

**Folder**:
A directory the user drops or picks. Scanned recursively for tracks. A run that produces a
**Snapshot** is given the card's root, and scans only its `music` directory.
_Avoid_: directory, library

**Relative path**:
A track's location inside the folder it was added from, including the file name. Equals the
file name for a single-file drop. The content hash, not the path, identifies the track.
_Avoid_: full path, filepath

**Snapshot path**:
A track's location below the card's `music` directory: the volume name and the `music` segment
dropped, `/`-separated, no leading slash. It exists only in a run that produces a **Snapshot**,
and it is the string the Track fingerprint hashes — see
[`snapshot-format.md`](../docs/specs/snapshot-format.md) §6.
_Avoid_: relative path (that one keeps the folder it was added from), absolute path

**Embedded metadata**:
Tags stored in the file itself — ID3 for MP3, Vorbis comments for FLAC.
_Avoid_: file info, tags (alone)

**Tagged value**:
A field taken from embedded metadata instead of computed. Marked as tagged and never silently
replaced by the analyser.
_Avoid_: cached value, existing field

**Tagged mood**:
A free-text mood label taken from embedded metadata, used instead of the six model
probabilities.
_Avoid_: mood tag (use for the model output)

**Content hash**:
The SHA-256 of a track's original bytes. Identifies the file independently of its name or
path.
_Avoid_: checksum, file id

**Model set**:
The exact ONNX models used for one analysis, identified as a set by their hashes.
_Avoid_: models, weights

**Analyzer version**:
The version of the analysis pipeline. Combined with the content hash and the model set, it
decides whether a cached analysis is still valid.
_Avoid_: app version

### The measurements

**Analysis**:
The complete set of measurements for one track.
_Avoid_: result, metadata, features

**Raw tempo**:
The detector's BPM before octave folding.

**Folded tempo**:
The raw tempo moved into 70–180 BPM by doubling or halving — the tempo presented to the
user.
_Avoid_: corrected tempo, bpm (reserve "BPM" for the number itself)

**Tempo confidence**:
A 0–1 heuristic from beat-interval consistency, used to decide whether deep analysis is
needed. Not a calibrated probability.
_Avoid_: accuracy, score

**Deep analysis**:
A re-analysis with the accurate tempo detector, triggered automatically when tempo
confidence is low, or requested for one track.
_Avoid_: retry, re-analysis

**Key**:
A tonic and a mode, for example A minor.

**Camelot code**:
The harmonic-mixing notation for a key, for example 8A. Derived from the key, not an
independent measurement.
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
The name the interface gives to arousal — or the value of an energy tag when one is present.
It is not an independent model output when tagged.
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
The lossless analysis this app writes into a file's `XTAPE` tag and reads back instead of
measuring again.
_Avoid_: cache, saved result

**Cached analysis**:
An analysis reused because its content hash, analyzer version and model set all match.
_Avoid_: saved result

### The library

**Playlist**:
An ordered selection of the Library's Tracks that a person authored and named, reorderable here
and sent to the device inside a **Snapshot**. The device owns its storage and its playback; this
page owns its authoring.
_Avoid_: setlist, crate, mixtape, queue (that one is the analyser's)

**Playlist row**:
One Track's place in a **Playlist**, named by its path below the card's `music` directory. A row
whose path is no longer on the card is skipped, never silently dropped.
_Avoid_: entry, item, slot

**Draft**:
A **Playlist** as this browser holds it, before it has been sent. Only sending an image turns a
Draft into the device's copy.
_Avoid_: unsaved, local copy, working set

**Cover**:
The picture a Track carries, kept in this browser because the device serves paths and
measurements and never a picture. A Track with no cover is ordinary, not an error.
_Avoid_: artwork, album art, thumbnail

### The player

**Playing**:
The Track the live stream is carrying at this moment, as the device reports it. A fact about the
broadcast, not a control.
_Avoid_: now playing, current song, track

### The assistant

**Ask**:
The prompt box in Playlists: the user's words, sent to the **Provider**, answered with an
**Intent** and nothing else.
_Avoid_: chat, assistant, generate

**Intent**:
The `library_search` filters a sentence was parsed into — BPM range, key, mood, energy, text,
sort and a count — shown on screen and editable before anything is searched. The model writes
filters, never results: a wrong filter is visible and correctable, a confident paragraph about
music that does not exist is not (ADR 0050's rule, adopted in ADR 0053).
_Avoid_: query, prompt, answer

**Provider**:
The OpenAI-compatible endpoint — base URL, model and key — the **Intent** is asked of, held in
this browser's settings and never sent to the device. No track is ever described to it.
_Avoid_: API, backend, model

**Settings**:
What this browser remembers for itself: the **Provider** and the Invidious **Instance**. Lost
with site data, never written to the device.
_Avoid_: preferences, config, options

### The videos

**Video**:
A YouTube search result: something to watch about a Track, never a Track. It cannot become a
**Playlist** row and the device never sees it.
_Avoid_: track, song, media

**Instance**:
The Invidious server the video search talks to, chosen in **Settings** because public instances
come and go.
_Avoid_: server, mirror, proxy

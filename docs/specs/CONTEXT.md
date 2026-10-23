# Stream server

An ESP32-S3 that owns a music library on a microSD card and serves it over HTTP. This context
covers the **origin** side: the device that produces a stream and hands out files. The
consumer side is a separate context — see [`CONTEXT-MAP.md`](../../CONTEXT-MAP.md).

## Language

### The library

**Music directory**:
The root directory on the mounted card. Its contents, recursively, define the library; the
directory is not itself the library. There is no database.
_Avoid_: library root, media path

**Library tree**:
The directories and tracks under the music directory, as one ordered tree. The playlist and the
JSON view are views of it.
_Avoid_: file list, index

**Track**:
One playable audio file in the library tree, in any of the three accepted source formats.
_Avoid_: song, item, entry

**Playlist**:
The ordered sequence of tracks the broadcast plays: depth-first, name-sorted, derived from the
library tree. It is not stored separately.
_Avoid_: queue, play list, rotation

**Source format**:
The container and codec a track is stored in — MP3, FLAC or WAV. What the card holds, not what
the server sends.
_Avoid_: input codec, file type

**Original bytes**:
A track as stored, unmodified. What a download returns, as distinct from what the stream
carries.
_Avoid_: raw file, raw audio

### The broadcast

**Station format**:
The single output format every track is converted to before it goes on the wire: MP3, at one
sample rate and one bit rate. Chosen at boot and fixed for the life of the process.
_Avoid_: output format, stream format, target format

**Station rate**:
The sample rate of the station format. Every track is resampled to it regardless of its own
rate.
_Avoid_: output rate, resample target

**Encoder frame**:
The unit the MP3 encoder consumes and emits — 1152 samples per channel. Not the same quantity
as the server's internal chunk size, and the two do not divide evenly.
_Avoid_: block, packet, MP3 frame

**Live stream**:
The single continuous broadcast every client shares. Every listener hears the same track at
the same moment, so it cannot be paused, seeked or replayed.
_Avoid_: feed, radio, channel

**Chunk**:
A fixed-size parcel of decoded audio the server produces internally on its way to the encoder.
It is an implementation quantity of the broadcast, invisible to listeners.
_Avoid_: buffer, block, window

**Metadata block**:
The announcement that names the currently playing track, interleaved into the live stream at
fixed intervals for clients that asked for it. Meaningful only because the stream is MP3.
_Avoid_: tag, ID3, title block

**Metadata interval**:
The number of *encoded* stream bytes between metadata blocks. A byte count, not a time.
_Avoid_: metaint length, ICY period

**Burst**:
The recent audio history sent immediately to a client on connect, so playback starts at once
instead of waiting for the next track. It costs memory proportional to its length.
_Avoid_: prebuffer, backfill, catch-up

**Lead**:
How far ahead of real time the server transmits, so that a client's own buffering can absorb
network jitter without an audible gap.
_Avoid_: buffer, latency, pre-roll

**Starvation rebase**:
Adjusting the broadcast clock when the server could not keep up — an empty library or a card
hiccup — so that a slow moment does not leave it permanently behind.
_Avoid_: drift correction, catch-up, resync

**Now playing**:
The display title of the track the live stream is currently carrying. A presentation string,
deliberately not the track's filename or the station name.
_Avoid_: current song, stream title, ID3 title

### The display

**Screen display**:
The module that brings up the panel (or the headless framebuffer), owns LVGL's memory
and hosts the **Cassette screen**. It reads the **live stream**'s status; it is never a
source of truth.
_Avoid_: UI, GUI

**Cassette screen**:
The 800x480 drawing of a cassette that shows what is playing: two reels, two tape
packs, a label and a **VU meter**. A presentation of the live stream, never a source of
truth.
_Avoid_: UI, GUI, dashboard

**Level**:
One of the three positions of the cassette screen, ordered by how much of it the
**sheet** covers: Bare, Label cover and Artwork. A one-finger down swipe moves one
towards Artwork, an up swipe one towards Bare, and the ends do not wrap. At Bare, a down
swipe dismisses the **Label** first if it is up, and only the next one moves. The Level
changes in one frame.
_Avoid_: state, step, mode, page

**Sheet**:
The layer a Level draws over the cassette. Exactly one is shown at a time.
_Avoid_: layer, overlay, page

**Reel frame**:
One step of the hub's animation: the divider turns 360/frame_count degrees per frame,
which at the 40 ms tick is ten degrees. The frame follows the reel's **phase**, which the
pack radius sets; a stopped track holds its last frame.
_Avoid_: sprite, animation step, image frame

**Reel phase**:
How far a reel has turned through one revolution, from its pack radius. The tape moves at
47.625 mm/s, so a full reel turns slower than an empty one; the supply's fill is
1000 minus the take-up's, so the two reels' phases and speeds differ. The **reel
change** is the one exception: inside its window the phase is interpolated, not
integrated.
_Avoid_: angle, offset

**Reel change**:
The eight seconds after a track boundary in which the screen shows the outgoing side's
tape winding back onto the supply reel - both packs migrating and both hubs turning in
reverse - instead of jumping to the new side. It is driven by the new track's elapsed
time and lands on exactly the state normal play shows when it closes, so the change has
no break in it. The title and the **VU meter** are not part of it: they follow the new
song from its first frame. For the window each hub's ring dims from the playing red to the
tape's own colour, then returns as the window closes - the ring is the playing indication,
and nothing is playing while the tape winds back.
_Avoid_: transition, animation, reel swap

**VU meter**:
The segmented display in the top right corner, showing the peak-hold of both channels:
one column of twelve segments per channel, filling from the bottom, on an 8-cell lattice.
Lit segments are the playing red, the rest a dim red; there is no panel behind them, so
the screen shows through. Each column is driven over -48..0 dB, so the bars move like a
meter rather than a linear amplitude, and the peak-hold falls about 23 dB/s, so they
follow a real drop instead of hanging. The meter is chrome, not tape: the **reel change**
does not touch it.
_Avoid_: level bar, meter, peak meter

**Label cover**:
The K7 sticker, `assets/labels/k7logo.png`, drawn full-bleed at 0,0 over the 800x480
panel at the Label cover Level; one-finger down from Bare reaches it. It is 800x480, the
panel's own size, so it carries no margin. Its black triangle and circle are
transparent and it is stored as RGB565A8, so the reels show through them. When the
Level settles the screen background fades from the cassette's colour to the cover's own
backdrop, `k_col_cover_bg`.
_Avoid_: big label, sticker, overlay, carátula

**Artwork**:
The Level that shows a **Track artwork** in the **Case**'s **Window**, with the **Now
playing** title on the band under the Case. It is not the **Label cover**: that is the K7
sticker, and the Case stays even when the Track carries no picture.
_Avoid_: cover, album art, portada

**Case**:
The cassette-case backdrop of the **Artwork** Level, `assets/labels/cover.jpg`,
full-bleed at 800x480. Its **Window** is where the **Track artwork** shows.
_Avoid_: frame, shell, background, funda

**Window**:
The rectangle of the **Case** where the **Track artwork** is drawn: (101,23) to
(704,408), 603x385. The picture is fitted cover-style and centre-cropped, never squeezed.
_Avoid_: hole, cutout, slot, ventana

**Track artwork**:
The picture a Track carries in its own file: an ID3v2.3/2.4 `APIC` frame in an MP3, or
a native FLAC `PICTURE` block, preferring the front cover. JPEG is decoded; a PNG is
located but not drawn. A Track with no usable picture leaves the **Window** empty.
_Avoid_: cover, embedded art, portada

**Label**:
The cream paper strip that carries the cleaned **Now playing** title, shown as an
overlay on **Bare** by an up swipe at that Level. It is hidden by the next vertical swipe
at Bare or by any Level change: a down swipe at Bare dismisses it before it is allowed to
move to the Label cover. It is not a Level and not one of the **sheet**s of the one-finger
cycle.
_Avoid_: title bar, caption, marquee

### The appliance

**Appliance**:
The device's entry policy: the station configuration, the card retry, the console line
and the skip button. It starts the services and the **Screen display**; it is never a
source of truth.
_Avoid_: main, firmware

### Clients and access

**Client**:
One connected consumer of the live stream. Clients are all equal and all hear the same audio;
the server does not track listeners as identities.
_Avoid_: listener, subscriber, user

**Index**:
The browsable listing of the music directory, and the device's diagnostic surface when the
card is missing.
_Avoid_: directory listing, browse page, file browser

**Download**:
Retrieval of one track's original bytes over HTTP. A separate act from listening to the live
stream, and mutually exclusive with other downloads.
_Avoid_: fetch, transfer, file request

**Open**:
The access posture of this device: no authentication anywhere, reachable by anyone who can
reach the port. A deliberate property of a LAN-only appliance, not a gap to be filled.
_Avoid_: unauthenticated, public, insecure

### Build and licensing

**Vendored**:
Third-party source kept in its own directory, unmodified, and built from source as part of
this project's build — never copied into this project's own source, and never pre-built into a
binary blob.
_Avoid_: bundled, embedded, inlined, submodule

### Testing

**Host build**:
The same component sources compiled for macOS, linked against a shim that stands in for the
device's operating system, so the server can be run and probed without a board. It is a second
way to build the one implementation, never a second implementation.
_Avoid_: simulator, emulator, mock server, desktop build

**Two-tier acceptance**:
The rule that a piece of work is finished only once its tests have passed on the host **and**
on the board. The host pass finds problems cheaply; the board pass is what makes the result
evidence about the device.
_Avoid_: CI, staging, pre-check

**Board adapter**:
The one module that differs between the firmware and the host build: Wi-Fi, the
card mount and the hardware census. Two adapters exist, so the seam is real rather
than hypothetical.
_Avoid_: platform layer, HAL, driver shim

**Board-only property**:
Something the host build cannot establish by construction — processor headroom, memory fit,
microSD timing, and card filesystem behaviour. Naming these is what keeps a host pass from
being mistaken for proof about the board.
_Avoid_: hardware concern, platform quirk

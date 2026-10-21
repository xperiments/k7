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
packs, a label and a **VU meter** on each reel. A presentation of the live stream,
never a source of truth.
_Avoid_: UI, GUI, dashboard

**Reel frame**:
One of the keyframes of the hub art, one per 360/frame_count degrees of rotation.
The frame follows the reel's **phase**, which the pack radius sets; a stopped track
holds its last frame.
_Avoid_: sprite, animation step, image frame

**Reel phase**:
How far a reel has turned through its art, from its pack radius. The tape moves at
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
no break in it. The label, the progress bar and the **VU meter** are not part of it: they
follow the new song from its first frame.
_Avoid_: transition, animation, reel swap

**VU meter**:
The dashed arc that continues the tape edge on each reel, showing the peak-hold of one
channel: the left reel is L, the right R. Its length is a screen setting, not part of
the cassette. The empty reel keeps a minimum tape load, so its arc stays outside the
hub ring. When the full **label cover** is up the reels are behind it, so the same two
meters move to the bottom row as rows of short vertical bars on the progress band's
centre line, in the free pockets that row leaves: same peak-hold, same dB scale, only
the drawing changes.
_Avoid_: level bar, meter, peak meter

**Label cover**:
The full-bleed cover art, `assets/labels/k7logo.png`, drawn at 0,0 over the 800x480
panel; a two-finger swipe slides it over the screen. Cover art is 800x480 from now on,
the panel's own size, so it carries no margin. Its black triangle and circle are
transparent and it is stored as RGB565A8, so the reels show through them. When the
slide ends the screen background fades from the cassette's colour to the cover's own
backdrop, `COL_COVER_BG`; the small **label** is hidden and the **VU meters** sit as
bar rows on the progress band's centre line.
_Avoid_: big label, sticker, overlay

**Label**:
The paper strip on the cassette that carries the cleaned **Now playing** title,
with parenthesised asides dropped.
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

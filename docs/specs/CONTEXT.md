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

**Shuffle mode**:
A mode of the **live stream**: a shuffled order of the whole **Playlist** that the broadcast
walks one **Track** at a time - forward when a Track ends and when it is told to skip, back
when it is told to go back. Turning it on does not interrupt the Track that is playing, picking a
Track moves the order's cursor to it without rebuilding the order, and a new **Library
generation** builds a new order. It changes neither the Playlist nor a Track's place in it.
_Avoid_: random play, shuffle action, random next

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
The name the screen gives the track the live stream is currently carrying. On the **Peek**
it is the **Text tag**'s song, or the cleaned filename when the Track has none. Never the
station name.
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
*(Removed by ADR 0019: Bare is the only screen. An up swipe at Bare toggles the **Peek**;
a down swipe shows the controls on the white page, and the next one turns the **Cover** on;
an up swipe turns it off before it closes the controls.
The Label cover and Artwork entries below are parked vocabulary for when those screens
return.)*
One of the three positions of the cassette screen, ordered by how much of it the
**sheet** covers: Bare, Label cover and Artwork. A one-finger down swipe moves one
towards Artwork, an up swipe one towards Bare, and the ends do not wrap. At Bare, a down
swipe dismisses the **Peek** first if it is up, and only the next one moves. The Level
changes in one frame.
_Avoid_: state, step, mode, page

**Sheet**:
*(Removed with the Label cover and Artwork screens, ADR 0019.)*
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
The two rows inside the right hub, L on top and R below, showing the peak-hold of both
channels. Each row is a rounded rectangle with an L/R badge on the left, a gap, seven
touching dim bars and one rounded lit fill that grows from the left and ends in a
semicircle; the fill is a step dimmer than the badge and the track a dim red.
Each row is driven over -48..0 dB, so it moves like a meter rather than a linear
amplitude, and the peak-hold falls about 23 dB/s, so it follows a real drop instead of
hanging. The meter starts hidden; a two-finger swipe left fades it in and a right fades it
out and hides it, over 220 ms. It is chrome, not tape: the **reel change** does not touch
it.
_Avoid_: level bar, meter, peak meter

**Controls**:
What the swipes bring in over the cassette: the **Transport drawer**, the DJ block, the header
and footer bands and the frame that closes around the tape, with the cassette posed to 134/256.
The down swipe shows them on the white **page**; either horizontal swipe shows the same parts
on the dark one. They never share the screen with the **Peek**.
_Avoid_: menu, overlay, chrome

**Page**:
The ink the **Controls** are drawn on: the white page the down swipe asks for, or the dark
screen either horizontal swipe keeps. It follows the gesture that opened the Controls, and
every part on it - the drawer, the block, the bands, the **Cover**, the **Menu** - turns with
it. It is not a Level or a **Sheet**.
_Avoid_: lineart view, dark mode, theme, skin

**Menu**:
A mode of the **Controls**, not a Level or a screen of its own: the cassette area gives way to
a list of destinations and the DJ block gives way to the **Pad**, while the **Transport
drawer**, the header and footer bands and the **Page** stay. The M cell opens it, on the white
page or the dark one, and it turns with the page like every other part. The cassette is hidden
behind it. The header says where you are and the footer what is playing; activating a row acts
in place, so the Menu stays up. An up swipe closes it, as P does, and as `<` does from the root;
while it is up the other swipes and the two-finger gesture do nothing. Its **Menu list** and
**Band** are remembered until the app restarts.
_Avoid_: screen, level, sheet, overlay, iPod menu

**Pad**:
The DJ block's footprint (190x284) as one touch surface while the **Menu** is up, drawn as one
opaque 0xFF00FF rectangle with an 8 px corner - the owner's marker for the area - rather than
the block's six cells.
A vertical drag ratchets the list one row per 56 px of travel and rests only on row boundaries;
a tap that never became a drag activates the **Band**.
_Avoid_: D-pad, keypad, touch buttons

**Band**:
The Menu row a tap on the **Pad** activates. It starts on the first visible row (or the one the
list remembers) and moves one row per pad step in the direction of the drag; when it reaches
the last visible row, the next step scrolls the list one row and the band stays on that edge
row.
_Avoid_: cursor, highlight, select bar

**Menu list**:
One screen of the **Menu**: its **Rows** and what they are drawn from - the root's four
destinations, Settings' five, the **Playlist**, a directory of the **Library tree**, or one of
the read-only lists. `<` goes up one, to the list that opened it - a directory's parent, or
Settings - and the header names the one on top.
_Avoid_: menu page, menu screen, level, sheet

**Row**:
One line of a **Menu list**: its icon, its label and, on the right, a chevron when it opens
another list or a value when it shows a setting. Five rows fill the page between the bands. A
row is never a touch target: only the **Pad** activates the **Band**.
_Avoid_: item, entry, cell

**Transport drawer**:
The three line-art buttons down the screen's left edge, P, `<` and `>`, hidden off-screen
until a one-finger right swipe and put away by a left swipe. Each button is a 96x96
rectangle rounded by 8 px with a 64 px circle at its centre and its character centred in
the circle in Jura; each carries a prebuilt pressed and an unpressed state, swapped on
press and release. A click is a scale pulse; `<` and `>` also call
`mini_icecast_prev()` / `mini_icecast_skip()`, so they are the transport's next/previous,
while P is still a placeholder. While the drawer is out the cassette screen scales to
134/256 and moves left to x=148, so the 402x232 tape clears the panel; the three buttons
start 64 px from the top so the middle `<` sits on y=220, and the cassette is centred to
that same line. It is chrome, not the **Peek**.
_Avoid_: menu, toolbar, controls

**Label cover**:
*(Parked with the removed Label cover screen, ADR 0019; the asset is not embedded.)*
The K7 sticker, `assets/labels/k7logo.png`, drawn full-bleed at 0,0 over the 800x480
panel at the Label cover Level; one-finger down from Bare reaches it. It is 800x480, the
panel's own size, so it carries no margin. Its black triangle and circle are
transparent and it is stored as RGB565A8, so the reels show through them. When the
Level settles the screen background fades from the cassette's colour to the cover's own
backdrop, `k_col_cover_bg`.
_Avoid_: big label, sticker, overlay, carátula

**Artwork**:
*(Parked with the removed Artwork screen, ADR 0019; the cover pipeline is not built.)*
The Level that shows a **Track artwork** in the **Case**'s **Window**, with the cleaned
filename on the band under the Case. It is not the **Label cover**: that is the K7
sticker, and the Case stays even when the Track carries no picture.
_Avoid_: cover, album art, portada

**Case**:
*(Parked with the removed Artwork screen, ADR 0019; the asset is not embedded.)*
The cassette-case backdrop of the **Artwork** Level, `assets/labels/cover.jpg`,
full-bleed at 800x480. Its **Window** is where the **Track artwork** shows.
_Avoid_: frame, shell, background, funda

**Window**:
*(Parked with the removed Artwork screen, ADR 0019.)*
The rectangle of the **Case** where the **Track artwork** is drawn: (101,23) to
(704,408), 603x385. The picture is fitted cover-style and centre-cropped, never squeezed.
_Avoid_: hole, cutout, slot, ventana

**Track artwork**:
The picture a Track carries in its own file: an ID3v2.3/2.4 `APIC` frame in an MP3, or
a native FLAC `PICTURE` block, preferring the front cover. JPEG is decoded; a PNG is
located but not drawn. A Track with no usable picture leaves the **Cover** empty. The
locator and fit are built on the panel board and tested on the host; the old **Artwork**
Level that showed it is still parked.
_Avoid_: album art, portada

**Cover**:
What the white controls page's down swipe prints on the cassette: the Track artwork scaled
to the outer frame's width, top-aligned, clipped to that frame and masked by the inner
frame. It is the **label**, the top layer: cinta < glow < label. The label covers the body
inside the outer frame and its window is a real transparent hole, so the glow and the tape
show through; the tape exposed below the frame stays visible under the glow. The up swipe
turns it off; every way off the white page hides it. Not a Level and not the removed Label
cover.
_Avoid_: artwork screen, Case, portada

**Peek**:
The overlay on **Bare** raised by an up swipe, and put away by the next up swipe. It has
the **Timeline bar** and the two **Now playing tag**s. There is no Level change to hide
it now (ADR 0019). While it is up the cassette shrinks and lifts so the bar clears the
tape, and the tags float over the cassette on their tethers to the hubs.
_Avoid_: title bar, caption, marquee

**Timeline bar**:
The 568x52 strip at the bottom of the **Peek**, at (116,424). The Track's elapsed time on
the left, its total on the right, and a 288x6 rounded track between them whose lit fill
follows the position. With nothing playing it shows the screen's idle sentence instead.
It replaced the old title bar: the names are on the **Now playing tag**s.
_Avoid_: progress bar, seek bar, player bar

**Now playing tag**:
One of the two rounded 504x44 labels floating over the cassette in the **Peek**, centred
on it at x=148..651: the group at (148,12) above the left hub, the song at (148,248)
below it, their centres the same distance from the hub's centre. Both hang from that
hub by a thin **Tether** that leaves on the hub's column (251 px); the text is 28 px. The text is
the Track's **Text tag**, with the artist directory and the cleaned filename as the
fallback. The two bob a few pixels out of phase; the tethers do not move with the bob.
_Avoid_: label, chip, pill, caption

**Tether**:
The vertical two-point line from just inside a **Now playing tag** to the left hub's near
rim: the group's from its bottom to the top rim, the song's from its top to the bottom
rim. Its hub end follows the pose; its tag end stays under the tag's opaque fill, so the
bob uncovers more or less of it.
_Avoid_: leader, wire, connector

**Text tag**:
The group and song names a Track's own container carries: the ID3v2.3/2.4 `TPE1` (artist)
and `TIT2` frames in an MP3, with `TPE2` as the artist fallback, or the `ARTIST` (with
`ALBUMARTIST`) and `TITLE` Vorbis comments in a FLAC. Read on the display's loader task,
not by the broadcast, into fixed UTF-8 buffers. A Track with no tag falls back to its path
and filename.
_Avoid_: metadata, ID3, tags, etiqueta

### The appliance

**Appliance**:
The device's entry policy: the station configuration, the card retry and the console
line. It starts the services and the **Screen display**; it is never a
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

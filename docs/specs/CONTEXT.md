# Stream server

An ESP32-S3 that owns a music library on a microSD card and broadcasts it over HTTP. This
context covers the **origin** side: the device that produces a stream. The consumer side is a
separate context — see [`CONTEXT-MAP.md`](../../CONTEXT-MAP.md).

## The global premise

**The ESP32-S3 never serves a Track and never accepts a Track upload. All SD content
processing happens outside the device.**

This outranks every requirement, proposal and road-map item written here or in any other
document in this repository. It is a premise, not a preference: work that violates it is
refused at review, not queued behind other work.

- **No Track download.** The device never hands out a Track as a file — neither the stored
  bytes nor a whole re-encoded copy. There is no per-Track endpoint — the `/music/<path>`
  route is gone — and no
  WebDAV, SMB, AFP, FTP or other mount that would expose the card's Tracks, individually or
  as a tree. The WebDAV research in `docs/research/server/s15-webdav.md` is a rejected
  option, kept for its evidence, not as a plan. Audio leaves only as the live stream, at the
  station format.
- **No Track upload.** No `PUT`, no multipart body, no body that becomes a Track, no
  upload-and-scan. The POST bodies that exist (`/api/play`, `/api/rescan`, `/mcp`) are
  commands: bounded, parsed, then discarded. There is no write path to the card at all.
- **The card is prepared outside.** Adding, removing, renaming or retagging music is a host
  operation — the card in a reader, or a host tool. The device's only SD access is the
  read-only access the broadcast itself needs: mount the card, scan it to build the Library,
  read a Track to encode it, read artwork for the display.

**What the premise does not forbid**, because the distinction is the point:

- **The live stream** (`/stream`). It carries the station's continuously encoded MP3, not any
  stored Track's bytes.
- **The device's own views.** `/api/library`, `/api/library.bin` and the opt-in
  `/api/screen.bmp` are computed by the firmware, not read out of the card. `/api/library.bin`
  is still sent with `Content-Disposition: attachment`, and that is fine: it is a photograph
  of the index, not a Track. "Attachment" describes how a response is framed; the premise
  bans what the bytes *are*, and these are not a Track.
- **The Index**, as a listing: names and sizes in Playlist order, with no links to bytes. It
  survives because it is the device's diagnostic surface when the card is missing.

Read literally, "all SD processing outside" would forbid the broadcast itself. It does not
mean that. **SD processing** in the premise means operating on the card's content on someone
else's behalf: adding, removing, renaming, rewriting, or shipping a Track out. Reading a Track
to put it on air is the appliance's own job, and it is what the Library scan and the decoder
do. Nothing else may read the card, and nothing may write it.

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
A track as stored, unmodified. What the broadcast reads and re-encodes; never what it sends,
because nothing hands the stored bytes out.
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

**Decoded source**:
The module that turns a **Track**'s **Original bytes** into stereo frames at the Track's own
rate: it owns the decoder, the card reads and the decode, on its own task on core 0. Its
interface to the broadcast is a ring of decoded frames; the broadcast keeps the **Track**
lifecycle, the resampling, the tone and the encoder. It exists so the card and the codec do not
sit on the encoder's core.
_Avoid_: decoder task, audio thread, producer

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
One of the four fixed screens the cassette screen shows, in a vertical stack: **Real** (the
top), **Art**, **Initial** and **Peek** (the bottom). The screen boots at Initial; a down
swipe takes one Level towards Real and an up swipe one towards the Peek, and the ends stop. A
Level is the whole of the cassette's control state together with the **Menu**; the **Page**,
the **Cover**, the **Peek** and the **Controls** are readings of it, not fields of their own
(ADR 0025). The Label cover and Artwork entries below stay parked vocabulary for screens that
have not returned.
_Avoid_: state, step, mode, screen

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
The two columns in the EQ group's plate, L on the left and R on the right, showing the peak-hold of both
channels. Each row is a rounded rectangle with an L/R badge on the left, a gap, seven
touching dim bars and one rounded lit fill that grows from the left and ends in a
semicircle; the fill is a step dimmer than the badge and the track a dim red.
Each row is driven over -48..0 dB, so it moves like a meter rather than a linear
amplitude, and the peak-hold falls about 23 dB/s, so it follows a real drop instead of
hanging. The meter starts hidden and stays hidden: it is parked (ADR 0025) - the view builds, places
and feeds it, and nothing shows it - until it has its new home. The two-finger gesture that
used to fade it in and out is gone. It is chrome, not tape: the **reel change** does not touch
it.
_Avoid_: level bar, meter, peak meter

**Controls**:
What the ladder brings in over the cassette: the **Transport drawer**, the DJ block, the header
and footer bands and the frame that closes around the tape, with the cassette posed to 134/256.
They are the **Art** and **Real** Levels: a down swipe from **Initial** shows them on the dark
**Page** (Art), and the next down turns the Page white and prints the **Cover** (Real). They
never share the screen with the **Peek**.
_Avoid_: menu, overlay, chrome

**Page**:
The ink the **Controls** are drawn on: the dark screen at **Art**, or the white one at
**Real**, which carries the **Cover** as well. The Page is a reading of the Level, not a choice
of its own, and every part on it - the drawer, the block, the bands, the **Cover**, the
**Menu** - turns with it. It is not a Level or a **Sheet**.
_Avoid_: lineart view, dark mode, theme, skin

**Bar**:
One of the two vertical strips at the screen's edges: the left **Bar** hosts the **Transport
drawer**, the right **Bar** hosts the **DJ block** and, while the Menu is up, the **Pad**. A
Bar owns its **Cells** - it builds each cell's carcase and holds the control inside - the
strip's 260 ms entrance, the **Page** ink dispatch and the hit rule, and it hosts one
**Panel** at a time, as a value.
_Avoid_: edge, rail, drawer (that is the panel)

**Panel**:
A **Bar**'s content, handed to it as one value: a grid of **Cells** - the mode bar's 1x3, the
EQ or RGB panel's 2x3 - or one plate, the **Pad**. A panel builds a control into each cell
the Bar hands it, and creates no carcase and no root of its own. One panel is up per bar;
showing another is a cut, not a slide. The right bar's panels are the three **Groups** and
the Pad.
_Avoid_: page, sheet, mode

**Group**:
The **Panel** the right **Bar** is showing: the Home keys by default, the EQ panel, or the
RGB panel. One at a time, and never two: the mode bar's group button toggles its Group on and
a second tap puts the Home keys back, and the Group dies with the chrome, so the Controls
always open on the Home keys (ADR 0026, amended by ADR 0046). The Pad is not a Group - it takes
the Bar while the Menu or a knob's dial owns the touch, and the Group comes back afterwards.
_Avoid_: tab, page, screen, mode (the mode bar selects a Group; the Group is what is shown)

**Install**:
The second half of putting a **Panel** on screen. A panel's module **builds** its values -
knobs, marks, names, a strip's state - and creates no object; the composition then installs
it with the right **Bar**. No module installs itself, and an install that arrives before the
Bars exist is refused with a log line rather than dropped (ADR 0042). The Peek's anchors are
the same shape: a part that is given the pose works its own geometry out instead of being
pushed coordinates.
_Avoid_: attach, mount, register, add

**Cell**:
One 96x96 carcase on a 94 px pitch inside a **Bar**, its 2 px border shared with its
neighbours; a plate panel such as the Pad's is one cell at its own rect instead. The Bar
builds either; a Cell hosts one control - a button, a knob, or the Pad's plate - which builds
its disc, its ring or its plate inside and, for a button, inks the carcase's ring on a
press.
_Avoid_: button, tile, box

**Menu**:
A mode of the **Controls**, not a Level or a screen of its own: the cassette area gives way to
a list of destinations and the DJ block gives way to the **Pad**, while the **Transport
drawer**, the header and footer bands and the **Page** stay. The mode bar's cassette button
opens it, on Art or on
Real, and it turns with the Page like every other part. The cassette is hidden behind it. The
header says where you are and the footer what is playing; activating a row acts in place, so
the Menu stays up. An up swipe closes it, as P does, and as `<` does from the root; while it is
up the other swipes do nothing. Its **Menu list** and **Band** are remembered until the app
restarts.
_Avoid_: screen, level, sheet, overlay, iPod menu

**Pad**:
The right **Bar**'s menu **Panel**, one plate at the DJ block's place - 186x284 at (596,76), 2 px
in from the block on each side in x and flush in y - while the **Menu** is up: a white plate
with a dark inner inset 8 px and one dotted vertical guide inside it, rather than the block's
cells. The plate is white on both pages and carries no page ink of its own. The inner and the
guides are decoration; the plate is the surface.
A vertical drag ratchets the list one row per 54 px of travel and rests only on row boundaries;
a tap that never became a drag activates the **Band**, and a leftward swipe as long as the
screen's own gesture limit is Back: up one **Menu list**, and out of the Menu at the root. The
press takes one axis, so a swipe never ratchets and a drag never goes Back.
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
another list or a value when it shows a setting. Five rows share the page's 6 px inset. A
row is never a touch target: only the **Pad** activates the **Band**.
_Avoid_: item, entry (a **Cell** is a **Bar**'s carcase, not a row)

**Transport drawer**:
The left **Bar**'s panel: three line-art buttons, P, `<` and `>`, hidden off-screen until the
screen's down swipe reaches **Art** and put away by the up swipe that leaves it. Each button is a 96x96 rectangle rounded
by 8 px with a 64 px circle at its centre and its character centred in the circle in Jura; a
press shows through the button's heavier ring, and a click is a short scale pulse. `<` and
`>` also call `mini_icecast_prev()` / `mini_icecast_skip()`, so they are the transport's
next/previous, while P is still a placeholder. While the drawer is out the cassette screen
scales to 134/256 and moves left to x=148, so the 402x232 tape clears the panel; the three
buttons start 76 px from the top so the middle `<` sits on y=218, and the cassette is centred
to that same line. It is chrome, not the **Peek**.
_Avoid_: menu, toolbar, controls

**DJ block**:
The right **Bar**'s EQ **Panel**: four knobs - GAIN, then HI, MID and LOW - and the R button,
all in 96 px **Cells**, with the Menu's M in the bar's chrome. It gives way to the **Pad**
while the **Menu** is up. R is a placeholder.
_Avoid_: EQ block, right bar (that is the **Bar**)

**Label cover**:
*(Parked with the removed Label cover screen, ADR 0019; the asset is not embedded.)*
The K7 sticker, `assets/labels/k7logo.png`, drawn full-bleed at 0,0 over the 800x480
panel at the Label cover Level; one-finger down from Initial reaches it. It is 800x480, the
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

**Card**:
The white page's cassette: one tinted rounded rectangle, 426x272 at (136,82), radius 12, drawn
above the tape and under the **Cover**, which it carries at +12,+20. It has no picture of its
own; its colours are a row of the palette table - a plastic, the label's fill and the ink it
would print in - picked from the Track's path. Nothing is read off a picture (ADR 0040). The
label is wordless: with no Cover, the print is the row's label fill. It and the Cover's container share one A8 mask, whose 291x87 pill
window at (203,157) is the only place the tape shows through. Shown and hidden with the
**Page**; it exists at the Controls pose and nowhere else. It is the body the screen never had:
the label and the outer frame are the same rectangle, so there was nothing to colour.
_Avoid_: plate, carcase

**Cover**:
What the white page - the **Real** Level - prints on the **Card**: the Track artwork scaled to
the outer frame's width, top-aligned, clipped to that frame and masked by the inner one. It is
the **label**, above the Card: cinta < Card < label. The window is a real transparent hole in
both, so the tape shows through it. The up swipe turns it off with the Page; every way off Real
hides it. It is part of Real, not a Level of its own and not the removed Label cover.
_Avoid_: artwork screen, Case, portada

**Peek**:
The bottom **Level**: raised by an up swipe from **Initial**, and put away by a down swipe. It
has the **Timeline bar** and the two **Now playing tag**s, and it can only be raised from the
settled Initial pose. While it is up the cassette shrinks and lifts so the bar clears the
tape, and the tags float over the cassette on their tethers to the hubs.
_Avoid_: title bar, caption, marquee

**Timeline bar**:
The 568x52 strip at the bottom of the **Peek**, at (116,416). The Track's elapsed time on
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
The listing of the Library in Playlist order, and the device's diagnostic surface when the
card is missing. It names Tracks and their sizes; it does not hand them out.
_Avoid_: directory listing, browse page, file browser

**Track transfer**:
Handing a stored Track's bytes out over HTTP, or taking a Track in over HTTP. The appliance
does neither; the phrase names the act the global premise forbids. Fetching
`/api/library.bin` is not a Track transfer — it is the index, and the index is not a Track.
_Avoid_: download, upload, file transfer, share, NAS

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

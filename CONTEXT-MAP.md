# Context Map

## Global premises

Premises that outrank every requirement in every context below. Work that violates one is
refused, not scheduled.

- **The ESP32-S3 never serves a Track and never accepts a Track upload; all SD content
  processing happens outside the device.** The one card write is the delete of an **Update
  file** it has already flashed and verified. Stated in full, with what it does and does not
  forbid, in [the stream server's premise](./docs/specs/CONTEXT.md#the-global-premise).

## Contexts

- [Stream server](./docs/specs/CONTEXT.md) — an ESP32-S3 that owns a music library on a
  microSD card and broadcasts it over HTTP as a live MP3 stream, with an index that lists
  it. A global premise forbids serving or accepting a Track, so all SD content processing
  happens outside the device. It also holds the **Snapshot** — the measurements and the
  **Playlists** the page produced — and walks a selected Playlist when told to. Specified in
  [`docs/specs/sd-stream-server.md`](./docs/specs/sd-stream-server.md); its measurements are
  specified in [`docs/specs/snapshot.md`](./docs/specs/snapshot.md) and byte-defined in
  [`docs/specs/snapshot-format.md`](./docs/specs/snapshot-format.md).
- [Front door](./web/CONTEXT.md) — the page the stream server serves at `http://xtape.local`:
  one shell with four areas that measures Tracks, authors Playlists, browses the library's
  albums and artists, and shows what the broadcast is playing. It is the producer of the
  **Snapshot** the stream server answers its queries from. Specified in
  [`docs/specs/front-door.md`](./docs/specs/front-door.md).
- [Radio player](./docs/research/01-library-architecture.md) — an ESP32-S3 that consumes
  someone else's internet radio stream and plays it out to a DAC over I2S. Researched in
  [`docs/research/`](./docs/research/); no `CONTEXT.md` yet, because work has not resumed
  there.

## Relationships

- **Stream server ↔ Radio player**: they share no code and flow in opposite directions. The
  player is an HTTP/ICY *client*; the server is the *origin*. The only thing they share is a
  vocabulary — "station", "ICY metadata", "burst" — which is why they are separate contexts
  and not one.

- **Stream server → Radio player**: the server is the natural loopback fixture for testing a
  client-side ICY parser. This is why the server's header emission is held to canonical
  Icecast form rather than "whatever a permissive parser accepts".

- **Stream server ↔ Front door**: the server serves the page, answers its JSON and MCP calls,
  and reports its **Card state**; the page reads the card where it lies and builds the one
  artifact the server accepts. They share three words with different scope, so each is defined
  in both glossaries:
  - **Track** — there, a file in the card's music directory, owned by the device; here, a file
    the user submits for measurement.
  - **Playlist** — there, an ordered selection stored in the Snapshot and walked by the
    broadcast; here, the same thing while it is being authored, named and reordered.
  - **Snapshot** — there, an immutable image in a flash partition; here, an image this browser
    builds, keeps as its **last image**, and sends.

- **Front door → Stream server**: the page can never change what plays by editing a Playlist.
  A Playlist reaches the device only inside a Snapshot (an image, a restart), and a chosen
  Playlist is a command afterwards (ADR 0050). This is why the page's editor is a draft and
  sending is an act.

## Note on the research tree

`docs/research/` currently mixes the player's and server's evidence at its top level: the player's evidence
(`00-audit-of-resources.md` … `07-esp32s3-hardware-verified.md`,
`radio-player-*.md`) sits beside the server's (`server/s0-…` … `s12-…`). Moving the player
files into a `docs/research/player/` sibling is the obvious tidy-up, but it is out of scope
here because the server's research files cite the player files by path. Recorded so it is not
rediscovered later.

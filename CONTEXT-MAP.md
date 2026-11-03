# Context Map

## Global premises

Premises that outrank every requirement in every context below. Work that violates one is
refused, not scheduled.

- **The ESP32-S3 never serves a Track and never accepts a Track upload; all SD content
  processing happens outside the device.** The one card write is the delete of an **Update
  file** it has already flashed and verified. A **Card mark** is written by the K7 app, not by
  the device, and the device that reads one reads it read-only. Stated in full, with what it
  does and does not forbid, in [the stream server's premise](./docs/specs/CONTEXT.md#the-global-premise).

## Contexts

- [Stream server](./docs/specs/CONTEXT.md) — an ESP32-S3 that owns a music library on a
  microSD card and broadcasts it over HTTP as a live MP3 stream, with an index that lists it.
  A global premise forbids serving or accepting a Track, so all SD content processing happens
  outside the device. It also holds the **Snapshot** — the measurements and the **Playlists**
  the app produced — and walks a selected Playlist when told to. Since
  [ADR 0057](./docs/adr/0057-the-app-lives-at-a-public-origin.md) its only address,
  `http://k7.local`, serves the **Launcher** rather than the app, plus `/api/*`, `/mcp`,
  `/library`, `/stream`, and the app's own bytes at `/app` as the Device copy. Specified in
  [`docs/specs/sd-stream-server.md`](./docs/specs/sd-stream-server.md); its measurements are
  specified in [`docs/specs/snapshot.md`](./docs/specs/snapshot.md) and byte-defined in
  [`docs/specs/snapshot-format.md`](./docs/specs/snapshot-format.md).
- [K7 app](./web/CONTEXT.md) — the application at `https://k7.xperiments.dev` and the Launcher
  beside it: one codebase, two origins. The App measures Tracks, authors Playlists, browses the
  library from either the device or its own **Last image**, and writes the card's **Update
  file** and its **Card mark**. The Launcher is the page on the device's own origin that shows
  its state, carries the stream's audio, and executes the **Bridge**'s operations. The App is
  the producer of the **Snapshot** the stream server answers its queries from. Specified in
  [`docs/specs/k7-app.md`](./docs/specs/k7-app.md); the channel in
  [`docs/specs/bridge-protocol.md`](./docs/specs/bridge-protocol.md); deployment in
  [`docs/specs/k7-deploy.md`](./docs/specs/k7-deploy.md).
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

- **K7 app ↔ Stream server**: the server serves the Launcher and the Device copy, answers the
  App's calls **through the Launcher's Bridge** (the App never fetches the device itself:
  mixed content and Local Network Access forbid it), and reports its **Card state**. The App
  reads the card where it lies and builds the one artifact the server accepts. They share three
  words with different scope, so each is defined in both glossaries:
  - **Track** — there, a file in the card's music directory, owned by the device; here, a file
    the user submits for measurement.
  - **Playlist** — there, an ordered selection stored in the Snapshot and walked by the
    broadcast; here, the same thing while it is being authored, named and reordered.
  - **Snapshot** — there, an immutable image in a flash partition; here, an image this browser
    builds, keeps as its **Last image**, and sends.

- **K7 app → Stream server**: the app can never change what plays by editing a Playlist. A
  Playlist reaches the device only inside a Snapshot (an image, a restart), and a chosen
  Playlist is a command afterwards (ADR 0051). This is why the app's editor is a draft and
  sending is an act.

- **Within the K7 app**: the App and the Launcher are one codebase in two origins, and the
  Bridge is the only thing between them (ADR 0058). The Device copy is the App's own bytes on
  the device's origin — reachable when the network is not, and incapable of the card write, the
  worker and WebGPU that its insecure origin forbids.

## Note on the research tree

`docs/research/` currently mixes the player's and server's evidence at its top level: the player's evidence
(`00-audit-of-resources.md` … `07-esp32s3-hardware-verified.md`,
`radio-player-*.md`) sits beside the server's (`server/s0-…` … `s12-…`). Moving the player
files into a `docs/research/player/` sibling is the obvious tidy-up, but it is out of scope
here because the server's research files cite the player files by path. Recorded so it is not
rediscovered later.

A third area, `docs/research/marketing/`, sits in its own directory because it is neither
context: [`campaign.md`](./docs/research/marketing/campaign.md) is the global campaign plan, and
the two files beside it are the primary-source evidence it cites (platform specs, industry data).
It is research about the product, not about its firmware, and it should not be read as a
specification.

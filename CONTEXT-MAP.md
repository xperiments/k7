# Context Map

## Global premises

Premises that outrank every requirement in every context below. Work that violates one is
refused, not scheduled.

- **The ESP32-S3 never serves a Track and never accepts a Track upload; all SD content
  processing happens outside the device.** The card writes are the delete of an **Update file**
  it has already flashed and verified, the **Playlist selection** record, and the **Playlists**
  the app sends it, one file per list under `k7.playlists/`
  ([ADR 0094](./docs/adr/0094-the-device-writes-the-lists-it-is-given.md)). A **Card mark** is
  written by the K7 app, not by the device, and the device that reads one reads it read-only.
  Stated in full, with what it does and does not forbid, in
  [the stream server's premise](./docs/specs/CONTEXT.md#the-global-premise).

## Contexts

- [Stream server](./docs/specs/CONTEXT.md) — an ESP32-S3 that owns a music library on a
  microSD card and broadcasts it over HTTP as a live MP3 stream, with an index that lists it.
  A global premise forbids serving or accepting a Track, so all SD content processing happens
  outside the device. It also holds the **Snapshot** — the measurements and the **Playlists**
  the app produced — and walks a selected Playlist when told to. Its own address,
  `http://k7.local`, serves the API and the stream — `/api/*`, `/mcp`, `/library`, `/stream` —
  and no product page; its root redirects a browser to the App at `/app`
  ([ADR 0067](./docs/adr/0067-chrome-only-direct-to-the-device.md)). It also carries its own
  **Setup network**, `K7.local`, and the one **Setup page** it serves, so a device with no
  network is still a device that can be given one ([ADR 0076](./docs/adr/0076-the-network-is-chosen-on-the-devices-setup-network.md)).
  Specified in [`docs/specs/sd-stream-server.md`](./docs/specs/sd-stream-server.md) and
  [`docs/specs/wifi-provisioning.md`](./docs/specs/wifi-provisioning.md); its measurements are
  specified in [`docs/specs/snapshot.md`](./docs/specs/snapshot.md) and byte-defined in
  [`docs/specs/snapshot-format.md`](./docs/specs/snapshot-format.md).
- [K7 app](./web/CONTEXT.md) — the Site at `https://k7.xperiments.dev/` and the application at
  `/app` on it. The **App** measures Tracks, authors Playlists, browses the library from either
  the device or its own **Last image**, and writes the card's **Update file**, its **Card mark**
  and its **Playlists** — one file per list, either directly or through the player. It reaches the device directly over Local Network Access, gated on the
  `local-network` permission, and only in Chrome
  ([ADR 0067](./docs/adr/0067-chrome-only-direct-to-the-device.md)). The App is the producer of
  the **Snapshot** the stream server answers its queries from. The **Site** is public: it offers
  the App's **Install** and the player's firmware **Flash**, the latter over USB rather than the
  network — the only browser surface that writes a board
  ([ADR 0078](./docs/adr/0078-the-site-owns-the-root-and-the-app-moves-to-app.md),
  [ADR 0079](./docs/adr/0079-the-firmware-is-flashed-in-parts-from-the-browser.md)). The same
  origin serves the **K7Art Editor** at `/k7art`, where a cassette's label and J-card are
  designed and a **K7Art pack** is published; the App installs that pack and draws its J-cards
  on the albums that have no cover picture ([`docs/specs/k7art.md`](./docs/specs/k7art.md),
  [`docs/specs/art-packs.md`](./docs/specs/art-packs.md)). Specified in
  [`docs/specs/k7-app.md`](./docs/specs/k7-app.md) and
  [`docs/specs/k7-site.md`](./docs/specs/k7-site.md); its look in
  [`docs/specs/k7-look.md`](./docs/specs/k7-look.md); deployment in
  [`docs/specs/k7-deploy.md`](./docs/specs/k7-deploy.md).
- [K7Player](./player/CONTEXT.md) — the second device: an ESP32 **classic** that joins a
  network, plays the K7's live stream and turns it into sound at a Bluetooth speaker (A2DP) or a
  line output (I2S to a DAC). It is a **Client** and nothing more: the K7 keeps no record of it,
  and the two services the K7 performs for it are a **handover** of its own network during a
  **share window** a person opens, and the signed **Player image** it carries, which any K7 holding
  it may hand over
  ([ADR 0098](./docs/adr/0098-the-player-is-a-client-the-k7-keeps-no-record-of.md),
  [ADR 0099](./docs/adr/0099-the-network-is-handed-over-esp-now-in-a-window.md),
  [ADR 0116](./docs/adr/0116-a-players-firmware-is-a-signed-image-the-k7-carries.md)). Its own
  firmware is a second ESP-IDF project at [`player/`](./player/)
  ([ADR 0097](./docs/adr/0097-the-player-is-an-esp32-classic-in-its-own-project.md)). Specified
  in [`docs/specs/k7player.md`](./docs/specs/k7player.md), its protocol in
  [`docs/specs/network-handover.md`](./docs/specs/network-handover.md) and its firmware update in
  [`docs/specs/player-update.md`](./docs/specs/player-update.md). The earlier
  internet-radio research in [`docs/research/`](./docs/research/) is evidence about a general
  stream client, not a plan for this one.

## Relationships

- **Stream server ↔ K7Player**: they flow in opposite directions. The player is an HTTP *client*
  of the live stream; the server is the *origin*. They share one vocabulary with the server's
  meaning — **Client**, **live stream**, **station format** — and nothing else, because the
  player has no card, no library and no Snapshot, and the server has no per-player state.

- **Stream server → K7Player**: the server is the natural loopback fixture for the player, as it
  was for the researched ICY parser: the player's first opponent is the stream this repo already
  serves, so a player bug that a general station would hide is visible here.

- **Stream server → K7Player, once**: beyond serving, the K7 hands a player its **network
  record** inside a **share window** a person opens from its Menu, and, on a person's press, the
  **Player image** it carries — signed, and the same image whichever K7 holds it. It is not a
  relationship: the K7 remembers nothing, addresses nothing, and cannot tell one player from
  another afterwards.

- **K7 app ↔ Stream server**: the App fetches the device directly, from the HTTPS origin to
  `http://k7.local`, once a person has granted the `local-network` permission (ADR 0067); the
  server reports its **Card state** and answers the App's calls on its own. The App
  reads the card where it lies and builds the one artifact the server accepts. They share three
  words with different scope, so each is defined in both glossaries:
  - **Track** — there, a file in the card's music directory, owned by the device; here, a file
    the user submits for measurement.
  - **Playlist** — there, an ordered selection on the card, one file per list, walked by the
    broadcast; here, the same list while it is being authored, named and reordered.
  - **Snapshot** — there, an immutable image in a flash partition; here, an image this browser
    builds, keeps as its **Last image**, and sends.

- **K7 app → Stream server**: a Playlist is the card's, and either side may write it — the app
  when the card is in a computer, the device when the card is in the player
  ([ADR 0095](./docs/adr/0095-a-playlist-is-a-file-on-its-card.md)). A measurement only ever
  arrives by flashing an image on the card, and flashes a restart; a list arrives as a file and
  does not.

- **K7Art Editor → K7 app → Stream server**: the editor at `/k7art` publishes a `.k7artpack` as
  a download; the App's **Art packs** page installs it on the K7. The K7 stores it in its `art`
  partition, reads its label sheet in place and serves the pack read-only, while the App caches
  its J-cards for the covers. The editor never reaches the device; the pack is art in flash,
  not a Track.

## The journey

[`docs/journey.md`](./docs/journey.md) maps one person's path across all three contexts — the
Site's flash and install, the card prepared in the App, the broadcast, the player's handover — with
a diagram for each stretch and a table naming the spec that owns every step. It cites and asserts
nothing: where it and a spec disagree, the spec wins.

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

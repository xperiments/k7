# Context Map

## Contexts

- [Stream server](./docs/specs/CONTEXT.md) — an ESP32-S3 that owns a music library on a
  microSD card and serves it over HTTP as a live MP3 stream plus a downloadable index.
  Specified in [`docs/specs/sd-stream-server.md`](./docs/specs/sd-stream-server.md).
- [Radio player](./docs/research/01-library-architecture.md) — an ESP32-S3 that consumes
  someone else's internet radio stream and plays it out to a DAC over I2S. Researched in
  [`docs/research/`](./docs/research/); no `CONTEXT.md` yet, because work has not resumed
  there.
- [Track analysis](./web/CONTEXT.md) — a browser app that measures tempo, key, mood and energy
  for a user's own audio files. Researched in
  [`docs/research/bpm-key-mood-energy-webapp.md`](./docs/research/bpm-key-mood-energy-webapp.md).

## Relationships

- **Stream server ↔ Radio player**: they share no code and flow in opposite directions. The
  player is an HTTP/ICY *client*; the server is the *origin*. The only thing they share is a
  vocabulary — "station", "ICY metadata", "burst" — which is why they are separate contexts
  and not one.

- **Stream server → Radio player**: the server is the natural loopback fixture for testing a
  client-side ICY parser. This is why the server's header emission is held to canonical
  Icecast form rather than "whatever a permissive parser accepts".

- **Stream server ↔ Track analysis**: both say "Track", with different scope. There it is a
  file in the card's music directory, owned by the device; here it is a file the user submits
  for measurement. The two share a vocabulary, not code or data flow. The analyser is
  host-side and the device stays a byte server.

## Note on the research tree

`docs/research/` currently mixes the player's and server's evidence at its top level: the player's evidence
(`00-audit-of-resources.md` … `07-esp32s3-hardware-verified.md`,
`radio-player-*.md`) sits beside the server's (`server/s0-…` … `s12-…`). Moving the player
files into a `docs/research/player/` sibling is the obvious tidy-up, but it is out of scope
here because the server's research files cite the player files by path. Recorded so it is not
rediscovered later.

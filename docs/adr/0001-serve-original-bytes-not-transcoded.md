# Serve original file bytes, not transcoded MP3

> **Superseded 2026-10-31.** The download route this ADR reasons about is withdrawn: the
> device never serves a Track (the global premise in `docs/specs/CONTEXT.md`). Kept as the
> record of why the route, while it existed, served originals rather than MP3.

The live stream is MP3-only, so the obvious symmetry would be for `GET /music/<name>` to
return MP3 too. We decided it returns the **original, untranscoded bytes** instead: the
stream's job is reachability and universal client support, while the download's job is
retrieval of what the card actually holds — a client that wants the FLAC should get the FLAC.

## Considered Options

- **Transcode on demand.** Rejected: it needs a second `shine` instance (the encoder handle
  cannot be shared with the broadcast), the same microSD has to be decoded twice concurrently
  while the radio is already reading it, and `Content-Length` becomes unknown unless the whole
  track is encoded to a buffer first — which is worse for memory than anything else here.
- **Serve originals.** Chosen. Zero codec work, byte-exact retrieval, `Content-Length` from
  `stat()`.

## Consequences

A download does **not** sound like the stream, and a client cannot assume the two agree. This
is deliberate, and it is why the two paths are separate words in the glossary: *live stream*
versus *download*.

It also makes large-file downloads the worst case for microSD contention, since a 40 MB FLAC
read competes with the broadcast's decoder reads on one card. That is the entire reason only
one download may proceed at a time, with a second request refused rather than queued.

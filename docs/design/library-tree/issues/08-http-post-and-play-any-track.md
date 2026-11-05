# 08 — HTTP POST and play any Track

**What to build:** Give the HTTP surface a bounded POST body and a control route that plays any
Track by relative path. `POST /api/play` with `{"path": "Artist/Album/song.mp3"}` selects the
Track at the next chunk boundary, like `skip` does, and reports acceptance. A path that does not
resolve is an error and does not interrupt the stream. This is the transport the MCP endpoint
will reuse.

**Blocked by:** 04 — Library at boot and the JSON endpoint; 07 — The broadcast plays the whole
Library tree.

**Status:** done (2026-10-20)

- [x] POST bodies are read with `Content-Length`, capped at 8192 bytes, and answered with 411
      when the length is missing, 413 when it is too large and 415 when the type is not JSON.
- [x] The body is held in heap, not on the connection stack, and bytes arriving with the header
      block are preserved.
- [x] `POST /api/play` with a nested path changes what is playing at the next chunk and returns
      success.
- [x] An unknown path returns an error and the live stream continues; `Accept` does not affect
      the response.

## Comments

- `http_server` now registers a method per route; the GET wrapper keeps every existing
  registration unchanged. A POST route selects its body before the handler, and a wrong
  method on a known path is 405 (the `/` catch-all is not treated as owning every path).
- POST bodies: `Content-Length` required (411), capped at 8192 (413), JSON required (415),
  heap-allocated and NUL terminated. Bytes already read with the header block are copied
  before the rest is read. Rejected oversized/typed bodies are drained (bounded) before close,
  so the error response is not destroyed by RST.
- `POST /api/play` accepts exactly `{"path":"<relative path>"}` with JSON string escapes,
  including UTF-8 `\u` and surrogate pairs. It calls `mini_icecast_play()`, which resolves
  through `library_find()` and sets a pending Playlist index; the audio task switches at its
  next chunk, not inside the HTTP worker. An unknown path is 404 and leaves the stream alone.
  A pending request is re-resolved by path if a rescan replaced the generation.
- `Accept` is never inspected. `mini_icecast_play()` is the transport the MCP endpoint will
  reuse.
- Tests: `t08` covers Content-Length/type/route matching; `post_play.py` sends raw packets for
  one-packet, split and straddling bodies, exact 8192, 8193, 9000-with-body, short body, 411,
  415, unknown path, malformed JSON and `Accept`; T15 checks the index changed Track and the
  stream survives.
- Verified: `make -C host check` (150 HTTP checks, t14 152, t08, layering, awkward names),
  `make -C host tools`, and both `build_sunton`/`build61` (0x2ec1b0, 27% free; 0x20e040, 49%).
- Board T15 is not run here; the route is host-only in the harness. Ticket 09 reuses the same
  play path for MCP.

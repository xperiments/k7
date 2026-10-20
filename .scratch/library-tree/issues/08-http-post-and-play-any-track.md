# 08 — HTTP POST and play any Track

**What to build:** Give the HTTP surface a bounded POST body and a control route that plays any
Track by relative path. `POST /api/play` with `{"path": "Artist/Album/song.mp3"}` selects the
Track at the next chunk boundary, like `skip` does, and reports acceptance. A path that does not
resolve is an error and does not interrupt the stream. This is the transport the MCP endpoint
will reuse.

**Blocked by:** 04 — Library at boot and the JSON endpoint; 07 — The broadcast plays the whole
Library tree.

**Status:** ready-for-agent

- [ ] POST bodies are read with `Content-Length`, capped at 8192 bytes, and answered with 411
      when the length is missing, 413 when it is too large and 415 when the type is not JSON.
- [ ] The body is held in heap, not on the connection stack, and bytes arriving with the header
      block are preserved.
- [ ] `POST /api/play` with a nested path changes what is playing at the next chunk and returns
      success.
- [ ] An unknown path returns an error and the live stream continues; `Accept` does not affect
      the response.

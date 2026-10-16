# 07 — The HTTP surface is separated from the broadcast

**What to build:** Listening, request parsing and routing move into their own component, and each
connection is handled on its own task. A client that connects and then sits silent — or reads
very slowly — no longer holds up anyone else. Requests for the live stream are handed to the
broadcast component exactly as they are today, so every existing client keeps playing.

This is deliberately not a tidy-up. Today the accept loop reads the request header inline with a
multi-second timeout, so one idle connection stalls every new connection. That was survivable
while the stream was the only endpoint; it is not survivable once a second HTTP surface exists,
which is why this lands before file serving.

**Blocked by:** 01 — Host build and fixture corpus for the broadcast path.

**Status:** done (host tier). The board run of T4 has not been done.

- [ ] A client that connects and sends no request cannot delay or block a second client. Verified on the host and on the board.
- [ ] The listener's pending-connection depth tolerates several simultaneous connections rather than a token queue.
- [ ] The live stream produced through the extracted server is identical to what the pre-extraction server produced for the same input.
- [ ] The broadcast component exposes only its start, skip, now-playing and connection-registration surface. It contains no HTTP parsing, no routing and no knowledge of files or markup.
- [ ] The dependency direction is one-way and enforced by the build: the file-serving and broadcast components depend on the HTTP component, and nothing depends on the file-serving component.
- [ ] The server identifies itself in canonical form — specifically, the station-name header carries no space after its colon, matching what real stations emit.
- [ ] An unrecognised request path receives a not-found response rather than the stream.
- [ ] Rejected connection setup releases its socket; no descriptor leak over repeated rejected connections.

Spec: `docs/specs/sd-stream-server.md` §3 (S1–S4), §5 D1, D2, §9 T4.
Decision: ADR 0003.

## Comments

- [x] A client that connects and says nothing cannot delay a second client: measured 0.0008 s for `/` and 0.0004 s to the first `/stream` byte with a silent socket held open (T4 in `host/tests/http_tests.sh`).
- [x] The listen backlog is 16 and the connection limit is 4 concurrent request tasks (a counting semaphore); the accept loop never parses a request itself.
- [x] The stream produced through the extracted server is the same audio as before for the same input: the decode, resample, chunk and pacing code moved unchanged, `mi_encode` is inserted between the chunker and `broadcast()`, and the host HTTP suite decodes its output with zero errors. Byte-for-byte comparison against the pre-extraction server is not meaningful because that server produced WAV (ticket 05 changes the format) — what is preserved is the PCM that reaches `broadcast()`.
- [x] `mini_icecast` exposes `mini_icecast_start`, `_skip`, `_now_playing` and `mini_icecast_register_client`, and nothing else. It has no socket, parses no request and renders no markup; it does write the stream's own response headers, because the specification asks the `/stream` hand-off to work exactly as `handle_new()` did.
- [x] The dependency direction is enforced by `make -C host layering`, not by convention: `mini_files` and `mini_icecast` include `http_server`, nothing includes `mini_files` except `main/`, no component branches on the target, and the host build must compile every component source with no `*_host.c` fork.
- [x] `icy-name:` carries no space after the colon.
- [x] An unrecognised path is a 404 (`/nope`, `/index.html`), never the stream; a non-GET is 405.
- [x] No descriptor leak: 160 rejected connections (404s and POSTs) leave the process's descriptor count unchanged (10 -> 10).

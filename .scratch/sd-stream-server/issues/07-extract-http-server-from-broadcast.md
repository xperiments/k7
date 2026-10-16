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

**Status:** ready-for-agent

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

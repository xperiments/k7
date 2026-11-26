# 07 — One audio client at a time

**What to build:** the K7 serves **one** audio client, not four. It is a personal player, not a
station: a second listener on `/stream` is refused rather than added to the airtime budget.
The connection pool stays as it is — the API and the UI need more than one socket — but only one
of them may be a `GET /stream` at a time.

**Blocked by:** nothing.

**Status:** built, and checked on the board.

- [x] `appliance.c` serves one audio client (`max_clients` 1, the pool unchanged). The boot log
      says it twice: `burst ring: 20098 B (~500 ms of audio) in external memory, sends at most 1
      clients` and `stream: 1 client slot(s), 324096 B send ring each (~10000 ms)`.
- [x] A second `/stream` is refused with the K7's existing 503 rather than answered: with one
      listener holding the stream, the second got `HTTP 503` in 0.24 s, body *every stream slot
      is taken; try again in a moment*.
- [x] The ESP-NOW player is not an HTTP client, so it does not take the slot: one browser and the
      player can coexist, and that is the intended shape.
- [x] The stream server's spec says one, and why — §6.1 — and ADR 0107 records the decision.

Measured on the board at 256 kbps (the transport was on HTTP when it was checked): a client's
send ring is 324 KB in PSRAM, and the three rings and sender tasks the other slots used to hold
are now free — `memory: internal 54187 B free, external 2536652 B free; clients 0/1`.

The slot frees when the socket dies: 5 of 5 connections were admitted 300 ms after the previous
one was killed, so a page refresh recovers on its first retry (the page waits a second).

Spec: `docs/specs/sd-stream-server.md` §6.1, `docs/specs/espnow-stream.md` E9. ADR:
`docs/adr/0107-the-k7-carries-one-listener.md`.

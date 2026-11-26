# 01 — The broadcast can be skipped by URL

**What to build:** a remote control's next and previous buttons have something to press. The device
already knows how to skip — the MCP control tools call the same primitive — but the HTTP surface
offers only "play this path", so anything driving the deck from outside has to reconstruct the
broadcast order itself.

**Blocked by:** None — can start immediately.

**Status:** built on the host; the board run is owed. HA-01 on the board (doing).

- [x] `POST /api/next` moves the broadcast to the next track in broadcast order, and the stream
      follows at the next chunk boundary.
- [x] `POST /api/prev` reaches exactly the track the deck's own previous primitive reaches — the
      route adds a way in, it does not invent a second meaning for "previous".
- [x] Both answer `204` with no body and no `Content-Length`; the caller learns what plays next from
      the status document, not from a second reply shape.
- [x] Both take the device's one POST shape: `Content-Type: application/json` and a declared length,
      which may be zero. A step with no card is accepted and the status keeps saying `no_card`
      rather than the route inventing a track.
- [x] A wrong method, and a near-miss path (`/api/nextXYZ`), are both refused — as a `404`, because
      the front door's index catch-all answers every unmatched GET before the method check can say
      `405`.
- [x] Host tests cover both routes, the near-miss, the wrong method and the card-less deck; the API
      contract is in the stream server's §6.4a.
- [ ] On the board, one `curl -X POST /api/next` moves the stream and the next status poll shows the
      new track. **Owed: the board is shared and was not taken for this.**

**Not in this:** a track index or a queue cursor over HTTP. The order is the broadcast's; the route
only steps it.

Spec: `docs/specs/sd-stream-server.md` §6.4a, T20.

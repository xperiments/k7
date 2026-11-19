# 07 — One audio client at a time

**What to build:** the K7 serves **one** audio client, not four. It is a personal player, not a
station: a second listener on `/stream` should be refused rather than added to the airtime budget.
The connection pool stays as it is — the API and the UI need more than one socket — but only one
of them may be a `GET /stream` at a time.

**Blocked by:** nothing.

**Status:** proposed.

- [ ] `appliance.c` serves one audio client (`max_clients` 1, the pool unchanged).
- [ ] A second `/stream` is refused with the K7's existing 503 rather than answered.
- [ ] The ESP-NOW player is not an HTTP client, so it does not take the slot: one browser and the
      player can coexist, and that is the intended shape.
- [ ] The stream server's spec says one, and why (personal device, one pair of ears, one radio).

Spec: `docs/specs/sd-stream-server.md` (the client slots), `espnow-stream.md` E9.

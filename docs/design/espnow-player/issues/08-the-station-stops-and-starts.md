# 08 — The station stops and starts

**What to build:** a lifecycle for the station, so the format can follow the transport the moment
the setting changes: `mini_icecast_stop()` as the pair of `mini_icecast_start()`, an `app_services`
restart that re-resolves the format, and the Player row applying both the emitter and the station.

**Blocked by:** nothing (ADR 0106).

**Status:** proposed.

- [ ] `mini_icecast_stop()` ends the broadcast, telemetry and sender tasks, waits for them, closes client sockets, and frees the encoder, order, rings and burst.
- [ ] `app_services_station_restart(kbps)` stops, re-resolves and starts, leaving the network, card, HTTP server and panel alone.
- [ ] The transport setter applies the emitter and the station together: ESP-NOW is 128 kbps, HTTP is 256, from the moment the row is touched.
- [ ] Ten consecutive restarts on the board, with the census and the heap recorded each time.
- [ ] `espnow-stream.md` E9 and `menu.md`'s Player row say the change is immediate.

Spec: `docs/specs/espnow-stream.md` E9, `docs/specs/menu.md`, ADR 0106.

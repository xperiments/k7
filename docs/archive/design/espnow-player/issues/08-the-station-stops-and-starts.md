# 08 — The station stops and starts

**What to build:** a lifecycle for the station, so the format can follow the transport the moment
the setting changes: `mini_icecast_stop()` as the pair of `mini_icecast_start()`, an `app_services`
restart that re-resolves the format, and the Player row applying both the emitter and the station.

**Blocked by:** nothing (ADR 0106).

**Status:** built, and checked on the board.

- [x] `mini_icecast_stop()` ends the broadcast, telemetry and sender tasks, waits for them, closes
      client sockets, and frees the encoder, the order, the rings and the burst. The lock, the route
      registration and the completion semaphores outlive the station, and `/stream` and
      `/api/play` answer 503 while it is down.
- [x] `app_services_station_restart(kbps)` stops, re-resolves and starts, leaving the network, card,
      HTTP server and panel alone. The composition moved to `start_station()`, so the boot and a
      restart build the station the same way — including the Playlist selection.
- [x] The transport setter applies the emitter and the station together: ESP-NOW is 128 kbps, HTTP
      is 256, from the moment the row is touched. One place holds the rule
      (`app_services_station_bitrate`), and `appliance.c` asks it at boot too.
- [x] Ten consecutive restarts on the board, with the census and the heap recorded each time.

**Measured on the board (ten restarts, alternating 128 and 256, one request):** 5.1 s for the ten,
~380 ms each, every one `ESP_OK`, and the serial log carries all ten of `the station is stopped`,
`station format: … kbps`, `burst ring …`, `stream: 1 client slot(s), …` and
`the station restarted at … kbps` with no error line in between. Internal RAM held at
47.8–50.8 KB free, external at 2.54–2.80 MB — the swing is the format (a 128 kbps client ring is
164 096 B, a 256 one 324 096 B), not a decline: twenty more restarts in two earlier runs returned
to the same levels. `big` (the largest internal block) stayed at 31 744 B, once 15 360 B.

**Two things the restart taught us, both now in the code:**

- The Track that was playing is asked for again on the new station, so a format change does not
  sound like a skip — but only while the Library's own order is walking. A pick that names a
  Track outside the selected Playlist *drops* the order, and with it the owner's choice; the first
  version did that on every restart and wiped the selection. The restart now leaves a Playlist
  alone and starts at the top of it.
- The tone knobs' targets survive a restart: the filter state is rebuilt for the new rate, the
  targets are not reset.

**Host tier:** `host/tests/t45_station_restart.c` (24 checks) starts at 128, stops, starts at 256,
plays again, refuses a second start, and churns three more restarts; `make -C host check` passes.

Spec: `docs/specs/espnow-stream.md` E9, `docs/specs/menu.md`'s Player row, ADR 0106.

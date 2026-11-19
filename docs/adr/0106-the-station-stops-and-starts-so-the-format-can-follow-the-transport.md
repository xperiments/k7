# The station stops and starts, so the format can follow the transport at runtime

The station's format is fixed when the encoder is built: 128 kbps for the ESP-NOW player, 256 for
the web's icecast (ADR 0105, the setting's rule). Today that is decided at boot from the stored
transport, so flipping the setting takes effect at the next start. The user wants the flip to take
effect when it is flipped.

Nothing survives a format change: the encoder holds the frame geometry, the clients' rings were
sized from the bit rate, and every task in the broadcast was created for a station. So the honest
shape is not a reconfiguration but a lifecycle: **the station can be stopped, and started again
with another format.**

## Decision

- `mini_icecast_stop()` exists and is the pair of `mini_icecast_start()`: it asks the broadcast,
  telemetry and sender tasks to end, waits for them, closes the clients' sockets, frees the
  encoder, the order, the rings and the burst, and leaves the process able to start again.
- `app_services` gains a station restart: stop, re-resolve the station for the new format, start.
  The rest of the services — the network, the card, the HTTP server, the panel — are untouched: a
  format change is not a reboot of the device.
- The transport setting (`app_services_player_espnow_set`) applies the emitter and the station
  together, so ESP-NOW is 128 kbps and HTTP is 256 from the moment the row is activated.
- A listener on `/stream` is disconnected by the change, which is what a station restart means; a
  browser reconnects, and the ESP-NOW player simply sees the frames stop for the moment it takes.

## Considered options

- **Reboot the K7 on the setting.** Simple and already possible by hand, but it takes the panel,
  the card scan and the network with it for several seconds to change one number, and a reboot is
  also the watchdog's last resort — a control should not look like a fault.
- **Build both encoders at boot and switch between them.** Two encoders' state at once (the
  encoder's own buffers are tens of kilobytes), two client rings, and the clients would have to be
  told which station they are listening to. The stop/start is smaller and has one truth.

## Consequences

- The broadcast's tasks need an orderly end: the existing task loops gain a stop check, and every
  allocation `mini_icecast_start` makes needs its free in `mini_icecast_stop` — the kind of pair
  that is worth a board test of ten consecutive restarts, not one.
- The web's icecast gets its 256 kbps back when the setting is on HTTP, immediately.
- `espnow-stream.md` E9 says the setting applies now, and `menu.md`'s Player row no longer says the
  format waits for a start.

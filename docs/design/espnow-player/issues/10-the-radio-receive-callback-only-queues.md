# 10 — The radio's receive callback only queues

**What happened:** the K7 ran 9 h 46 m and then stopped serving anything: no ping, no
`/api/status`, no `k7.local`, and the station frozen — `decode 0 ms, decoded 0.0 s` in the
telemetry, the screen's VU line printing the same stale level every second, the ESP-NOW player
waiting with no frames. The telemetry task and the panel were still alive. A reset restored it.

**Why:** the receive callback ran in the Wi-Fi task, and on the player's status packet it took
the emitter's lock and *sent* — the NACKed frames, and, after a run of failures, the
`esp_wifi_disconnect()`/`esp_wifi_connect()` recovery — all from inside the callback. The
broadcast published every encoded frame by taking that same lock with `portMAX_DELAY`. So one
wedged send held the lock the station needed: the radio stopped, the encoder followed, and the
send watchdog could not fire because the send path *was* the thing stuck.

**Status:** built and checked on the board.

- [x] The receive callback copies the packet into a queue and returns to the driver — status and
      share-window frames alike.
- [x] One task (`espnow`) owns the emitter: it applies the credit, answers the NACKs, sends, and
      runs the recovery (re-associate, then restart as the last resort). The lock is that task's
      own state; nothing else waits on it.
- [x] The broadcast's publish path never touches the lock: `app_board_audio_frame` copies into
      the queue and returns to encoding. A full queue drops the frame and counts it, and the
      player's next gap names it again.
- [x] The share window's responder moved with it, so `share.c` no longer answers from the Wi-Fi
      task either.
- [x] `espnow-stream.md` E11 states the rule.

**Measured on the board, with every send forced to fail:**

- 0.78 s after the failures began: `the radio stopped sending: re-associating (1)`, then the
  re-association (`wifi:state: run -> init`, `connected with xperiments-2.4G`, `sta ip` back),
  and no reboot — the device's uptime kept counting through it.
- The station never stopped: the VU line kept changing and the broadcast advanced to a new Track
  *during* the failure window (`04 - The Man Who Sold the World` at 68 s).
- With the failures cleared the player resumed on its own — `receiving 192.168.0.13: 44100 Hz,
  128 kbps` with the frame count climbing — without a reset of either board.
- The clean build, playing: `espnow audio: 2012 frames, 0 resent, 5 held, 0 dropped, 4024
  packets, a player is listening`, and the frame queue's storage is in PSRAM, not internal RAM.

**Not checked:** the share window's handover from the panel (it needs the Menu's `Share` row to be
tapped, and the player's network record is already stored). The path is the same queue and the
same worker, but only a window will prove it. A transport change still takes the emitter's lock
for its state change; it is rare, and it is the one remaining task that can wait on a send.

Spec: `docs/specs/espnow-stream.md` E11, §7. Ticket for the bench: this file.

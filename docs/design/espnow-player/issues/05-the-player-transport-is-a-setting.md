# 05 — The player's transport is a setting

**What to build:** a row in the K7's Settings menu that chooses how the player's audio is emitted —
ESP-NOW or HTTP — persisted in NVS, applied without a reboot, and named in the log. The micro
switch on the player will be the same call when it exists.

**Blocked by:** nothing (EN-04's constant is the value it will replace).

**Status:** proposed.

- [ ] A Settings row `Player: ESP-NOW / HTTP` beside Backlight and Rescan, with its value on the row.
- [ ] Choosing ESP-NOW feeds the emitter; choosing HTTP clears the feed, stops the resends and forgets the player's peer.
- [ ] The choice survives a power cut.
- [ ] The board's `app_board_audio_set(bool)` is what both the row and the future switch call.
- [ ] `menu.md`'s Settings group and `espnow-stream.md` E9 say so.

Spec: `docs/specs/espnow-stream.md` E9, ADR 0104.

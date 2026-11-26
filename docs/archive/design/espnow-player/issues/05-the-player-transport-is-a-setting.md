# 05 — The player's transport is a setting

**What to build:** a row in the K7's Settings menu that chooses how the player's audio is emitted —
ESP-NOW or HTTP — persisted in NVS, applied without a reboot, and named in the log. The micro
switch on the player will be the same call when it exists.

**Blocked by:** nothing (EN-04's constant is the value it replaces).

**Status:** done — the row, the runtime switch and the storage; the tap itself needs a finger.

- [x] A Settings row `Player` showing `ESP-NOW` or `HTTP` (fifth in Settings, after About, so no other list's indexes move).
- [x] Choosing ESP-NOW feeds the emitter; choosing HTTP clears the feed, stops the resends and forgets the player's peer.
- [x] The choice is stored in NVS (`k7.prefs`/`player`) and read at boot, defaulting to ESP-NOW.
- [x] `app_board_audio_set(bool)` is what the row and the future switch both call, through `app_services_player_espnow_set()`.
- [x] `menu.md`'s Settings group and `espnow-stream.md` E9 say so.

## Comments

- **The policy lives in `app_services`, not the appliance.** The panel's action needed the setter,
  the boot needed it, and the appliance already depends on the screen's component — putting it in
  `app_services` (which owns the broadcast and can reach the emitter, the sink and the storage)
  keeps the dependency one-way. The K7's host shim gained the emitter's stubs, which truthfully
  report "no radio" so the fallback logs as it would.
- **Two test findings worth keeping**: `menu_facts_t`'s new field had to join `facts_same()`, or a
  change to the transport alone never reached the model; and every other menu suite navigates
  Settings by index, which is why the row went last.
- **Checked on the host**: all unit tests, including the row's value before and after the facts
  change and the action it returns. **Checked on the board**: the K7 boots through the loader and
  logs `the player's audio goes out over ESP-NOW`. **Not checked**: a tap on the row, because that
  needs a finger on the panel.

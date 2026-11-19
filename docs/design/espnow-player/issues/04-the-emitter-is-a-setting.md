# 04 — The emitter is a setting, never both

**What to build:** the choice. The K7 emits the player's feed over HTTP or over ESP-NOW, one at a
time, decided at boot and logged. The web UI's own HTTP listeners keep working either way; they are
a different client and the rule is about the feed the player consumes.

**Blocked by:** 02 — the emitter.

**Status:** done — both modes booted on the K7, each logging its choice.

- [x] `appliance.c` names the transport and logs it once at boot.
- [x] ESPNOW starts the emitter and leaves the broadcast's HTTP clients untouched.
- [x] HTTP starts nothing new: the player is expected to fetch `/stream` as it does today.
- [x] Neither mode ever runs both emitters.
- [ ] A Menu row that chooses between them is the next surface, and it is not in this ticket.

## Comments

- **The setting is one constant** beside the station format, `player_audio_espnow`, because a
  build cannot run both and a Menu row needs a surface that does not exist yet.
- **Both modes on the board**, in one sitting, by flipping the constant and reflashing:
  `app: the player's audio goes out over ESP-NOW` and
  `app: the player's audio is fetched over HTTP; the ESP-NOW emitter is off`. The board was left
  on the ESP-NOW build, which is the source's default.

Spec: `docs/specs/espnow-stream.md` E9.

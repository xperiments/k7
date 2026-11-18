# 04 — The emitter is a setting, never both

**What to build:** the choice. The K7 emits the player's feed over HTTP or over ESP-NOW, one at a
time, decided at boot and logged. The web UI's own HTTP listeners keep working either way; they are
a different client and the rule is about the feed the player consumes.

**Blocked by:** 02 — the emitter.

**Status:** pending.

- [ ] `appliance.c` names the transport and logs it once at boot.
- [ ] ESPNOW starts the emitter and leaves the broadcast's HTTP clients untouched.
- [ ] HTTP starts nothing new: the player is expected to fetch `/stream` as it does today.
- [ ] Neither mode ever runs both emitters.
- [ ] A Menu row that chooses between them is the next surface, and it is not in this ticket.

Spec: `docs/specs/espnow-stream.md` E9.

# 02 — The K7 emits the player's audio over ESP-NOW

**What to build:** the sender. Every frame the broadcast encodes goes to an ESP-NOW emitter as well
as to its HTTP clients (or instead of them, per 04); the emitter keeps the last two seconds of
frames, sends only what the player's status packet says fits, and resends what the player names.

**Blocked by:** 01 — the wire.

**Status:** pending.

- [ ] `mini_icecast` hands every encoded frame to a sink set at boot, without changing its HTTP clients.
- [ ] The emitter fragments each frame and sends it unicast to the player's MAC, within the credit.
- [ ] The last two seconds of frames are kept and a NACK in a status packet resends the named sequences, inside the same credit.
- [ ] No status packet yet means nothing is sent: the player speaks first.
- [ ] The census reports frames sent, resends and drops.

Spec: `docs/specs/espnow-stream.md` E1–E7.

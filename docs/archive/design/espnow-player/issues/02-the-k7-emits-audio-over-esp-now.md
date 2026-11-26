# 02 — The K7 emits the player's audio over ESP-NOW

**What to build:** the sender. Every frame the broadcast encodes goes to an ESP-NOW emitter as well
as to its HTTP clients (or instead of them, per 04); the emitter keeps the last two seconds of
frames, sends only what the player's status packet says fits, and resends what the player names.

**Blocked by:** 01 — the wire.

**Status:** done — host tier (t43, t44) and the K7 on the board with the emitter up.

- [x] `mini_icecast` hands every encoded frame to a sink set at boot, without changing its HTTP clients.
- [x] The emitter fragments each frame and sends it unicast to the player's MAC, within the credit.
- [x] The last two seconds of frames are kept and a NACK in a status packet resends the named sequences, inside the same credit.
- [x] No status packet yet means nothing is sent: the player speaks first.
- [x] The census reports frames sent, resends and drops.

## Comments

- **Board evidence, and what it cannot show yet.** The K7 boots with the emitter and holds the
  stream at 38 frames a second with `0 packets` sent, `held` climbing one per frame, and its HTTP
  listener still answering 200 with the card intact — the emitter costs the broadcast nothing when
  nobody is listening. No packet has crossed the radio because nothing sends a status packet yet:
  that is EN-03, and it is where `0 packets` becomes a stream.
- **The radio's lifecycle moved.** The audio needs ESP-NOW up permanently, so the board initialises
  it once and the share window borrows it (`network-handover.md` H13 revised,
  `espnow-stream.md` E10). The regression risk is the handover, and it needs a window opened on the
  panel to test — the one thing this ticket could not do without a finger on the screen.
- **The arena is 135 KB in PSRAM** (96 slots of 1 441 bytes). Internal RAM was already the scarce
  resource on this board and is unchanged: the icecast census still reads ~51 KB free and a
  31 744-byte largest block.


Spec: `docs/specs/espnow-stream.md` E1–E7.

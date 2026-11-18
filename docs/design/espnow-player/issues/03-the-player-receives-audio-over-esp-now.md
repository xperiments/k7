# 03 — The player receives the audio over ESP-NOW

**What to build:** the receiver, and the HTTP stream client's removal. Packets arrive in the board's
ESP-NOW callback; they are reassembled into whole frames, held in order across a gap for up to a
second, and fed to the ring through the MP3 framer the player already has. The status packet goes
back every 100 ms.

**Blocked by:** 01 — the wire; and the K7's emitter (02) to hear anything.

**Status:** pending.

- [ ] The board's ESP-NOW receive callback dispatches `0x04` to the receiver and leaves the handover's `0x01`–`0x03` alone.
- [ ] Frames are reassembled and handed to `player_mp3_feed` in sequence order; a duplicate is dropped and a gap is not guessed.
- [ ] A gap holds the frames behind it for at most one second, is asked for on every status packet, and is abandoned after it: abandoned frames are counted.
- [ ] The status packet carries the ring's free bytes, the highest sequence without a gap, and up to eight missing sequences.
- [ ] The HTTP client, its reconnect schedule and its stall rules are gone; three seconds without an audio packet is *Ready* and nothing reconnects.
- [ ] The ring, the prime and the playout are untouched.

Spec: `docs/specs/espnow-stream.md` E3–E8; `docs/specs/k7player.md` R23–R30 (revised).

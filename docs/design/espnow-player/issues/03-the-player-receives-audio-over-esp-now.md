# 03 — The player receives the audio over ESP-NOW

**What to build:** the receiver, and the HTTP stream client's removal. Packets arrive in the board's
ESP-NOW callback; they are reassembled into whole frames, held in order across a gap for up to a
second, and fed to the ring through the MP3 framer the player already has. The status packet goes
back every 100 ms.

**Blocked by:** 01 — the wire; and the K7's emitter (02) to hear anything.

**Status:** done — the player plays from the K7's emitter, host tier and board.

- [x] The board's ESP-NOW receive callback dispatches `0x04` to the receiver and leaves the handover's `0x01`–`0x03` alone.
- [x] Frames are reassembled and handed to `player_mp3_feed` in sequence order; a duplicate is dropped and a gap is not guessed.
- [x] A gap holds the frames behind it for at most one second, is asked for on every status packet, and is abandoned after it: abandoned frames are counted.
- [x] The status packet carries the ring's free bytes, the highest sequence without a gap, and up to eight missing sequences.
- [x] The HTTP client, its reconnect schedule and its stall rules are gone; three seconds without an audio packet is *Ready* and nothing reconnects.
- [x] The ring, the prime and the playout are untouched.

## Comments

- **On the bench**: `receiving 192.168.0.13: 44100 Hz, 128 kbps, 905 frames, ring 70/96 KB,
  flushed 0, lost 0, dups 28, assembled 905, dropped 1` and `playout: output 44100 Hz: 847872
  frames, 0 underruns, 0 refused, 1 primes`. The K7 answers with `a player is listening` and
  sends two packets a frame. No HTTP request is made by the player.
- **Three bugs the bench found**, all worth knowing: the stream module cannot own a clock (the
  host harness drives a virtual one, so it takes its clock from the tick); the receiver's 12 KB
  of frame buffers cannot be .bss (the player's DRAM overflowed by 7 KB - they are heap now, and
  the image is 891 KB with 15 % free); and a receiver that starts listening mid-frame fills every
  assembly slot with orphans - `assembled 0, dropped 1498` - so a slot is now evicted for the
  oldest incomplete frame.
- **Not re-checked after this change**: the handover (share window), because it needs a window
  opened on the panel. The radio lifecycle under it did change. That is the one risk this ticket
  leaves.


Spec: `docs/specs/espnow-stream.md` E3–E8; `docs/specs/k7player.md` R23–R30 (revised).

# 04 — The K7 probes and hands the image over

**What to build:** the act's engine on the K7: rising the radio and probing, collecting the
players that answer, and serving each stale one in turn — stop the station, send 4 KB blocks under
a stop-and-wait acknowledgement, restart the station — all of it in the emitter's one worker.

**Blocked by:** 01 — The update's bytes; 02 — The release signs the image, and the K7's flash
carries it.

**Status:** built; host tier and the K7's firmware build, nothing on a board. `t48_player_update_act`
drives the act with a scripted player (99 checks): three broadcast probes, the answers deduped by
MAC, the up-to-date players skipped, and each stale player served in turn - station stopped, begin,
`ready`, blocks, result, station started - with the retries, the drops, two players in one press and
a refusal; the K7's firmware builds at 2,129,200 B with 49 % of its partition free. The board pass
is PU-06.

- [x] The probe goes out three times when the list opens, the answers make the set (a MAC and a
      version each, in RAM only), and a player on the image's version is skipped without a byte
      being sent.
- [x] A block of up to seventeen packets is sent back to back, the acknowledgement is awaited for
      200 ms, and the block is re-sent up to three times before that player is dropped; the others
      continue.
- [x] The station stops before a transfer and restarts after it, once per player, so the player
      that just rebooted can prove itself; the station is playing again when the act ends, whatever
      the act's outcome.
- [x] The sending is a mode of the emitter's worker, never the receive callback and never a second
      sender beside it; an aborted act restarts the station and leaves nothing half-sent.
- [x] The K7 reads the image's header from its `player` partition at boot, hashes the image it
      holds and reports **No image** rather than a bad one; the row's version comes from that
      header and the act waits for the player's `ready` before the first block.
- [x] The update is offered only with the ESP-NOW transport, where the player can hear the K7; with
      HTTP the row says `Transport is HTTP`, and the transport row above it is the way out.

Spec: `docs/specs/player-update.md` U4–U10, U26–U29; `docs/specs/espnow-stream.md` E10–E11;
[ADR 0117](../../../adr/0117-a-players-update-is-a-persons-act.md).

## Comments

- **The K7 still keeps no record of a player.** The set is the act's, not the device's: it is gone
  when the list closes, and a reboot in the middle of an act starts from a probe again.
- **~218 blocks at ~40 ms is ten seconds a player.** The flash's erase is the pace-setter, not the
  radio; a windowed protocol would only hide waiting that is not the link's.

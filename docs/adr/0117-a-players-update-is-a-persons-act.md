# A player's update is a person's act, and the new image has to prove itself

The K7Player has no button, no screen and no page (`k7player.md` R31–R32); a person can only act
on it from the K7, which is why the share window is opened from the Menu rather than by the player
([ADR 0099](./0099-the-network-is-handed-over-esp-now-in-a-window.md)). Its flash has one 1 MB
factory slot and an image that is 891,712 B of it, so a second slot is the first thing an
over-the-air update needs. And a headless device that goes
silent cannot be told from a broken one: a release that boots but never joins leaves a player
with no way back except a cable, which is the thing the update exists to avoid.

## Decision

- **A person opens it.** `Settings → Player` becomes a list: the transport row it has today, and
  `Update`. The row is inert with no image on the K7, with no player answering, and when every
  player is already on that image.
- **Opening the list probes, with the ESP-NOW transport chosen.** The K7 broadcasts a probe three
  times; a player answers with its version and its board, and is addressed by the answer's source
  MAC. The answers collected while the list is open are the players this press serves; nothing
  about them is stored after the list closes. With HTTP chosen there is nothing to offer — the
  player hears no K7 to prove an image against — so the row says so.
- **One at a time, and the station stops for it.** For each player whose version differs: stop the
  station, send the 128-byte header and wait for the player's `ready` — the slot is erased before
  a byte moves, and the erase is about five seconds — then 4 KB blocks of at most 17 frames under
  a stop-and-wait acknowledgement, restart the station so the player that just rebooted can prove
  itself, then the next. 218 blocks is about fifteen seconds a player.
- **A failure costs one player, not the act.** A block is retried three times; a player that stops
  answering is dropped, the rest continue, and the row reports what happened at the end.
- **An aborted transfer is a no-op on the player.** It discards the spare slot's handle and keeps
  running the image it was running.
- **The player writes only its inactive slot, verifies, switches and reboots**, lighting
  **Updating** from the moment it accepts the header until it reboots.
- **The image proves itself by doing the job.** Before switching, the player writes a marker in
  NVS; on the boot after an update it marks the image valid on the first frame that decodes and
  reaches the output, and after 90 s without one it asks the bootloader to roll back. A boot with
  no marker — the first boot after the cable — marks itself valid at once.
- **The player's table gains `otadata` and two 1.75 MB OTA slots**; `nvs` and `phy_init` keep the
  offsets they had, so the USB flash that migrates a player keeps the network record it was given.

## Considered options

- **Automatic on a version mismatch.** No one has to know; the music stops unasked, and an image
  that fails keeps being retried on every boot.
- **From the App.** A familiar surface, and a local act made to depend on the App being open, when
  the person is in the room with the K7.
- **The player pulls at boot.** It has no HTTP client and no stored K7 address, so this is a new
  address discovery before it is an update.
- **A sliding window with NACKs**, reusing the audio stream's machinery. The player's flash writes
  are the bottleneck, not the radio, so the speed it buys is small and it inherits the audio
  stream's failure modes for a 900 KB image.
- **Interleaving with the stream.** No gap in the sound, and a stutter for the whole act while the
  flash writes starve the audio.
- **1.25 MB slots, the IDF default.** Fits today's image and puts the 1.48 MB speaker build out of
  reach of the air for good; **one slot, overwritten in place**, is not available to a running app
  at all.
- **No rollback.** Simplest, and a silent player is then a player with a cable in its future.

## Consequences

- **Ten seconds of silence per player, chosen by the person**, announced by the row and by the
  player's light.
- **The light's closed list gains a fifth state**, so every sentence that says "four states"
  changes, and the light's own test grows one case.
- **The Menu's Settings gains a list where it had a toggle**, and `menu.md`'s tree gains the
  Player row it was already missing.
- **The K7's radio gains a third user** — the audio, the share window, and now the update — and the
  update runs in the emitter's one worker rather than beside it (espnow-stream.md E11).
- **The signature refusal and the rollback are board evidence.** The host proves the frames, the
  version rule, the block machine and the rollback decision behind a stub; the ECDSA verify is
  mbedTLS on the board, exactly as X25519 is in the handover.
- **A player that is silent at the end of an act is the one case the K7 cannot read**: it cannot
  tell a player that failed to boot from one that is switched off. The rollback is the player's,
  and the K7's word for the outcome is what the player last reported.

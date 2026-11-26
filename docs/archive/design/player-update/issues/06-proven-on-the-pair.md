# 06 — The update is proven on the pair

**What to build:** the board evidence for everything above, on the two real boards: the update that
works, the three ways it must not hurt, and the record that must survive the flash that makes it
possible.

**Blocked by:** 02 — The release signs the image, and the K7's flash carries it; 03 — The player's
second slot, its verify and its rollback; 04 — The K7 probes and hands the image over; 05 — The
Menu's Player list and its progress.

**Status:** proven on the pair, with three cases not run. On the bench, 2026-11-24: the K7 was
flashed with the new table (`player 01 42 00710000 00200000` in its boot log) and its network
survived; the player was flashed onto the two-slot table and kept the record the K7 had handed it
(`a record is stored: xperiments-2.4G`, `joined` — no share window after the flash). The player on
0.6.2 took the K7's 0.7.0 over the air: blocks 0..224 written, `ota_1` holds `0.7.0`, otadata says
`seq 2, state VALID`, and the boot log reads `Loaded app from partition at offset 0x1d0000`,
`App version: 0.7.0`, `joined`, `light: playing`. A 0.6.2 image signed with a key that is not the
project's was transferred completely and written into `ota_0`, and the boot stayed on `ota_1` — the
verify refused the switch. Not run: both power-cut cases (they need a person to cut power
mid-transfer) and the two-player press (one player board). The light's patterns were not recorded
by eye.

- [x] The pair, on the bench: the row goes `0.6.2 → 0.7.0`, the player lights **Updating**, the ten
      seconds pass and the music comes back on the new version, with the numbers logged.
- [ ] A power cut mid-transfer: the old image is still what runs and a second press completes the
      update.
- [ ] A power cut after the switch and before the proof: the bootloader restores the old image and
      the player plays again.
- [x] An image signed with another key is refused (result `4`), the player keeps running, and the
      act says so.
- [ ] Two players, one press: both update, and the row says which it is on.
- [x] A K7 flashed with four parts keeps `nvs`: its network record survives and the player joins
      without a share window. A player flashed onto the two-slot table boots and plays and is not
      rolled back for never hearing a K7.
- [ ] The light's five patterns, by eye.

Spec: `docs/specs/player-update.md` §8.

## Comments

- **The panel shift, and the two wrong fixes before the right one.** The first update put the picture
  a band to one side for the whole act. A mapping of the player partition did not help — a cache
  miss is still a flash access, and ADR 0055 says any flash access takes the PSRAM window and
  displaces the scan. A retrying settle then blocked the act for forty seconds (the row sat on
  Searching) because it ran inside the act's lock. The fix that holds: the K7 copies the image into
  PSRAM once at boot, before the panel's first clock, and the act hands the blocks out of PSRAM —
  no flash access in the transfer at all. Measured on the bench: 224 blocks, the picture straight
  throughout, `update: done: updated 1`, the player `switching to 0.7.2` and `the image made sound:
  it stays`.

- **What the bench found, and the fixes it forced:** the player never registered its update
  callback, so a probe was answered by nobody (`No player`); its descriptor carried IDF's
  git-describe string, so the row read `0.0.0`; a 4 KB block's seventeen frames overran the
  driver's send queue (`ESP_ERR_ESPNOW_NO_MEM`) until the sender paced them; the audio emitter's
  resends crowded the transfer's airtime (`5 frames lost to a full queue`) until the act switched
  the emitter off; the station's stop and start from the update task left the panel's scan offset
  until the act asked for the settle; and the player's verify did not check the image's own
  descriptor against the header's version (U18) until it did.
- **The two power-cut cases are the one gap a board session cannot fake.** The rollback is
  host-proven (`t08_ota`) and the bootloader is configured for it; pulling power mid-transfer is a
  person's act and is left for one.

- **The signature refusal and the rollback are board evidence, not host evidence.** The host
  proves the machine behind a stub; the ECDSA verify is mbedTLS on the board, exactly as X25519 is
  in the handover.
- **The record surviving the flash is the one that bites later if it is not checked now.** A
  partition table that moves `nvs` would make every provisioned player need a share window after
  the first flash, which is the ceremony the whole design avoids.

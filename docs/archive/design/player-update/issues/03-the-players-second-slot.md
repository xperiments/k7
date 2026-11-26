# 03 — The player's second slot, its verify and its rollback

**What to build:** the player's half: a partition table with two OTA slots, a receive path that
writes only the slot it is not running from, the verify that decides whether the image is worth
booting, and the rollback that puts the old image back when the new one never makes sound.

**Blocked by:** 01 — The update's bytes.

**Status:** built; host tier and the player's firmware build, nothing on a board. `t08_ota` drives the
block machine, the refusals, the retry's acknowledgement, the verify's refusal and the rollback
clock (59 checks); `t05_light` proves the fifth state and that it outranks Ready (44 checks); the
player builds onto the two-slot table at 915,792 B with 50 % of the slot free. The board pass is
PU-06.

- [x] `player/partitions.csv` is the layout of `player-update.md` U24: `ota_0` and `ota_1` at
      1.75 MB, `otadata` after them, `nvs` and `phy_init` where they were; a player flashed over
      USB onto it boots, plays, and keeps the network record it was handed.
- [x] The blocks arrive in the radio's callback, are staged one at a time, and are written from the
      player's own task to the inactive slot only — never to the running one.
- [x] The verify: the image's chip, its application descriptor, the SHA-256 of the written bytes
      and the signature against the compiled-in key; a failure answers result `4` and leaves the
      boot partition untouched.
- [x] The marker in NVS, the switch and the reboot; a boot with no marker marks itself valid at
      once, and a boot with one marks valid on the first frame that decodes and reaches the output,
      or asks for a rollback after ninety seconds without one.
- [x] The light's fifth state, **Updating**, with its own pattern, and the host tests that drive
      the block machine, the verify's refusal and the rollback decision behind stubs.

Spec: `docs/specs/player-update.md` U11–U25; `docs/specs/k7player.md` R31, R34–R38;
[ADR 0117](../../../../adr/0117-a-players-update-is-a-persons-act.md).

## Comments

- **The begin is a round trip.** `esp_ota_begin` erases the whole spare slot — about five seconds
  — so the K7 cannot send block 0 into it. The player now answers a begin with result `6`, ready,
  and the K7 sends no block before it (player-update.md U10, U13a, U16).
- **The block stage is the heap's, not the link's.** As a 4 KB static the player's `.dram0.bss`
  overflowed by 208 bytes; the stage is `heap_caps_malloc`ed in `player_ota_init` now.

- **The first boot after the cable must not roll back.** A player that has just been flashed has
  no network and cannot hear a K7; if the rollback applied to it, a fresh player would roll into
  an empty slot. The marker is what tells an over-the-air boot from a cable one.
- **One 4 KB block in RAM at a time.** This chip has no PSRAM and its own header says twelve
  kilobytes is a third of what is left after the ring; 4 KB is what the flash sector needs anyway.

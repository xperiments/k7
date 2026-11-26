# The player's update

**Status:** accepted · **Updated:** 2026-11-25

How a K7Player's firmware is replaced over the air, from the image a K7 carries to the frame the
new image proves itself with: [ADR 0116](../adr/0116-a-players-firmware-is-a-signed-image-the-k7-carries.md)
for the signed image and [ADR 0117](../adr/0117-a-players-update-is-a-persons-act.md) for the act.
The player's side of the flash is `k7player.md` R31 and R34–R38; this document owns the bytes and
the act.

Both boards already share the handover's version byte and type byte on one radio
([`network-handover.md`](./network-handover.md), [`espnow-stream.md`](./espnow-stream.md)): `0x01`
beacon, `0x02` request, `0x03` record, `0x04` audio, `0x05` status and `0x06` tone. The update adds
`0x07` probe, `0x08` answer, `0x09` begin, `0x0A` block, `0x0B` block acknowledgement and `0x0C`
result, and every integer is big-endian.

## 1. The image the K7 carries

**U1. The image is a 128-byte header and then the release's `k7player.bin`, in one 2 MB
partition.** `player` is a data partition at `0x710000`, 64 KB-aligned, so the K7 can read it in
place and read its header without parsing the image:

| offset | size | field |
|---:|---:|---|
| 0 | 4 | magic, `K7PU` |
| 4 | 1 | header version, `1` |
| 5 | 1 | board, `1` = the classic ESP32 K7Player |
| 6 | 1 | signature length, 1..72 |
| 7 | 1 | reserved, `0` |
| 8 | 4 | image length, in bytes |
| 12 | 6 | version: major, minor and patch, each a `u16` |
| 18 | 2 | reserved, `0` |
| 20 | 32 | SHA-256 of the image |
| 52 | 72 | the signature, DER, the first `signature length` bytes |
| 124 | 4 | reserved, `0` |
| 128 | | the image, `image length` bytes |

A header whose magic, version, board, signature length or image length is wrong, whose image would
run past the partition, or whose SHA-256 does not match the stored image, is not an image: the K7
reports **No image** and offers nothing. The K7 reads the version it would hand over from the
header and never from a file name.

**U2. The release signs the image, and a release without the signature is refused.**
`player/build.sh release` signs `SHA-256(k7player.bin)` with the project's ECDSA P-256 key and
hands the image and its DER signature to `tools/release_publish.py`, which writes the header, the
signed blob and the manifest. The signature's DER encoding is at most 72 bytes. The public key is
compiled into the player; the private key is never in the repository. `check-firmware.mjs` verifies
the committed signature with the committed public key.

**U3. The K7's flash writes it as the fourth part.** `web/firmware.json` gains the part
(`k7player.img` at `0x710000`) pointing at the player's release bytes, and the page writes it like
the other three (`k7-site.md` F3, amended). The K7 never writes that partition itself.

## 2. Finding the players

**U4. The probe is six bytes, broadcast.** A person opening `Settings → Player` sends the probe
three times, 100 ms apart, on the channel the K7 is on:

| offset | size | field |
|---:|---:|---|
| 0 | 1 | version, `1` |
| 1 | 1 | type, `0x07` |
| 2 | 4 | the nonce, a counter that changes with every act |

**U5. The answer is fourteen bytes, unicast to the probe's source.** A player that has a network
record, is not already updating and is on that channel answers:

| offset | size | field |
|---:|---:|---|
| 0 | 1 | version, `1` |
| 1 | 1 | type, `0x08` |
| 2 | 4 | the probe's nonce, echoed |
| 6 | 6 | its own version: major, minor, patch, each a `u16` |
| 12 | 1 | its board, `1` = the classic ESP32 K7Player |
| 13 | 1 | flags, `0` |

**U6. A player with no record does not answer.** It sweeps channels, cannot stay on this one, and
could not prove the image afterwards; taking one would only cost the air.

**U7. The answers are the act's players, and nothing outlives the list.** The K7 collects answers
until 300 ms pass without one. The set — a MAC and a version each — lives in RAM while the list is
open and is not written anywhere; the K7 still keeps no record of a player (ADR 0098).

**U7a. Both sides register the other for the act and forget it.** The K7 adds an answering player
as a unicast peer when the answer arrives, and the player adds the probe's source before it
answers, as the handover does (`network-handover.md` H30). Both are dropped when the act ends or
the list closes, so the peer table is where it was.

**U8. The update is offered only when the player can hear the K7.** With the ESP-NOW transport the
radio is already up (`espnow-stream.md` E10) and the probe goes out; with HTTP the K7 emits nothing
over ESP-NOW, the player hears nothing by design (ADR 0105), and the row reads **Transport is
HTTP** — the person changes the transport row above it, which restarts the station at ESP-NOW's
format, and the next time the list opens the update is offered.

**U9. A player on the image's version still answers, and the K7 does not send it anything.**
That is how the row can say **Up to date** instead of guessing.

## 3. The transfer

**U10. The begin is 118 bytes, unicast, and it is the whole claim.**

| offset | size | field |
|---:|---:|---|
| 0 | 1 | version, `1` |
| 1 | 1 | type, `0x09` |
| 2 | 6 | the image's version |
| 8 | 1 | board |
| 9 | 4 | image length |
| 13 | 32 | SHA-256 of the image |
| 45 | 1 | signature length |
| 46 | 72 | the signature, DER |

The frame is 118 bytes, and the last field ends exactly at it.

The player accepts it only when the board is its own, the version differs from its own, the image
fits its spare slot, and it is not already updating. It answers U16's result either way: `ready`
once the spare slot is erased and open, or the refusal that says why. The K7 sends no block
before `ready` — the erase is about five seconds, and a block sent into it is a block lost.

**U11. A block is 4 KB in at most seventeen packets.** Each packet carries its block's index and
its place in the block:

| offset | size | field |
|---:|---:|---|
| 0 | 1 | version, `1` |
| 1 | 1 | type, `0x0A` |
| 2 | 2 | block index, from 0 |
| 4 | 1 | fragment index, from 0 |
| 5 | 1 | fragment count, 1..17 |
| 6 | 1..244 | payload |

A 4 KB block is sixteen fragments of 244 bytes and a seventeenth of 192; the 0.6.2 image's last
block is 2 880 bytes, and the image is 218 blocks.

The block is copied out of the image the K7 holds in PSRAM, never read from flash during the act:
a flash access displaces the RGB panel's scan until the engine restarts (ADR 0055), and 224 reads
in ten seconds is a picture a band to one side for the whole transfer. The K7 copies the image into
PSRAM once at boot, before the panel's first clock (ADR 0116); a device that cannot hold it says so
and falls back to flash reads, at the panel's price.

**U12. The acknowledgement is five bytes.** `version, type 0x0B, block index (u16), result`: `0`
written, `1` retry me, `2` refuse the block. It is also the progress the K7's row shows.

**U13a. The begin is a round trip.** The K7 sends the begin once and waits up to fifteen seconds
for U16's `ready` or a refusal; only then does block 0 go out. A second begin while one is being
opened restarts the act (the player drops the half-written slot), which is the recovery a lost
begin needs.

**U13. The block is stop-and-wait.** The K7 sends the block's fragments back to back, waits 200 ms
for the acknowledgement, and re-sends the whole block if none or a retry comes. Three block sends
without a written acknowledgement and that player is dropped from the act; the others continue.

**U14. The player writes each block as it arrives, and holds one at a time.** The block is copied
into a single 4 KB staging buffer and written to the inactive slot from the player's own task; the
radio's callback does nothing but hand the packet over (`espnow-stream.md` E11, mirrored).

**U15. The player never writes the slot it is running from.** It writes
`esp_ota_get_next_update_partition()`, so the 4 KB erase and the cache's stall land on the image
that is not running.

**U16. The result is nine bytes, and the K7 waits up to ten seconds for it.** `version, type
0x0C, result, version of the image`: `0` switched, `1` same version, `2` wrong board, `3` no room,
`4` verify failed, `5` aborted, `6` ready. A verify over 891 KB is a flash read, a SHA-256 and one
ECDSA, so the wait is generous on purpose; ten seconds without a result and the player is dropped
like a failed block. The begin's own wait is fifteen seconds — the erase is the one step that is
not the radio's — and a `ready` that never comes drops the player before a byte moves.

**U17. An abort is a no-op on the player.** A transfer that loses its K7, or is abandoned, discards
the update handle; the running image is untouched, the spare slot is left partial, and the next
begin starts again at block 0. The K7 restarts the station on any abort.

## 4. Verify and switch

**U18. The player verifies before it switches, and a failure switches nothing.** In order: the
image's own chip identifier is this chip's; the image's application descriptor names this project
and the header's version; the SHA-256 of the bytes written to the spare slot equals the header's;
the signature verifies against that digest with the compiled-in key. Any failure answers result `4`
and the boot partition is not touched.

**U19. The switch is three steps and the last one is a reboot.** Set the boot partition to the
written slot, write the marker of U20, reboot.

**U20. The marker says an update just landed, in NVS beside the record**: the version the update
installed and a flag. A boot whose running version is not the marker's is the old image after a
rollback: it clears the marker and marks itself valid at once. The marker is cleared the moment the
image it names is marked valid.

**U21. A boot with no marker marks itself valid at once.** The first boot after the cable carries
no marker and must not be rolled back for failing to hear a K7 it has not been given a network for.

**U22. A boot with a marker proves the image by doing the job.** The first frame that decodes and
reaches the output marks the image valid and clears the marker. Ninety seconds from the boot
without one and the player clears the marker and calls
`esp_ota_mark_app_invalid_rollback_and_reboot()`; the bootloader restores the other slot, and the
player is what it was before the update.

**U23. Nothing weaker counts as proof.** Booting, joining the network, hearing a probe or receiving
a packet that does not decode never marks a pending image valid.

## 5. The player's flash

**U24. Two OTA slots, and the record's NVS does not move.**

| Name | Type | SubType | Offset | Size |
|---|---|---|---:|---:|
| `nvs` | data | nvs | `0x9000` | 24 KB |
| `phy_init` | data | phy | `0xF000` | 4 KB |
| `ota_0` | app | ota_0 | `0x10000` | 1.75 MB |
| `ota_1` | app | ota_1 | `0x1D0000` | 1.75 MB |
| `otadata` | data | ota | `0x390000` | 8 KB |

`nvs` and `phy_init` keep the offsets the single-app table gave them, so the USB flash that moves
a provisioned player onto this table keeps its network record, and no share window is needed after
it. The two slots hold the 0.6.2 image at 891,712 B and the speaker build at 1,550,272 B with room
to spare.

**U25. The light says Updating from the begin's acceptance until the reboot** (`k7player.md` R31),
and nothing else it could say is true while it is writing its own flash.

## 6. The act on the K7

**U26. The row is a state, not a button that hopes.** The direction reads the player's version,
then the image's. Under `Settings → Player`, `Update` shows:
**No image** (inert, no partition), **Transport is HTTP** (inert, U8), **No player** (inert, no
answer), **Up to date** (inert), the direction `0.6.2 -> 0.7.0` or `0.6.2 <- 0.7.0` (actionable),
`Updating 37 %` / `Updating 2 of 3` while it runs, and the outcome when it ends, naming the
firmware it ended with rather than a count, on a second line because the value column is too
narrow for the verb and the version together: `Updated to ⏎ 0.7.3`, `2 updated to ⏎ 0.7.3`,
`0.7.3 failed, ⏎ still 0.7.2` when a player kept the image it had, `Up to date ⏎ (0.7.3)` when the
probe finds nothing to do. The version direction is computed numerically, and a downgrade is
offered, not hidden: the signature is what makes it safe and the person is the one who knows why.

**U27. One transfer at a time, and the station's state comes back.** For every player in the set:
stop the station, transfer (the begin's erase included), restart the station. Each restart is what lets the player that just
rebooted prove itself inside U22's ninety seconds.

**U28. A failure costs one player.** The act continues through the set and reports, at the end,
which players took the image and which did not and why.

**U29. The sending is the emitter's worker's.** The update is a mode of the one task that owns the
radio's sending (`espnow-stream.md` E11) — never the receive callback, never a second sender
beside it.

## 7. What this does not change

The audio's frames, its sequence and its resends (`espnow-stream.md` E1–E8); the tone (E12–E13);
the handover and its key (`network-handover.md`); the status packet's 100 ms cadence. The K7's own
firmware is still flashed over USB (ADR 0079), and the player still has no HTTP client
(ADR 0105). A K7 without the fourth part offers **No image** and is otherwise the K7 it was.

## 8. Tests

| # | Test | Tier |
|---|---|---|
| U-T1 | The header and every frame round-trip; a wrong magic, version, board, signature length, image length or type is refused, as is a truncated or over-long packet | host |
| U-T2 | Stop-and-wait: a lost acknowledgement re-sends the block, three failed sends drop the player, and an abort before the last block leaves the spare slot's written count where it was at the switch | host |
| U-T3 | The version rule: the same version is refused before a byte is sent, a different one is accepted, and the direction and the row's text follow the numbers | host |
| U-T4 | The rollback decision: a marker and a decoded frame mark valid; a marker and ninety silent seconds ask for a rollback; no marker marks valid at once; a probe or a non-decoding packet never does | host |
| U-T5 | The act: the answers make the set, the up-to-date players are skipped, the station stops and restarts once per transferred player, and one player's failure does not stop the next | host |
| U-T6 | The K7's header check: an image whose stored bytes disagree with the header's SHA-256 is **No image**, and one that fits is read at the right offset | host |
| U-B1 | The pair: the row goes `0.6.2 -> 0.7.0`, the player lights Updating, the ten seconds pass, the music returns and the row says `Updated to 0.7.0` | **board only** |
| U-B2 | Power cut mid-transfer: the old image is still what runs, and a second press completes the update | **board only** |
| U-B3 | Power cut after the switch and before the proof: the bootloader restores the old image and the player plays again | **board only** |
| U-B4 | An image signed with another key is refused (result `4`), the player keeps running and the act reports it | **board only** |
| U-B5 | Two players, one press: both update, and the row says which it is on | **board only** |
| U-B6 | A K7 flashed with four parts keeps `nvs`: the network record survives the flash and the player joins without a share window | **board only** |
| U-B7 | A player flashed over USB onto the two-slot table boots and plays, and its first boot is not rolled back for never hearing a K7 | **board only** |

## 9. Open questions

1. **Key rotation.** A lost key ends the path for good (ADR 0116), and a leaked one lets its
   holder sign. A rotation frame signed by the old key would fix the second and not the first; no
   frame is specified until there is a second key to move to.
2. **A second player board.** The header and the answer both carry a board byte so a different
   target can be told apart; nothing else about a second board is specified.
3. **The K7 over the air.** This is the player's update only; the K7's flash is still its cable.
4. **Two K7s and one player.** Both hear the same status and both may stream to it; that predates
   this document and is not the update's to fix.

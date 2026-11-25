# A player's firmware is a signed image the K7 carries, and any K7 may hand it over

A K7Player has one surface, a light, and no controls; its firmware is 891,712 B in a 1 MB factory
partition, and the only way to replace it is the Site's Web Serial flash
([ADR 0079](./0079-the-firmware-is-flashed-in-parts-from-the-browser.md)). The K7 is the device a
person keeps in the room, it already talks to its players over ESP-NOW for audio and for the
share window, and 9 MB of its 16 MB flash is unallocated. The player is also **universal**: it is
bound to a K7 by what it stored, not by what any K7 remembers
([ADR 0098](./0098-the-player-is-a-client-the-k7-keeps-no-record-of.md)), so a household with two
screens may hand the same player a record from either and either may want to update it. A write
that any K7 may make cannot be gated by an address — one MAC is as forgeable as another — so the
only gate that stays honest is the bytes themselves.

## Decision

- **The player's release is signed.** `player/build.sh release` signs `k7player.bin` with an ECDSA
  P-256 key over the image's SHA-256, and the DER signature travels in the 128-byte header of the
  one signed file the release publishes, `k7player.img`. A release without a signature is refused.
- **The K7 carries the image in its own flash.** A `player` partition, 2 MB at `0x710000` and
  64 KB-aligned, holds a 128-byte header — magic, header version, board, image length, version,
  SHA-256 and the signature — followed by the image. The same Web Serial flash writes it as the
  K7's fourth part, from the release's own bytes; the K7 never writes the partition itself.
- **Any K7 holding it may hand it over.** There is no pairing, no ownership and no record of
  which player took which image: a probe finds whoever is in range, the transfer serves one player
  at a time, and the K7 forgets it afterwards (ADR 0098 stands).
- **The player verifies before it switches.** Its running firmware checks the header's board
  against its own, the image's chip and application descriptor, the SHA-256 of the bytes written
  to the spare slot, and the signature against a public key compiled into itself. Only then does
  it set the boot partition.
- **Any signed version that differs is accepted**, and the row that offers it states the
  direction. The same version is a no-op, refused before a byte is sent.
- **The private key lives on the release machine**, never in the repository; the public key is
  committed in the player's source; CI verifies the published signature against it.

## Considered options

- **An address-bound write** — the player remembers the MAC of the K7 that provisioned it and
  refuses anything else. It refuses the second screen in the same house, which is the case the
  player exists for, and a spoofer defeats it anyway.
- **A person-opened window per update, without a signature** — the handover's ceremony applied to
  firmware: universal, and no key to own. But a presence window authorises *when*, not *who*,
  which is [ADR 0099](./0099-the-network-is-handed-over-esp-now-in-a-window.md)'s own boundary, and here it would write code that outlives the window.
- **A key compiled into both firmwares** — one constant, and no defence at all in a public
  repository; ADR 0099 refused the same shape for the record.
- **The App hands the image to the K7 over HTTP** — a second write surface, an App in the loop and
  a flow to build before any player can be updated. The `player` partition leaves this available
  later without a layout change.
- **Embedding the image in the K7's app** — no fourth part and no partition-table change, at the
  cost of its 2.11 MB app slot becoming 3.00 MB, and the image then only changing by rebuilding
  and reflashing the K7.

## Consequences

- **The K7's flash writes four parts.** The browser page gains the fourth, and ADR 0079's "three
  parts at their own offsets, never a merged image" is amended rather than retired: the reason it
  gave — a merged image pads `nvs` — is untouched by a part at `0x710000`.
- **The two releases become one release.** `VERSION` already names both boards and
  `npm run release` cuts both in order; the K7's manifest now points at the player's bytes, so the
  K7's release cannot be cut without them.
- **Losing the private key ends over-the-air updates for every player**, because the public key is
  in firmware that only such an update could change. The only way back is the cable this feature
  exists to avoid, so the key is backed up like the project depends on it, because it does.
- **A player must be flashed over USB once** before any of this reaches it: the table with two OTA
  slots and the verifying firmware arrive the old way. That first flash keeps `nvs` where it is,
  so the network record the K7 handed over survives it.
- **The image is copied into PSRAM at boot, and the act never touches flash.** A flash access —
  a read as much as a write — takes the PSRAM window away and displaces the RGB panel's scan until
  the engine restarts (ADR 0055; screen_display.c says so in its own header). A transfer is 224
  flash reads in ten seconds, and measured on the bench the picture sat a band to one side for the
  whole act: no settle can keep up, and a mapping is still a cache miss. The K7 reads the image
  once at boot, before the panel's first clock, holds it in PSRAM, and hands the blocks out of
  there; the boot's flash work is what the first clock deliberately waits for.
- **The image is public and an unsigned one is worthless.** The player's bootloader verifies no
  signature and secure boot stays off; the running app is the verifier, so a player that was
  written but not verified never boots the new slot.
- **The Site's checker grows a second verification.** `check-firmware.mjs` already proves each
  manifest against its files; it now also proves the player's signature against the committed
  public key.

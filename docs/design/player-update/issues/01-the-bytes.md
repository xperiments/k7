# 01 — The update's bytes

**What to build:** the format both firmwares speak: the 128-byte header the K7's image is wrapped
in, the six ESP-NOW frames that find a player, claim an image and move it in 4 KB blocks, the
portable encoder and parser, and the host tests that hold them. No radio in it, so both sides
compile one implementation and the host can prove it.

**Blocked by:** nothing.

**Status:** built, host tier — `t47_player_update`, 333 checks. The module is in the K7's portable
tree and in the host build; its first firmware caller is PU-04, and the player's build takes it in
PU-03. No board claim here: the signature verify is mbedTLS and belongs to PU-03.

- [x] The header encodes and parses its magic, version, board, signature length, image length,
      version, SHA-256 and signature; one whose fields disagree with each other or with the image
      is not an image.
- [x] The probe, answer, begin, block, acknowledgement and result frames round-trip byte for byte;
      a truncated, over-long, wrong-version or wrong-type packet is refused.
- [x] A block is at most 17 fragments, the fragment count and each payload length agree, and a
      block past that is refused rather than truncated.
- [x] The version rule: the same version is refused before a byte is sent, a different one is
      accepted, and the direction and the row's text follow the numbers.
- [x] One portable module, `components/portable/player_update`, with the host suite beside the
      player's own, and no target conditional in it.

Spec: `docs/specs/player-update.md` U1, U4–U16, U18; [ADR 0116](../../../adr/0116-a-players-firmware-is-a-signed-image-the-k7-carries.md).

## Comments

- **The type bytes are the handover's.** `0x01`–`0x06` are already on this radio (beacon, request,
  record, audio, status, tone), so the update takes `0x07`–`0x0C` and the one dispatcher serves
  three protocols without a length-prefix collision.
- **The header is 128 bytes so the image starts on a line.** The K7 reads the header alone to know
  what version it would hand over; it never parses the image.
- **The signature is DER**, because that is what `openssl dgst -sha256 -sign` and mbedTLS's
  `mbedtls_pk_verify` both speak, so the release tool and the board need no conversion.

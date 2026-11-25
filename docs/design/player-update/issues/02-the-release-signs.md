# 02 — The release signs the image, and the K7's flash carries it

**What to build:** the path that puts the player's signed image into a K7: the signing step in the
release, the header around the image, the fourth part in the K7's manifest, the `player`
partition in its table, the page that writes it, and the checker that refuses a release the
players would refuse.

**Blocked by:** 01 — The update's bytes.

**Status:** built; nothing is claimed on a board. A rehearsal in a temporary tree published both
releases at 0.7.0 from the real build directories: the player's release signed `k7player.img`
(899,312 B, signature 71 B) and `openssl -verify` and the CI checker both accept it; the K7's
release refused without it and carried it at `0x710000`; a tampered image failed the hash and the
signature; a missing key and a missing image were refused. The committed 0.6.2 release is
untouched — the next release is the first with four parts.

- [x] `player/build.sh release` refuses to publish without the release key, signs
      `SHA-256(k7player.bin)`, and writes the header of `player-update.md` U1 around it; the
      manifest and the published directory carry `k7player.img` and its signature.
- [x] The K7's `partitions.csv` gains `player` (data, `0x42`) at `0x710000`, 2 MB; `nvs`,
      `snapshot` and `art` keep their offsets and the K7 still boots.
- [x] `web/firmware.json` carries the fourth part at `0x710000`, the flasher page writes it like
      the other three, and the manual `esptool` command includes it.
- [x] `check-firmware.mjs` verifies the committed signature against the public key committed in
      the player's source, and fails on a mismatch, a missing signature or a `VERSION` that
      disagrees.
- [x] The K7's release refuses an image the players would refuse: a missing key, a missing
      player image, a header that disagrees with the image, a signature made with another key, or
      a partition table without the `player` partition. (PU-04 adds the K7's runtime read of the
      same header.)

Spec: `docs/specs/player-update.md` U1–U3; `docs/specs/k7-site.md` F3, F5, R2–R6;
[ADR 0116](../../../adr/0116-a-players-firmware-is-a-signed-image-the-k7-carries.md).

## Comments

- **The release key is not in the repository.** It lives on the machine that cuts releases; the
  public key is committed in the player's source. Losing it ends over-the-air updates for every
  player, so the key is backed up like the project depends on it, because it does.
- **The K7's manifest points at the player's release bytes** rather than duplicating them: one
  version, one copy in git.

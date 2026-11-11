# 03 — The flasher: three parts, a fake transport, and no merged image

**What to build:** the flash in [`docs/specs/k7-site.md`](../../../specs/k7-site.md) §3.

- `web/src/site/flash/` holds the engine, not the page: a `SerialTransport` seam, the state
  machine (`idle → port → identified → confirmed → writing → verifying → done`, plus a failure
  that carries a cause), and the rules. `esptool-js` is the protocol behind the seam and is
  bundled, not fetched — the origin's CSP allows `script-src 'self'` and nothing else.
- The engine writes `web/firmware.json`'s three parts at their offsets: `bootloader.bin` at
  `0x0`, `partition-table.bin` at `0x8000`, `sd_stream_server.bin` at `0x10000`. It never pads a
  gap, never writes a merged image, and never reaches `nvs` at `0x9000` or the Snapshot at
  `0x410000`. Each part's sha256 is checked before it is written.
- Before the first byte: the chip must be `ESP32-S3`, the flash 16 MB, and a confirmation must
  name the port, the chip, the size and the version from `firmware.json`. Erase is a separate,
  explicit choice that names what it wipes (k7-site.md F4–F8).
- Each failure names a cause a person can act on: the BOOT-button sequence, a port held by
  another program, a Linux `dialout` permission, a charge-only cable, a wrong chip, a wrong
  flash size, a hash that did not match.
- The ReUI section around it: prerequisites before the control, progress that is a sentence and
  not a fake percentage, the success state's three steps (join `K7.local`, open
  `http://192.168.4.1/setup`, install K7 from this Site), and a disclosed manual path whose
  `esptool` command and links are generated from `firmware.json` (F10–F11).

**Blocked by:** 01 (the Site's document) and 02 (`firmware.json` and the committed parts).

**Status:** pending

- [ ] A fake transport drives every state; no test needs a serial port.
- [ ] A non-S3 chip is refused before any write; an 8 MB flash is refused before any write.
- [ ] Nothing is written before the confirmation; the confirmation shows the version from the
      manifest, not a constant.
- [ ] The happy path writes exactly `0x0`, `0x8000`, `0x10000`, in that order, with the
      manifest's bytes.
- [ ] Erase runs only when it was chosen, and the copy names the WiFi record, the setup password
      and the Snapshot as lost.
- [ ] A part whose sha256 does not match is refused before it is written.
- [ ] Without `'serial' in navigator`, the section states what is needed and offers the manual
      path; with it, the manual path is a disclosure, not the primary control.
- [ ] The manual command carries the offsets from `firmware.json` and cannot drift from them.

**Files:** `web/src/site/flash/` (new: the engine, the transport seam, the causes),
`web/src/site/flash/*.test.ts` (new), `web/src/site/Flash.tsx` (new), `web/package.json` (the
`esptool-js` dependency), `web/firmware.json` (read only).

**Tests:** k7-site.md T4, T5, T10.

**Source:** ADR 0079.

## Comments

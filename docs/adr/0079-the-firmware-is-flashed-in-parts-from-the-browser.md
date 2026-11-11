# The firmware is flashed in the browser, in parts

The only way to flash K7 was `idf.py -p PORT flash`: an ESP-IDF environment, a Python virtualenv
and a serial device name. The board that ships is a Sunton 8048S050C — an ESP32-S3 with 16 MB of
flash — and the people who need K7 on it are not expected to own a toolchain. The Site exists
([ADR 0078](./0078-the-site-owns-the-root-and-the-app-moves-to-app.md)); it can also be the
flasher. What that must not cost is the state a working device already holds: the WiFi record the
setup network stored, and the Snapshot with the playlists in it.

## Decision

- **The Site flashes over Web Serial, with `esptool-js` as the engine and a ReUI page we own.**
  The page, the port picker, the states, the failure copy and the design are ours; the protocol
  is Espressif's. Chrome and Edge on a computer are the only browsers that have it.
- **Three parts at their own offsets, never a merged image:**
  `bootloader.bin` at `0x0`, `partition-table.bin` at `0x8000`, `sd_stream_server.bin` at
  `0x10000`. A merged image from `0x0` pads the `nvs` partition at `0x9000` with `0xFF`, so every
  update would erase the network the device had just been given.
- **Check, then confirm, then write.** The chip must be an `ESP32-S3` and the flash 16 MB, or the
  page refuses; the confirmation names the port, the chip, the size and the version. Erase is a
  separate, explicit act, never a consequence of a mismatch.
- **One variant: `sunton`.** The capture, dump, verify, headless and backlight builds are a
  developer's business, not a choice on a product page.
- **Every release is a version and a file.** `VERSION` (semver) is baked in as `PROJECT_VER`; the
  three parts and a `firmware.json` with their offsets, sizes and sha256 are committed together;
  CI proves the manifest against the bytes. A clone with no ESP-IDF deploys a working flasher.
- **The manual path is disclosed, not primary.** Without Web Serial the page states what is
  needed and offers the same three parts with an `esptool` command generated from the manifest.

## Considered options

- **`esp-web-tools`**, Espressif's install-button component. The fastest route and a tested flow;
  its dialog is its own UI, and a page that must speak K7's design system would be hosting a
  foreign one.
- **One merged factory image.** One file and one offset, and a factory reset on every flash,
  because the gaps it pads include `nvs`.
- **Espressif's hosted Launchpad.** No code and no design of ours, on somebody else's origin and
  somebody else's roadmap.
- **Download only.** It is the toolchain path with a page in front of it; the people this exists
  for do not have `esptool`.
- **Provisioning over serial (Improv).** It would teach the firmware a second channel for a
  password the setup network already takes, and duplicate a capability specified last week
  ([ADR 0076](./0076-the-network-is-chosen-on-the-devices-setup-network.md)).
- **OTA.** There is no OTA slot (`partitions.csv` has one factory app), and a device that has just
  been flashed has no network to fetch from anyway.

## Consequences

- **The flash is desktop Chromium only.** Safari, Firefox and every phone are told so; the
  download path is the only route there, and it needs a toolchain.
- **Firmware binaries live in git** — about 2.1 MB per release — because the deploy must carry
  them and CI cannot build firmware.
- **Publishing the binary publishes an LGPL-2.0 work.** `shine` is vendored and separately built;
  the public repository plus `THIRD_PARTY_NOTICES` and its relink instructions are the obligation,
  and the Site links them.
- **The partition table is rewritten on every flash.** A future layout that moves `nvs` makes the
  update incompatible with the stored record, and the answer is the explicit erase, not a silent
  wipe.

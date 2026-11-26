# 07 — The Site installs the K7Player as well as the K7

**What to build:** the install page flashed the K7 and nothing else: one variant, one chip check
(`ESP32-S3` or refuse), one set of offsets, one release action. The K7Player had no browser
install at all — its firmware existed only for someone with the IDF and `player/build.sh`.

**Blocked by:** nothing.

**Status:** built. The artifact is checked on the player board; the browser pass is not run.

- [x] Two boards, chosen before a port is asked for. K7 (ESP32-S3, 16 MB) and K7Player (classic
      ESP32, 4 MB), each with its own version and one sentence about what it is. Choosing another
      board starts the section over: a port is never carried across.
- [x] The chip and the flash size are the chosen board's, and a mismatch names both: *the board
      reports ESP32-S3. K7Player runs on a classic ESP32.* Nothing is written.
- [x] The erase's cost and the steps afterwards are per board: the K7's setup network and App
      install; for the player, the K7's **Menu → Wi-Fi → Share** window, which hands it the
      network over ESP-NOW.
- [x] Both manifests are the only truth: `web/firmware.json` and `web/firmware-player.json`, with
      the parts under `/firmware/<version>/` and `/firmware/player/<version>/`. The page holds no
      offset, hash, chip or flash size.
- [x] A release action for the player: `./player/build.sh release`, publishing through
      `tools/release_publish.py` — the same script the K7's release now uses — from the build's
      own `flasher_args.json`, refusing a part table that is not the board's.
- [x] CI proves both manifests against their files (`web/scripts/check-firmware.mjs`,
      `npm run check:firmware`).
- [ ] **The browser pass:** an ESP32 classic flashed from the deployed page in Chrome. This is
      the one box a host cannot tick.

**Measured / checked:**

- The player's release, published from a fresh build: `bootloader.bin` 26,176 B at `0x1000`,
  `partition-table.bin` 3,072 B at `0x8000`, `k7player.bin` 891,712 B at `0x10000`
  (sha256 `4e99d6d0…`), flash `dio / 4MB / 40m`.
- Flashed to the real player board with the manifest's offsets and parameters — the page's own
  manual command — `Hash of data verified`, then the board booted and played:
  `receiving 192.168.0.13: 44100 Hz, 128 kbps`, `playout: output 44100 Hz … 0 underruns`,
  `light: playing`, no error line.
- Host: `npm run typecheck`, 103 test files (the site's suite grew to 29 cases: two boards'
  manifests, URLs and commands, and the machine refusing a K7 module for the player and a 4 MB
  module's flash size for the K7), `npm run build`, `check-firmware`, `check-shell`.

Spec: [`k7-site.md`](../../../../specs/k7-site.md) §3 F2–F11 and §4 R2–R6, both amended for two
boards. The page is served from the deployed origin, so the change is visible there after a
deploy, or under `npm run dev`.

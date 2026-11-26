# 03 — `player/`, and a host harness that runs its core

**What to build:** The second IDF project skeleton and the means to test it without a board: a
`player/` project whose `main` links the portable components it shares with the K7, a host build
that compiles the same sources with a shim, and a `make check` target that runs both firmwares'
host tiers in one command.

It is second because every later player ticket needs somewhere to put code that the host can
execute, and because the repo's rule that `components/portable` has no target conditionals is the
thing this project must not break.

**Blocked by:** None — can start immediately.

**Status:** done (host tier, and the firmware builds and boots on the board).

- [x] `player/` builds with one command for `IDF_TARGET=esp32`, with its own `sdkconfig` family, and links `../components/portable` read-only.
- [x] A host build compiles the player's core against a shim, with no forked copy of any source.
- [x] A layering check fails the build if a target conditional appears in the shared or player code it compiles.
- [x] One documented command runs the K7's host tier and the player's, and CI runs both.

Spec: `docs/specs/k7player.md` R9, `docs/adr/0097-the-player-is-an-esp32-classic-in-its-own-project.md`.

## Comments

- **The shape is the K7's, scaled down.** `player/CMakeLists.txt` is a project of its own; it
  knows exactly two component directories — its own `player/components`, and the one shared
  component it links (`../components/portable/handover`, read-only). Pointing the project at all
  of `components/portable` would pull the K7's cassette, menu and panel code onto the player's
  chip, so the list is explicit and `P5` fails if it grows a K7 name.
- **The seam has two adapters from day one**, which is the repo's rule for a seam: the ESP32
  classic in `player/components/board_player/` (NVS, and later the radio and the outputs) and the
  host in `player/host/board_player_host.c`. `player_board.h` is the only thing both include.
- **The core's first module is the light's rule** (`player_status`): the four states of R31, with
  Fault first, Playing requiring both the stream and an audio output, and a missing speaker
  answered as Ready rather than a fault. It is pure — no task, no clock, no I/O — which is what
  makes the harness real rather than a stub.
- **The shim is the K7's, not a second one.** `player/host/Makefile` compiles
  `../host/shim/shim.c` against `../host/include` and reuses `../host/tests/test_util.h`; a
  forked shim would be a second thing that knows it is a Mac.
- **One command, and CI already runs it.** `make -C host check` gained a `player` target that
  recurses into `player/host check`, placed before the Chrome-dependent `label` step. CI runs
  `make -C host check` unchanged, so both firmware host tiers are now on every pull request.
- **Evidence:** `player/host/build/bin/t01_status` — **15 checks, 0 failures**; the player's
  layering — 5 checks; `make -C host test layering player integration` green (the K7's 43 test
  binaries, the K7's layering, the player's test, the player's layering, the HTTP acceptance).
- **Firmware, on the board:** `./player/build.sh build` builds `player/build/k7player.bin` for
  `esp32` in one command; flashed to the ESP32-D0WD-V3 on `/dev/cu.usbserial-8344420` it boots
  with `board: esp32 classic: 2 core(s), revision 3.1, 4 MB flash` and
  `player: k7player on esp32: searching`.
- **Deliberately not here:** Bluetooth is off in `sdkconfig.defaults` until the speaker output
  lands (PL-08); there is no network, no stream and no output yet (PL-04 to PL-08), and no
  `partitions.csv` beyond the default, because the skeleton does not fill one.
- **`make -C host check`'s `label` step still fails in this environment** (headless Chrome's GPU
  process dies before any assertion); everything before it passes, the player's checks included.

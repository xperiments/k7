# 18 — Compose the services once, above both entry points

**What to build:** The composition of the device's services and its ordering constraints
exist twice. `app_main` validates the station, brings the network up, starts the file
surface, then the broadcast, then HTTP, then the UI; `host/icecast_host.c` starts the
broadcast, then files, then HTTP, and never validates. The device's order is load-bearing
(the encoder's internal-RAM allocation must follow Wi-Fi's; the station check must precede
both) and is recorded only in comments. Because the host tier compiles only `components/`,
neither composition is testable.

Add an `app_services` component whose interface is one call — resolve the station, start
the board, start the services in the documented order, return a report of what came up and
what did not — and a board seam with two real adapters:

- `components/app_services/include/app_board.h` — what the services need from the board and
  nothing else: `app_board_net_start()`, `app_board_card_mount()`,
  `app_board_wait_online()`, `app_board_log_hardware()`.
- `main/board_esp32.c` — Wi-Fi (credentials, event logging, retry), SPI microSD mount and
  the hardware census, moved out of `main.c` unchanged.
- `host/board_host.c` — no network, the fixture directory always mounted, no-op wait.

`app_services_start(cfg, report)` calls the board first (so Wi-Fi's DMA buffers win the
internal-RAM race), then files, broadcast and HTTP. `main.c` becomes NVS, board log, the
services call, `ui_start()`, the card-retry task and the console loop. The host main
becomes arguments, the same services call, and the console loop. The layering check learns
that `app_services` may depend on `mini_files` (it is the application layer, moved).

**Blocked by:** 12 — the resolved station is what the services pass to the broadcast.
16 — the console loop's status line changes shape.

**Status:** done (host tier and both firmware builds; board tier not run - no board attached)

- [ ] One function starts the file surface, the broadcast and HTTP, in one order.
- [ ] The station is resolved before any board call allocates anything.
- [ ] `app_services_start` returns a report naming which services came up and why the rest
      did not; a missing card is a report, not a failure.
- [ ] The host binary and the firmware run the same composition.
- [ ] `main.c` contains no Wi-Fi, SPI or FAT code; `board_esp32.c` contains no service
      calls.
- [ ] `make -C host layering` allows and names the app_services dependency.
- [ ] Host tests, integration, and both firmware builds pass.

**Files:** `components/app_services/include/app_services.h` (new),
`components/app_services/app_services.c` (new),
`components/app_services/CMakeLists.txt` (new),
`components/app_services/include/app_board.h` (new),
`main/board_esp32.c` (new), `main/main.c`, `main/CMakeLists.txt`,
`host/board_host.c` (new), `host/icecast_host.c`, `host/Makefile`,
`host/tests/layering.sh`, `README.md`, `CONTEXT.md`.

**Tests:** integration (the host binary now exercises the device's composition); layering
checks the dependency direction; no unit test starts tasks.

**Source:** architecture review, candidate 5.

## Comments

- Added `components/app_services/` (`app_services_start()`, `app_services_check()`, the report)
  and its board seam `app_board.h`.
- Added `main/board_esp32.c` (Wi-Fi credentials and events, the retry-safe SPI mount, the
  hardware census) and `host/board_host.c` (no network, the fixture directory always mounted).
- `main/main.c` is now NVS, the board census, one `app_services_start()`, `ui_start()`, the
  card-retry task, the skip button and the console line. `host/icecast_host.c` calls the same
  `app_services_start()`, so the host tier runs the device's composition.
- `layering.sh`'s S4 now names `app_services` as the application layer.
- Verified: `make -C host check` (10 unit tests, layering, 92 integration checks, awkward
  names), `idf.py -B build61 build`, and the Sunton build.

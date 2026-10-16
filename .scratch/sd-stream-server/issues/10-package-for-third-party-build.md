# 10 — Somebody else can build, flash and deploy this

**What to build:** A person who has never seen this project can take the repository, follow the
readme, build it, flash a board, put audio on a card, and get a working stream and library. They
also learn, before they deploy it anywhere, that the device serves an unauthenticated library and
that its network credentials are compiled into the binary.

The packaging obligation is also a licensing one: the vendored encoder's licence text and notice
must be present, or the arrangement that permits distributing this project under its own terms is
not satisfied.

**Blocked by:** 05 — The live stream is continuous MP3; 08 — The card's library is browsable and downloadable.

**Status:** done for everything that can be checked without hardware.

- [ ] The readme covers building, flashing, the card layout the device expects, the pin assignment for the card bus, and the button that skips to the next track.
- [ ] The readme states the external-memory mode must be set explicitly to match the fitted chip, that it is not detected automatically, and that the wrong mode corrupts memory at boot.
- [ ] The readme states, where a deployer will actually read it rather than in a footnote, that the device serves a live stream and an unauthenticated file index, that its network credentials are compiled into the binary, and that it must not be placed on an untrusted network or exposed to the internet.
- [ ] The vendored encoder's licence text and a third-party notice naming it are present in the repository.
- [ ] Component metadata carries version, description, licence, the supported target, and a minimum framework version.
- [ ] A clean checkout builds and flashes following only the readme, on a machine that has never built this project.
- [ ] The project's own components' licensing is unaffected by the vendored encoder's.

Spec: `docs/specs/sd-stream-server.md` §8 (L1, L2, L4), §7 A1, §2.2, §2.3, §10.3.

## Comments

- [x] `README.md` covers building and flashing, the card layout (`/sdcard/music`, flat, FAT32, accepted extensions), the card bus and its pin assignment (as it turned out, SPI: CS 10 / CLK 12 / MOSI 11 / MISO 13), the GPIO0 skip button, and the cost of using the USB-JTAG pins.
- [x] The README states that the PSRAM mode must be set explicitly to match the fitted chip, that it is not detected automatically, and that the wrong mode corrupts memory at boot — in the "PSRAM is a hard prerequisite" section, with the Kconfig lines quoted.
- [x] The deployment warning is the first thing after the introduction, as a block quote, not a footnote: live stream plus unauthenticated index, credentials compiled in, not for an untrusted network, never port-forwarded. The access model is also restated in `main/main.c`'s header comment.
- [x] `LICENSES/LGPL-2.0.txt` (byte-identical to `components/shine/COPYING`) and `THIRD_PARTY_NOTICES` are in the repository; `make -C host layering` asserts both.
- [x] Every component has `idf_component.yml` with version, description, licence, `targets: [esp32s3]` and `dependencies.idf: ">=5.4"`.
- [x] The project's own licensing is unaffected: this project's code is MIT (`LICENSE`), the vendored encoder stays LGPL-2.0-only in its own component, and the README's licensing section says so.
- [ ] A clean checkout builds and flashes following only the README. **The host build does (`make -C host check`, from a clean `host/build`); the firmware build has not been run** because there is no ESP-IDF toolchain in this environment, and no board to flash. `main/CMakeLists.txt` names IDF 5.4 component names (`esp_driver_gpio`, `esp_driver_sdmmc`, `esp_flash`, `esp_psram`); if a newer IDF has renamed them again, that one line is the fix, and the README says so.

Note: the project licence was chosen as MIT so the component metadata could carry a licence at all. If you want something else, change `LICENSE` and the four `idf_component.yml` files.

**Board run of the build-and-flash half: done.** `rm -rf build sdkconfig && idf.py
build && idf.py -p /dev/cu.usbserial-834430 flash` works from a clean checkout with
ESP-IDF v5.4.4 installed, following only the README. The 972 KB image fits a 1.5 MB
partition with 37 % free.

Corrections the board forced, both now in the repository: `main/CMakeLists.txt` must
name `spi_flash`, not `esp_flash` (the latter is not a component in IDF 5.4); and
`sdkconfig.defaults` must set the flash size and partition table explicitly, because the
default assumed a 2 MB chip and left the application partition 93 % full.

Outstanding: the "flash a board that has never built this project" part is done here on
this board; a second machine has not been tried.

**Credentials moved out of version control.** `main/wifi_credentials.h` is gitignored and
holds `WIFI_SSID`/`WIFI_PASS`; `main/wifi_credentials.h.example` is committed. A clean
checkout still builds, with `#warning "main/wifi_credentials.h is missing: the device will
not join a network"`. That keeps the compile-time-credential design the specification asks
for while not putting a real network password in the repository. The README says so in the
build section.

**Moved to ESP-IDF 6.1.** The firmware builds on 6.1 without a source change, and the whole
board tier was re-run there: `80 passed, 0 failed`, three runs in a row with no reboot. The
component names in `main/CMakeLists.txt` were already the 5.3+ names
(`esp_driver_gpio`, `esp_driver_sdmmc`, `esp_driver_sdspi`, `esp_driver_spi`, `spi_flash`,
`esp_psram`), which is why nothing needed changing; the component metadata keeps
`idf: ">=5.4"` because both 5.4.4 and 6.1 build and were tested. The application grew from
972 KB to 1.04 MB with IDF 6, leaving 32% of the 1.5 MB partition free.

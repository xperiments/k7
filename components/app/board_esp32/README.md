# board_esp32

The firmware's board adapter: Wi-Fi, the SPI microSD mount and the hardware census.
It implements the `app_board.h` interface; the host build's adapter is
`host/board_host.c`, and the two are the real seam that keeps the services free of
GPIO, `esp_wifi` and FAT.

The Wi-Fi credentials are compiled in but live in a gitignored header here: copy
`wifi_credentials.h.example` to `wifi_credentials.h` and fill it in. The
placeholders keep a clean checkout building.

# board_esp32

The firmware's board adapter: Wi-Fi, the SPI microSD mount and the hardware census.
It implements the `app_board.h` interface; the host build's adapter is
`host/board_host.c`, and the two are the real seam that keeps the services free of
GPIO, `esp_wifi` and FAT.

The Wi-Fi credentials are not compiled in and not here: the network is chosen on the
device's own setup network and stored in NVS, and this adapter is what raises that
network, scans, and joins. See
[`docs/specs/wifi-provisioning.md`](../../../docs/specs/wifi-provisioning.md) and
[ADR 0076](../../../docs/adr/0076-the-network-is-chosen-on-the-devices-setup-network.md).

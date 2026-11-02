# appliance

The device's entry policy, above the composition and the screen:

- NVS, the hardware census, the three screen calls and the one call to `app_services_start()`.
- The station format this product ships (44.1 kHz / 256 kbps) and its measured
  justification.
- The card retry after a missing card and the console line. The transport is the screen's
  DJ block, not a physical button.

The screen reaches into the services on both sides. `screen_display_boot()` takes its
hardware and the LVGL port before `app_services_start()` - the port's 16 KB internal stack
has to win the RAM Wi-Fi's DMA buffers want (ADR 0004) - but starts neither the clock nor the
scan, so the boot's flash work (the Snapshot, the Shell's table, Wi-Fi's NVS) happens on a
dark panel. `screen_display_mark()` then starts the first clock, paints the boot mark on a
quiet bus, lights the backlight over it and holds it for `APP_UI_MARK_HOLD_MS`;
`screen_display_start()` replaces it with the cassette. The mark is on the panel for exactly
as long as there is nothing to show, and never while the boot is touching flash.

The interface is `include/appliance.h` — `appliance_run()` and nothing else. It is
app-only: GPIO, NVS and the console loop cannot run on the host, so the host tier
exercises the same composition through `app_services` instead.

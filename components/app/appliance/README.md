# appliance

The device's entry policy, above the composition and the screen:

- NVS, the hardware census, the two screen calls and the one call to `app_services_start()`.
- The station format this product ships (44.1 kHz / 256 kbps) and its measured
  justification.
- The card retry after a missing card and the console line. The transport is the screen's
  DJ block, not a physical button.

The screen is the one thing that runs before the services: `screen_display_boot()` paints
the boot mark before the backlight is lit (the panel's framebuffers are uninitialised PSRAM),
and `screen_display_start()` replaces it after `app_services_start()` has mounted and scanned
the card. The mark is on the panel for exactly as long as there is nothing to show.

The interface is `include/appliance.h` — `appliance_run()` and nothing else. It is
app-only: GPIO, NVS and the console loop cannot run on the host, so the host tier
exercises the same composition through `app_services` instead.

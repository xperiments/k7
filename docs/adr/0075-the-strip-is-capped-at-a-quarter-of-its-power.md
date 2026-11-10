# The strip is capped at a quarter of its power

ADR 0074 put eight WS2812B pixels on GPIO 17 and wrote every frame every tick, and the first
build that lit them at full white rebooted the device. The chain is arithmetic, not a bug:
a WS2812B channel at full is 20 mA, so a pixel at white is 60 mA and eight of them are 480 mA -
on a board that already carries a 800x480 panel, Wi-Fi and the encoder, from the same USB port.

The failure is a brownout, and the strip cannot argue with a supply. What the firmware can do is
stop asking for more current than the rail can give. Nothing about the light's state, effects or
clock was wrong; the frame was simply allowed to draw half an amp.

## Decision

- **The frame has a ceiling, and it is the last stage.** `rgb_limit_power()` keeps
  `CONFIG_APP_STRIP_MAX_PCT` of every channel, after the effect and after the brightness, so no
  effect, no knob position and no frame can step around it.
- **The ceiling is 25% by default** (`components/app/strip/Kconfig`,
  `APP_STRIP_MAX_PCT`, range 0..100): about **120 mA** for eight pixels at white. That is a
  quarter of the strip's full draw and well inside what a USB port gives the board.
- **The arithmetic is portable and pinned**, in `components/portable/rgb` next to `rgb_frame()`
  and `t25_rgb`, because it is part of what the light shows, not part of the RMT transaction.
- **The ceiling is a current limit, not a display setting.** A single colour at 25% is about
  40 mA; the brightness knob still dims below the ceiling; the effect list is unchanged.

## Considered options

- **A per-frame current budget** - model the frame, scale it only when it exceeds the budget,
  so a dim colour passes untouched. Rejected: above the point where the budget binds, the
  brightness knob does nothing at all, because the budget pulls every brighter frame back to the
  same current. A knob with a dead top is worse than a light that is quieter than it could be.
- **Lower the default brightness instead of capping.** Rejected: it changes the panel's knob
  defaults for the sake of the driver, and full white is still one knob turn away - the exact
  turn the owner made.
- **Leave the firmware alone and fix the supply** (bulk capacitor, separate 5 V). Rejected as
  the *only* answer, not as an answer: it is the better cure, and it is still the owner's to
  make. Until it is made, a firmware that assumes a supply it cannot see is a firmware that
  reboots.
- **Drop the strip from the firmware.** Rejected: it works, and the case for it has not
  changed - only the current it may draw has.
- **PWM the whole strip to lower its average** - switch the data line off and on. Rejected: it
  is the same cap with a flicker and a timing problem, and `led_strip` already gives the exact
  duty control this needs.

## Consequences

- **Full white is now about 120 mA** and a solid colour about 40 mA; the strip is visibly
  dimmer than it can be. That is the point, and `APP_STRIP_MAX_PCT` is one menuconfig away from
  being raised when the strip has its own supply.
- **The brightness knob's top means the ceiling.** `BRI` at 100 is 25% of the part's full
  output, not 100%; the knob still works over its whole range because the cap is a scale, not a
  clamp on the top frames only.
- **The host tier pins the cap**, so a future effect cannot quietly reintroduce the fault:
  `t25_rgb` checks a quarter of white, the ratio between channels, zero, and the clamps.
- **The board's power problem is mitigated, not solved.** A strip on the board's USB rail still
  pulls its current through the same port as the panel; the cap only makes that current small.
  The bulk capacitor and a separate supply remain the right wiring.
- **The value is the component's Kconfig default**, not an entry in `sdkconfig.defaults*`: a
  reconfigure picks it up, the generated `sdkconfig.sunton` records it, and the build fails
  loudly rather than silently if the symbol is ever dropped.

**Status:** accepted; `t25_rgb` and the host suite green, firmware builds and flashes. The
brownout itself is not reproducible from the host, so "it no longer reboots" is the owner's
check.

**Amends** [ADR 0074](./0074-the-strip-is-eight-pixels-on-gpio-17.md), which set the pixels,
the pin and the clock but no current.

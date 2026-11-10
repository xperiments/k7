# strip

The addressable light: **eight WS2812B pixels on GPIO 17**, refreshed at 30 Hz from the **RGB
group**'s state.

This is the board half of a light whose behaviour lives elsewhere. `components/portable/rgb`
owns the three channels, the brightness, the six effects, and `rgb_frame()` - a pure function
that fills a pixel buffer from `(state, now_ms, level, n)`, pinned by `host/tests/t25_rgb`. What
is here cannot run on the host: an RMT channel and a task.

- `strip_start()` takes an RMT TX channel through `espressif/led_strip`, installs itself into
  `cassette_rgb_set_publish()`, and starts the 33 ms task.
- Each tick copies the last published state, takes `level` as the louder of the broadcast's two
  channels, calls `rgb_frame()`, and writes the eight pixels.
- The frame is written every tick, changed or not: 192 bits at 800 kHz is about 240 us plus the
  latch, under 2% of a tick, and a strip that lost a latch recovers in one tick.

It runs on its own clock rather than LVGL's, because the screen task's cadence is adaptive (it
rests at least three times its own frame cost, over 100 ms for a full-screen invalidation) and
would drag the chase with it.

## Wiring

- **Data into a 5 V WS2812B is the one thing to check.** The part's datasheet threshold is
  0.7 x VDD - 3.5 V at 5 V - and the ESP32-S3 drives 3.3 V. Supply the strip at 4.5 V (a series
  diode) or shift the data (a 74AHCT125).
- Eight pixels at full white draw about 0.5 A from the 5 V rail; the strip's supply is not the
  board's 3.3 V rail.
- Common ground to the strip's own end of the run.

The behaviour is [`docs/specs/led-strip.md`](../../../docs/specs/led-strip.md) and the decision
is [ADR 0074](../../../docs/adr/0074-the-strip-is-eight-pixels-on-gpio-17.md).

## Acceptance

There is no host test for this file, by design: what is worth pinning is rgb's arithmetic and
that is already pinned. The driver is proven on the board - OFF dark, SOLID the colour the knobs
say, BRI dimming without a hue shift, BREATHE, RAINBOW turning, CHASE lapping, and LEVEL
following the VU meter.

# The strip is eight WS2812B pixels on GPIO 17, refreshed at 30 Hz

The RGB group has had a state and no light since ADR 0026. `components/portable/rgb` owns the
three channels, the brightness and the six effects, `rgb_frame()` turns them into pixels, and
`t25_rgb` pins every effect on the host - but the panel's publish hook had nothing to publish
to. `cassette_rgb.h:24-26` says so outright: "There is no strip in the firmware yet, so the
seam is `cassette_rgb_set_publish()`", and the screen's README said the same - no pin, chipset
or count was decided.

The owner has now decided the pin: **17**. That is the fact this record takes; the chipset, the
count and the shape of the driver are the consequences it draws from it, because a pin alone
does not make a driver.

GPIO 17 is a good pin on this board and a contested one on another. On the Sunton 8048S050C it
is one of exactly four the panel, the touch, the card and the console leave free - 0, 17, 43,
44 - and the other three are the BOOT strap and the console's UART pair
([`s15-i2s-dac-output.md:629-637`](../research/server/s15-i2s-dac-output.md#L629-L637)). On
the bare N16R8 devkit the same research recommends 17 for I²S BCK (§6.2). A strip and that DAC
cannot share a board, and this record says which way that is decided: the strip has 17.

## Decision

- **Eight WS2812B pixels in one string**, data on GPIO 17, GRB order, 800 kHz, supplied at
  5 V. Eight is what the fitted strip has; a second strip changes one constant.
- **The driver is RMT**, through Espressif's `espressif/led_strip` managed component, in a new
  board-only component `components/app/strip/` - `strip`, because `led_strip` is the
  dependency's own component name and two of them would be one too many. It is not portable and
  never will be: the arithmetic that is worth pinning already lives in
  `components/portable/rgb`.
- **It installs through the screen's public header**, `screen_display_rgb_publish()`, which
  forwards to the panel's private `cassette_rgb_set_publish()`. The alternative was to move
  `cassette_rgb.h` into `screen_display/include/`, which would publish the whole panel - its
  cells, its banks, its pulse - to serve one hook.
- **The strip has its own 30 Hz clock**, a 33 ms task, and does not ride LVGL. The screen
  task's cadence is adaptive - it rests at least three times its own frame cost, which on this
  part has been over 100 ms for a full-screen invalidation (`screen_display.c:443`) - so a
  strip that rode it would run the chase at whatever pace the screen left behind.
- **The frame comes from `rgb_frame(state, now_ms, level, px, 8)`.** The state is the last one
  `screen_display_rgb_publish()` handed over; `now_ms` is `esp_timer_get_time() / 1000`;
  `level` is **`max(level_l, level_r)`** from `mini_icecast_levels()`, sampled on the tick.
- **Brightness stays the last stage**, applied to what the effect produced. That order is
  already `rgb.h`'s and this record does not move it.
- **The frame is written every tick**, whether or not it changed. 8 px x 24 bits is 192 bits
  at 800 kHz - about 240 microseconds plus the part's reset - so the tick costs under 2% of a
  core, and a strip that missed a latch recovers in one tick instead of waiting for a knob to
  move.

## Considered options

- **FastLED.** Rejected: a C++ library and its own build story for eight pixels, against a
  managed component that is the vendor's own RMT encoder.
- **A hand-rolled RMT encoder.** Rejected: at 192 bits the encoder is the whole of what
  `led_strip` does, and writing it ourselves buys nothing but code that needs its own tests.
- **Bit-banging the data line.** Rejected: it blocks for the frame and drifts with interrupts;
  RMT exists to make that somebody else's problem.
- **The LVGL cadence**, e.g. a timer inside the screen component. Rejected above: the screen's
  cadence is adaptive and is the wrong clock for an animation that has its own periods.
- **Skip the write when the bytes are unchanged.** Rejected: it saves 1.5% of a core and adds
  a comparison and a stale-latch path to a driver that is otherwise one line of state to one
  RMT transaction.
- **Average the two channels for the level.** Rejected: a mono strip has no stereo image to
  preserve, and averaging halves a hard-panned track. `max` is what the eye reads.
- **Dim the strip with the backlight's ladder.** Rejected as out of scope rather than wrong:
  the screen dims at 2, 5 and 10 minutes, and whether the light should dim with it is a
  question the owner has not answered. It is an open item in the spec, not a decision taken
  here.

## Consequences

- **The manual gains the pin.** `README.md`'s hardware section states eight WS2812B pixels on
  GPIO 17, the 3.3 V data line against the part's 3.5 V threshold at a 5 V supply, and the
  ~0.5 A eight pixels take at full white. The strip is either supplied at 4.5 V or fed through
  a level shifter; that is a wiring fact, not a firmware one.
- **A future devkit build has a conflict**: 17 is the research's recommended BCK. If both land
  on one board, the strip moves, because the DAC has fewer clean pins than the strip.
- **The screen's README stops saying no pin is decided**, and points at
  [`led-strip.md`](../specs/led-strip.md) instead.
- **The board tier is the only proof of the driver.** `t25_rgb` already pins the arithmetic,
  and nothing board-only gains a host test from this: the seam is a header. OFF, SOLID, BRI,
  BREATHE, RAINBOW, CHASE and LEVEL are proven by flashing and looking.
- **`CHASE_WINDOW` is probably one too large for eight pixels.** Three of eight is 37% of the
  strip lit; whether it reads as a head or a block is a tuning item until the strip is seen.
- **The strip is not part of the screen's power state yet.** Nothing in this change ties it to
  the backlight's ladder; whether it should dim with the screen is the spec's open item, and
  dimming it on a timer would contradict the knob that set it.

**Status:** accepted and implemented in `components/app/strip/`; the board proof - the effects
on the strip - is FW-03's review. The spec is [`docs/specs/led-strip.md`](../specs/led-strip.md).

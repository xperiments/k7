# The strip

**Status:** accepted · **Updated:** 2026-11-10

The addressable light of the device: eight WS2812B pixels that show the **RGB group**'s state.
The state, the effects and the frame arithmetic are portable and pinned on the host; this
document is the board-only half - the count, the pin, the supply, the cadence, and where the
level comes from. The decision behind it is
[ADR 0074](../adr/0074-the-strip-is-eight-pixels-on-gpio-17.md).

## What it is

- Eight WS2812B pixels in one string, data on **GPIO 17**, GRB order, 800 kHz.
- The state is `rgb_state_t` ([`rgb.h`](../../components/portable/rgb/include/rgb.h)): three
  channels 0..100, a brightness 0..100 applied last, and one effect from OFF, SOLID, BREATHE,
  RAINBOW, CHASE and LEVEL. `rgb_frame()` turns it into pixels and is pure - the caller owns
  the clock and the buffer.
- The **RGB group** ([`cassette_rgb.c`](../../components/app/screen_display/cassette_rgb.c)) is
  the only writer. Its five knobs publish the whole state through `cassette_rgb_set_publish()`;
  the strip adapter is the reader.

GPIO 17 is one of the four pins the Sunton 8048S050C leaves free - 0, 17, 43, 44 - and the
other three are the BOOT strap and the console's UART pair
([`s15-i2s-dac-output.md:629-637`](../research/server/s15-i2s-dac-output.md#L629-L637)). The
bare N16R8 devkit's recommended I²S pinout takes 17 for BCK; a strip and that DAC cannot share
a board without one of them moving.

## Wiring

| Signal | Pin or rail |
|---|---|
| strip DIN | GPIO 17 |
| strip VDD | 5 V |
| strip GND | board GND, joined at the strip's end of the run |

- **The data line is 3.3 V and the supply is 5 V.** The WS2812B datasheet puts VIH at
  0.7 x VDD - 3.5 V at a 5 V supply - so the S3's high is below the guaranteed threshold.
  Either supply the strip at 4.5 V (a series diode) or shift the data (a 74AHCT125), and decide
  against the part actually fitted before the driver is flashed.
- **Eight pixels at full white are about 0.5 A** (up to ~60 mA each on the datasheet). That is
  the five-volt rail's problem, not the GPIO's.
- The strip's supply is not the board's 3.3 V rail.

## Power

Eight pixels at full white are about 480 mA - 60 mA a pixel, 20 mA a channel - and that
browned out this board: the first build that showed its full white rebooted the device under
the audio load. So the light has a ceiling.

`rgb_limit_power()` is the frame's last stage, after the effect and the brightness. It keeps
`CONFIG_APP_STRIP_MAX_PCT` of every channel - **25% by default**, about **120 mA** for eight
pixels at white - and no effect, no brightness and no frame can go above it. The ceiling is a
current limit, not a display setting: a single colour at the same 25% is about 40 mA, and the
brightness knob stays a working dimmer underneath it.

A per-frame current budget - which lets a dim colour through untouched and clamps only the
bright ones - was rejected: above the point where the budget binds, the brightness knob stops
doing anything, and a light whose knob has a dead top half is worse than one that is quieter
than it could be.

The ceiling is Kconfig (`APP_STRIP_MAX_PCT`), so a strip with its own supply can be given more
without touching the code. The wiring cure is separate and still the owner's: a bulk capacitor
across the strip's supply, and a supply that is not the board's own USB port.

## What it shows

The state is the owner's setting and outlives the panel: hiding the chrome or leaving the RGB
group does not turn the strip off, and only OFF makes it dark.

The six effects, and what eight pixels make of them:

| effect | at eight pixels |
|---|---|
| OFF | black, whatever the colour says; brightness has nothing to act on |
| SOLID | the colour, steady |
| BREATHE | the colour faded up and down over 2.4 s |
| RAINBOW | eight hues 45 degrees apart, the wheel turning once every 4 s |
| CHASE | a three-pixel head over a tail at 6% of the colour, one pixel every 60 ms, a lap in 480 ms |
| LEVEL | the colour scaled by the broadcast's RMS, 0..1 |

Brightness is applied **last**, after the effect, so turning the strip down keeps its hues.

## The tick

The strip has its own clock, not the screen's. The screen task's cadence is adaptive - it rests
at least three times its own frame cost ([`screen_display.c:443`](../../components/app/screen_display/screen_display.c)),
so a strip that rode it would run the chase at whatever pace a full-screen invalidation left
behind.

- **30 Hz.** One 33 ms task calls `rgb_frame(state, now_ms, level, px, 8)`,
  writes the result, and rests.
- `now_ms` is `esp_timer_get_time() / 1000`, the monotonic clock the panel adapter already uses.
  The effects take it modulo their own periods, so the 49.7-day wrap is a phase jump of at most
  one cycle.
- `level` is **`max(level_l, level_r)`** from `mini_icecast_levels()`, sampled on the tick. A
  hard-panned track still lights the strip; averaging would halve the picture for a mono strip
  that has no stereo image to preserve. It is 0 when nothing is playing.
- **The frame is written every tick**, changed or not: 192 bits at 800 kHz is about 240
  microseconds plus the part's reset, under 2% of a core at 30 Hz, and a strip that lost a latch
  recovers within one tick instead of waiting for a knob to move.

## The adapter

Board-only, in `components/app/strip/`: RMT through `espressif/led_strip`, the GRB order the
part wants, and the publish hook. It installs the screen's hook - `screen_display_rgb_publish()`,
which is the panel's own `cassette_rgb_set_publish()` behind the screen's public header - at start,
and keeps the latest `rgb_state_t` (five ints, copied under a short critical section). The hook's
contract is in [`screen_display.h`](../../components/app/screen_display/include/screen_display.h):
called after every move, and once with the current values when a hook is installed late.

The strip does not go through LVGL: no object, no invalidation, no flush. It has no
relationship to the **Page**, the **Bar** or the **Panel** - only to the state those knobs
write.

## Acceptance

- **Host:** `t25_rgb` already pins every effect's arithmetic and the brightness-last order. The
  adapter is board-only and gains nothing from a host test; the seam's shape is the header's.
- **Board:** build `sunton`, flash, and see on the strip: OFF dark, SOLID the colour the knobs
  say, BRI dimming without a hue shift, BREATHE, RAINBOW turning, CHASE lapping, and LEVEL
  following the VU meter. LEVEL is the one that proves the level source is the right field.

## Open items

- **The level shifter or the 4.5 V supply**, decided against the part actually fitted.
- **`CHASE_WINDOW` at eight pixels.** Three of eight is 37% of the strip lit, which may read as
  a block rather than a head. One constant is the edit.
- **Whether the strip follows the backlight sleep.** The screen fades at 2, 5 and 10 minutes
  without a touch. The strip is a light in the room, and the owner has not said whether it dims
  with the screen or stays where it was set; until then it stays.
- **The task's core and priority.** Chosen at core 0, priority 2 - below the screen's 3 and the
  LVGL port's 4 - without measuring the tick on the board. The first `render_bench`-style reading
  of the 30 Hz task is still owed.

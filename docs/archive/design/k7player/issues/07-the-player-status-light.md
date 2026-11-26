# 07 — The status light

**What to build:** One LED and its four states — *Searching*, *Ready*, *Playing*, *Fault* — driven
by the same state the stream client and the network code already keep, so the light is a reading
and not a second state machine.

**Blocked by:** 05 — The player's stream client and its ring.

**Status:** proposed

- [ ] The four states and their patterns are the spec's: 1 Hz blink, dim steady, bright steady, 4 Hz blink.
- [ ] *Playing* means audio is going out of an output, so a missing speaker is *Ready*, not a fault.
- [ ] The light says nothing about the speaker and nothing about the network's name.
- [ ] No other surface exists: no page, no button, no beep.

Spec: `docs/specs/k7player.md` R31–R32, T5, B6.

**What to build:** One LED and its four states — *Searching*, *Ready*, *Playing*, *Fault* — driven
by the same state the stream client and the network code already keep, so the light is a reading
and not a second state machine.

**Blocked by:** 05 — The player's stream client and its ring.

**Status:** done — host tier, and B6 by eye on the board.

- [x] The four states and their patterns are the spec's: 1 Hz blink, dim steady, bright steady, 4 Hz blink.
- [x] *Playing* means audio is going out of an output, so a missing speaker is *Ready*, not a fault.
- [x] The light says nothing about the speaker and nothing about the network's name.
- [x] No other surface exists: no page, no button, no beep.

Spec: `docs/specs/k7player.md` R31–R32, T5, B6.

## Comments

- **The rule was already there** (`player_status`, PL-03). This ticket is the wiring: the light
  reads three machines' status — the network's, the stream's, the playout's — and folds them into
  that rule:
  - *network* is `PLAYER_NET_CONNECTED` and nothing earlier: while it sweeps, connects or retries
    it has no network it can use, which is what Searching means;
  - *stream* is `PLAYER_STREAM_BODY`;
  - *audio* is the output being open **and the last output tick having written decoded frames**
    rather than silence — which is exactly R14's "a missing speaker is Ready";
  - *fault* is the stream's non-MPEG fault or an output that refused to open (R31's two examples).
- **The board is a PWM duty, not a lamp.** `player_board_light(level)` with 0 off, 32 dim and 255
  bright, so "Ready is dim" is a real state rather than a second blink pattern. The devkit's own
  LED is GPIO 2; a board with its LED elsewhere changes one line (`light_led.c`).
- **The board found a starvation the host could not.** The playout held its lock **across the
  blocking I2S write**. Being higher priority than the light and re-taking that lock the instant it
  was free, it starved the light task completely: the LED showed `ready` and then nothing, while
  the audio played perfectly. The fix is structural — decode and write outside the lock, and take
  it only to read the rate and to publish the counters. A fake whose `write` returns instantly
  cannot show this; only a board can.
- **Evidence, host:** `t05_light` — **32 checks, 0 failures**: the 1 Hz pattern's lit and unlit
  halves, Ready's steady dim, Playing's bright steady, the 4 Hz fault from a body of noise, and a
  fault from an output that will not open. `t04` 17, `t03` 87, `t02` 64, `t01` 15; both layers and
  the HTTP acceptance green.
- **Evidence, board:** the console carries the transitions —
  ```
  light: searching (net connecting, stream idle, audio 0, failed 0) level 255
  light: ready
  light: playing
  light: playing (net connected, stream streaming, audio 1, failed 0) level 255
  ```
  and by eye: **blinking at about 1 Hz for the first seconds, then steadily bright** once the
  stream was playing. That is B6, seen rather than inferred.
- **Firmware:** `k7player.bin` is `0xdd900` (908 KB) in the 1 MB partition, 13 % free.
- **The light is the whole surface.** There is no page, no button, no beep and no second LED; a
  person diagnoses the player by looking at it, which is why the state rule lives in one tested
  place rather than in the LED driver.

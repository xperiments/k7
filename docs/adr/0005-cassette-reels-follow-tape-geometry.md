# The cassette reels follow the tape geometry, not a fixed loop

The Cassette screen used a fixed two-second loop: both hubs advanced one art frame
per 100 ms of audio, at the same rate, regardless of how much tape was on each side.
A real compact cassette does not work that way. The tape moves at 47.625 mm/s
(1 7/8 ips), the hub is 22 mm across, and a full C60/C90 pack is about 48 mm, so a
reel's angular speed is v/r: one revolution every 1.45 s when empty and every
3.1–3.2 s when full. The supply and take-up reels sit at opposite ends of that range;
they meet at half the side.

## Decision

The model derives each reel's speed from its own pack fill. The radius comes from the
pack area — the tape length, and therefore the area, is proportional to the fill — and
the phase integrates the tape speed at that radius, `v / (2πr)` revolutions per second.
The frame is the phase times the art's frames per revolution. The phase starts at zero
for a track; a stop or a track change starts it again. The absolute layer count is not
modeled: the art is periodic, and the speed is what the eye reads.

The art is one revolution, not a loop with a duration. Its frame count is the angular
resolution. 36 frames is 10 degrees per frame, and the fastest reel (empty, 1.45 s)
plays at 24.8 fps, inside the 25 fps cadence measured on the panel. The old 20-frame
art was 18 degrees per frame.

The pack fill is also mapped to the pack diameter through the same area relation, so
the drawn pack and the reel phase agree.

## Considered options

- **Keep the fixed two-second loop.** Simple, already tested, but both reels turn
  together at a speed that matches only one pack radius; the average real revolution
  is about 2.30 s, so the loop was also 13 % fast.
- **Fixed loop at the real average (58 frames / 2.32 s).** Closer in average speed,
  still a single speed and still both reels together.
- **Derive the phase from the tape position and the pack fill (chosen).** Two reels at
  their real speeds, 36 frames per revolution, 0.66 MB of art. The frame rate varies
  from 11.3–11.6 fps at the full pack to 24.8 fps at the empty hub; the model needs the
  geometry constants and the pack fill.

## Consequences

- The two reels turn at different speeds: the supply starts full and slow and ends
  empty and fast; the take-up does the reverse. They meet at half the side.
- The model no longer takes a loop length. `cassette_new(frame_count, bar_period_ms)`
  takes the art's angular resolution.
- The generated hub header no longer carries `HUB_LOOP_MS`/`HUB_FRAME_MS`: the art is
  one revolution.
- A track whose status has no duration uses a nominal 30-minute side for the reel fill;
  the progress bar stays at zero because it has nothing to measure.
- The cadence remains 25 fps (`UI_FRAME_MS = 40`, display refresh 40 ms). The measured
  render headroom with two hub blits and the scrolling title was ~34 fps; the panel's
  own ceiling at 18 MHz is 43.9 fps. 25 fps is the real model's fastest reel, not a
  panel limit.
- The art is 36 frames (0.66 MB) instead of 50 (0.92 MB).

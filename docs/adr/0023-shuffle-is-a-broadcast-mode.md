# Shuffle is a mode of the broadcast, and one order is walked both ways

The Menu's root has a Shuffle row. The owner wants it to be a **mode** - a shuffled order of
the whole Playlist that the broadcast walks forward and back - not a one-shot random pick. That
makes it a capability of `mini_icecast`, not of the screen: the Menu shows Off/On and toggles it,
and nothing else about the order is the screen's business.

## Decision

- **One order, walked both ways.** Enabling Shuffle builds a permutation of the whole Playlist.
  The Track that is playing is not interrupted: its place in the order is the cursor, and the
  next advance - the Track's natural end or the transport's next command - takes the order's next
  element, while the previous command walks the existing history. The end of a lap clears the
  history, as the linear Playlist's lap does today, so the previous command at the first Track of
  a lap replays it.
- **A pick moves the cursor.** Playing a Track from Tracks or Browse while the mode is on forces
  that Track, as it does today, and moves the cursor to that Track's place in the order. The mode
  stays on, the order is not rebuilt, and the next advance continues from the pick.
- **A new generation rebuilds it.** A rescan publishes a new Library generation and ends the
  pass; the next pass builds a new order from the new Playlist with the mode still on. A Track
  that vanished is not in the new order, and the old order is freed.
- **The order is the broadcast's, and it is built off the UI thread.** The Menu only flips a flag
  (`mini_icecast_shuffle_set()`); the broadcast builds the order at the top of its next pass,
  where it already holds the Library lease. `mini_icecast_shuffle_enabled()` feeds the row, so the
  row reads On from the tap and the order catches up at the next Track boundary.
- **Memory and failure.** The order is one `count x 4` byte allocation - 80,000 B at 20,000
  Tracks, for scale the Cover buffer is 464,310 B - allocated when the mode is enabled and
  freed when it is disabled or the app restarts. If the allocation fails the mode stays Off and
  the failure is logged; the row reads Off. There is no NVS entry: the mode lives until reboot,
  like the Menu's own remembered list.
- **Seeding.** The order draws from a seed the app sets at boot
  (`mini_icecast_shuffle_seed(esp_random())`), so the board gets the hardware RNG and the host
  tests pin the sequence. `mini_icecast` never calls `esp_random()` itself: components carry no
  board conditionals, and the host shim has no such symbol.
- **The row is the only indication.** The root row reads Off/On and toggles on activation.
  Nothing on the cassette, the Peek or the footer says the mode is on; adding such an indicator
  is a later decision, not this one.
- **The status is unchanged.** `track_index` stays the Playlist position, and `path`, `title` and
  the clock are the Track's own. A listener cannot tell the order from the stream.
- **The arithmetic is host-tested on its own.** The order - build, next, prev, seek, rebuild -
  is a pure portable unit with an injected seed, tested in `t19_shuffle`; the broadcast's
  eight-second-track integration test is not the place to pin a permutation.

## Considered options

- **One-shot random** (the tree as first drawn): no mode, no state, one random
  `mini_icecast_play()`. The owner asked for a mode; this ADR replaces it.
- **Random on every advance**, with no order: a fresh random Track at each boundary. No
  allocation, but A-B-A repeats are possible immediately, the whole library is never guaranteed
  to be reached, and the previous command has no order to walk.
- **A visited bitset and a random unvisited pick**: 2,500 B at 20,000 Tracks instead of 80,000,
  with the same one-lap coverage. Rejected because it has no order for a pick to move the cursor
  in and no notion of "the next element", so the very behaviour the owner asked for - one order,
  a pick moves the cursor - would have to be approximated with a visited set per session; the
  memory saved is well inside what the board has free.
- **The Menu owns the order**: the screen would decide what plays next, making the screen a
  source of truth and putting playback state where the status cannot see it. Rejected; the note
  behind the Menu warns against inventing playback semantics in a menu.
- **Persisting the mode in NVS**: one setting is not worth a settings store, and the Menu's other
  state (its list and band) also lives until reboot. Rejected for this cut.
- **Building the order on the UI thread** when the row is tapped: 80,000 B allocated and 20,000
  swaps executed under the LVGL lock. Rejected; the flag-and-build-in-the-broadcast split keeps
  the screen's frame budget untouched.

## Consequences

- `mini_icecast` gains the mode, the order and cursor, the seed setter, the two accessors, and
  the generation rebuild; `appliance.c` seeds it at boot from `esp_random()`; the Menu's root row
  renders Off/On from `mini_icecast_shuffle_enabled()` and calls `mini_icecast_shuffle_set()`.
- `t19_shuffle` pins coverage, next/prev/seek, the lap reset, the rebuild and the seed. The
  broadcast's existing history is reused unchanged; only the "next index" branch of the pass
  learns about the order.
- A rescan while the mode is on keeps it on and builds a new order; the current Track finishes on
  the old generation, as the broadcast already does.
- The mode costs 80,000 B of heap at 20,000 Tracks only while it is on. If the allocation fails,
  the row cannot lie: it reads Off.
- The note `docs/research/player-menu-ipod-style.md` lists shuffle as "a small adapter" and warns
  that playback semantics are not menu business. This ADR records the deliberate exception to
  the second half: the owner wants a mode, so the mechanism lives in the broadcast and the Menu
  only flips it.
- The Menu's tree and geometry are in [`docs/specs/menu.md`](../specs/menu.md); the Menu itself is
  [ADR 0022](./0022-the-menu-is-a-controls-mode.md).

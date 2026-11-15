# The player boots shuffled

[ADR 0023](./0023-shuffle-is-a-broadcast-mode.md) made Shuffle a mode of the broadcast and left
its boot state to the caller: the Menu's root row toggles it, the mode lives until reboot, and it
is not stored. The app never turned it on, so every power cycle started in the library's own row
order and a person who listens shuffled opened the Menu and toggled it first. The owner asked for
the mode to be on by default.

Two facts shaped the decision. A broadcast with the mode on builds its order at the top of the
first pass, so the seed the order draws from has to be in place before the station starts — the
app used to seed after it. And a fresh pass has no Track playing, so the order's build put the
cursor on Track 0: the first Track was always the same, and a "shuffled" boot that opens on the
same Track every time is not shuffled.

## Decision

- **The portable broadcast starts with the mode on.** `mini_icecast`'s own state carries the
  default, so every build boots shuffled and the Menu row reads On at its first paint. A caller
  that needs the library's own order turns the mode off before `mini_icecast_start()`; the
  transport test is that caller.
- **A fresh pass starts at the order's first element.** With no Track playing, the build's
  `current` is outside the order, so its cursor lands on position 0 and the first Track is drawn
  from the permutation. A pass that restarts mid-generation still keeps the playing Track's place,
  as ADR 0023 says.
- **The app seeds the order before the services start**, because the first pass now builds one.
- **Off lasts until the next boot.** There is still no NVS entry: turning the mode off is a choice
  for this session, and a reboot is shuffled again, as ADR 0023's "lives until reboot" already
  said.
- **A failed allocation still turns the mode off**, and the row then reads Off, unchanged.

## Considered options

- **Turn it on in the app** — a config field, or a call after the services start. Rejected: the
  first pass can already be walking by then, the app's boot configuration is board-only so the
  host could not pin the default, and another build of the same portable station would not inherit
  it.
- **Remember the mode in NVS.** Rejected: ADR 0023 deferred a settings store, and the owner asked
  for a default, not a memory. A remembered Off under a default On would also need a first-boot
  flag before it meant anything.
- **Keep starting on Track 0.** Rejected: the order is random but the first Track is fixed, and
  the first Track is the part a listener notices every time.
- **Start shuffled only in the shipping variant.** Rejected: the mode is portable, the variants are
  the same player with different drivers, and a variant-specific default would be a second boot
  rule to keep true.

## Consequences

- **Every boot opens on a random Track** and walks the permutation; the Menu's row reads On
  without a toggle. The stream and the status carry the Track, so nothing else changes.
- **The app must seed before the broadcaster starts**, or the first order is built from an
  unseeded generator.
- **The host pins both sides.** The transport test asks for the library order before start, so it
  still tests the linear pass; `t39_shuffle_default` starts a fresh broadcast with a fixed seed and
  checks the mode is on, the order is not the row order, and the first two Tracks follow it.
- **A rescan keeps the mode on** and rebuilds the order, as ADR 0023 already decided.
- **The Menu spec records the default**, and [ADR 0023](./0023-shuffle-is-a-broadcast-mode.md) is
  amended, not superseded: its decision that shuffle is the broadcast's mode and lives until
  reboot stands.

## Supersedes

None. It amends ADR 0023's boot state.

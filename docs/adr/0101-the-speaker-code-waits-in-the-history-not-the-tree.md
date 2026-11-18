# The speaker code waits in the history, not the tree

[ADR 0100](./0100-the-player-ships-with-the-line-output-and-the-speaker-waits.md) made the line the
player's only output and kept the Bluetooth work behind `CONFIG_PLAYER_OUTPUT_SPEAKER`: the A2DP
source, the pairing rule, the sink switch and their host tests. The reason to keep it was its cost
to re-derive — a week of protocol work and one hard-won measurement (a listener's send buffer, a
scan that starves Wi-Fi, an address that was BLE and not classic).

Two things became clear since. The speaker path was never proven: no speaker on the bench answered
the player's inquiry, so every line of it is a hypothesis with tests around it, not behaviour. And
its presence was not free — the seam carried a `player_sink_t`, the playout chose between sinks,
the network and stream machines held for the radio, `main` started a policy, a Kconfig option and a
stub existed, the host fake modelled a radio, and every reader of the player paid for a path that
could not ship. The change set was the evidence: the speaker was the largest single source of
complexity in it.

## Decision

The Bluetooth code is **removed from the tree**. The player has no radio, no sink choice and no
pairing rule; `player_output_*` is the line, and the spec says so.

What the code taught us is kept where it survives review: PL-08's ticket (the pairing rule, the
measurements, the bench attempts, the address that was BLE) and ADR 0100's table (the stream cost
of compiling the stack in, the inquiry, the coexistence). The code itself is one `git show` away on
the branch that built it.

## Considered options

- **Keep it behind the option**, as ADR 0100 said: every reader keeps paying for a path that cannot
  ship, and nothing about it can be tested without a speaker that answers inquiry.
- **Delete the tickets and the ADRs with it**: the measurements are the part worth keeping; the
  code is the part that is cheap to rewrite with them.

## Consequences

- The player's seams have one implementation each: one output, one network, one stream, one light.
- The partition table went back to the default 1 MB; the image is 908 KB with 13 % free.
- Rebuilding the speaker variant starts from PL-08's ticket and `git show` of the removed files —
  a day's work with the numbers already in hand, not a week of rediscovery.
- ADR 0100 stands: its decision (the line is the output) is unchanged. This one records what
  happened to the code that waited behind it.

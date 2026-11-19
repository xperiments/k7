# The speaker needs a second radio, not a second build

[ADR 0100](./0100-the-player-ships-with-the-line-output-and-the-speaker-waits.md) left the speaker
as a firmware variant behind `CONFIG_PLAYER_OUTPUT_SPEAKER`, waiting for a speaker that would
answer the inquiry, and costed it in stream frames. [ADR 0101](./0101-the-speaker-code-waits-in-the-history-not-the-tree.md)
took the code out of the tree and kept it a `git show` away. Both treated the open question as the
price: how much of the stream a proven speaker would cost.

The bench answered the question differently. Measured on the K7Player against the "BT5.0-Audio"
sink at `4b:68:00:36:09:6d` (the full matrix is [section 6 of the
note](../research/player-a2dp-output.md)):

- **BR/EDR-only is deaf.** Six inquiries across two firmware bases returned no devices at all,
  while the same board in dual mode heard the speaker and a phone. What read as coexistence was
  the controller mode.
- **Dual mode pages only while the radio is quiet.** Inquiry, page (`st 0x0`), A2DP connection and
  PCM pull all work with Wi-Fi up and idle. With the ESP-NOW stream running, four pages in a row
  timed out (`st 0x4`); handing the radio over (stop the driver, page, restart it) landed one in
  ten and still could not hold the path.
- **The buffer does not fit either.** The A2DP source allocates 4 112 B per frame; with Wi-Fi up
  the board has 3 104 B free and 2 816 B in its largest block, so every frame logged `no buffer`.
- **The S3 cannot carry it at all.** No `SOC_BT_CLASSIC_SUPPORTED` — ADR 0097's chip choice was
  the right one and it is not the emitter.

## Decision

The player's firmware has no speaker path, and a speaker is **not a build of this player**. The
radio belongs to the stream, and the line is the output. If the K7 is ever to play on a Bluetooth
speaker, the Bluetooth radio comes from a **second chip** — an A2DP bridge fed from the player's
line or I2S, or an off-the-shelf transmitter plugged into the line out. That is a hardware
decision with a part and a wire in it.

## Considered options

- **A speaker variant behind the build option (0100's shape).** Measured and rejected above: it
  neither pages while the stream runs nor has the RAM for the source's buffers.
- **Stop Wi-Fi for the page.** It works once (`st 0x0` in 2.2 s) and the buffers fail the moment
  the driver restarts; the line also pays the pause in seconds of silence.
- **A WROVER module for the PSRAM.** It does not help: the wall is the radio's arbiter and the
  page, not flash memory.
- **The K7 as the emitter.** The S3 has no Bluetooth Classic at all — `SOC_BT_CLASSIC_SUPPORTED`
  is not defined and `CONFIG_BT_CLASSIC_ENABLED` cannot be selected
  ([the note, § 1](../research/player-a2dp-output.md)). Dead by silicon.

## Consequences

- The line build is the player, and its radio is the stream's; nothing is compiled, no seam is
  called and no build option exists for a speaker.
- A speaker is a BOM line, not a Kconfig line: one more chip with its own radio, fed from the
  line. It is the only shape the measurements leave.
- The numbers that close it are in the research note, including the mode matrix and the 4 112 B
  buffer, so the next person starts from the answer instead of the bench.
- The branch that tried (`speaker-a2dp`) does not merge; its code is one `git show` away for as
  long as the branch exists.

## Supersedes

[ADR 0100](./0100-the-player-ships-with-the-line-output-and-the-speaker-waits.md)'s second half —
that the speaker is a firmware variant waiting for a proven device. Its first half, that the line
is the player's output and the whole radio, stands and is now the only output.
[ADR 0101](./0101-the-speaker-code-waits-in-the-history-not-the-tree.md) stands unchanged: the
code is not in the tree.

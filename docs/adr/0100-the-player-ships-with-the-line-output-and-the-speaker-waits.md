# The player ships with the line output, and the speaker waits for its own firmware

The player was specified with two outputs: an I2S line and a Bluetooth speaker, with the speaker
winning whenever it was connected (R10), pairing itself with no control surface (R13), and the
light saying nothing about it (R14). The classic ESP32 was chosen for exactly that — the S3 has no
Bluetooth Classic (ADR 0097) — and the firmware now contains an A2DP source, a pairing rule and a
sink switch.

It also cannot carry both well. Measured on the board, with the same stream:

| Firmware state | Stream rate |
|---|---|
| No Bluetooth in the firmware | **38–40 frames/s** |
| Bluetooth compiled in, radio never started | ~24 frames/s, in degrading bursts |
| Bluetooth compiled in, radio up but idle | 3–5 frames/s |
| Bluetooth in inquiry | 0 frames/s |
| Coexistence arbitration disabled | no better — it is not the arbiter |
| Heap over two minutes | flat — it is not a leak |

And the bench speaker — a classic A2DP sink by its own device record — never answered the
player's inquiry at all, even with the station disassociated, and refused a direct connection to
the address its phone reported. The speaker path is therefore not just costly, it is **unproven**
against a real device.

## Decision

The player's output is the **line**. The firmware builds no Bluetooth by default
(`CONFIG_PLAYER_OUTPUT_SPEAKER` off), and the line gets the whole radio and the whole internal
heap: 38–40 frames a second, no silent frames, no underruns.

The speaker is **a second firmware variant**, behind the same build option, for when a speaker has
been proven on the bench. It keeps the code that exists — the A2DP source, the pairing rule, the
sink choice — and it is not part of the shipping build.

R10–R14 are superseded: there is no "the speaker wins", because there is no speaker in this
build. R31 (the light) is unchanged and says nothing about an output that is not there.

## Considered options

- **Both outputs, one active** (the request that stood before this): power the radio down when it
  is not in use. Measured: it recovers the stream from 0 to ~24 frames a second, not to 40, and
  every connect attempt still costs the line seconds of silence. It is the shape the speaker
  variant would use if it ever ships, not the line build.
- **Both compiled, speaker first, as R10–R14 said**: this is the state the measurements above
  reject. The line is the player's only proven output, and it must not pay for an unproven one.
- **Remove the Bluetooth code**: it is the tree's real work, tested on the host, and re-deriving
  it later would repeat this week. It stays behind the build option.

## Consequences

- The line-only build is the default and the one that ships. Its evidence is the board: 38–40
  frames a second, `0 silent, 0 underruns`, and the light's four states.
- The flash keeps its 3 MB factory partition (`player/partitions.csv`), which is room the speaker
  variant will need; the line build uses a third of it.
- A speaker variant is a build, not a setting: a device is either a line player or a speaker
  player. That is honest about the hardware, and it makes the choice visible instead of implied.
- The tickets that measured the speaker path (PL-08, PL-10) keep their evidence and their code;
  they wait for a device that answers the inquiry.
- If the line build ever ships on hardware with a radio, nothing about it depends on Bluetooth:
  no seam is called, no policy runs, and the sink is always the line.

# The player is an ESP32 classic in its own project, because A2DP is the one thing the S3 cannot do

The K7 is an ESP32-S3. The second device has to play the K7's live stream into a Bluetooth
speaker and into a line output, and the S3 cannot do the first of those: its `soc_caps.h` has
`SOC_BT_SUPPORTED` and **no `SOC_BT_CLASSIC_SUPPORTED` at all**, while the classic ESP32 defines
`SOC_BT_CLASSIC_SUPPORTED 1`. A2DP is Bluetooth Classic; a BLE-only radio cannot be an A2DP
source, and LE Audio is a different speaker population with a different stack that the chip does
not have either. The chip choice is therefore not a preference — it is what "Bluetooth" costs.

The classic ESP32 also carries the two 8-bit DACs the S3 lacks
(`SOC_DAC_SUPPORTED 1`, `SOC_DAC_CHAN_NUM 2`, `SOC_DAC_RESOLUTION 8`). The K7Player does not use
them — 8 bits is about 48 dB of signal-to-noise — but it is worth recording that both of the
player's outputs are decided by one chip's capability list rather than by a board.

## Decision

- **The K7Player is an ESP32 classic.** Its line output is I2S to an external DAC and whatever
  amplifier follows; its speaker output is an A2DP source.
- **It is its own ESP-IDF project at `player/`**, beside `web/`: its own `main`, its own
  `sdkconfig` family, its own host harness, and its own build entry. It links only the
  **Vendored** and portable pieces it needs from `../components/portable` — the MP3 decoder, the
  source-format table, and the resampler if the station rate ever stops being 44.1 kHz — and it
  writes to none of them.
- **No `CONFIG_IDF_TARGET_*` reaches the shared code.** The two firmwares are two builds, not one
  build with two targets; the rule that `components/portable` carries no target conditionals is
  what decides this, not a preference for separate directories.

## Considered options

- **An S3 plus an external Bluetooth audio transmitter** (a second radio, bridged over I2S or
  UART). It keeps one chip family and one project, and it replaces a stack we cannot change with
  a module whose AT-style control surface becomes a second protocol to specify. It also doubles
  the antenna count in a box whose whole argument is that it is small.
- **One project, two targets** (`idf.py set-target esp32` for the player). It is the least
  directory churn and the most conditional compilation: every component that touches the panel,
  the card, the strip or the Bluetooth stack grows a target branch, and the host build starts
  compiling two shapes of the same module. The repo's layering test exists because that road was
  already found expensive.
- **An S3 with no Bluetooth** (line output only). It is the cheapest device that fits today, and
  it is half of what was asked for.

## Consequences

- **Two firmwares, two build entries, two host harnesses.** `make -C host check` has to run the
  player's core as well as the server's, or the player's logic is untested until it is on a
  board — and the two-tier rule is not a suggestion.
- **The player has no PSRAM by default.** Its ring, decoder scratch and A2DP buffers all live in
  the 520 KB of internal SRAM, so the memory budget is a measured number from the first board
  boot, not an afterthought the way it was allowed to be on the S3's 8 MB.
- **Wi-Fi and Bluetooth Classic share one 2.4 GHz radio.** IDF time-slices them
  (`ESP_COEX_SW_COEXIST_ENABLE`), and whether a continuous 256 kbps inbound stream survives SBC
  going the other way is a **board-only property** — the host cannot establish it, and neither
  can a datasheet.
- **The player is not governed by the stream server's global premise.** It has no card, no
  library and no Snapshot, so it cannot violate a rule about SD content. What binds it instead is
  the client rule: the live stream is the only thing it asks the K7 for.

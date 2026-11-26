# 06 — The line output, and the first sound

**What to build:** The wired half of the output seam: I2S to an external DAC, configured to the
rate in the MP3 frame header, fed from the ring, with a board adapter that names its pins.

**Blocked by:** 05 — The player's stream client and its ring.

**Status:** done. The firmware and host tiers were already running; with a PCM5102A wired to the
pins, B1 is proven by ear, and the listening found three defects that no host test could.

- [x] A WROOM-32 devkit and a PCM5102A breakout play the K7's stream at 44.1 kHz, and a 32 kHz station follows on the line rather than failing. *(The 44.1 kHz path is B1, heard; the 32 kHz follow is `t04_output`'s.)*
- [x] The output's rate is set from the stream's first frame header, and the seam presents a second implementation that does nothing, so the seam is real rather than hypothetical.
- [x] Full scale is the line's only level: no firmware volume, no mute.
- [x] The K7's `GET /stream` and the burst produce sound in under a second from a cold start. *(Measured: the stream connects at 4.58 s, the line opens at 4.86 s, and by 5.8 s the output has written 0.44 s of audio.)*

Spec: `docs/specs/k7player.md` R4, R10–R11, R27; B1.

## The wiring

| PCM5102A breakout | ESP32 classic | Why |
|---|---|---|
| BCK | GPIO 26 | bit clock |
| LRCK | GPIO 25 | word clock |
| DIN | GPIO 22 | data |
| SCK | GND | no master clock: the PCM5102A's own PLL derives it from LRCK |
| FMT | GND | I2S, not left-justified |
| XSMT | 3V3, or unconnected | not soft-muted |
| VIN / GND | 3V3 / GND | |
| LOUT / ROUT | amplifier | line level, full scale: the amplifier owns the volume |

The pins are named once, in `player/components/board_player/output_i2s.c`.

## Comments

- **The stage is a decoder plus a writer.** The ring holds whole MP3 frames (PL-05) and the
  output needs PCM, so `player_playout.c` decodes one frame at a time with `dr_mp3`'s frame
  decoder — the same vendored header the K7 decodes its card with — and writes it. A mono frame is
  doubled; a frame with nothing behind it is a gap.
- **A ring with nothing in it writes silence, not a stall** (R28): ten milliseconds at the
  stream's rate, so the DAC keeps clocking and the gap is the gap the network left.
- **`dr_libs`'s manifest claimed `esp32s3`.** Header-only public-domain C, now compiled by a
  second target: its `idf_component.yml` lists both. The vendored header itself is untouched.
- **The build forced a link question open.** The portable components call seams the board adapter
  implements, and IDF's archive order can put the adapter before its callers, leaving the symbols
  undefined. `board_player` is now `WHOLE_ARCHIVE`: a board adapter's every object is needed by
  definition.
- **The board found the bug that mattered: a zero-tick delay.** `vTaskDelay(pdMS_TO_TICKS(5))` is
  `vTaskDelay(0)` at a 100 Hz tick — a yield, not a sleep. The playout task spun and starved every
  other task on its core: the task watchdog fired every five seconds, the stream never connected,
  and the output never opened. One tick, with a comment, is the fix.
- **Evidence, host:** `t04_output` — **17 checks, 0 failures**: the 44.1 kHz fixture decodes
  through to the null output with a non-zero peak, the 32 kHz fixture makes the output open at
  32 000 (R27), and a ring with nothing in it writes silence after the audio stops. `t03` 87,
  `t02` 64, `t01` 15; both layers and the HTTP acceptance green.
- **Evidence, board:** the console, with the K7 broadcasting —
  ```
  net: the K7 is at 192.168.0.13
  i2s: line output: 44100 Hz, BCK 26, LRCK 25, DIN 22
  playout: output open at 44100 Hz
  playout: output 44100 Hz: 452736 frames, 54243 silent, 123 underruns, 0 refused
  ```
  The frames written climb by **46 080 per second** — 40 MP3 frames × 1152 samples — which is the
  audio rate; the decoder refused none; the underruns are the start-up fill and stop growing; the
  stream's ring stopped dropping once the playout began draining it.
- **Firmware:** `k7player.bin` is `0xdb2d0` (897 KB) in the 1 MB partition, 14 % free.
- **What is left is a wire.** With the PCM5102A on the pins above and an amplifier after it, B1 is
  a listening test — the one thing this ticket's evidence cannot supply.

## Board tier, 2026-11-18 — with the DAC wired

The PCM5102A went on the pins above and the stream played. Listening then found three things the
host could not, in the order they were heard:

1. **A cut every 100 ms** — IDF's Wi-Fi modem sleep. The radio parked between DTIM beacons, the
   access point could not buffer 3.2 KB of stream, and TCP spent its time recovering: the player
   received about 1 KB/s with the socket open and never reconnecting. `esp_wifi_set_ps(WIFI_PS_NONE)`,
   which the K7's adapter had already learned (*"modem sleep adds latency spikes: bad for
   streaming"*). 1 KB/s became 32 KB/s, and 3 frames/s became 38.
2. **An occasional cut** — the I2S DMA was 33 ms deep, a scheduling budget rather than an audio one,
   so any preemption longer than that underran it and `auto_clear` wrote silence into the music.
   Now 8 × 512 frames, about 93 ms. The playout's own counters showed **zero** underruns throughout,
   which is what ruled the ring out and the DMA in.
3. **A wedged connection** — a connection that stays open and dribbles bytes is invisible to a
   byte-based stall rule. The player now drops and reconnects after **3 s without a frame**, which
   is `PLAYER_STREAM_FRAME_STALL_MS`; `t03_stream` covers it.

The ring went from 8 frames to **16 (418 ms)** as a measured trade, not a guess: at 209 ms a
five-minute listen cut once or twice, each at a moment when the source paused.

**What is left is not this ticket.** The remaining cuts correlate with the origin: in the same
window the K7 logged **76 "cannot decode … skipping"** lines on ordinary MP3s while its internal RAM
dipped to 5 539 B (largest block 2 944 B) — an allocation failure at the track change, not a corrupt
file. That is [FW-06](../../broadcast-decode-failures/issues/01-the-broadcast-skips-tracks-it-can-decode.md),
and the player cannot buffer its way out of a source that stops sending.

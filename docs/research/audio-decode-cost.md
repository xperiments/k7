# What a 24-bit / 192 kHz FLAC costs this board

Measured on the Sunton ESP32-S3 (240 MHz, 8 MB octal PSRAM, SD over 20 MHz SPI,
1-bit) while the station was 44,100 Hz / 256 kbps and one client was attached.
The Track is `01 - I Surrender.flac`: 24-bit / 192 kHz stereo, 173 MB, 244.6 s,
about 708 KB/s. It is first in the Playlist, so it is the Track a boot plays.

## The numbers

| Run | Stream rate | Decode | Resample | Encoder | Starvation |
|---|---|---|---|---|---|
| Original, one broadcast task | 13.3 KB/s (41.4 %) | 38.5 % of wall, SD 25.9 % | 29.4 % of wall | 14.1 ms/frame (53.9 %) | 69 rebases in 2 min |
| 32-phase resampler in internal RAM | 17.8 KB/s (55.5 %) | 16.7 % of wall, SD 32.7 % | 10.3 % of wall | 14.4 ms/frame (55.1 %) | 26 in 60 s |
| Decoded source on core 0 | 21.0–28.1 KB/s (65–88 %) | 32 % of wall, SD 35.2 s | 12.2 % of wall | 16.6 ms/frame (63.4 %) | 20 in 60 s |
| Prefetch reader task (rejected) | 21.8 KB/s (68 %) | 31.9 %, SD 36.3 s | 13.8 % | 18.3 ms/frame (70.2 %) | task watchdog |

"Of wall" is the device's own 60-second telemetry window. A percentage above 100 %
would mean the stage took more than the whole window; the figures are shares of a
single core's wall time, not of the audio duration.

The second run's decode share is higher than the first because it produced 34 %
more audio in the same window; per audio-second the decoder is roughly constant.
The next Track in the Playlist, `01 Serve the Servants.flac` (16-bit / 44.1 kHz),
measured **101.4 %** of real time over a 45 s window after the boundary fix: a
16/44.1 FLAC is not the problem.
The encoder moves between 54 % and 70 % depending on what else is touching PSRAM:
its 95 KB of state is external by ADR 0004's decision.

## The shapes behind the numbers

- **Core-seconds per audio-second.** Before: 0.63 card + 0.30 decode + 0.71
  resample + 0.54 encode + 0.006 tone = 2.18. The resampler was the single largest
  piece after the card, and the whole chain ran on one core.
- **The resampler was a placement problem, not an arithmetic one.** 128 phases ×
  74 taps × 4 B = 38 KB of kernel rows in PSRAM, streamed through the same 32 KB
  data cache the screen's framebuffer uses. At 32 phases the table is 9.8 KB and
  fits internal RAM; the same audio measures 2.9× cheaper. Linear interpolation
  between 32 phases is −88 dB at 19 kHz and −100 dB at 10 kHz against an exact
  per-output kernel, below what a 256 kbps MP3 carries.
- **The card is the floor.** 16 KB reads with a 32 KB FILE buffer and a 32 KB
  `spi_bus_config_t.max_transfer_sz` do not beat 4 KB reads by much: the SPI bus
  delivers about 1.0–1.2 MB/s, so a 708 KB/s source spends 0.6–0.8 of a core-second
  per audio-second just moving bytes. It is also why the prefetch reader did not
  overlap: the transfer is not a free DMA engine, so a reader task and a decoder
  task add their wall time instead of hiding it, and core 0's idle task starved.
- **The system is over capacity for this file.** Source (decode + card) needs
  ~0.89 of core 0; the encoder and resampler need ~0.76 of core 1; the screen,
  Wi-Fi and lwIP are also on core 0. The two cores have no room for all three.
- **The artwork loader is a second card user at boot.** On the same Track it
  logged `read+decode 29.5–38 s` for a 603×385 cover, on core 0, while the first
  Track was starting. It watches `mini_icecast_lag_ms()` and backs off, but it
  neither parks the card nor bounds the work. It is now lazy: tags on the Peek,
  the cover on Real, with the loader remembering its answer. Boot makes no
  artwork card reads.

## The policy that came out of it

A Track above `MINI_ICECAST_MAX_SOURCE_HZ` (96 kHz by default) is skipped at the
start of the Playlist walk, with a log naming the rate and the budget. The Track
stays in the Library and its original bytes stay downloadable. The alternative —
letting one 24/192 file starve every listener for four minutes — is worse than a
named refusal. Transcoding the file to 44.1/16 or 96 kHz on the card is the way to
hear it.

After both changes, the common case was measured again: a 16-bit/44.1 kHz FLAC at
**101.3 %** of real time (32.4 KB/s against 32.0), zero starvation rebases,
encoder 61.1 %, decode 14.6 % of wall (SD 12.6 s), resample 0 (bypass), tone 0.

## What was tried and rejected

A prefetch task (`ice_read`) filled a 384 KB byte ring from the file while the
decoder task consumed it, on the theory that the blocking SPI read would overlap
the decode. It did not: throughput fell to 68 %, the reader and decoder summed
their wall time, IDLE0 starved and the task watchdog fired. The experiment is
recorded here so it is not re-suggested without a different card path (4-bit
SDMMC, which this board does not wire, or a higher SPI clock that the wiring and
card would have to tolerate).

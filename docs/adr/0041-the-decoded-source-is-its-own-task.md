# The decoded source is its own task, and the encoder core stops paying for the card

The broadcast task used to do everything in one loop: read the card, decode at the
file's rate, resample to the Station rate, apply the tone, encode one frame, and
pace it. That is a shallow arrangement in the way that matters here — the stages
have different bottlenecks, but they share one interface: the loop's own body. A
blocking SPI read, a FLAC decode, a resampler tap loop and the MP3 encoder all
serialised on core 1, and the encoder's 26 ms budget paid for every one of them.

Measured on the board with a 24-bit / 192 kHz FLAC (173 MB, ~708 KB/s, first in the
Playlist): the stream carried 41.4 % of real time, the decoder alone was 38.5 % of
the wall with 25.9 % of it blocked on the card, and the broadcast rebased its clock
69 times in two minutes. MP3 sources fit; this FLAC did not. See
[`audio-decode-cost.md`](../research/audio-decode-cost.md) for the full table.

## Decision

The decoded source is a **module with its own task**:

- `ice_decode`, pinned to core 0 at priority 3, owns the `mi_dec_t`, opens the
  Track, reads the card and decodes at the file's own rate. It fills a 384 KB ring
  of interleaved stereo frames (PSRAM; half a second at 192 kHz, two seconds at
  44.1 kHz).
- `ice_broadcast` on core 1 pulls frames from that ring, resamples to the Station
  rate, applies the tone, encodes and paces. It no longer opens a decoder, and its
  stack shrank from 24 KB to 16 KB.
- The **Track lifecycle stays in `stream_file`/`broadcast_task`**: skip, previous,
  `play_pending`, the card park, the idle bed and the end-of-track flush are the
  broadcast's, because they are policy about what plays, not about how a file is
  decoded. The source is per Track: it is created when the Track starts and joined
  when it stops.
- `mi_decode`'s public interface did not change. The seam is the ring, not the
  decoder: the decoder still offers `open/read/hz/duration/close`.
- The task's stack is external (32 KB, PSRAM) because `drflac_open()` keeps 16 KB
  of stream state on the caller's stack and internal RAM is the scarce class here.
  The broadcast stack stays internal and small.
- The broadcast task's own stage costs are published in the 60-second telemetry —
  `decode X ms (of which SD Y), resample Z ms, tone W ms` — next to the encoder's
  existing `us/frame`. The board's log is the only place that split is visible.

## Considered options

- **Keep one task.** Rejected: that is the arrangement measured above.
- **Move the encoder off core 1 and keep the source there.** The screen and Wi-Fi
  are already on core 0; moving the encoder there only moves the collision.
- **Prefetch the file on a second reader task.** Tried and rejected. The SDSPI
  transfer at 20 MHz is not a free DMA engine: the reader and decoder added their
  wall time instead of overlapping, the stream fell to 68 %, IDLE0 starved and the
  task watchdog fired. Recorded in the research note so it is not re-suggested
  without a different card path.
- **Lower the Station rate to 32 kHz / 96 kbps.** It would fit — the appliance's
  own fallback table says so — but it trades the station's quality for every Track
  to accommodate one source. That is a product decision, left out of this ADR.

## Consequences

- The 24/192 Track improves from 41 % to 65–88 % of real time depending on what the
  screen and the artwork loader are doing, but it still does not reach 100 %: the
  source alone needs ~0.89 of core 0, which the screen, Wi-Fi and lwIP also use.
  The next decision is a source-rate policy at the Library scan, not more threading.
- The artwork loader is a second card user at boot: on this Track it read and
  decoded a 603×385 cover for 29.5–38 s on core 0, which is why the first Track is
  the worst one. It needs a budget or a park; it is not part of this ADR.
- The broadcast no longer opens a decoder, so a failed source allocation skips the
  Track with a log line instead of falling back to decoding on the broadcast task.
  The 32 KB source stack exists because a 16 KB broadcast stack could not hold
  `drflac_open()`'s stream state; a same-task fallback would overflow it.
- The source task owns its external stack and deletes itself after signalling
  `done`; the broadcast never deletes it. The first version had the broadcast call
  `vTaskDeleteWithCaps` on the source handle, and that race crashed the device at
  the first Track boundary (IllegalInstruction, PC in flash, A1 on the source
  task's PSRAM stack). The boundary is now the regression check: play from one
  Track into the next.
- A 16-bit / 44.1 kHz FLAC measured **101.3 %** of real time across a 45-second
  window after the fix (32.4 KB/s against 32.0 KB/s), with no gap and zero
  starvation rebases. That is the common case.
- The 24/192 Track is now refused by a source-rate budget:
  `MINI_ICECAST_MAX_SOURCE_HZ` (96 kHz by default) is checked as soon as the
  decoder reports the Track's rate, before it reaches the encoder. The skip is
  named in the log, the Track stays in the Library, and its original bytes stay
  downloadable. This is a policy at the Playlist walk, not a second decoder.
- The artwork loader became lazy with the same change: tags are read when the
  Peek is raised, the cover when Real is opened, and the loader remembers its
  answer so reopening a level does not read the card again. Boot now makes no
  artwork card reads; the 29–38 s cover load that used to sit on the first
  Track's card is gone.
- Both new stages are testable on the host: the unit and integration suites drive
  the same `stream_file` with host pthreads and the shim's semaphores.
- The decoder's own change is separate and smaller: stereo decodes straight into
  the caller's buffer (no native-channel scratch), and the two hot translation
  units are built `-O3`.

**Status:** accepted and measured on the Sunton, 2026-10-29.

# 19 — A download must not starve the broadcast, or drop the listener

**What to build:** Under the documented T9b load — one listener on `/stream` while the
largest track is downloaded repeatedly — the Sunton board showed two distinct faults:

1. **The broadcast starved.** With a fixed 1 ms yield between download chunks the
   starvation was reduced but not eliminated (52 rebases in 12 minutes, then 2 in one
   5-minute run, then 16 in the next). The contention arrives in bursts, not as a steady
   share of the bus.
2. **The listener was dropped.** The stream client connected and was gone 10–30 s later,
   because lwIP's default send buffer (~5.7 KB) fills in 180 ms at 32 KB/s when a download
   shares the radio, and the 400 ms send timeout then drops the client. Tried and rejected:
   `SO_SNDBUF` is not available in this build (`Protocol not available`), and raising the
   stall timeout would stall the audio task, which is the thing being protected.

Fault 1 is partly fixed: the download now starts gently, has a steady ~1.3 MB/s ceiling,
and backs off for two seconds whenever `mini_icecast_lag_ms()` reports the broadcast behind.
Fault 2 is fixed: the broadcast no longer writes a socket at all. Each client has a 10-second
send ring in PSRAM and a permanent sender task; the audio task queues framed bytes and moves
on. On the Sunton a listener now survives the whole 10-minute download soak, where it used to
be dropped in 10-30 s.

The residual is fault 1's tail: the same soak still reports 17 starvation rebases (bursty,
around 2-4 minutes in), so the broadcast occasionally falls more than a second behind under
the worst-case download. The connection and the audio survive; the pacing clock rebases.

**Blocked by:** none

**Status:** open (listener stability fixed and soaked; residual starvation under worst-case downloads)

- [x] `mini_icecast_lag_ms()` reports the broadcast's lag, lock-free.
- [x] The download starts with a 1 s slow phase and backs off for 2 s after a lag event.
- [x] `board_soak.sh` waits for the device before starting load (it used to race the boot
      and silently turn a download soak into a stream-only one).
- [x] 10-minute soak with repeated downloads: encoder 62.3 %, **0 starvation rebases**,
      0 faults, internal free stable.
- [x] Release the download lock as soon as the file's known length has been sent. The
      backoff's sleep ran after the last chunk too, so a client that asked again the moment
      it had its last byte got a 503 ("a download is already running"); the host suite
      caught this as 15 failures and now passes 92/0.
- [x] 5-minute soak of the final build: encoder 61.1 %, 0 starvation rebases, 0 faults.
- [x] A listener survives a download sharing the same Wi-Fi: connected at 12 s and still
      connected at the end of a 10-minute soak with repeated 41 MB downloads.
- [x] Per-client sender task and ring buffer, so the audio task enqueues encoded bytes and
      never blocks on a socket. `mi_ring` is in PSRAM; the ring holds 10 s of audio at the
      station rate; the sender's timeout matches.
- [x] The download is paced (~1.3 MB/s) rather than bursty, on top of the lag backoff.
- [ ] The same soak still reports 17 starvation rebases (bursty). Reduce them: candidates
      are a decoded-audio read-ahead buffer so an SD stall does not immediately starve the
      encoder, a slower download ceiling, or the documented lower station rate.

**Files:** `components/portable/mini_icecast/mini_icecast.c` (lag, client send),
`components/portable/mini_files/mini_files.c` (download backoff), `host/tests/board_soak.sh`.

**Evidence:** the board logs and soak scripts are transient; the numbers above are from
the 2026-10-18 runs on the Sunton (192.168.0.44), station 44.1 kHz / 256 kbps.

**Source:** board check of the architecture-review changes.

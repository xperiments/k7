# 05 — The player's stream client and its ring

**What to build:** `GET /stream` against the K7's actual response — `HTTP/1.0 200 OK`,
`audio/mpeg`, `Connection: close`, no length, no chunking — MP3 frame extraction from a body whose
chunk boundaries are arbitrary, the burst on connect, a fixed ring that discards instead of
growing, and the stall and reconnect rules.

**Blocked by:** 03 — `player/`, and a host harness.

**Status:** done — host tier, and the player streaming from the K7 on the board.

- [x] The client accepts the close-delimited response and treats the close as the end of the stream.
- [x] Frames are extracted correctly across arbitrary chunk boundaries, including a frame split over three reads.
- [x] The ring's overrun discards to the newest frames and its depth never grows; an underrun returns "empty" rather than blocking the socket.
- [x] Ten seconds without bytes ends the connection; reconnection is 1 s, then 5 s, for ever.
- [x] A body that is not MPEG audio is a *Fault*, not a retry loop.
- [x] The buffer depth is a named board number with its measured value written beside it.

Spec: `docs/specs/k7player.md` R23–R30, T1–T3; the stream's framing is `components/portable/mini_icecast/mini_icecast.c:390`.

## Comments

- **Four portable pieces and one seam:** `player_http` (the head, across reads), `player_mp3` (whole
  frames out of arbitrary boundaries, and the rate and bit rate read from the first header),
  `player_ring` (eight slots, overrun drops the oldest), `player_stream` (connect, head, body,
  10 s stall, 1 s then 5 s backoff, fault on a non-MPEG body), and `player_board_stream_*` for the
  socket (lwIP on the board, scripted on the host).
- **The board found the bug that mattered, and it was a rate.** The stream task read one 512-byte
  buffer per 50 ms tick — 10 KB/s against a 32 KB/s station. It parsed the head and then fell
  behind for ever: 13 frames a second instead of 38. The fix drains the socket each tick (bounded
  at 32 reads), and `t03_stream`'s burst case scripts 512-byte reads so the shape is in the test.
  After it, the board reads **38.2 frames/s** — 44 100 / 1152 to three digits.
- **The test's own bugs were worth having too.** A `build_response(…, audio)` parameter that
  actually meant *noise* wrote 5 000 bytes into a 4 096-byte buffer and crashed the test with
  `__chk_fail_overflow`; AddressSanitizer named the line in one run. The fake also had to learn
  to reset between cases and to keep a consumed script consumed — a reconnect that replays a
  stream is not a stream.
- **Evidence, host:** `t03_stream` — **87 checks, 0 failures** (a head split byte by byte, frames
  across arbitrary boundaries, a burst that needs several reads a tick, a stall at ten seconds, the
  1 s/5 s schedule, a body of noise refused as a fault, and the ring's overrun keeping the newest);
  `t02_net` 64; `t01_status` 15. Both layers, both firmwares and the HTTP acceptance are green.
- **Evidence, board:** the console while the K7 was broadcasting —
  ```
  net: joined xperiments-2.4G at 192.168.0.24
  stream: streaming 192.168.0.13: 44100 Hz, 256 kbps, 100 frames, ring 8/8, dropped 92
  ...
  stream: streaming 192.168.0.13: 44100 Hz, 256 kbps, 451 frames, ring 8/8, dropped 443
  ```
  The rate and bit rate came from the frame headers, matching the K7's station format; 100 → 451
  frames over 9.2 s is **38.2 frames/s**. The ring sits full and drops one frame per frame because
  nothing consumes it yet — that is PL-06, and it is what "discards to the newest" looks like
  before there is an output.
- **The depth is a number, not a measurement:** eight frames is about 209 ms at 44.1 kHz, chosen
  so a burst is not lost whole. PL-10's B4 is what replaces it with a measured value.
- **Firmware:** `k7player.bin` is `0xcfce0` in the 1 MB app partition, 19 % free.
- **`make -C host check`'s `label` step still fails in this environment**; everything before it
  passes.

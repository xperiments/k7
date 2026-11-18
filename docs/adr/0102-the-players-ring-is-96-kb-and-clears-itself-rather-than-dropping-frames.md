# The player's ring is 96 KB and clears itself rather than dropping frames

R28 gave the player one fixed ring of sixteen frames — 418 ms at 44.1 kHz — with the policy that an
overrun "discards to the newest frames rather than buffering them, so a slow moment costs a glitch
and never a growing delay". The depth was set on the board while Bluetooth was compiled in: eight
slots then, sixteen once the radio was out ([ADR 0100](./0100-the-player-ships-with-the-line-output-and-the-speaker-waits.md)).

That trade was made for a stream that was assumed to be steady with occasional hiccups. The
measurement on 2026-11-18 says otherwise, and the two ends of it are worth separating:

- **The K7's stream is honest.** Four captures from a 5 GHz client beside the player: 32 011 B/s,
  which is exactly the 256 kbps station format; a mean inter-frame gap of 52 ms and a worst of
  263 ms; no MPEG frame boundaries missing over 20 to 90 seconds; and the same numbers *while the
  player was receiving nothing at all*.
- **The 2.4 GHz link to the player is not.** RSSI −51 dBm, so not coverage, with power save off
  (`WIFI_PS_NONE`): the downlink stops dead for one to nine seconds at a time — 11 outages in a
  149 s session, 24 % of the time, longest 9 s; 5 in 120 s, 6 %, longest 3 s; and one 90 s session
  with none at all, which is the "time to time" the listener reports.
- **The DAC was cleared before any of this.** The I2S DMA's own sent-events counted 89 088 frames
  in 2.019 s — 44 116 Hz against the 44 100 asked for — and every write was taken whole, so the
  line was never the suspect.

Against those outages the 418 ms ring behaves in exactly the two ways the listener hears. The ring
empties and the playout writes silence: 4 549 underruns and 21 s of silence in one session. Then
the link recovers, the K7 sends the backlog it has been holding — it keeps 10 s per client
(`CLIENT_RING_MS`), and its own comment says why — and the ring, which is a quarter of a second
deep, discards the oldest frame on *every push* while that backlog drains: 573 frames, 15 s of
music, thrown away in 120 s. One outage becomes hundreds of small jumps in the music, which is the
"acceleration".

## Decision

The ring is a **byte FIFO of 96 KB**: whole frames behind a two-byte length, about six seconds of
the station at the 128 kbps it runs (and three at the 256 kbps it ran while this was measured), with
the frame sizes it actually sends (417 bytes at 128, not the 1 441-byte worst case a fixed slot had
to reserve). 96 KB is the largest depth that still leaves the Wi-Fi its heap on
this chip: the census with the stream up reads 85 KB internal free and 43 KB largest, where 128 KB
of ring would leave 53 KB — under the 51 KB of dynamic RX buffers alone.

An overrun **clears the ring once** and keeps the newest audio. That is one discontinuity per
outage that outlasts the ring, not one per frame; the socket still never blocks, and the ring still
never grows. The playout takes at most sixteen frames per tick, so a two-second backlog cannot hold
the player's highest-priority task for the two seconds it would take to write.

The output **primes**: it writes silence until the ring holds 4.5 s of the station's audio (or the
ring's whole room, when 4.5 s is more than it can hold), and waits one second again after it has
run dry — the listener has already sat through the outage by then, and what follows is the backlog,
not a stream that has to be re-established.

The depth of the prime *is* the depth of the buffer, and that was the part this ADR first got
wrong: primed to 1.5 s, the ring never grew past 38 KB of its 96 KB and a three-second outage ran
it dry, because a station feeding at the rate the output plays never fills a ring on its own. The
prime is what fills it, so it is 4.5 s and it costs 4.5 s of silence before the first sound. The board showed why this is not optional — once the ring was
deep but nothing filled it, it sat at 27 KB of its 64 KB, because the K7's feed is realtime and a
realtime feed never fills a buffer on its own. Priming is bounded by the connection: an ended body
is played, not waited for.

## Considered options

- **More slots of the same size.** Six seconds at 128 kbps is 235 slots of 1 441 bytes: 338 KB,
  and most of that is air because the station's frames are 417 bytes. The byte ring holds the same
  audio in 96 KB.
- **Backpressure: stop reading the socket and let the K7's 10 s ring hold the backlog.** Nothing is
  discarded at the player, but the player never catches up either — every outage it survives moves
  it further behind live, the latency grows without bound, and the K7's ring eventually overflows
  and discards in the same per-push shape, one layer down. The listener would hear the same jumps
  later, with a growing delay before them.
- **Drop half the ring on overrun** instead of all of it: the same number of discontinuities during
  a long burst, with one more number to tune and a depth that no longer means anything.
- **Leave the policy and only grow to one second.** It halves the damage and leaves the mechanism,
  which is the part that turns a nine-second outage into hundreds of jumps.
- **Prime from the K7's side instead: raise the connect burst (R25) from 500 ms to 1.5 s.** It fills
  the ring at connect with no silence at all, which is genuinely nicer — but it does nothing after
  an outage, and an outage is what empties the ring. Keeping both would be two numbers for one
  budget. The player owns the buffer because the player is what knows when it is safe to play.

## Consequences

- The common outage is now inaudible: a one-second gap costs two seconds of latency and no audio,
  where it used to cost a second of silence and a second of skipping.
- An outage longer than the ring costs **one click and one jump to live** — the player keeps the
  newest audio, which is the right choice for a live broadcast.
- 96 KB of DRAM instead of 23 KB: 85 KB internal free and 43 KB largest with the stream, the
  decoder, the ring and the Wi-Fi up (B5).
- The station's bit rate is now part of the picture, and it moved: 44100 Hz / 128 kbps instead of
  256 in `appliance.c`. At a fixed ring it doubles the seconds of audio the player can hold, and it
  halves what a recovery has to carry. It costs the SNR the appliance had bought — 11.9 dB at
  256 kbps against 3.3 at 128 on pink noise at −10 dBFS — which is why the station ran at 256 in
  the first place. On a 2.4 GHz link that loses the stream for one to nine seconds, the seconds of
  buffer are worth more than the decibels; on a link that is not the constraint, 256 is one line
  away.
- The log line becomes `ring N/64 KB, flushed F, lost B`, and T3 pins the policy on the host: two
  seconds of the station's frames fit without a flush, one more clears the ring exactly once, and
  ten ringfuls cost at most ten flushes.
- Board evidence, the same report before and after, with the station at 128 kbps and the player
  priming its ring:

  | | before: 16 slots, drop per frame (120 s) | after: 96 KB, prime 4.5 s (205 s) |
  |---|---|---|
  | outages | 5, 6 % of the time, longest 3 s | 4, 3 % of the time, longest 2 s |
  | ring empty | 14 s | **0 s** |
  | audio discarded | 15.0 s | **0.0 s** |
  | discontinuities | 573 frame drops | **0** |
  | underruns | 1 496 | **1** |

  The buffer is only as deep as the prime, and the run that proved it is worth keeping: primed to
  1.5 s, the ring held 38 KB of its 96 and a three-second outage emptied it; the same code primed
  to 4.5 s holds 78 KB — 4.8 s — and the one- and two-second outages cost nothing at all.
- An outage longer than the buffer still silences the player for the difference: a nine-second
  outage in an earlier run cost about four seconds of silence with nothing discarded, which is the
  outage outlasting the buffer rather than the player throwing audio away. Covering nine seconds at
  128 kbps is 144 KB, and the board's free heap after the ring is 85 KB.
- B5's census after it: 85 KB internal free and 43 KB largest with the stream, the decoder, the
  ring and the Wi-Fi up.
- R28 is a spec line, not an earlier ADR, so nothing is superseded. ADR 0100's decision — the line
  is the player's only output — stands.

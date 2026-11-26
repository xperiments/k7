# The tone on the player: where the K7's EQ should run when the radio carries the audio

**Status:** research · **Updated:** 2026-11-20
**Conclusion:** Move the tone to the player only if it is the player's room control; the win is latency, not CPU or codec quality.

**The question.** The K7's one audio effect is the DJ tone — GAIN, HI, MID and LOW on the right
bar. Today it runs on the S3, on the PCM chunk the encoder is about to take. When the K7's
transport for the player is ESP-NOW, should the player apply it instead, with the K7 sending the
four values over the radio "when needed"?

**Short answer.**

- **It fits.** `tone_eq` is already portable, uses no heap, and is 160 B of state. The player
  decodes the PCM at the point the tone would be applied.
- **The reason to do it is latency, not CPU.** Today the tone is baked into encoded frames that
  wait **4.5 s** in the player's ring before they are played. Applied at the player's output, the
  same tone is heard in tens of milliseconds. The knob's glide is unchanged.
- **The cost is one output.** A station has one encoder, so the tone is either in the broadcast
  or applied after it. Moving it makes the broadcast flat while ESP-NOW, so a browser listening
  at the same time hears the stream untinted. If the tone is the player's room control, that is
  the right shape; if it is a broadcast control for every listener, it is not.
- **CPU is not an argument either way.** The active path is about 1.4 % of a core (the code's own
  note) and the neutral path returns before touching a sample.
- **Compatibility is clean.** A 10-byte `0x06` tone packet, and one optional trailing byte in the
  player's existing status packet to say it can apply the tone. An old player keeps today's
  path, an old K7 keeps a new player neutral, and neither has to be reflashed in lockstep.

The recommendation is at the end: do it if the tone is the player's; keep it on the S3 if it is
the broadcast's. Nothing is measured on a board yet; the board test is in § 8.

---

## 1. Where the tone runs today

```
panel knob  cassette_eq.c:89-94
  -> mini_icecast_eq_set() / mini_icecast_gain_set()      mini_icecast.c:1903-1912
  -> S.eq_db / S.eq_gain_db, read once per 1024-frame chunk
broadcast task
  -> decode + resample
  -> tone_apply() = set targets, tone_eq_process()         mini_icecast.c:658-666
  -> measure_level()                                       mini_icecast.c:668-675
  -> encoder -> one whole MP3 frame per callback
  -> ESP-NOW emitter (fragments)                           board_esp32.c:927-1115
player
  -> radio callback -> ring                               player_stream.c:72-141
  -> prime 4.5 s                                          player_playout.h:24-36
  -> decode -> PCM -> line output                         player_playout.c:96-112
```

The tone is part of the **encoded** signal. Every listener hears it, and there is exactly one
encoder, which is why ADR 0018 could say the EQ is a broadcast tone control "and there is no
local audio path to fight with" ([ADR 0018](../adr/0018-dj-eq-knobs-and-transport-arrows.md),
the paragraph beginning "The output is the Icecast stream"). The player did not exist then.

The stream is smooth but not immediate: a knob moved at *t* is in frames that are behind a 4.5 s
prime, so the player's listener hears it at *t + 4.5 s*. The VU on the panel is measured after the
tone, so it moves at *t* — the panel reacts before the ear does.

## 2. What would move

```
panel knob
  -> app_services_tone_set()                    one owner of the four values
  -> mini_icecast's tone source says "not mine" -> broadcast is flat
  -> app_board_audio_tone()                     copy, mark dirty
  -> ESP-NOW worker                             send 0x06 on change and 1 s repeats
player
  -> radio callback -> player_tone_packet()     targets, mutex, parse/clamp
  -> playout: decode -> PCM -> player_tone_process() -> line output
```

The panel, the ranges, the module and the glide all stay. What changes is the **point of
application**: after the lossy codec and after the ring, rather than before both. The player
already has the decoded stereo PCM in `player_playout.c`, after the mono-to-stereo copy at
lines 103-109 and before the write at line 110, so the insertion is one call.

## 3. The latency, which is the point

The player's ring is primed to 4.5 s and stays that deep: at a steady state the K7 feeds at
exactly the rate the output plays ([ADR 0102](../adr/0102-the-players-ring-is-96-kb-and-clears-itself-rather-than-dropping-frames.md),
lines 33-53; [k7player.md](../specs/k7player.md) R28). A tone change baked before the encoder is
behind that whole ring.

A tone applied at the output is not. The control path is:

| Step | Cost |
|---|---|
| panel → `app_services` → dirty | a copy, the UI task |
| worker wake and `esp_now_send` | the next status (≤ 100 ms) or audio frame (≤ 26 ms); usually the same pass |
| radio → player callback → targets | one ESP-NOW frame, milliseconds |
| next decoded frame applies the new targets | ≤ 26 ms, plus the tone's own 2 dB-per-frame glide (~0.3 s for a full move) |

So the first audible difference is **tens of milliseconds**, then the same glide the S3 has. The
audio being filtered is at the head of a 4.5 s ring; the **change** is not.

This is the one product-visible improvement the move buys, and it is the one a board test can
show: turn GAIN and listen. It does not make the stream itself lower latency.

## 4. The cost: one broadcast, two outputs

The station has one encoder. A tone is either in the encoded signal for every listener, or it is
applied by each listener after decoding. It cannot be both.

So while the transport is ESP-NOW:

- the player hears its own tone, immediately;
- a browser listening to `GET /stream` at the same time hears the stream **flat**, because the
  broadcast is neutral;
- the K7's VU, measured on the broadcast, shows the source level and no longer moves with the
  tone knobs.

That is a product decision, not a defect. ADR 0107 says the intended shape is one browser plus
the player; the player is the output the transport setting is about, and the browser is a second
output. If the tone is a room control for the player, untinted browser audio is arguably correct
— the browser listener is usually on headphones, not in the tuned room. If the tone is the
broadcast's DJ control, moving it changes what the product is.

The third possibility is to apply the same values in the browser with Web Audio, fed by a small
API that exposes the four numbers. That keeps both outputs tinted and is a separate piece of
work: the web app would have to build a `BiquadFilterNode` chain and handle the stream's
cross-origin rules. It is noted here so the browser's flatness is a known consequence rather than
a surprise; it is not part of this move.

## 5. The wire

### 5.1 The tone packet

Type `0x06`, 10 bytes, the same version byte as the handover and the audio:

| offset | size | field |
|---|---|---|
| 0 | 1 | version, `1` |
| 1 | 1 | type, `0x06` |
| 2 | 2 | GAIN, big-endian int16, centi-dB |
| 4 | 2 | LOW, big-endian int16, centi-dB |
| 6 | 2 | MID, big-endian int16, centi-dB |
| 8 | 2 | HIGH, big-endian int16, centi-dB |

The ranges are the knobs': bands −2600..+600 (the DJ cut is deeper than the boost), GAIN
−1200..+1200. Sender and receiver clamp; a value outside the range is a sender bug, not a reason
to drop the packet. Centi-dB because the wire is integers and the knobs' float dB does not need
more than 0.01 dB.

It is **idempotent and absolute**: every packet is the whole state, not a delta. The sender sends
on a change and repeats once a second while the emitter is on and the player is listening, so a
lost packet is corrected in at most a second and a duplicate does nothing. No acknowledgement and
no revision in the first version: the repeat is the repair, and ESP-NOW is one hop. A revision is
the fallback if the bench ever shows a stale packet arriving after a newer one.

**The sender does not send from the radio's receive callback.** The board already has one worker
that owns every send, exactly because a send that blocks in the Wi-Fi task once froze the K7 for
hours ([espnow-stream.md](../specs/espnow-stream.md) E11, EN-10). The tone is queued and sent by
that same worker, after the frames and the status in its pass.

### 5.2 The capability flag

The player already sends a status packet every 100 ms (E3). Append **one optional flags byte**
after the missing-sequence list:

| offset | size | field |
|---|---|---|
| `8 + 2 * nack_count` | 1 | flags: bit 0 = "this player applies the K7's tone" |

This is backward compatible in both directions because the existing parser reads the sequence
list by its declared length and tolerates a longer packet
([`es_status_parse`](../../components/portable/espnow_stream/espnow_stream.c), lines 72-84): an old
K7 ignores the byte, and a new K7 reads flags `0` from an old player's shorter packet. The old
player never sets the bit, so the new K7 never sends it `0x06` and keeps applying the tone on the
S3 — today's behaviour, unchanged.

The compatibility matrix:

| K7 | player | tone runs on | knob latency | browser while ESP-NOW |
|---|---|---|---|---|
| old | old | S3 | 4.5 s | tinted |
| old | new | S3 | 4.5 s | tinted |
| new | old | S3 | 4.5 s | tinted |
| new | new | player | tens of ms | flat |

Only the last row is the new path, and it is the only one where the broadcast needs to be flat.
No capability negotiation message is needed; the status already carries it.

### 5.3 The transition

When the status first carries the flag, the K7 starts bypassing the broadcast tone and sends the
current values. Two details are worth stating:

- The player defaults to neutral until a tone packet arrives, so a new player beside an old K7
  adds nothing to the K7's tone. This is what makes the new-player/old-K7 row work.
- At boot the panel's values are 0 dB, so the normal first transition is inaudible. The one case
  where the S3 was tinting and the player then applies the same values is a fast HTTP ↔ ESP-NOW
  toggle with a non-zero tone: the player's ring can still hold frames the S3 tinted, and for up
  to the ring's 4.5 s the player applies the same tone on top. It is a rare edge; flushing the
  player's ring would replace it with a click and a 4.5 s silence, which is worse. It is noted,
  not solved.

## 6. The seams

The change is one new owner and two small seams, not a second tone implementation.

| Where | What it becomes |
|---|---|
| `app_services` | Owns the four values (the panel's target). One interface for the screen: set a band, set the trim. Routes them to the board always, and to the broadcast only when the broadcast owns the tone. |
| `mini_icecast` | Stops owning the values. Gains a tone **source** hook: the composition returns the four values, or says "the player owns this one" and the station skips the stage. The existing `mini_icecast_eq_set()` / `mini_icecast_gain_set()` retire; the tone module and the broadcast's processing point stay. |
| `app_board` | `app_board_audio_tone(values)` copies and marks dirty; `app_board_player_tone_capable()` is what the source hook asks. The emitter worker sends `0x06`; the status parser sets the capability from the flag. |
| `espnow_stream` | `0x06` encode/parse and the status flags byte. Both firmwares already compile this module. |
| `player_tone` | New, small: parse `0x06`, hold the targets under a short lock, `tone_eq` at the station rate, `process()` the decoded PCM. The interface is `packet`, `set_rate`, `process` — the parse, clamp, mutex, rate change and neutral case are behind it. |
| `player_playout` | One call after the mono-to-stereo copy and before `player_output_write()`; one `set_rate` in `follow_rate()`. |
| `player_board` | One more dispatcher case: `0x06` to the tone callback, as `0x04` already goes to the stream. The host fake gets the setter; the host tests call `player_tone_packet()` directly. |
| `cassette_eq` | Calls `app_services` instead of `mini_icecast`. No knob, range, gesture or VU change. |

`tone_eq` is already in the host build and is shared portable code; the player's CMake gains it in
its `EXTRA_COMPONENT_DIRS` and `REQUIRES`, and the player's host Makefile compiles it. The
layering check's P5 rule (the player names no K7 component) is unaffected.

## 7. Failure and limits

- **A lost tone packet** is stale for at most the next repeat, 1 s. A knob drag sends on each
  change; a loss mid-drag is corrected by the next change or the repeat.
- **A player reboot** leaves the player neutral until the next repeat (≤ 1 s). The K7's worker
  runs on the player's status packets even when the station is stopped, so the repeat does not
  depend on audio flowing.
- **A K7 reboot** resets the panel to 0 dB and the player to neutral, exactly as the S3 resets
  today. The values are not persisted on either board; nothing new to persist.
- **The player's output sits at the head of a 4.5 s ring**, so the tone applies to audio that
  entered the ring 4.5 s earlier. That is the ring's latency, not the tone's; the change is heard
  when it is made.
- **CPU:** 160 B of state, three biquads per channel, no allocation. The neutral path returns
  before touching a sample; the active path is the same code the S3 runs today, whose comment
  puts it at about 1.4 % of a core. The player's decode is the heavier stage. The board test
  measures it; the host cannot.
- **The VU** is the broadcast's level. With the player owning the tone it does not show the
  player's boost or cut. Reporting the player's post-tone level back would be a second telemetry
  packet and a new status field; not in the first version.
- **A third output** (the browser) is untinted while ESP-NOW. It can be tinted later in the web
  app, from the same four values, if the API exposes them.

## 8. What it would take

**Decision and specs.** An ADR that amends ADR 0018's broadcast-tone premise: the tone belongs to
the output that makes the sound, so when ESP-NOW carries the player's audio the player applies it
and the broadcast is flat; the block's geometry, ranges and gestures stand. The next free ADR
number (0108 is the last at the time of writing). `espnow-stream.md` gains E12 (the tone packet)
and the flags row in E3; `k7player.md` gains the player's "apply after decode" requirement;
`CONTEXT.md`'s live-stream gloss says the track is shared, the tone is the output's; the README's
tone paragraph and the tree line change.

**Host tests.**

| # | Test | Tier |
|---|---|---|
| T-T1 | `0x06` round-trips four values, clamps out-of-range ones, and refuses a short or wrong-version packet | host |
| T-T2 | A status with the flags byte parses the capability; a status without it parses flags 0; a malformed list is still refused | host |
| T-T3 | A tone packet sets the player's targets; DC is boosted by LOW +6 and cut by LOW −26 after the glide; GAIN +6 about doubles a sample; no packet is neutral | host |
| T-T4 | A tone packet delivered before the output opens is held and applied when it opens; a rate change rebuilds the filters and keeps the targets | host |
| T-T5 | The playout applies the tone to decoded audio: the same fixture at GAIN +6 has a higher peak than at 0 dB | host |

**Board tests** (the host cannot hear the radio or the DAC):

| # | Test | Tier |
|---|---|---|
| B-T1 | A new K7 and a new player: GAIN moves the player's level immediately, not 4.5 s later; the serial lines name the packet and the values | **board only** |
| B-T2 | The same K7 with an old player: no flag, the tone stays on the S3, the knob is late, the VU moves — today's behaviour | **board only** |
| B-T3 | A browser listens while the player is on: the player is tinted, the browser is flat; switching the transport to HTTP tints the browser and silences the player | **board only** |
| B-T4 | A player reboot: the tone returns within a second without touching a knob | **board only** |
| B-T5 | Ten station restarts with the tone set: the tone survives on the player, the emitter and the heap behave as EN-08 measured | **board only** |

`make -C host check` and `make -C player/host check` for the first two; `./tools/build.sh sunton
build` and `./player/build.sh build` for the two firmwares. The board evidence is a flash of both
boards, not a host claim.

## 9. Recommendation, and what would make it wrong

**Do it if the tone is the player's room control.** Then the latency win is real and the browser's
flat stream is the secondary output's correct behaviour. The packet is small, the capability flag
makes the old pair a no-op, and the DSP already exists.

**Do not do it if the tone is the broadcast's.** A DJ tone control that changes what every
listener hears is the thing ADR 0018 decided; moving it makes the browser and the player hear
different products, and the VU stops telling the truth about the player. The same work would be
better spent on the encoder or on the player's output, not on moving a 1.4 % stage.

**Do not do it for CPU or codec quality alone.** The CPU is already negligible, and applying the
tone after a lossy decode is not self-evidently better than letting the encoder see the tone: a
boost after decode amplifies the codec's noise in that band, while a boost before encode lets the
encoder spend bits on it. The honest claim is "the tone is no longer encoded", not "the sound is
better".

The open questions, in the order they decide the work:

1. **Is the tone the player's or the broadcast's?** This is the decision; everything else follows.
2. **Is a flat browser acceptable while ESP-NOW, or is Web Audio a required follow-up?**
3. **Should the VU stay the broadcast's level, or should the player report its post-tone level?**
   The first version recommends the former.
4. **Is the optional status flag worth one byte and a compatibility test?** It is what keeps an
   un-reflashed player working; the recommendation is yes.
5. **Repeat or revision for the tone packet?** Repeat, until the bench shows reordering.

## Sources

The repository, read for this note; no code was changed.

- [`components/portable/mini_icecast/mini_icecast.c`](../../components/portable/mini_icecast/mini_icecast.c) — `tone_apply()` 658-666, `measure_level()` 668-675, `tone_eq_init()` 1453, the setters 1903-1912.
- [`components/app/screen_display/cassette_eq.c`](../../components/app/screen_display/cassette_eq.c) — the four knobs and `knob_send()` 89-94.
- [`components/portable/tone_eq/tone_eq.c`](../../components/portable/tone_eq/tone_eq.c) — the 1.4 % note and the neutral bypass, 180-200; `sizeof(tone_eq_t)` is 160 B.
- [`components/portable/espnow_stream/include/espnow_stream.h`](../../components/portable/espnow_stream/include/espnow_stream.h) — the version/type bytes 22-25; `espnow_stream.c` — `es_status_encode/parse()` 50-84, `es_emitter_status()` 142-160.
- [`components/app/board_esp32/board_esp32.c`](../../components/app/board_esp32/board_esp32.c) — the emitter 927-1115, the worker 1095-1125, the receive callback 1192-1205, the audio setter 1234-1281.
- [`components/portable/app_services/app_services.c`](../../components/portable/app_services/app_services.c) — the transport apply 243-276.
- [`player/components/player/player_playout.c`](../../player/components/player/player_playout.c) — the decode and mono copy 96-112, the prime 125-141; [`player_playout.h`](../../player/components/player/include/player_playout.h) — the 4.5 s prime 24-36.
- [`player/components/player/player_stream.c`](../../player/components/player/player_stream.c) — the receive path 72-141; [`player/components/board_player/board_player_esp32.c`](../../player/components/board_player/board_player_esp32.c) — the dispatcher 180-190.
- [`docs/adr/0018-dj-eq-knobs-and-transport-arrows.md`](../adr/0018-dj-eq-knobs-and-transport-arrows.md) — the broadcast-tone premise, the paragraph beginning "The output is the Icecast stream".
- [`docs/adr/0102-the-players-ring-is-96-kb-and-clears-itself-rather-than-dropping-frames.md`](../adr/0102-the-players-ring-is-96-kb-and-clears-itself-rather-than-dropping-frames.md) — the 96 KB ring and the 4.5 s prime, lines 33-53.
- [`docs/adr/0107-the-k7-carries-one-listener.md`](../adr/0107-the-k7-carries-one-listener.md) — one browser plus the player.
- [`docs/specs/espnow-stream.md`](../specs/espnow-stream.md) — E3 and E11; [`docs/specs/k7player.md`](../specs/k7player.md) — R23 and R28.
- [`README.md`](../../README.md) — the tone paragraph at line 771 and the tree line at 1023.

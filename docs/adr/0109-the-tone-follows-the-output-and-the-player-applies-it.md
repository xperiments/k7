# The tone follows the output, and the player applies it when it makes the sound

[ADR 0018](./0018-dj-eq-knobs-and-transport-arrows.md) made the DJ block's four knobs a
**broadcast tone control**: the EQ ran on the PCM before the encoder, so it changed what every
listener heard, and "there is no local audio path to fight with" (the paragraph beginning
"The output is the Icecast stream"). The player is that local audio path now. It decodes the
broadcast and feeds the line output, and its ring is primed to 4.5 s
([k7player.md](../specs/k7player.md) R28,
[`player_playout.h`](../../player/components/player/include/player_playout.h), lines 24-36).

A tone applied before the encoder is baked into frames that wait behind that ring, so a knob turn
is heard about **4.5 s** later. A tone applied at the player's output is heard at once: the player
already has the decoded PCM, and `tone_eq` is the same module on both boards. The knob's glide is
unchanged either way. [The research note](../research/player-tone.md) has the arithmetic, the
packet and the tests.

The tone is the one audio effect. It is 160 B of state, no heap, and the code puts its active path
at about 1.4 % of a core in its own note, with the neutral path returning before it touches a
sample. **CPU is not
the reason to move it.** The reason is that the output that makes the sound should be the one that
applies the tone.

The obstacle is that the station has **one encoder**. The tone is either in the broadcast for
every listener, or it is applied by each output after decoding; it cannot be both. With ESP-NOW
the player is the output, and the browser is a second one on the same broadcast. Under HTTP the
player is silent and the browser is the output. The rule has to follow that.

## Decision

- **The tone belongs to the output that makes the sound.** It is not a property of the encoded
  stream; it is the last stage of the output.
- **When the transport is ESP-NOW and the player says it can apply the tone**, the K7's broadcast
  is **flat** and the K7 sends the four values to the player over ESP-NOW
  ([`espnow-stream.md`](../specs/espnow-stream.md) E12). The player applies them to the decoded
  PCM after the ring and before the line output ([`k7player.md`](../specs/k7player.md) R33).
- **When the transport is HTTP, or the player has not said it can apply the tone**, the K7 applies
  the tone to the broadcast before the encoder, exactly as ADR 0018 decided. That is the browser's
  path, and an old player's path.
- **The wire is 10 bytes.** Type `0x06`, version `1`: GAIN, LOW, MID and HIGH as big-endian int16
  centi-dB, at the knobs' ranges (−1200..+1200 for GAIN, −2600..+600 for a band). It is the whole
  state, not a delta, and it is idempotent: the K7 sends it on every change and repeats it once a
  second while the emitter is on and the player is listening. There is no acknowledgement; the
  repeat is the repair. It goes through the emitter's worker, never the radio's receive callback
  (E11), and it does not count against the audio credit.
- **The player's capability is one optional trailing flags byte in the status packet** (E3a), bit
  0. It is additive: an old K7 reads the sequence list by its declared length and ignores the
  byte, and a new K7 reads flags 0 from an old player's shorter packet. An old player therefore
  never receives `0x06`, and a new player beside an old K7 starts neutral and adds nothing to the
  old K7's tone. The compatibility matrix:

  | K7 | player | tone runs on | knob heard | browser while ESP-NOW |
  |---|---|---|---|---|
  | old | old | S3 | 4.5 s | tinted |
  | old | new | S3 | 4.5 s | tinted |
  | new | old | S3 | 4.5 s | tinted |
  | new | new | player | at once | flat |

- **One owner, one control.** The K7's panel stays the only surface. `app_services` owns the four
  values and routes them to the board and to the broadcast; `mini_icecast` stops owning them and
  asks a tone source whether the broadcast owns the tone; the board's emitter worker sends the
  packet; the player's `player_tone` module parses it and runs `tone_eq` at the output.
- **The browser is deliberately untinted while the player owns the tone.** That is the consequence
  of one encoder, and it is the right one when the player is the output the transport setting is
  about. A per-listener tone in the browser is a later Web Audio option fed by the same four
  values; it is not in this change.
- **The VU stays the broadcast's level.** It is measured on the PCM after the tone stage, so it
  shows the source when the player owns the tone. Reporting the player's post-tone level back is
  not in this change.

## Considered options

- **Keep the tone on the S3 (ADR 0018's shape).** It needs no packet and no new owner, but it
  keeps the 4.5 s knob delay for the player — the thing that makes the knob feel broken — and the
  player already has the PCM. Rejected as the default; it remains the path for HTTP and for an old
  player.
- **Move the tone to the player always.** Under HTTP the player is silent and the browser is the
  only output, so the browser would lose the tone entirely. Rejected.
- **Two encoders, one flat for the player and one tinted for the browser.** It buys both outputs
  their own tone at the price of a second encoder, a second listener ring and a second broadcast
  for one pair of ears. ADR 0107 made one listener the shape; rejected.
- **Bypass on the transport alone, without the capability flag.** Simpler, but a new K7 with an
  old player would silently lose the tone: the K7 would go flat and the old player would ignore
  `0x06`. The optional status byte is one byte and two host cases; rejected as the cheaper-looking
  option that breaks the pair.
- **Web Audio in the browser now.** It would keep both outputs tinted, but it needs an API for the
  four values, a `BiquadFilterNode` chain and the stream's cross-origin path. It is a real option
  for a later change, not a prerequisite for the player's knob; deferred.
- **Flush or shorten the player's ring when a knob moves.** It would bring the change to the ring's
  head, but it skips audio and spends the outage budget the ring exists for. Rejected.
- **Keep the tone on the S3 and send the values only for display.** It has no latency win and adds
  a second copy of the values; rejected.

## Consequences

- **The player's knob is heard at once** (tens of milliseconds, then the same 2 dB-per-frame
  glide), instead of after the ring. That is the change's product win, and it is a board
  observation: host tests prove the arithmetic, not the ear.
- **The browser's HTTP stream is flat while ESP-NOW** and the VU no longer moves with the tone. A
  browser that needs its own tone gets Web Audio later; the App spec does not gain one here.
- **The pair is compatible in both directions.** A new player beside an old K7 is neutral and the
  old K7's tone is the one heard; an old player beside a new K7 keeps the S3 tone and the late
  knob. Neither board needs the other's new firmware to keep working.
- **Nothing is persisted.** The K7's panel starts at 0 dB and the player starts neutral; the K7's
  repeat restores the player's values within a second of a player reboot. A K7 reboot resets both,
  as it resets the S3 tone today.
- **A fast HTTP ↔ ESP-NOW toggle with a non-zero tone** can leave tinted frames in the player's
  ring while the player applies the same values, so for up to the ring's 4.5 s the tone is doubled.
  Flushing the ring would be worse (a click and a silence); it is noted, not solved.
- **The tone's active CPU moves to the player** (about 1.4 % of a core, with the neutral path
  free). The board census is the evidence, not the host.
- **The specs change in the same commit:** `espnow-stream.md` E3, E12 and E13; `k7player.md` R33;
  `sd-stream-server.md` §4.6; the stream server's glossary. The README's "before the encoder" line
  and the tree line follow with the implementation (TN-02 and TN-03), because they describe the
  firmware that exists, not the decision.
- **The player is still a client the K7 keeps no record of** (ADR 0098): the K7 remembers the
  peer only while the emitter is on, as it does for the audio, and sends the tone to that peer.
- The code seams are `app_services` (one owner), `mini_icecast` (a tone source), the board's
  emitter (send and capability), `player_tone` and one call in `player_playout`. The research note
  has the file-level map.

## Supersedes

[ADR 0018](./0018-dj-eq-knobs-and-transport-arrows.md)'s premise that the EQ is a **broadcast**
tone control, the paragraph beginning "The output is the Icecast stream". Its block, its sizes,
its ranges, its gestures and the `tone_eq` module stand, and the broadcast remains the tone's
home when the broadcast is the output. ADR 0018 gains an "Amended by" line that points here.

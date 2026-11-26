# The ESP-NOW audio stream

**Status:** accepted · **Updated:** 2026-11-25

The player's audio path when the K7 emits it over ESP-NOW instead of HTTP:
[ADR 0103](../adr/0103-the-players-audio-arrives-over-esp-now.md). The ring, the prime, the
playout and the light are unchanged and are `k7player.md` R26–R28's; this document is only the
transport.

Both boards are ESP-IDF 6.1 and both already speak ESP-NOW for the handover
([`network-handover.md`](./network-handover.md)), so the version byte and the type byte are shared:
`0x01` beacon, `0x02` request, `0x03` record, `0x04` audio, `0x05` status and `0x06` tone. The
player's update adds `0x07`–`0x0C` and is defined in [`player-update.md`](./player-update.md).

## 1. The frame the K7 sends

The broadcast already emits **one whole MP3 frame per callback** (`mi_enc_out_t`): 417 bytes at the
station's 128 kbps, 38.3 of them a second. That frame is the unit of the stream.

**E1. ESP-NOW carries 250 bytes.** `ESP_NOW_MAX_DATA_LEN` is 250 (v2 raises it to 1470, and v1
peers — the handover — must still receive what shares the radio). So a frame travels in fragments
of at most 244 bytes, with a six-byte header:

| offset | size | field |
|---|---|---|
| 0 | 1 | version, `1` |
| 1 | 1 | type, `0x04` |
| 2 | 2 | sequence, big-endian, per MP3 frame, wrapping at 65 536 |
| 4 | 1 | fragment index, from 0 |
| 5 | 1 | fragment count, 1..6 |
| 6 | 1..244 | payload |

A 417-byte frame is two packets; a 1 441-byte one is six.

**E2. The sequence counts frames, not packets**, so a gap in it is a lost frame and every fragment
of that frame carries it. The sender starts at 0 when the emitter starts and never reuses a number
inside a stream.

## 2. What the player sends back

**E3. The player sends a status packet every 100 ms**, and immediately after a gap is seen. It is
the only flow control the transport has, and it is also how the receiver asks for what it missed:

| offset | size | field |
|---|---|---|
| 0 | 1 | version, `1` |
| 1 | 1 | type, `0x05` |
| 2 | 3 | free bytes in the ring, big-endian |
| 5 | 2 | highest sequence received without a gap, big-endian |
| 7 | 1 | how many sequences follow, 0..8 |
| 8 | 2 each | the sequences it is missing, oldest first |
| 8 + 2 × count | 1 | flags, optional (absent means 0); bit 0 = the player applies the tone (E12) |

Eight missing frames is 209 ms of audio and the reorder window (§ 3) is wider than that; a receiver
that is missing more than eight is in a state a retransmission will not repair.

**E3a. The flags byte is optional and additive.** It follows the sequence list, and a receiver
reads the list by its declared length and ignores anything after it, so an old K7 accepts a new
player's longer packet. A new K7 reads no byte from an old player's shorter packet as flags `0`.
Bit 0 is "this player applies the K7's tone" (E12); the other bits are reserved and sent `0`.

## 3. Loss, and what it costs

**E4. A lost frame is asked for again, up to five times at 100 ms intervals, and then abandoned.**
The sender keeps the last **two seconds** of frames (77 of them, about 32 KB) and resends on
request. The receiver holds completed frames that are ahead of a gap for up to **one second**, so a
resend arriving a few milliseconds late still lands in order. After that second the gap is
abandoned: the held frames are released, the audio jumps over the missing frame, and the player
counts it. Abandoning is the last resort, not the policy: the ring's 4.5 s prime is what gives the
resend somewhere to land.

**E5. Nothing is retransmitted by the application twice for the same gap**, and a resent frame that
arrives after its gap was abandoned is discarded, not played out of order.

## 4. Pacing

**E6. The sender sends only what the receiver says it can hold.** It keeps `free - in flight` and
sends a frame only when that is at least the frame's length. Both ends run at the station's rate,
so this bites only when the ring is full — which is exactly when sending more would mean dropping
it at the receiver. A sender that has no status packet from the receiver yet sends nothing: the
player speaks first, once it has a ring to offer.

**E7. The receiver's ring never blocks the radio.** The status packet is a report; the radio's
receive callback copies a frame into the ring (or the pending queue) and returns. If the ring is
full, the frame is dropped there and counted, exactly as a full ring has always discarded the
newest audio.

## 5. Timing

**E8. Three seconds without an audio packet is a silent stream**, not a fault: the light says
*Ready*, the ring keeps draining, and there is nothing to reconnect. The player does not reboot,
does not retry and does not sweep — the handover gave it an address, and the sender is either
there or it is not. A packet at any time after that starts the stream again.
([`k7player.md`](./k7player.md) R29–R30.)

**E9. The emitter is a setting, never two at once, and it applies at once.** The way is a row in
the K7's Settings menu,
persisted in NVS, with the micro switch to become the same call (EN-05). The write happens on the
panel's task, and the board settles the panel once it has returned, so choosing a transport does
not leave the picture a band to one side (ADR 0055). With ESP-NOW chosen,
every encoded frame is fed to the emitter, resends are answered and the player's peer is
remembered. With HTTP chosen, nothing is fed, nothing is resent and the peer is forgotten — but the
radio stays up, because the share window borrows it. The player is **ESP-NOW only** (ADR 0105): on
HTTP it hears nothing and says *Ready*, while the web UI's own listener, a different client on the
same broadcast, keeps working — and it is the only one, because the K7 carries one listener
(ADR 0107). The station's format follows the transport — 128 kbps on ESP-NOW,
256 on HTTP — and the change restarts the station rather than the device, at once (ADR 0106). Sending the player's feed twice would spend twice the airtime for one
listener.

**E10. The radio stays up.** A stream cannot be a window: ESP-NOW is initialised once, at boot,
and stays up while the player's audio uses it, so the share window stops owning the radio and only
adds its beacon to it (`network-handover.md` H13). When the station emits over HTTP instead, the
radio keeps the window's old lifecycle and costs nothing outside it, and the Player list offers no
update there, because the player could not hear the K7 to prove one (player-update.md U8).

**E11. Nothing that can block runs in the radio's receive callback.** The callback copies the
packet — the player's status, or a share-window frame — into a queue and returns to the Wi-Fi
driver. One task of the board owns the emitter: it applies the credit, answers the NACKs, sends,
and runs the recovery (a re-association, and a restart as the last resort). The player's update is
a mode of that one task, never a second sender beside it (player-update.md U29). The station's
publish path never takes the emitter's lock either: the broadcast copies the frame into that queue and goes
back to encoding, and a full queue drops it, which the player's next gap names again. A send that
blocks costs the player frames; it must never cost the station, and it must never hold a lock
another task waits on (EN-10: a send from the callback froze the K7 for hours, with the radio and
the encoder down and the panel still painting).

## 6. The tone

**E12. The tone travels beside the audio when the player owns it.** The K7 sends the four values
as one 10-byte packet:

| offset | size | field |
|---|---|---|
| 0 | 1 | version, `1` |
| 1 | 1 | type, `0x06` |
| 2 | 2 | GAIN, big-endian int16, centi-dB, −1200..+1200 |
| 4 | 2 | LOW, big-endian int16, centi-dB, −2600..+600 |
| 6 | 2 | MID, big-endian int16, centi-dB, −2600..+600 |
| 8 | 2 | HIGH, big-endian int16, centi-dB, −2600..+600 |

The packet is one ESP-NOW frame: it is not fragmented and it does not count against the audio
credit. It is the whole state, not a delta, so it is idempotent. The K7 sends it on every change
and repeats it once a second while the emitter is on and the player is listening; there is no
acknowledgement, and the repeat is the repair. The sender is the emitter's worker, never the
radio's receive callback (E11). The receiver clamps an out-of-range value and ignores a packet
that is not version 1, type `0x06` and 10 bytes. The values are targets for the same `tone_eq` the
S3 uses ([ADR 0018](../adr/0018-dj-eq-knobs-and-transport-arrows.md)), applied after the MP3 decode
and before the output (`k7player.md` R33).

**E13. The player's flag chooses where the tone runs.** When the current peer's status carries the
tone flag (E3a), the K7 bypasses the broadcast's tone stage and sends E12; the encoded stream is
then flat, and a browser listening to the same broadcast hears it untinted
([`sd-stream-server.md`](./sd-stream-server.md) § 4.6). When the flag is absent — an old player, a
player that has not spoken, or the HTTP transport — the K7 applies the tone to the broadcast before
the encoder as before, and sends no E12. A player that does not set the flag is never sent E12, and
a new player starts neutral, so an old K7's tone is not doubled.

## 7. What this replaces

The player has no HTTP stream client any more: no `GET /stream`, no `Connection: close`, no
close-delimited body, no reconnect schedule, no 500 ms connect burst. `k7player.md` R23–R25 and
R29–R30 change with it; R26–R28 stand. The web UI keeps its listener against the same broadcast.

## 8. Tests

| # | Test | Tier |
|---|---|---|
| E-T1 | A frame of every legal length fragments and reassembles byte for byte; a 417-byte frame is two packets and a 1 441-byte one is six | host |
| E-T2 | A gap in the sequence is detected, the status packet names it, and the held frames wait; a resend that arrives inside the second is played in order, and one that arrives after it is discarded | host |
| E-T3 | A gap abandoned after a second releases what was held, counts the frame lost, and the stream continues | host |
| E-T4 | The sender sends nothing without credit, sends up to the credit, and resends on request without exceeding it | host |
| E-T5 | A tone packet round-trips the four values, clamps a value outside the knobs' ranges, and refuses a short, wrong-version or wrong-type packet | host |
| E-T6 | A status packet with the tone flag parses it, one without the byte parses 0, and a longer packet is accepted while a malformed list is still refused | host |
| E-B1 | The player makes sound at 44.1 kHz from the K7's emitter, with the station's census beside it | **board only** |
| E-B2 | A weak link loses frames and the player recovers them: the counters show resends and a count of abandoned frames | **board only** |
| E-B3 | A new K7 and a new player: the player applies the K7's tone and the knob is heard at once; an old player keeps the S3 tone; a browser hears the flat stream. A new player beside an old K7 is neutral. | **board only** |

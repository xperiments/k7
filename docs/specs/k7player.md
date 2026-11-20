# K7Player: the second device

**Goal.** A box that is switched on, finds the K7, and plays what the K7 is broadcasting down a wire into an
amplifier. It is a **Client** of the live stream and
nothing else, so it has no card, no library, no Snapshot, no transport and no volume; the one
thing it needs help with is a network, and the K7 hands it one.

The decisions are [ADR 0097](../adr/0097-the-player-is-an-esp32-classic-in-its-own-project.md)
(the chip and the project), [ADR 0098](../adr/0098-the-player-is-a-client-the-k7-keeps-no-record-of.md)
(the relationship) and [ADR 0099](../adr/0099-the-network-is-handed-over-esp-now-in-a-window.md)
(the handover). The words are [`player/CONTEXT.md`](../../player/CONTEXT.md); the protocol's own
document is [`network-handover.md`](./network-handover.md).

**Status:** built, and measured on the board on **2026-11-18** (§6): the line output making sound
at 44.1 kHz, the ESP-NOW receiver with the ring at 70/96 KB and nothing refused or underrun, and
the memory census with the stream, decoder and ring up. The board is ADR 0097's — a WROOM-32 with
a PCM5102A — and the image is 891 KB with 15 % of its 1 MB partition free. The handover is not
this document's to prove: its own board pass is
[`network-handover.md`](./network-handover.md) §7, which records the 2026-11-18 run. What is not
measured here: the outages B2–B4 inject, and B6's four patterns by eye. The tone was heard on both
boards on **2026-11-20** (B7, TN-04), without the 0.6.2 old-player fallback.

---

## 1. Scope

### In scope

| # | Requirement |
|---|---|
| R1 | It joins a network it was handed, preferring the home network and falling back to the K7's own |
| R2 | It plays `GET /stream` continuously, from the first bytes, with no transport of any kind |
| R3 | It decodes the station format (MP3) and passes stereo 16-bit frames to one output |
| R4 | Its output is a line: I2S to an external DAC, configured to the station's rate |
| R5 | It has no Bluetooth, no volume and no control surface of any kind |
| R6 | It reconnects on its own, and returns to searching when it has nothing left to retry |
| R7 | It reports its state on one status light, with no other surface |
| R8 | Every behaviour above has a host test except the six things § 6 marks board-only |

### Out of scope, explicitly

- **The card, the library and the Snapshot.** It has no reader and no storage but the network
  record.
- **Any transport.** The live stream cannot be paused, seeked or replayed, so there is nothing
  for a button to do.
- **Volume and mute.** The line has one fixed level, set by the board (R11).
- **The "now playing" name.** The K7's ICY metadata exists; the player does not ask for it,
  because it has nowhere to show it.
- **Any other stream.** It is not a general internet-radio client; the earlier research into one
  is evidence, not a backlog.
- **Talking to the K7.** It sends one frame in its life — the handover request — and nothing else.
- **Over-the-air updates.** Not in version one, for either device ([README](../../README.md)).

---

## 2. The device

**R9. One ESP32 classic, one output, one light, no controls.** The board fits a line output and
the status light; there is no button and no second surface.

**R10. The output is the line.** The player drives an I2S DAC and nothing else. There is **no
Bluetooth in this firmware**: compiling the stack in cost the line roughly half its stream rate
before a speaker was even connected, and no speaker on the bench ever answered the player's
inquiry, so the A2DP work was removed rather than shipped unproven (ADR 0100 decided it, ADR 0101
records why the code lives in the history and not in the tree).

**R11. The line has a fixed level, and it is the board's.** There is no hardware volume on the DAC
parts this targets (a PCM5102A has no control bus, a MAX98357A has gain strapped by resistors), and
no surface to change a digital one, so the level is a **board constant**: a single number in the
board adapter, chosen for what the line feeds. The WROOM-32 bench board feeds active speakers
directly and is set to **−20 dB**; a board feeding an amplifier would set 0 dB and let the amplifier
own the rest. It is not a volume: nothing can change it while the player runs.

**R12. No volume, and no remote control.** The player sets no digital volume and speaks no
profile. The level is the board's (R11) and the amplifier after it owns the rest.

**R13. The speaker is not in this firmware.** The A2DP source, the pairing rule and the sink
switch were built and measured, then removed when ADR 0100 made the line the only output: the
record is [PL-08](../design/k7player/issues/08-the-player-speaker-output.md) and the ADRs. If a speaker variant is ever built, that work
is the starting point, not a design.

**R14. The light says nothing about the output.** It reports the stream (§ 5).

**R15. The board's pins, the I2S peripheral and the DAC parts live in the board adapter**, as
they do on the K7 ([`components/app/`](../../components/app/)). The first target is a WROOM-32
devkit and a PCM5102A breakout; the PCB follows the two board-only numbers in § 7.

---

## 3. The network

**R16. The record is the K7's, stored whole.** One blob in NVS: the home network when the K7 had
one, the K7's setup network always, the K7's hostname and address. It survives `idf.py flash`;
`erase_flash` returns the player to sweeping. It is never returned by anything — the player
serves no page and prints no password.

**R17. The order is home, then the K7's own network**, and it is the same order every boot. The
K7's own network is joined with the setup password from the record; there is no prompt.

**R18. An attempt is 25 s or five tries**, the K7's own ceiling applied to the player
([`wifi-provisioning.md`](./wifi-provisioning.md), P29), so a wrong password is not retried for
ever.

**R19. The K7 is found by `k7.local` first and by the record's IPv4 second.** A moved DHCP lease
is therefore invisible to the player as long as mDNS answers; the address is the fallback for a
network whose mDNS does not.

**R20. The recovery alternation is 20 s of retrying, then 10 s of sweeping, for as long as the
player is failing.** It cannot sweep while it is connected, because the stream is on the channel
it would be leaving.

**R21. A player that has never been provisioned sweeps from boot** and is *Searching* until a
handover happens (§ [`network-handover.md`](./network-handover.md), H19–H21).

**R22. It never writes to the K7.** One request frame, one encrypted record arriving, and nothing
after that but `GET /stream`.

---

## 4. The stream

**R23. The audio arrives over ESP-NOW**, and only over it. The packets, their sequence, the resend
and the status packet are [`espnow-stream.md`](./espnow-stream.md); the decision is ADR 0103. The
player has no HTTP client: whether the K7 emits at all is the K7's setting (EN-05), and with the
emitter off the player hears nothing and says *Ready* (ADR 0105).

**R24. Nothing is asked for.** The K7 emits as it encodes, and the player takes what arrives. The
web UI's own HTTP listeners are a different client and are not this.

**R25. The player answers every 100 ms** with the ring's free bytes and the sequences it is
missing, and that is the transport's only flow control and its only retransmission request
(espnow-stream.md E3, E6). A player that has not spoken yet is not sent anything.

**R26. It decodes MP3 only**, because the station format is MP3 and the station format is chosen
at boot and fixed. The radio carries frames from the K7's encoder, so a frame that is not MPEG
audio is a *Fault*: not a link that will recover, but two firmwares disagreeing about the format.
The line between the two is time, not bytes — **two seconds of frames that never decode** is the
Fault, and anything shorter is a packet that arrived while something else did (E5's abandoned
frames are not this).

**R27. The output's rate follows the stream.** The station rate is read from the MP3 frame header
(44.1 kHz by default) and the line output is configured to it, so a station at any rate the DAC
can clock plays at that rate.

**R28. One ring, six seconds deep, and it never grows.** The socket task writes and the output
task reads, whole frames only. A push that does not fit **clears the ring once** and keeps the
newest audio, so an outage the ring cannot ride costs one discontinuity instead of one per frame.
Its depth is the jitter budget and a board number (§ 7): on this board the 2.4 GHz link loses the
stream for one to nine seconds at a time (T3), so the ring holds six seconds of the station at the
format it runs (128 kbps; three at 256) — outages up to that are absorbed and the listener hears
nothing, and a longer one is a single click rather than a hundred small jumps.

The output **does not play until the ring holds 4.5 s** (or all it can hold, when the station's
rate makes that more than 96 KB), and waits one second again after it has run dry. A realtime feed
never fills a buffer by itself, so **the ring is only ever as deep as the prime**: measured the
hard way, primed to 1.5 s the ring sat at 38 KB of its 96 and a three-second outage ran it dry.
The wait is bounded by the connection — a body that has ended is played, not waited on — and its
price is 4.5 s of silence before the first sound. The socket never blocks on the ring.

**R29. Three seconds without an audio packet is a silent stream.** The ring drains, the light says
*Ready*, and nothing happens: no reboot, no retry, no sweep, no fallback. The K7 is either emitting
to this player or it is not, and the network machine owns every question about the network (R20).

**R30. There is no reconnection schedule** because there is no connection. A stream that stops
resumes when packets resume, with the ring's prime and the receiver's gap window doing what R28
and espnow-stream.md E4 say they do.

**R33. The K7's tone is applied after decode when the K7 says the player owns it.** The player's
status advertises the capability in its optional flags byte
([espnow-stream.md](./espnow-stream.md) E3a, E12); the K7 then sends the four values over the
radio, and the player applies them to the decoded PCM just before the output, with the same
`tone_eq` module the S3 uses
([ADR 0018](../adr/0018-dj-eq-knobs-and-transport-arrows.md)). It starts neutral,
ignores a packet that is not version 1, type `0x06` and 10 bytes, and rebuilds the filters on a
rate change without losing the targets. It has no tone control of its own and persists nothing:
the K7 repeats the values, so a reboot is neutral for at most a second. The player is still a
client; the K7 keeps no record of it or of the tone.

---

## 5. The status light

**R31. Four states, and nothing between them:**

| State | Meaning | Pattern |
|---|---|---|
| **Searching** | No network it can use: sweeping, or retrying a network that will not take it | 1 Hz blink |
| **Ready** | On a network, no stream established: connecting, retrying, or nothing to play to | steady, dim |
| **Playing** | The stream is established and audio is going out of an output | steady, bright |
| **Fault** | It cannot do its job for a reason the network is not (two seconds of frames that never decode, R26, or an output path that will not initialise) | 4 Hz blink |

**R32. The light is the whole surface.** No beep, no screen, no web page, no button. A person
diagnoses the player by looking at it and by the K7's own panel, which is where the share window
they would open is.

---

## 6. Tests

Host tests run the player's core against a scripted socket and a null output, so the stream
client, the ring policy, the record's validation and the network preference are all shown wrong
without a board. The handover's own tests are in [`network-handover.md`](./network-handover.md).

| # | Test | Tier |
|---|---|---|
| T1 | The receiver reassembles fragments into whole frames in sequence order, across the sequence wrap, and hands a fragment that arrives twice to the stream once | host |
| T2 | Three seconds without a packet is a silent stream and not a fault; two seconds of frames that never decode is a fault | host |
| T3 | The ring holds two seconds of the station's frames; an overrun clears it once and keeps the newest, and its depth never grows; an underrun repeats silence rather than blocking the socket | host |
| T4 | The record's validation and the preference order: home, then the K7's own network, then the 20 s/10 s alternation | host |
| T5 | The state machine drives the four light states, including *Fault* when two seconds of delivered frames never decode | host |
| T6 | The station rate from the frame header configures the line output; a 32 kHz station opens it at 32 000 | host |
| T7 | The output holds silence while the ring is shallow even with audio in it, and plays once it holds the buffer | host |
| T8 | The player's tone is the K7's: a tone packet sets the four targets, a boost and a cut reach the PCM after the glide, no packet is neutral, a malformed packet is ignored, and a rate change keeps the targets | host |
| B1 | The line output makes sound at 44.1 kHz, and a 48 kHz station follows it | **board only** |
| B2 | A one-second outage of the link is inaudible: the ring absorbs it and nothing is discarded | **board only** |
| B3 | An outage longer than the ring is one discontinuity, not a stream of them: `flushed` counts them | **board only** |
| B4 | Ten minutes of Wi-Fi with the stream: outages, underruns and discarded audio counted, and the line's own rate beside them | **board only** |
| B5 | The memory census with the stream, the decoder, the ring and the Wi-Fi up | **board only** |
| B6 | The light's four patterns, by eye | **board only** |
| B7 | The tone on the board: with a new K7 and a new player a knob is heard at once; with an old player the S3 tone and the late knob stand; a browser hears the flat stream while the player owns the tone; a player reboot restores the tone within a second | **board only** |

**Measured on the board, 2026-11-18**, with a WROOM-32 and a PCM5102A, the K7 on `192.168.0.13`:

| # | Result |
|---|---|
| B1 | **proven** — the line makes sound: `playout: output 44100 Hz: 847872 frames, 0 underruns, 0 refused, 1 primes`, and the image is 891 KB with 15 % of its 1 MB partition free |
| B5 | **proven** — boot heap 201 020 B free; **177 212 B** with the stream, decoder and ring up; largest free block 110 592 B |
| B7 | **proven 2026-11-20** — a knob turn is heard at once, the browser hears the flat broadcast, a player reboot gets its tone back, and station restarts keep it. The 0.6.2 old-player fallback was not run |
| B2, B3 | **not proven** — the run injected no outage; it recorded `flushed 0, lost 0, dups 28` |
| B4 | **not proven** — no ten-minute Wi-Fi soak is recorded |
| B6 | **not recorded by eye** — the light logged `playing (net connected, stream streaming, audio 1, failed 0) level 255` |

The receiver's own numbers, from the same pair over ESP-NOW: the K7 logged `espnow audio: 1204
frames, 174 resent, 22670 held, 2 dropped, 2757 packets, a player is listening`, and the player
`receiving 192.168.0.13: 44100 Hz, 128 kbps, 905 frames, ring 70/96 KB, flushed 0, lost 0, dups 28`.

---

## 7. Open questions

1. **Does the board exist? Answered.** The first target is a WROOM-32 devkit with a PCM5102A, and
   it is the board every run above used. The PCB follows B4 and B5, because those two numbers
   decide whether a WROVER (PSRAM) is needed.
2. **How deep is the ring?** Answered: 96 KB, six seconds at the station's 128 kbps
   ([ADR 0102](../adr/0102-the-players-ring-is-96-kb-and-clears-itself-rather-than-dropping-frames.md)). The number
   comes from the measured outages of the 2.4 GHz link (T3) — one to nine seconds, 6 % to 24 % of a
   session — against the 96 KB that still leaves the Wi-Fi 85 KB of internal heap. The A2DP buffer
   that used to bound it is gone with the speaker, and the station's format is the other half of
   this number: 128 kbps buys twice the seconds of 256.
3. **Does the site flash it? Answered.** The Site offers it as its own board with its own three
   parts at `0x1000`, `0x8000` and `0x10000` ([`k7-site.md`](./k7-site.md) F2–F3,
   [ADR 0079](../adr/0079-the-firmware-is-flashed-in-parts-from-the-browser.md)).

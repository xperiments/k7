# The player's audio arrives over ESP-NOW, one MP3 frame at a time

The player has taken its audio over HTTP from the K7 since R23: one `GET /stream`, a
close-delimited body of MP3 frames, and a reconnect schedule. What that path has cost was measured
on 2026-11-18 with the player beside the K7 and a 5 GHz client between them: the K7's stream is
honest — 32 011 B/s, a mean inter-frame gap of 52 ms, no MPEG boundary missing over 20 to 90
seconds — and the player's downlink to the same K7 stops dead for **one to nine seconds at a time**,
in 6 % to 24 % of a session, with RSSI at −51 dBm and power save off. Every one of those stalls is
the player's ring being asked to cover a gap that the access point created: a second hop, a foreign
radio's airtime, and a TCP connection whose window is only as fast as the app that reads it.

The two boards already talk without that access point: the handover is ESP-NOW (ADR 0099), a share
window and an encrypted record between the same pair of radios. ESP-NOW is one hop, has no
association, no window and no retransmission beyond the MAC's own, and its payload is small — 250
bytes — which is exactly why the broadcast is convenient: it already emits **one whole MP3 frame
per callback**, 417 bytes at the station's 128 kbps, 38.3 times a second.

## Decision

The player's audio arrives over **ESP-NOW**, and HTTP leaves the player entirely.

- A frame travels in fragments of at most 244 bytes behind a six-byte header that shares the
  handover's version and type bytes (`0x04` audio, `0x05` status). A 417-byte frame is two packets;
  the stream is ~77 packets a second.
- The player answers every 100 ms with **free ring bytes and the sequences it is missing**. That is
  the transport's only flow control, and it is also the retransmission request.
- A missing frame is **asked for again, up to five times**, from a two-second history the sender
  keeps. The receiver holds frames that are ahead of the gap for up to **one second** and then
  abandons it: the ring's 4.5 s prime is what gives a resend somewhere to land.
- The K7's emitter is a **setting, never two at once** — HTTP or ESP-NOW for the player's feed. Its
  web UI keeps its own HTTP listeners, which are a different client.
- What does not change: the ring, the prime, the playout, the line's level, the light, and the
  station format. The player loses a client and gains a receiver.

The contract, byte for byte, is [`docs/specs/espnow-stream.md`](../specs/espnow-stream.md) E1–E9.

## Considered options

- **Keep HTTP.** It is the path being replaced: the stalls live in the access point between the two
  boards, and TCP's retransmission cannot make a stalled access point deliver.
- **ESP-NOW with no application retransmission.** The simplest build: the MAC's own retries, and a
  lost fragment costs its 26 ms frame as a click. At 77 packets a second, one packet in a thousand
  is a click every thirteen seconds — worse than what the ring was built to hide.
- **ESP-NOW v2's 1 470-byte payload**, one packet per three frames: fewer packets, but a loss costs
  ~90 ms of audio instead of 26, and the handover's v1 frames share the same radio.
- **Duplicate every fragment** (repetition FEC): hides roughly half the loss for exactly twice the
  airtime, and still cannot repair the other half.
- **Raw PCM instead of MP3**: 176 400 B/s is about 700 packets a second and no encoder on the
  critical path — ten times the airtime for a station that already has a working encoder.

## Consequences

- Loss stops being an outage and becomes an event with a policy: one frame is 26 ms, the reorder
  window is a second, and the ring is unchanged.
- The player is smaller: no HTTP client, no close-delimited body, no reconnect schedule, no connect
  burst, no 10-second stall rules. One receiver replaces them, and R23–R25 and R29–R30 of
  `k7player.md` change with it while R26–R28 stand.
- The protocol is host-testable without a radio: the packet codec, the gap and the credit are
  functions over byte buffers, which is what T1–T4 pin.
- Airtime: 77 packets × ~250 bytes is about 19 KB/s. At ESP-NOW's default 1 Mbps that is roughly
  18 % of the radio; at a raised rate it is a few percent. The access point is no longer in the
  path, so the audio and the K7's own web UI no longer compete through it.
- Both boards must be on the same 2.4 GHz channel; as stations of the same access point they are,
  and the handover already depends on it.
- The emitter setting is `appliance.c`'s for now. A Menu row that chooses HTTP or ESP-NOW is the
  natural next surface, and until it exists the two are still never both.

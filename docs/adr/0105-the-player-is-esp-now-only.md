# The player is ESP-NOW only, and the K7's setting chooses what it emits

ADR 0104 gave the player both transports: the radio first, and an HTTP fallback that took over
after three seconds of silence. The design says otherwise. The player is **ESP-NOW only** — it is
the small board with the switch and no surface, built to take one thing off the air and put it on
a DAC — and the choice of transport belongs to the K7, whose radio either feeds the player or does
not.

The switch and the Settings row are about the K7's emitter, not the player's fallback: with
ESP-NOW the K7 pushes the audio to the player, and with HTTP it stops pushing. The web UI's own
listeners are unaffected either way, because they are a different client on the same broadcast.

## Decision

- The player has one transport. `player_stream` is the ESP-NOW receiver, and the HTTP client, the
  close-delimited body and the reconnect schedule are not in this firmware.
- The K7's Settings row chooses the emitter (EN-05). With HTTP, the emitter is off and nothing is
  resent; the player hears nothing and says *Ready*, which is honest: the radio is not carrying
  this player's audio.
- The HTTP path still serves the web UI, and that is the only thing it serves.

## Considered options

- **Keep ADR 0104's fallback.** It is built and host-tested (parked on the branch
  `en06-fallback-parked`, one commit): the parser, the socket seam, the source, the preemption and
  `t03_fallback`. It contradicts the design and doubles the player's firmware for a state the
  product does not have — the switch is on the K7's side of the air.
- **Receive both at once.** The player would need to decide which stream is "the" stream, and the
  K7 would be sending two copies of one broadcast for one listener.

## Consequences

- The player's spec loses the fallback: R23 is the radio, R29 is a silent stream, and R30's "no
  reconnection" stands. The Fault rule (two seconds of frames that never decode) is unchanged.
- A browser and the player never share a transport. With the setting on ESP-NOW a browser still
  listens over HTTP; with it on HTTP the player is silent and the browser is not.
- The parked branch is the starting point if the constraint ever changes, with the work already
  tested on the host.
- Supersedes ADR 0104's fallback half; its analysis of why the radio wins for this link stands.

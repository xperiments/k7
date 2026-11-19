# The player hears either transport, and the radio wins when it speaks

The K7 can put the player's audio on two transports. ESP-NOW is the one this week's work built:
one MP3 frame at a time, credit and resends, no access point in the path. HTTP is what the board
has served since the beginning: `GET /stream`, a close-delimited body of MP3 frames, and the web
UI's own listeners on the same route.

The choice has to be made somewhere, and the design says it is a **micro switch on the small
board** — the player is the one with no surface, so it cannot offer a menu; the panel is the K7's.
Until that switch exists, the same choice appears as a row in the K7's Settings menu, and the
switch will call the same thing.

The question that had to be settled is how one player firmware consumes both without a second
protocol. Three ways were open:

- the K7 **announces** its transport in a new packet type, and the player obeys;
- the player **probes** the K7's web API for the setting;
- the player **listens to the radio and treats its silence as the answer**.

## Decision

**The player keeps its ESP-NOW receiver up always, restores the HTTP client as its fallback, and
lets the radio win.**

- **The K7's emitter is a setting.** On: every encoded frame is fed to it, resends are answered,
  the player's peer is remembered. Off: nothing is fed, nothing is resent, the peer is forgotten,
  and the radio stays up for the handover. The value is a row in Settings, persisted in NVS, and
  the micro switch will be the same call.
- **The player waits three seconds.** With no audio packet for that long it fetches `/stream` and
  plays the body through the same framer, ring and prime the radio's frames use. The ESP-NOW
  receiver keeps listening while the HTTP body is playing.
- **The radio preempts.** The first audio packet after that closes the HTTP connection and the
  stream is the radio again. No announcement, no new packet type, no probe: the radio speaking is
  its own announcement.

## Considered options

- **A transport announcement in a new packet type (`0x06`).** It would make the switch instant and
  explicit, at the cost of a second protocol: a timer or task to send it, a type to carry, and a
  player that must trust the last announcement over the packets it is actually receiving. The
  silence rule needs none of that and cannot disagree with the truth.
- **The player probing the K7's web API** for the setting. It couples the player to the web
  surface, which is the K7's UI contract and not a transport contract, and it still needs the
  fallback for the moment the setting changes.
- **The player trying only one transport.** Every change of the switch would need a reflash, which
  is the opposite of what a switch is for.

## Consequences

- The switch costs up to three seconds of silence in each direction: the HTTP side's stall rule is
  what notices the radio stopped, and the radio preempting is immediate. That is the price of doing
  without an announcement.
- The player carries both clients again — the HTTP one deleted in EN-03 returns — but not both
  *policies*: the ring, the prime, the rate from the header and the Fault rule are shared, and the
  HTTP body keeps its close-delimited rules only for as long as it is the stream.
- The K7's setting is the emitter's switch, not the web UI's: the browser's listeners work
  whichever way it is set, because they are a different client (`espnow-stream.md` E9).
- Specs: `k7player.md` R23–R25 and R29–R30 gain the fallback; `espnow-stream.md` E9 names the
  setting and what "off" means.

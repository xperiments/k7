# The K7 carries one listener, and a second one is refused rather than given airtime

The broadcast was built with four client slots — `MAX_CLIENTS 4`, `max_clients = 4` — because
it came from a general-purpose Icecast server, and the stream specification even defends the
range (`sd-stream-server.md` D3, `1..4`). This device is not a station. It is a personal
player: one pair of ears, one encoder, one 2.4 GHz link.

Four slots made sense of a server other people listen to. Here they buy nothing and cost
something. Each slot is a 164 096 B send ring at 128 kbps (324 096 B at 256) in PSRAM, a sender
task, and a share of a downlink that has been measured stalling for one to nine seconds at a
time — the player's 96 KB ring and its prime exist because of those stalls (ADR 0102). A
second browser does not add a second stream: it divides the same airtime, and the person
already listening is the one who hears it.

So the K7 carries **one** audio client. A second `GET /stream` is refused with the station's
existing `503` and its sentence, `every stream slot is taken; try again in a moment`, rather
than being registered. The HTTP connection pool is a different budget and is untouched: it
stays at four, because the API and the page need those workers, and a registered listener
releases its worker immediately.

## Decision

- `max_clients = 1` in `appliance.c`. The station carries one `/stream` at a time; the second
  is refused, not queued and not added to the pacing.
- `max_conns` stays four. The pool bounds request parsing, not listeners.
- The ESP-NOW player takes no slot, because it is not an HTTP client at all (ADR 0105). One
  browser plus the player is the intended shape; two browsers are not.
- The three rings and three sender tasks that are no longer allocated are what this buys:
  about 480 KiB of PSRAM at 128 kbps (950 KiB at 256) and three 4 KB stacks.

## Considered options

- **Keep four, as the server always had.** It is what the code does today and needs no change.
  But there is no product case for four listeners on a personal player, and every extra
  listener is a share of the airtime measured above.
- **A pool with eviction — newest listener wins.** The person already listening is dropped
  without a word and cannot tell why. Worse than being refused at the door.
- **Per-listener quality, so four listeners fit.** One encoder, one stream: a second quality
  level is a second encoder and a second broadcast, not a listener slot.

## Consequences

- A second page that presses play meets a `503`; its `<audio>` element reports a network
  error, so the page says *The stream stopped; reconnecting…* and retries with its backoff
  until the first listener leaves. The refusal is correct and the sentence is not — the page
  cannot yet tell "the station is full" from "the link dropped", because the media element
  does not show it the body. Telling it would need the page to ask the K7 whether a listener
  is present; not in this change.
- The stream specification states the one-listener rule in §6.1 and the K7's manual shows the
  value; the four-slot table is history in the tickets that describe it.
- One listener is also the shape the ESP-NOW transport expects: the player never asks for
  the slot, so the slot is the page's, and the player has its own transport (ADR 0103).

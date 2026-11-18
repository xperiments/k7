# The K7Player is a client, and the K7 keeps no record of it

The second device has an obvious other shape: a **paired sink**. The K7 would keep the speaker's
MAC and a name, push the stream to it, mute it, set its volume, and show it in the Menu. That
shape has a product's confidence about it — "the pair works together" is a sentence a buyer
understands — and it is the one the marketing notes reach for when they call the thing "the
bridge".

It also buys a great deal of surface that nothing else in this repo has. A registry is state in
NVS that has to be created, edited, forgotten and recovered; a push protocol is a second
transport beside HTTP; per-device volume is a control surface with no UI on the second box; and
"the speaker is out of range" becomes a fault state the K7 has to report somewhere, on a device
whose only output is a cassette. The K7 is a broadcast: every listener hears the same audio at
the same moment, and the glossary already says so — **Client**, *"one connected consumer of the
live stream. Clients are all equal and all hear the same audio; the server does not track
listeners as identities"* ([`docs/specs/CONTEXT.md`](../specs/CONTEXT.md)).

## Decision

- **The K7Player is a Client.** No registry, no per-device state, no control channel, no push.
  It joins a network and fetches `/stream`; from that moment the K7 cannot tell it from a
  browser.
- **The K7 helps it in exactly two ways**, and neither outlives the act: it hands over a
  **network record** during a **share window** ([ADR 0099](./0099-the-network-is-handed-over-esp-now-in-a-window.md)),
  and that record carries its hostname and address so the player can find it again.
- **A player is bound by what it stored, not by anything the K7 remembers.** Re-provisioning is a
  new handover; a K7 that has changed networks strands its old player until one happens, and it
  cannot tell whether the player it is helping is the one it helped before.
- **Nothing on the K7 is addressed to a player.** No mute, no volume, no transport, no "now
  playing" channel. Volume belongs to the amplifier or the speaker.

## Considered options

- **A paired sink.** A registry, a push model, a per-player fault story, and a Menu list of
  speakers — for an appliance whose entire state today is the card, the snapshot and one
  selected Playlist.
- **Discovery only** (mDNS, no handover). The player would find the K7 and still have no network
  to find it on; the credentials are the whole problem.
- **The setup page as the only provisioning.** It works, and it needs a phone, a browser and
  someone typing a password on a device that has no keyboard — which is the complaint the
  handover exists to answer.

## Consequences

- **A network change on the K7 strands the player.** It is what makes the player's recovery
  alternation load-bearing rather than decorative: without a periodic sweep, a stranded player
  could never be reached again.
- **Marketing may not claim a pair the firmware does not track.** "The cassette broadcasts;
  anything on your network can play it" stays true; "the K7 knows its speaker" does not.
- **Two players, one K7, is not a special case.** It is two clients, and the K7 serves them
  identically — which is what the burst and the shared live stream already do for two browsers.

# K7Player

The second device: an ESP32 classic that joins a network, plays the K7's live stream and turns it
into sound at a Bluetooth speaker or a line output. It owns no card, no library and no Snapshot,
and it is not a K7 — the only thing it ever asks a K7 for is the stream, and the only thing a K7
ever gives it is a network to ask on.

## Language

### The device

**K7Player**:
The second device: one ESP32 classic, one status light, two outputs and no controls. It is a
Client of the K7's stream, and it is bound to a K7 by what it stored rather than by anything the
K7 remembers.
_Avoid_: bridge, receiver, speaker, player on its own (the app and site specs call the K7 that), the device (that is the K7)

**Line output**:
The K7Player's wired output: stereo audio on an I2S line to an external DAC and whatever
amplifier follows it. It has no volume of its own, so the amplifier owns it.
_Avoid_: DAC mode, analog out, wired output

**Speaker output**:
The K7Player's wireless output: the stream sent to a Bluetooth speaker, the K7Player being an
A2DP source.
_Avoid_: Bluetooth mode, BT sink, wireless output

**Status light**:
The K7Player's only surface: one LED whose state is one of **Searching** (no network it can use),
**Ready** (on a network, no stream yet), **Playing**, or **Fault** (it cannot do what it was told,
and the reason is not the absence of a network).
_Avoid_: LED, indicator, lamp

### The network

**Network record**:
What a K7 hands a K7Player so it can join a network: the home network and its password when the
K7 has one, the K7's own setup network and its setup password always, and the K7's hostname and
address. The K7 composes it, the player stores it, and no route and no panel ever returns it.
_Avoid_: credentials, Wi-Fi config, provisioning blob

**Handover**:
The one exchange that gives a K7Player a network record: a request inside a K7's share window,
answered encrypted. It leaves no lasting relationship — the K7 forgets it, the player keeps the
record.
_Avoid_: pairing (that implies both sides remember), linking, registration

**Share window**:
The sixty seconds during which a K7 will answer a handover request, opened by a person from the
K7's Menu. It belongs to the K7, it exists only while it is open, and a player cannot open one.
_Avoid_: pairing mode, discovery mode, access point

**Sweep**:
The K7Player's walk across channels 1–13 while it has no network record, dwelling on each one long
enough to hear a beacon.
_Avoid_: scan (the K7's setup page scans for networks; a sweep listens for one K7)

**Beacon**:
The secret-free frame a K7 sends inside a share window, and the only thing a sweeping K7Player can
hear of it.
_Avoid_: advertisement, broadcast, hello (that is the player's request)

**Network preference**:
The K7Player's order of networks: the home network first, the K7's own setup network second. It is
a policy the player runs, not a mode anyone chooses.
_Avoid_: fallback, priority, mode

### The stream

**Client**:
*(The K7's word, defined in the [stream server's glossary](../docs/specs/CONTEXT.md#clients-and-access).)*
One connected consumer of the live stream. Every K7Player is one, and is indistinguishable from a
browser to the K7.
_Avoid_: listener, subscriber, user

**Live stream**:
*(The K7's word.)* The one continuous broadcast every client shares. The K7Player has no
transport because the stream has nothing to pause, seek or replay.
_Avoid_: feed, radio, channel

**Station format**:
*(The K7's word.)* The single format every track is converted to before it goes on the wire. The
K7Player's decoder follows whatever the K7 sends; it does not choose.
_Avoid_: output format, codec

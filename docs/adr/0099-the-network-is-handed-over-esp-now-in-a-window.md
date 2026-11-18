# The network is handed to a player over ESP-NOW, in a window a person opens

The K7Player needs a Wi-Fi network before it can be a Client of anything. There are four ways to
give it one, and only one of them works in the situation the second device was asked for — a K7
that already has a network and a player that has just been switched on.

- **A setup page on the player**: a second access point, a second hand-written page, and a phone
  to type into. It is the K7's own answer to "a device with no network is not a dead device"
  ([ADR 0076](./0076-the-network-is-chosen-on-the-devices-setup-network.md)), and it is exactly
  the ceremony the second device exists to avoid.
- **A route on the K7's setup network**: the player joins `K7.local` and asks the K7 for the home
  network. It works only while that AP is up, and it still needs the setup password, which is a
  password someone has to know.
- **A compiled-in network**: one network per build, which ADR 0076 already refused.
- **The radio**: ESP-NOW, which is the only carrier that reaches a player while the K7 is on its
  own network with no access point raised.

Two properties of ESP-NOW then decide the design. It is a **same-channel** protocol: a peer must
be on the channel the other's radio is on, and the K7's radio is pinned to its station's channel
— it cannot move without dropping off its own network. And its encryption is **per-peer and
unicast** (`ESP_NOW_MAX_ENCRYPT_PEER_NUM` is 6, and the peer lookup ignores multicast), so an
encrypted frame can only go to a MAC that is already known, and the two ends need a key they have
never shared.

## Decision

- **A share window, not a mode.** The K7 will answer a player only during a 60 s window that a
  person opens from its Menu (`Wi-Fi → Share`). While it is open the K7 beacons a secret-free
  frame every 200 ms on its current channel and will answer **one player**. The window closes
  on the countdown or on a stop, and it stops beaconing as soon as it has answered, so the rest
  of the window exists only for that player's retries. Like the setup page, it is a
  capability that does not exist the rest of the time
  ([ADR 0077](./0077-the-setup-surface-exists-only-while-its-hotspot-does.md)).
- **The player sweeps.** While it has no record it cycles channels 1–13 at 250 ms a channel —
  about 3.25 s a sweep — listening for a beacon. It cannot sweep while it is connected: the
  stream is on the channel it is listening to.
- **The key is derived, not shipped.** The K7 puts an ephemeral **X25519** public key in its
  beacon; the player answers with its own; both derive the shared secret and the K7 registers
  that one peer with an LMK from it (HKDF-SHA256 over the secret and both public keys). Only then
  does the **network record** travel, encrypted by ESP-NOW's own AES-128. Both sides already link
  mbedTLS, and X25519 and ECDH are enabled in this build (`sdkconfig.sunton:3374`, `:3387`).
- **The record is the network the K7 is on.** It carries the home network's SSID and password
  when the K7 has one, its own setup network's SSID and setup password always, and its hostname
  and IPv4. The player prefers the home network and falls back to the K7's own; that is a policy
  it runs, not a mode anyone chooses.
- **The exchange is defined in [`docs/specs/network-handover.md`](../specs/network-handover.md)**,
  byte by byte, because both firmwares implement it and neither may guess.

## Considered options

- **Plaintext inside the window.** One frame, one line of code, and the household's Wi-Fi
  password is on the air once per pairing for anyone within range holding a sniffer — in a
  project whose protocol is documented in a public repository.
- **A compiled-in key shared by both firmwares.** It costs one constant and defends against
  nobody who has read the source; it is obfuscation with a security-shaped name.
- **A confirmation on the K7's panel per player.** It would authenticate *that a person is
  present* a second time, and it still cannot authenticate *who answered*; the window is already
  that act.
- **Always-on advertisement.** No human step anywhere, and any ESP32 in range can ask a K7 for
  its network the first time it tries.

## Consequences

- **A person must be at the K7 to provision a player.** There is no remote pairing, and no way to
  provision a K7Player from the app.
- **An impostor inside the window is still answered.** The window authorises *when*, not *who*:
  a rogue device that beats the real player to the request is given the record, encrypted to a
  key it derived with the K7. Passive capture is what the encryption kills; an active attacker
  standing in a room during a 60 s ceremony is not. That is the honest boundary.
- **The window answers one player, not one frame.** That MAC may retry its request up to three
  times, so a lost record costs a retry rather than a new window; a request from any other MAC
  is ignored until the window closes, so a second candidate still has to wait for a second
  person.
- **A frame is at most 250 bytes** (`ESP_NOW_MAX_DATA_LEN`), which is a hard ceiling on the
  record's format — the one number that keeps the protocol from growing a field for every idea.
- **ESP-NOW is 2.4 GHz only**, which costs nothing here: the K7's radio is 2.4 GHz only, and so
  is the classic ESP32's.

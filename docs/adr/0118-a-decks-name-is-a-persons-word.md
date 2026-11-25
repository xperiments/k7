# A deck's name is a person's word, and its address is the product's word plus theirs

Two decks on one LAN both answer `k7.local`, and mDNS resolves it to one of them. The second
deck is not misconfigured; it is the same product with the same name, and nothing in the
firmware lets a person say which one they mean. A home automation that wants two decks in one
house has the same problem: an entity keyed on the hostname is an entity that moves when the
hostname does, and today there is exactly one hostname for every deck.

The name is also the last thing a person cannot change about the device. The network is theirs
(the setup page), the card is theirs, the art is theirs; the address their browser types was
chosen by us.

## Decision

- **The setting is a word.** A person gives the deck a lowercase DNS label — `kitchen`,
  `studio`, `studio-2` — at most 24 characters: `a-z`, `0-9` and single hyphens, not starting
  or ending with one. Empty means "no name" and is the default, so the setting is optional.
- **The address is derived, never stored.** A deck with no name is `k7`, answering `k7.local`.
  A named deck is `k7-<word>`, answering `k7-<word>.local`: the product's word plus theirs.
  The word is what is stored; the address is computed by one function, so a change to the
  derivation is not a migration.
- **The bare product name is an alias.** `k7.local` keeps answering while a name is set — by
  mDNS delegation and by the MCP origin guard — so a bookmark, a player or a Home Assistant
  entry that has not learned the new name is not orphaned. That is why the change is additive
  and why the default can stay `k7`.
- **The word is a device setting, in the device's own namespace**, beside the network record
  and the setup password (`wifi-provisioning.md` P25). It is read once at boot and kept, so a
  status poll never touches flash (ADR 0054).
- **The setup page is the only place it is typed.** It is the one surface with a keyboard a
  person already has; the panel has a Pad, and the App is a follow-up. The route answers the
  rule's sentence when the word is not a label, like every other setup refusal.
- **The address applies at the next start.** mDNS is advertised once, after the station is
  online (P13), and a rename does not restart it: the page says so, and a rename made while
  provisioning is in force by the boot that follows.
- **The handover record carries the derived name**, so a K7Player looks the deck up by what it
  was told; HA-05 makes that lookup name-first, then the alias, then the address.
- **Not a new trust boundary.** The name decides an address, not a permission: the setup
  surface, the origin guard and the CORS posture are unchanged, and `k7.local` answering is
  the compatibility promise rather than a second device.

## Considered options

- **The bare word as the hostname (`kitchen.local`).** Short and pleasant, and the collision
  it invites is the one this exists to avoid: a neighbour's `kitchen.local` is a different
  device with the same name. The product's word is the namespace.
- **A numeric suffix (`k7-2.local`), or a random one.** Unique and unmemorable: the person
  chooses a word because they will type it and say it.
- **Storing the full address.** Then a rename is a stored migration and a half-migrated device
  can advertise one name and answer another; deriving it makes the two impossible to disagree.
- **Renaming from the App or the panel.** The panel has no keyboard; the App is a device-away
  surface and a rename from it would change the address the phone is talking to. A follow-up
  can add it once the shape is settled.
- **Dropping `k7.local` when a name is set.** It reads as a clean rename and breaks every
  bookmark, player record and dashboard that had the old name — the opposite of the point.

## Consequences

- Two decks can be named apart on one LAN, and a Home Assistant entry can key on the deck's
  `id` (HA-02) rather than on the address the name makes.
- The setup page grows a name row and the surface grows one route; `P22`'s refusals gain the
  name's sentence, and `P25`'s two keys become three.
- The origin guard accepts the deck's name and the bare product name; a deck named `kitchen`
  refuses `studio.local`, which is the same rule as before with the name moved.
- A rename is not hot: the deck answers the new name after it next starts. The page says so.
- The two-unit check — two named decks on one LAN each answering their own name — is owed
  until a second board is on the bench.

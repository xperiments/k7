# 11 — Card insertion and rescan generations

**What to build:** Make the Library tree survive a card that arrives after boot and a rescan
that happens while the broadcast is playing. A successful late mount publishes a complete
generation and the broadcast starts without a reboot. An explicit rescan builds a new generation
off to the side, publishes it atomically, lets the broadcast finish its current Track and lets
old JSON readers finish. Only one scan runs at a time.

**Blocked by:** 04 — Library at boot and the JSON endpoint; 07 — The broadcast plays the whole
Library tree; 08 — HTTP POST and play any Track.

**Status:** ready-for-agent

- [ ] Boot without a card, insert one, and both the index and the broadcast recover without a
      reboot.
- [ ] A rescan while playing publishes a new generation; the broadcast switches at the next
      Track and the surfaces switch at the swap.
- [ ] A slow reader holding the old JSON completes; the retired generation is freed only after
      its last reader releases it.
- [ ] A second rescan while one is running is declined, and the card-less state remains
      diagnosable.

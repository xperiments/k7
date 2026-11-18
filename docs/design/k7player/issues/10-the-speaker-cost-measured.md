# 10 — What Bluetooth costs the stream

**Superseded by [ADR 0100](../../../adr/0100-the-player-ships-with-the-line-output-and-the-speaker-waits.md).**
The shipping player has no Bluetooth, so there is no coexistence cost to measure. The numbers this
ticket was built to produce are in ADR 0100's table and on PL-08's ticket, and they are what
decided the output. If a speaker variant is ever built, this measurement is part of that work.

**What to build:** The one number the whole second output rests on: ten minutes of a 256 kbps MP3
stream inbound over Wi-Fi while SBC goes out to a speaker, with the gaps counted and the K7's
encoder frame cost recorded beside it. It is the board-only property
`docs/adr/0097-the-player-is-an-esp32-classic-in-its-own-project.md` names, and nothing in this
repo can establish it without the two radios.

**Blocked by:** 08 — The speaker output, paired by itself.

**Status:** proposed

- [ ] Ten minutes with both radios live: dropouts counted, the longest gap in milliseconds, and the ring's occupancy at its worst.
- [ ] The K7's encoder frame cost during it, against the band its boot log already records.
- [ ] The result written in the README's board tier with the date and the material, or the claim that Bluetooth output is not viable, with the number that says so.

Spec: `docs/specs/k7player.md` B4; `docs/adr/0097-the-player-is-an-esp32-classic-in-its-own-project.md`.

## Comments

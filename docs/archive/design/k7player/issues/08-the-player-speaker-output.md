# 08 — The speaker output, paired by itself

**What to build:** The wireless half of the seam: an A2DP source behind the same interface as the
line, SBC at 44.1 or 48 kHz, the bonded speaker stored in NVS and reconnected at boot, the 60 s
forget-and-pair rule that replaces a button, and the rule that a connected speaker wins over the
line.

**Blocked by:** 06 — The line output, and the first sound.


- [ ] A speaker pairs with no control surface, plays the stream, and is reconnected after a reboot.
- [ ] A speaker that does not answer for 60 s is forgotten, and the next speaker in pairing mode pairs without a button.
- [ ] SBC is configured and its bitpool reported; a 32 kHz station is played on the line and not on the speaker.
- [ ] A connected speaker takes the audio from the line automatically, and losing the link returns to the line rather than to silence.

Spec: `docs/specs/k7player.md` R5, R10, R12–R14, R27, B2–B3.

**What to build:** The Bluetooth speaker as the player's second output: an A2DP source that pairs
with no control surface, reconnects what it remembers, forgets what does not answer, and takes
the audio only when it can carry it.

**Blocked by:** 06 — The player's line output.

**Status:** deferred by [ADR 0100](../../../../adr/0100-the-player-ships-with-the-line-output-and-the-speaker-waits.md).
The shipping player drives the line only; this work stays behind `CONFIG_PLAYER_OUTPUT_SPEAKER`
for when a speaker answers the inquiry.

- [ ] A speaker pairs with no control surface, plays the stream, and is reconnected after a reboot. *(Needs a speaker.)*
- [ ] A speaker that does not answer for 60 s is forgotten, and the next speaker in pairing mode pairs without a button. *(The rule is host-tested; the radio path needs a speaker.)*
- [ ] SBC is configured and its bitpool reported; a 32 kHz station is played on the line and not on the speaker. *(SBC is configured and the sink's capabilities are logged when one connects; the 32 kHz half is `t06_bt`'s.)*
- [x] A connected speaker takes the audio from the line automatically, and losing the link returns to the line rather than to silence.

Spec: `docs/specs/k7player.md` R5, R10, R12–R14, R27, B2–B3.

## Comments

- **The rule and the radio are separate.** `player_bt` owns the clock and the order — a bonded
  speaker is reconnected, 60 s without an answer forgets it, a window of scanning follows, the
  first speaker that accepts "just works" is remembered — and it is host-tested against a
  scripted radio. `bt_a2dp.c` is the radio: Bluedroid, an A2DP source, inquiry, SSP, bonds.
- **The sink choice is the playout's** (R10): a connected speaker takes 44.1 kHz; anything else
  goes down the line. A dropped link re-opens the line on the next tick, so losing the speaker is
  a change of output, not silence.
- **The board found the price of having no button.** With the radio in inquiry the player's
  stream receives **nothing** (0 B/s), and with the radio idle it is **40 frames a second**: the
  classic ESP32's coexistence is enabled (`SW_COEXIST_ENABLE=y`) and still has nothing to give
  Wi-Fi while Bluetooth scans. A continuous scan would take the line output away to look for a
  speaker that may not be there. So a scan is **one inquiry round (15 s) followed by a five-minute
  rest**, repeating: a speaker in pairing mode is found in the window at power-up — R13's own
  remedy for a replaced speaker is a power cycle — and the line loses five percent of its time
  instead of half. The spec carries the measurement.
- **Bluetooth costs flash and DRAM.** The stack does not fit the 1 MB app partition: the player
  gained `partitions.csv` (nvs 24 K, phy 4 K, factory 3 MB) and the image is now **1.42 MB, 55 %
  free**. It also costs DRAM, which is why the ring went back from 16 frames to **8** (209 ms):
  the link failed with `region dram0_0_seg overflowed by 10 920 bytes` at sixteen. Internal free
  at boot with the radio up is **112 664 B (98 304 B largest)**.
- **Evidence, host:** `t06_bt` — **30 checks, 0 failures**: nothing remembered means scanning; a
  window with nothing means a rest, then another window; a remembered speaker is tried and
  forgotten after 60 s; a speaker that leaves is reconnected and then forgotten on the same rule;
  no radio is a state and not a crash; a connected speaker takes a 44.1 kHz stream, gives it back
  to the line when the link goes, takes it again on return, and a 32 kHz station stays on the line.
  `t01`–`t05` still green, both layers green.
- **Evidence, board:**
  ```
  board: esp32 classic: 2 core(s), revision 3.1; internal 112664 B free, largest 98304 B
  bt: radio up
  bt: nothing remembered: looking for a speaker in pairing mode
  playout: output open at 44100 Hz on the line
  ```
  During the window the stream stalls at 81 frames; once it closes, 84 → 181 → 757 frames over
  twenty seconds, which is 38 frames a second. That is the duty cycle working, and the line's
  silence during a window is the cost measured above.

## Both outputs, one active — and what it measured

The player keeps both outputs in its firmware and powers the one that is not in use off. The radio
is **started on demand** (a connect attempt, a scan window) and **shut down** when neither is
running: `radio_off` after the line logs *"radio off: the line has the air to itself"*. The bond
lives in NVS, so the player knows a speaker is worth waking the radio for without waking it first.

What the board says about the cost, all measured the same day with the same stream:

| Firmware state | Stream |
|---|---|
| Before Bluetooth existed in this firmware | **38–40 frames/s** |
| Bluetooth compiled in, controller never started | **~24 frames/s**, and it degrades in bursts |
| Bluetooth compiled in, radio up but idle | **3–5 frames/s** |
| Bluetooth in inquiry | **0 frames/s** |
| Coexistence arbitration disabled (radio off) | **23 frames/s** — no better, so it is not the arbiter |
| Heap over two minutes | **flat** (59 776 → 96 412 B, no drift) — so it is not a leak |

So: powering the radio off is necessary and correct (0 → 3–5 → 24 is a real ladder), but it is not
sufficient. The Bluetooth stack's *presence* still costs the stream about half its rate, and the
loss arrives in bursts rather than as a steady ceiling. That is a platform question, not a pairing
one, and it is exactly what PL-10 exists for: the cost of coexistence measured with a speaker on
the air, and — if the stack alone is the cost — the build-time split the spec currently forbids
(R10–R14).

## What is left

- **B2 and B3 need a speaker.** Nothing on the bench advertises A2DP.
- **48 kHz on the speaker is not delivered.** The source is configured at 44.1 (the station's
  rate), so the playout sends only 44.1 to it; R27's 48 kHz would need a resampler before the
  speaker, and a 48 kHz station would otherwise play at the wrong pitch.
- **The cost of a connected speaker** — how much the A2DP link takes from the stream — is PL-10's
  question, and it now has a baseline: 40 frames/s with the radio idle, 0 while inquiry runs.

## The decision that parked it

ADR 0100: the player's output is the line, and the speaker is a second firmware variant built
with `CONFIG_PLAYER_OUTPUT_SPEAKER`, off by default. The measured reason is on this ticket (the
stream halves with the stack compiled in, and dies under inquiry) and the bench reason is that
the one speaker here never answered the player's inquiry — its phone-reported address begins
`0x4B`, a random static BLE address, and a direct connection to it was refused.

The code is kept, not deleted: the A2DP source (`bt_a2dp.c`), the pairing rule (`player_bt.c`),
the sink choice in the playout and the seam are all still here, gated by that option. Turning it
on needs `CONFIG_BT_ENABLED` and its classic/A2DP set in the defaults, as the ticket records.

## Removed from the tree, 2026-11-18

The cleanup before the first commit removed the Bluetooth path entirely (ADR 0101): the A2DP
source, the pairing rule, the sink switch, the Kconfig option, the stub, the host fake's radio and
`t06_bt`. The player has one output and no radio, and its partition went back to the default 1 MB.

Everything this ticket learned is above and in ADR 0100: the pairing rule, the 60 s forget, the
scan window and why it rests, the stream cost of compiling the stack in, and the bench speaker that
advertised a BLE address and refused a classic connection. The code is in the history of the branch
that built it. If the speaker returns, start here and `git show` the removed files.

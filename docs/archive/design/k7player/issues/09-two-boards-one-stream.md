# 09 — Two boards, one stream

**What to build:** The wired path's acceptance, on hardware: a K7 with a network, a K7Player with
none, a share window opened on the K7's panel, and a player that finds it, joins, and plays — with
the evidence recorded in the README's board tier.

**Blocked by:** 02 — The K7's share window; 04 — The player's sweep, request and record; 06 — The
line output, and the first sound.

**Status:** done, 2026-11-18, both cases on hardware.

- [x] A window opened on the K7 is found by a sweeping player, and the record arrives encrypted.
- [x] The player joins the home network and plays; the K7 shows it as an ordinary listener (`client 0 connected`).
- [x] The AP fallback works: an unprovisioned K7 hands over `K7.local`, and the player streams from `192.168.4.1`.
- [x] A window left open expires on its own and leaves no peer behind.
- [x] The K7's memory census with a window open is recorded beside the boot log's numbers.
- [x] The player's heap with the stream, decoder and ring up is recorded.

Spec: `docs/specs/network-handover.md` T11–T16; `docs/specs/k7player.md` § 6.

## Comments

## The wired run, 2026-11-18

The player's network record was erased (`esptool erase_region 0x9000 0x6000`), so it booted with
no credentials and swept. The window was opened by hand on the K7's panel — there is no remote
trigger, by design — and both consoles were recorded through it. Everything in the checklist
above passed in one run:

```
K7:     share window open for 60 s on channel 1; internal free 56695 B before, 50071 B after, largest 31744 B
K7:     answered 3c:8a:1f:7f:f5:c8
K7:     share window closed
K7:     client 0 connected (icy=0, burst=19226 B); internal 47899 B free, largest 31744 B
player: heard a K7 on channel 1: asking
player: record stored: xperiments-2.4G and K7.local
player: joining xperiments-2.4G
player: the K7 is at 192.168.0.13
player: joined xperiments-2.4G at 192.168.0.24
player: internal 201020 free, largest 110592
light:  playing (net connected, stream streaming, audio 1, failed 0) level 255; internal 177212 free, largest 110592
playout: output 44100 Hz: ... 0 refused
```

The numbers are in the README's board tier (ADR 0100's line build: 908 KB in a 3 MB partition).

## The AP fallback, 2026-11-18

The K7's record was erased through its own setup surface (`POST /api/setup/forget`, reachable while
Wi-Fi → Setup is open), the K7 rebooted unprovisioned, and the player's record erased again. One
tap of Wi-Fi → Share, and:

```
K7:     share window open for 60 s on channel 1; internal free 53571 B before, 46963 B after, largest 31744 B
player: heard a K7 on channel 1: asking
player: record stored: (no home network) and K7.local
player: the K7 is at 192.168.4.1
player: joined K7.local at 192.168.4.2
stream: streaming 192.168.4.1: 44100 Hz, 256 kbps, 13 frames
```

The record says what the spec says it should: *no home network*, and `K7.local` as the network the
player can reach. The K7 then went back to its router through the same setup surface, and the
player needs one more window to take the home record — its record currently names the AP.

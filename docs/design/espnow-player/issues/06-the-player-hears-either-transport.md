# 06 — The player hears either transport

**What to build:** the HTTP client back, as the fallback behind the radio. The ESP-NOW receiver
stays up always; with no audio packet for three seconds the player fetches `/stream` and plays the
body through the same framer, ring and prime; the first audio packet after that closes the
connection and the radio is the stream again.

**Blocked by:** 03 (the receiver, done) and 05 (a way to choose).

**Status:** proposed.

- [ ] The HTTP client and its socket seam return, used only when the radio has been silent for three seconds.
- [ ] Both paths feed the same ring, the same prime, the same rate from the header and the same Fault rule.
- [ ] An audio packet preempts the HTTP body: the connection closes and the radio is the stream.
- [ ] The HTTP path keeps its own close-delimited and reconnect rules while it is the stream.
- [ ] `k7player.md` R23, R29 and R30 are the contract; the host tests cover the fallback and the preemption.

Spec: `docs/specs/k7player.md` R23–R30, ADR 0104.

# 06 — Encoder cost and memory are measured on the device

**What to build:** The device reports what encoding actually costs it, so the choice of station
format stops resting on a figure taken from someone else's unverified benchmark. Startup reports
the station format, the memory available in both memory classes, and which allocation path each
large buffer took; a running device reports encoder cost and free memory periodically.

This is the ticket that turns the specification's memory budget from arithmetic into observation,
and it is what makes the acceptance threshold for the recommended format decidable. The soak is
the evidence, not a formality.

**Blocked by:** 05 — The live stream is continuous MP3.

**Status:** ready-for-agent

- [ ] Startup reports the chip model, flash size, external memory size and mode, and free memory in both the internal and external classes.
- [ ] Startup reports which allocation path each large buffer took — external memory or internal fallback — so a device without external memory is diagnosable rather than mysterious.
- [ ] The encoder's per-frame cost is measured on the device, averaged over at least a thousand frames, and logged.
- [ ] Free memory in both classes is logged periodically, at least once a minute.
- [ ] A one-hour soak with a client attached to the live stream shows encoder cost below the threshold stated in the specification, free memory stable across the hour, and no starvation rebase events logged.
- [ ] The same measurement is available as a host baseline, and is recorded as scaling data only — never as a pass or fail, because host processor speed says nothing about the device's.
- [ ] If the threshold is exceeded, the documented fallback is a **lower** rate and bit rate. Raising the bit rate is not an available response to a cost problem.

Spec: `docs/specs/sd-stream-server.md` §4.3 F4, §2.4 (P2, P3), §4.1, §9 T11, T11h.

## Comments

# 10 — Somebody else can build, flash and deploy this

**What to build:** A person who has never seen this project can take the repository, follow the
readme, build it, flash a board, put audio on a card, and get a working stream and library. They
also learn, before they deploy it anywhere, that the device serves an unauthenticated library and
that its network credentials are compiled into the binary.

The packaging obligation is also a licensing one: the vendored encoder's licence text and notice
must be present, or the arrangement that permits distributing this project under its own terms is
not satisfied.

**Blocked by:** 05 — The live stream is continuous MP3; 08 — The card's library is browsable and downloadable.

**Status:** ready-for-agent

- [ ] The readme covers building, flashing, the card layout the device expects, the pin assignment for the card bus, and the button that skips to the next track.
- [ ] The readme states the external-memory mode must be set explicitly to match the fitted chip, that it is not detected automatically, and that the wrong mode corrupts memory at boot.
- [ ] The readme states, where a deployer will actually read it rather than in a footnote, that the device serves a live stream and an unauthenticated file index, that its network credentials are compiled into the binary, and that it must not be placed on an untrusted network or exposed to the internet.
- [ ] The vendored encoder's licence text and a third-party notice naming it are present in the repository.
- [ ] Component metadata carries version, description, licence, the supported target, and a minimum framework version.
- [ ] A clean checkout builds and flashes following only the readme, on a machine that has never built this project.
- [ ] The project's own components' licensing is unaffected by the vendored encoder's.

Spec: `docs/specs/sd-stream-server.md` §8 (L1, L2, L4), §7 A1, §2.2, §2.3, §10.3.

## Comments

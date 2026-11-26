# 06 — Prove the flash and the install on the real origin and the board

**What to build:** the evidence in [`docs/specs/k7-site.md`](../../../../specs/k7-site.md) §6, and the
answers its §7 is missing. Nothing here is a host test that pretends to be a board one.

- Run the board checklist B1–B7 and record each result in the spec, with the numbers:
  how long a flash takes, the baud rate the bridge sustains, and the bridge's chip (the driver
  question, §7.1–2).
- Re-sign installability (A13) on the deployed origin: the omnibox icon appears on `/`, the
  install opens `/app` in its own window, the maskable icon is not cropped, and an existing
  install opens `/app`.
- Check the Site on Safari, Firefox and a phone: the sentence, the manual path, and no dead
  control.
- Update the README's **Build and flash**: the Site is the normal way to put K7 on a board;
  `idf.py` stays as the developer path. The two-tier acceptance keeps its rule: a host test is
  not a board claim.
- Settle the open questions the pass answers, and leave the ones it does not — the photograph is
  content, not a ticket.

**Blocked by:** 01, 02, 03, 04, 05.

**Status:** pending

- [ ] B1–B7 have a result, a date and the evidence line for each; anything not run says so.
- [ ] B2 is the one that matters most: a reflash over a working device keeps its WiFi record,
      rejoins its network, and its Snapshot and playlists are intact.
- [ ] B3 proves the erase is recovery and not data loss on the card.
- [ ] The flash duration and the sustained baud rate are recorded as measurements, not estimates.
- [ ] The deployed origin's headers, rewrite and worker match §5 of the deploy spec.
- [ ] The README's flashing section names the Site first and `idf.py` second.

**Files:** `docs/specs/k7-site.md` (the measured section), `README.md` (Build and flash),
`web/public/firmware/0.5.0/` (read only).

**Tests:** k7-site.md B1–B7.

**Source:** ADR 0078, ADR 0079.

## Comments

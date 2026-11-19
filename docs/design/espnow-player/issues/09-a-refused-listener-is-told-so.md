# 09 — A refused listener is told so

**What it builds:** the second tab's refusal is correct and its sentence was not. A browser's
audio element cannot read the 503 body, so the page said *The stream stopped; reconnecting…* to
someone who never started, and retried every ten seconds until the first listener left.

**Blocked by:** EN-07 (done).

**Status:** built. The device half is checked on the board; the page half is checked on the host,
because it ships with the App, not with the firmware.

- [x] The device reports its stream slots: `/api/status` gains
      `"listeners":{"used":0|1,"slots":1}`. The broadcast counts them under its own lock, so the
      number is the client table's, not a second guess.
- [x] The page tells the two failures apart with a fact it already has: whether **this** page ever
      played the stream. A refusal arrives before the first `play`, so a stream that never played
      and finds every slot taken was refused; a stream that was playing lost its link, even if its
      own slot is still counted for the instant before the device reclaims it.
      `web/src/ui/stream-slot.ts` is the rule, and it is pure.
- [x] The sentence: *Another page is listening. This one starts when it closes.* While waiting,
      the page asks the status every two seconds and reconnects itself when the slot frees — no
      ten-second backoff, and no pretending it lost a stream.
- [x] Firmware that predates the field reports no `listeners`; the page then reads "not known" and
      reconnects as before, which is the safe default: waiting for a slot that may be free would
      hide a real failure.
- [x] A stream that had been playing still reconnects at once, without asking.

**Checked:** on the board, `listeners` went `{used 0, slots 1}` → `{used 1}` while a stream was
held → `{used 0}` a second after it was released. On the host: `web/tests/stream-slot.test.ts`
(5 cases), four new cases in `web/tests/status.test.ts`, two in `host/tests/t06_status.c`, and
`make -C host check`, `npm run typecheck`, `npm test` (896) and `npm run build` all pass.

**To see it:** the page is served from the deployed origin, so this needs the App deployed (or
`npm run dev`); the firmware only needed the status field.

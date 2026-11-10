# 08 — Prove the label on the panel

**What to build:** the board evidence. The host tier cannot see a glyph, a mask or a gesture, and
two of the three are the feature.

**First, the door.** ADR 0068 recorded that the GT911's two-contact report has never been
observed: `[GDBG] contacts=` is logged on every change of the contact count and no log has ever
shown 2. Until it does, the two-finger screen cannot be reached by hand and the label cannot be
seen. So the first step is a finger pass on the serial log. If the driver never reports two
contacts, that is a fix of its own — ADR 0014's board pass is still owed — and it blocks this
ticket rather than being worked around; a temporary forced `CONTROLS_INTENT_FULL_ON` may be used
to check the drawing meanwhile, but the ticket stays open.

**Then the screen**, on the Sunton, flashed with `./tools/build.sh sunton flash`:

- the two-finger pose is the design: paper, bands, rules, the title and the artist where the
  design put them;
- the Card's window is a hole through the design, and the reels turn through it without the label
  repainting — the tick must not touch the 748x359 print;
- a Track change re-texts in one frame, and a Track whose `ink_seed % count` lands elsewhere
  changes design;
- a card with no `k7.db` (or an unanalysed file) prints the design with blank fields;
- a one-finger step off the pose gives the print back to the Cover, and back again;
- the design's own faces are the ones on the glass (ticket 05): the numeral is Barlow Condensed,
  not Jura.

**Blocked by:** 06, 07 — and the gesture, above.

**Status:** open, but the open question is answered — **on 2026-11-10 the owner pressed two
fingers on the Sunton and the design drew.** The gesture is reachable and the picture is right,
which is what ADR 0071 recorded as owed and what this ticket was blocked on.

The boxes below are still unticked, because they ask for the *artefact* rather than the
observation: the `[GDBG] contacts=2` line, a frame, and the reel tick's measurement. None of them
is in doubt now; none of them has been captured.

- [ ] `[GDBG] contacts=2` on the log from two fingers. *(or this ticket stays open)*
- [ ] A photograph or a `tools/capture_screens.py` frame of the two-finger pose showing the
      design, attached to this ticket.
- [ ] The reel tick does not repaint the print: measured (a dirty-area log or the frame's own
      timing), not assumed.
- [ ] A Track change and a design change both land in one frame, seen on the panel.
- [ ] A card without a Snapshot shows blank fields, not stale ones.
- [ ] The numbers go where the repo keeps them: this ticket's comments, and the README's
      two-tier section if it carries the boot/screen numbers already.

**Files:** none — evidence and comments only, unless the pass finds a defect.

**Source:** ADR 0071; ADR 0068's owed gesture.

## Comments

- **Flashed and booted.** `./tools/build.sh sunton flash` wrote 1,966,048 B, esptool verified the
  hash and the board came up once (`rst:0x1 (POWERON)`), with no panic, no assertion and no
  reboot. That is board evidence for the pipeline behind the screen: the packer's sheet is in
  the image, the reader opened it, and a design was built.

  ```
  I (7172) label: label sheet: 1 designs
  I (7261) screen: cassette 2 of 7: (no track)
  I (7596) screen: cassette 2 of 7: 01 - El Sol De Verano
  I (7610) label: design 0 of 1: K7 reference
  ```

  The design is built when the first Track starts, which is the path that also resolves its
  Snapshot row - so `library_acquire()` and `app_services_snapshot()` both ran on the device
  without a fault.

- **Still owed, and it needs a hand:** `[GDBG] contacts=2` from two fingers, then the picture of
  the two-finger pose. No amount of host or build evidence can stand in for that, and the
  gesture has been owed since ADR 0014. When a finger is available:

  1. `timeout 25 ./tools/build.sh sunton monitor` and put two fingers on the panel;
  2. look for `[GDBG] contacts=2` - if it never appears, the GT911's two-contact report is the
     defect and this ticket stays open behind it;
  3. with the pose up, check the four things this ticket lists: the design's paper and type on
     the glass (Barlow Condensed as the numeral, not Jura), the Card's window as a hole with the
     reels turning through it and the print not repainting on the reel tick, a Track change
     re-texting in one frame, and a card with no `k7.db` printing blank fields.

- If the gesture cannot be settled, the drawing can still be checked with a temporary
  `CONTROLS_INTENT_FULL_ON` at boot: that proves the renderer and the mask, but the ticket's
  first checkbox is the gesture and it stays unticked either way.

- For the measurement above the pose was reached by a forced `CONTROLS_INTENT_FULL_ON`
  (`full=1 card=1` in the log), not by a finger. That is enough to price the pose and to prove
  the design builds; it is not the gesture, and the picture is still owed.

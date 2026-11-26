# 06 — Draw the design on the two-finger print

**What to build:** `components/app/screen_display/cassette_label.c` — the view that turns the
selected **Label template** into objects inside the print, and keeps them fed —
[`docs/specs/label-templates.md`](../../../../specs/label-templates.md) §6.

```c
void cassette_label_create(lv_obj_t *print);              /* nothing when !screen_caps()->cover */
void cassette_label_set_pose(bool full);                  /* show/hide with the two-finger pose */
void cassette_label_update(uint16_t template, const label_values_t *values);
```

- **Build once per template.** `create()` opens the embedded sheet and validates it; on the first
  `update()` whose `template` differs from the one on screen, the objects are deleted and rebuilt:
  a styled box per rect, an `lv_label` per text, in sheet order, as children of the print. The
  same `template` only re-sets the texts — a Track change is not a rebuild.
- **The cut is free.** Children of the print take the Card's A8 mask exactly as the Cover's
  picture does, so the window shows the tape and the print's box clips the rest. Nothing here
  draws a mask.
- **The pick is the model's.** `cassette_label_update` is handed the template index, computed in
  the view as `label_pick(&sheet, frame.ink_seed)` so shell and design always pair.
- **Values arrive resolved.** The caller fills `label_values_t` (ticket 04) on a Track change; each
  text reads its slot by name, cuts at the slot's `max` with `label_cut()`, and sets it.
- **Hidden with the pose**, and the Cover's picture takes the print back at the Controls pose.
- **A sheet that fails to open** leaves the flat palette fill and logs the reason once: that is a
  build bug, not a card state, and the screen must still work.

**Blocked by:** 01, 04 (the reader and the values), 07 (the sheet is embedded), and the fonts of
05 only for the designs that use them.

**Status:** done (the Sunton build compiles it; the panel is ticket 08's)

- [ ] The two-finger pose shows the design (needs the panel); a one-finger step off it hides the design and shows
      the Cover again, in one frame.
- [x] A Track change re-texts without deleting objects; a different `ink_seed % count` rebuilds.
- [ ] The Card's window is a hole in the design (needs the panel), and the reels still turn through it.
- [ ] A design with a blank slot (no Snapshot row) prints its furniture and nothing where the
      value goes.
- [x] `cassette_cover_print()` is the only new seam in the Cover module; this module owns no
      geometry and no colour of the Card's.
- [x] An invalid sheet logs once and leaves the flat fill.

**Files:** `components/app/screen_display/cassette_label.c` (new),
`components/app/screen_display/cassette_label.h` (new),
`components/app/screen_display/cassette_cover.h` and `.c` (one accessor for the print),
`components/app/screen_display/cassette_view.c` and `.h` (the pose, the Track change, the pick),
`components/app/screen_display/CMakeLists.txt`,
`components/portable/app_services/include/app_services.h` and `app_services.c` (one accessor for
the opened `snapshot_t`).

**Coordination:** `app_services.c` is held by another lane in this tree (it is modified and
uncommitted there). Land this ticket's accessor after that work, or as its own small commit.

**Tests:** the build-time packing and the reader are host-tested; this module's behaviour is a
panel fact, so ticket 08 is its test. Its arithmetic — the pick, the rebuild rule — should stay
small enough to read.

**Source:** ADR 0071.

## Comments

- `cassette_label.c` builds one container the size of the design's space inside the print -
  the container is what makes 748x359 coordinates mean the same thing at both Card layouts - and
  one object per prim: a styled box for a rect, an `lv_label` for a text.
- The design is rebuilt only when the picked template changes; a Track change rewrites the
  eleven texts. Rotated prims turn about their own centre, and a rotated text re-pivots when its
  words change, because the centre moves with them.
- Two new seams: `cassette_cover_print()` (the print, so the label needs no geometry of its own)
  and `app_services_snapshot()` (the opened Snapshot, borrowed for one step). The view creates
  the label after the Cover with the embedded sheet, hides and shows it with the FULL bit, and
  re-resolves when the seed or the path changes - `library_acquire()` around the read.
- `app_services.c` was carrying another lane's uncommitted work when this ticket took it; the
  accessor is one line and their change is intact in the same file.
- The Sunton build compiles it with no warnings. **The panel behaviour - the window as a hole in
  the design, one frame for a Track change - is ticket 08's.**

### The pose was slow, and it was not the label

The first build on the panel felt slow at the two-finger pose. Measured with the screen task's
own frame-cost log (a temporary 50-frame period and a forced `CONTROLS_INTENT_FULL_ON`), per pass:

| | avg | worst |
|---|---|---|
| Controls pose | 4-5 ms | 20 ms |
| Two-finger pose, label shown | 13 ms | 98 ms |
| Two-finger pose, label hidden | 18 ms | 97 ms |
| Two-finger pose, **label never built** | 11-21 ms | 89-243 ms |

So the label was exonerated by an A/B: removing it changed nothing. The cost is the pose's own
reel tick - a 556x166 window repainted 25 times a second inside the Card's 424 KB masked ARGB
layer - and the screen task's `rest = 3 x frame` controller then turns that into a ~60-90 ms pass.

**The fix is in the view, not the label.** `cassette_view` parks the tick at the pose: the tape is
drawn when the pose opens (from the last frame the model gave), when the Track changes and when it
closes, and nothing between. Same measurement after: **avg 1 ms, worst 4-5 ms**, and the pose's
pass is 41 ms - the same cadence as the Controls pose. The entry is one 75 ms frame.

The first fix was to park the tick, which is fast but stops the reels; the owner wants them
turning, so the search went further.

- **A pose-only rate limit does not work.** 10 Hz still measured avg 28-51 ms with 276-814 ms
  spikes when the rewind moved the packs, because each tick still recomposites the masked Card
  and each pack redraw is clamped to the 556x166 window.
- **One marks object per hub (fewer refresh areas) does not work.** It made the *Controls* pose
  worse (avg 23 ms against 4-5): a bigger box forces the pack's ring callback to redraw every
  tick, and that costs more than the areas saved. Reverted.
- **The porthole looked like it did, and then did not.** `CARD_MASK 0` was already in the tree,
  abandoned when it used `clip_corner`. Its measurement of 4-7 ms at the pose turned out to be
  against the *Controls* window: the capsule had never been re-placed on a layout change, so the
  full-screen pose was showing a 291x87 window (which the owner spotted immediately). With the
  capsule following the layout - 556x166 r83 - the porthole measured **20-60 ms** a pass, worse
  than the masked cut's 11-21, and `CARD_MASK` went back to 1 with the layout-following capsule
  kept for the record.
- **What the cost actually is.** Not pixels and not content: the screen task's own lock and
  update measured 0-6 and 1-4 ms while the pass was 26-47 ms, with 4.5-20K px dirtied in 3-8
  boxes. Each refreshed rectangle is a masked layer plus a panel flush, and at the entry grid
  each is several times the Controls pose's. Eight small wedges, one box per hub and a per-hub
  object above the tape were each measured; the best of them is the rate limit.
- **The shipped compromise:** the pose's tick runs at 8 Hz (`CASSETTE_POSE_TICK_MS`), measured
  11 ms a pass in the pose's first seconds and 38 ms twenty seconds later with no Track change -
  a drift this change does not explain and that is owed a profile. The owner's suggestion of one
  object per mark angle would only merge rectangles if all the angles shared one box, which is
  the "one box per hub" row above: it measured worse.

The opening paragraph of this ticket stands; `cassette-card.md` ("The mask") records the
measurement and keeps the masked cut documented as the path `CARD_MASK 1` still describes.

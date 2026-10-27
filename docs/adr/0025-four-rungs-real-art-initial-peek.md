# Four fixed screens in a stack: Real, Art, Initial, Peek

**Supersedes in part** [ADR 0016](./0016-transport-drawer-and-horizontal-swipes.md) (the
horizontal toggle), [ADR 0020](./0020-down-swipe-controls-and-the-white-page.md) (the down
swipe's white Page) and [ADR 0021](./0021-white-page-cover.md) (the cover as a rung of its
own). Amends [ADR 0014](./0014-three-level-swipe-order-and-menu-gesture.md) (the order) and
[ADR 0022](./0022-the-menu-is-a-controls-mode.md) (the pages the Menu opens on). Retires the
two-finger VU gesture and parks the meter.

The cassette screen had been read as one overlay stack - Bare, the Peek, the Controls - with
the Controls on **two pages**: the dark one, which either horizontal swipe opened, and the
white one, which a down swipe opened with the Track's picture a further down. The owner now
reads the screen as **four fixed screens in a vertical order**, and gives them names:

| rung | name | what is on screen |
|---|---|---|
| top | **Real** | the controls on the white Page, the Track's picture on the cassette |
| | **Art** | the controls on the dark Page, orange line art only |
| | **Initial** | the full cassette, nothing over it |
| bottom | **Peek** | the timeline bar and the two floating tags |

"We init every time at initial. Swipe up from initial moves to peek. Swipe down from peek
returns Initial. Swipe down from initial moves to Art. Swipe down another time moves to
Real." The other half - the two-finger VU gesture - goes: "I will emplace the vu-meter
another side later". And the horizontal swipes go with it: "We only have an swipe left in the
pad as counting all gestures", which is the Menu's Back.

## Decision

- **The ladder is one value, and the enum is the stack.** `controls_level_t` is
  `PEEK, INITIAL, ART, REAL`, bottom first, so a down swipe is `level + 1` and an up swipe
  `level - 1`. The ends stop: a down at Real and an up at the Peek change nothing. The order
  is the owner's; the arithmetic is only how it is written down.
- **Real is the Page and the Cover as one rung.** ADR 0021's cover was a second state on the
  white Page; now the picture is what the white Page is for. A down from Art flips the page
  and fades the picture in as one step, an up undoes both. The white Page without a picture
  stops existing as a state.
- **Two settled-pose guards, and only two.** A down from Art needs `at_controls_pose`: the
  picture's mask is built at the controls pose (ADR 0021), and a swipe during the shrink is
  refused rather than printed at the wrong size. An up from Initial needs `at_entry_pose`:
  the Peek is raised from the entry view, never from a slide (ADR 0020's rule, kept). The
  down into Art has no guard - the chrome cuts in while the cassette is still moving - and
  neither do the other three rungs.
- **Art and Real share the Controls' room**, so moving between them moves no pose: the step
  is the page flip (one frame, as ADR 0020 chose, because a colour fade repaints the whole
  panel) under the picture's 1 s fade. In both directions the flip is immediate and the
  picture fades over whatever page is now painted; the up therefore fades the picture out
  over a page that has already gone dark.
- **The state is `{ level, menu }`.** The Page, the Cover, the Peek and the chrome are
  readings of the level, not fields: `page_white` and `cover` become `level == REAL`, the
  chrome is out at Art and Real, and the one-overlay rule is true by construction. The mask
  keeps one bit per group the view applies - `PAGE` (flip **and** picture, in that order),
  `PEEK`, `SHOWN`, `MENU`, `ROOM` - and `ROOM` means the room changed, so Art <-> Real is not
  a `ROOM` step.
- **The horizontal swipes are retired at the screen.** `screen_input` reports the vertical
  pair only; `BUTTONS_SHOW`/`BUTTONS_HIDE` and their two intents are gone. The drawer and the
  DJ block are reached only as Art (one down swipe from Initial) and stay as Real. The Menu's
  Back is a left swipe on the Pad, consumed by the bar, and is the only left swipe in the
  build.
- **The two-finger gesture is removed end to end.** `touch_gt911`'s contact-pair detector and
  its callback, `screen_input`'s latch and press handler, `SCREEN_INTENT_VU_*`,
  `CONTROLS_INTENT_VU_*`, the meter's wish and `CONTROLS_CHANGED_METER`, and the `reserved`
  term in `cassette_button_is_tap()` all go: the five buttons now share a two-term rule.
- **The meter module is parked, whole.** `cassette_meter.c/h` is untouched: the view still
  creates it, places it with the pose and feeds it every frame; nothing calls `show()`. It
  starts hidden and stays hidden until it has its new home.
- **Real exists for every Track.** A Track with no picture, or a decode that failed, still
  has the white Page and the chrome; the picture is best-effort, as it was on the white Page.

## Considered options

- **Keep the horizontal toggle as a shortcut into the chrome.** It would jump straight to Art
  from anywhere, so the ladder would stop being the only path, and the sideways gesture the
  owner is retiring would still be there.
- **Keep the white page without the picture as a state of its own.** Five rungs instead of
  four: the owner's order names four, and a page whose only purpose is to be the picture's
  ground is not one of them.
- **Make the cover a fifth rung above Real.** The same five-state problem in the other
  direction, and it keeps ADR 0021's extra swipe between the paper and the picture.
- **Flip the page white at once and show the picture when the shrink lands.** A double down
  from Initial would never be lost, but Real would exist for ~200 ms with no picture and the
  view would need a pending-cover state - the thing the model exists to avoid.
- **Delete the meter module with the gesture.** Cleaner tree, but the owner is re-homing the
  meter and its rows, badges and lit fill are the parts worth keeping.
- **Wrap the ends** (Real's down to the Peek). Faster cycling, but then Art and Real are also
  one swipe from the Peek in one direction, and "one rung per swipe" stops reading.
- **Reach the Peek from Art's up.** It would skip Initial and take the entry-pose guard with
  it; the stack's whole point is that every move is one rung.

## Consequences

- `controls.h`/`controls.c` are rewritten around `controls_level_t`, and
  `host/tests/t24_controls.c` with them: six states (four rungs plus the Menu on Art and on
  Real) x four intents x the fact sweep, the mask pinned as an exact diff, and a walk up and
  down the whole stack. `t21_button.c` loses the `reserved` combinations.
- `cassette_view.c` applies `PAGE` as the flip plus `cassette_cover_set_shown(level == REAL)`
  and no longer touches the meter's wish; the page comment, the build comment and the header
  now describe the ladder instead of the two ways into the controls.
- `screen_input.h/.c` keep two intents, one gesture handler and no latch;
  `touch_gt911.h/.c` lose the callback, the midpoint detector and their state;
  `cassette_menu.c`, `cassette_eq.c` and `cassette_button.c` lose their latch reads.
- The chrome is only reachable through the ladder. The transport's P, `<`, `>` and the EQ
  knobs are one down swipe from Initial (Art) instead of one sideways swipe, and there is no
  way back to the full cassette except an up swipe.
- The picture is one swipe **closer** than it was: two downs from Initial, where the white
  Page used to be, instead of three.
- A quick double down from Initial lands in Art, not Real: the second swipe arrives before
  the controls pose has settled and is refused, and the next one takes the rung.
- The picture fades out over a page that has already flipped dark. It reads as a dissolve in
  the animation model; it has not been checked with a finger on the panel yet.
- The white page's edge mark no longer waits 260 ms for the chrome's entrance before it fades
  in: that entrance can never coincide with the flip any more, because the flip is only
  reachable from Art, where the chrome has already landed. `EDGE_GLOW_DELAY_MS` is gone and the
  mark fades from nothing immediately.
- ADR 0016's horizontal toggle, ADR 0020's "the down swipe shows the controls on the white
  Page", ADR 0021's cover rung and ADR 0022's "the Page it was opened on" (the Menu can now
  be over Art or Real) are superseded or reworded by this one; the drawer, the pose, the
  white Page's ink and the Menu itself are unchanged.
- `.scratch/html-sim/specs/04-peek-and-gestures.md` describes the old state machine and is
  stale; `.scratch/` is not tracked.

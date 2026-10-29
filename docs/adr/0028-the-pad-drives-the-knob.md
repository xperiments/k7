# The Pad drives the knob; the dial only shows it

**Amended by [ADR 0036](./0036-the-value-marker-is-half-its-length.md):** the value marker's
sizes are 4 x 6 at 1x and 5 x 9 at 3x; it is no longer the longest line on the dial.

**Amended by [ADR 0029](./0029-the-dial-is-quiet.md):** the blink this record left in place is
gone, with its two targets and the bank's `cell_disc` hook; the dial sits 20 px below the pillow's
row; and a stepped knob prints one plaque per position instead of a title pillow.

**Amended by [ADR 0034](./0034-the-mode-bar-puts-the-dial-away.md):** the mode bar is no longer
inert while a dial is up. A tap on any of its three buttons ends the edit and runs nothing else, so
the Pad's leftward swipe is no longer the only way out.

**Amends [ADR 0027](./0027-the-knobs-are-dragged.md).** That record made the dial a control: a
drag on the 3x surface trimmed the value, a tap on the centre closed it, a tap on a plaque jumped
to that end. This one takes the touch off the dial and puts it on the Pad.

The owner, after using it:

- "A lot of times the direct knob adjusting works but tends to open knob at end."
- "Big knob 'lines' colour are too dark, make it brighter. Also grow a little the width of the
  'value' lines."
- "Hide the right menu when knob editing, show the same pad we use in menu for navigating,
  removing any touch event from the knob itself - we use the pad for all the actions."

The third is the structural one, and it settles something the first record left half-decided: the
screen has **one** way of navigating a mode, and it is the Pad. The Menu already used it; a knob
now does too, so the two are the same surface with a different target.

## Implementation status

*Landed. The audio path, the ranges, the panels' cells and the groups model are untouched.*

- **`cassette_pad`** is a module of its own, extracted from `cassette_menu.c`: the plate (white
  card, dark inner, dotted guide), the press machine (`pad.h`) and a **target** - `press`, `step`,
  `tap`, `back`, `release`. `cassette_pad_set_target()` aims it; a NULL target leaves it inert. The
  Menu installs its ratchet; the knob's editor installs its own.
- **The overlay is gone.** The editor no longer builds a full-screen click catcher, and the dial
  takes no touch at all: no drag on it, no plaque taps, no centre-circle close. While a dial is up
  the right bar shows the Pad, and the mode bar's three buttons are inert (each callback checks
  `knob_editor_is_editing()`).
- **The editor borrows the bar.** `knob_editor_set_bar(claim, release, ctx)`; the view implements
  `claim` as "install `cassette_pad_panel()`" and `release` as "`groups_install()`". A panel's
  `shown(false)` no longer drops the bank while the editor is the one taking the bar, and a
  panel's `repaint`/`cell_disc` hooks return early when its cells are away - the cells are deleted
  by the swap and come back with the group.
- **The Pad's three actions**: ratchet = the value (4 px a step, magnetised, published live);
  tap = **cycle the knob's stops** (`knob_cycle()`: -12 → 0 dB → +12 → -12 for the trim, -26 → 0 →
  +6 for a band, one slot at a time for a selector); left swipe = close.
- **`knob_cycle()`** is new in the model: the next stop above the value, wrapping at the top, where
  a stop is the knob's neutral, its marks and - on a stepped knob - every slot. `t22_knob` pins it.
- **The click after a drag is dropped by the drag itself** (`s_swallow_tap`, consumed once by
  `knob_editor_tap()`): LVGL's own moved/gesture state could not tell a short trim from a tap, so
  trimming a knob used to open it.
- **Ink**: the value marker is the widest, brightest line on the dial (5 x 18 at 3x, 4 x 11 at 1x),
  the two ends and the detent use the same bright tick ink at 2 px, and the graduations dropped
  from the dark ink to the flat red. The marker used to be the *darkest* line of the three.

## Decision

- **One control per mode: the Pad.** A dial is read, not touched. Everything a dial can do is on
  the Pad, and the Pad is already how the Menu is navigated, so the screen teaches one gesture
  vocabulary instead of two.
- **The Pad is a surface with a target**, not a Menu feature. `cassette_pad` owns the plate and the
  press machine; the Menu and a knob's editor are targets. Extracting it was the price of reusing
  it, and it removes the last piece of chrome that only the Menu knew how to draw.
- **The editor takes the bar and gives it back**, through two callbacks the view implements. The
  editor still knows no panel value: the bar, the groups and the Pad are the view's, which is the
  same seam the Menu's restore hook already used.
- **The group's cells are absent while the dial is up.** That is what "hide the right menu" means
  in a bar that owns its cells, and it is why the bank's hooks must tolerate a panel with no
  objects: a value can move while nothing of the panel's is on screen.
- **A tap cycles the stops, it does not reset.** The owner chose the cycle over the reset: the
  three positions a knob means to be visited at are the three its plaques name, so a tap is the
  plaque jump that survived the plaques losing their touch. Reset is still the double tap on the
  small knob, which is not available while the dial is up.
- **The dial's lines are read at arm's length.** The value marker gets the ink and the width the
  ends had, the ends get the bright ink, the graduations the flat red - a three-step hierarchy by
  brightness instead of the old one, where the value line was the darkest thing on the dial.

## Considered options

- **Keep the overlay and put the Pad above it.** Rejected: two blockers for one screen, and the
  overlay then needs a hole exactly where the Pad is - four extra objects and a rect that has to
  track the layout.
- **Keep the plaque taps** (they were the "tap a pillow to jump" the owner asked for in ADR 0027).
  Rejected by "removing any touch event from the knob itself"; the Pad's tap keeps the behaviour
  without keeping the target.
- **Tap = reset to neutral.** Rejected by the owner in favour of the cycle.
- **Keep the dial's drag as well as the Pad's ratchet.** Rejected: two controls for one value at
  two resolutions, and the one that opened the dial by accident.
- **Give the knob its own copy of the Pad.** Rejected: two plates that drift the first time either
  is tuned, which is the same reason there is one dial.

## Consequences

- The dial lost every hit test, so `knob_editor` lost its overlay, the centre-close, the plaque
  jump and the dial's drag. What is left is a display and the timer that opens it.
- The bar's panel can now change while a knob is open, so a bank has **two** states its hooks must
  handle: cells present (nothing open, or another knob) and cells absent (the dial is up). Both
  panels guard on `s_shown`.
- The mode bar is inert while a dial is up. It was already inert in practice - the overlay ate the
  press - but now that is two `if` statements rather than an object.
- The Menu and the knob editor can never both own the Pad: the controls model refuses to open the
  Menu while editing, and the editor cannot open while the Menu owns the bar.
- A selector's tap now walks its slots one at a time, which is a gesture a list would have given
  for free and a dial could not.

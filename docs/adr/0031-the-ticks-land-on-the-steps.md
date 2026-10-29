# The ticks land on the steps

**Amends [ADR 0029](./0029-the-dial-is-quiet.md)'s graduations** (which were a fixed 22.5 degree
grid) **and nothing else.**

The owner: *"small line dividers must adjust angle division to match the step points."*

The dial's graduations were thirteen lines every 22.5 degrees, whatever the knob was. On a
continuous knob that is a scale with no claims: any tick is as good as any other. On a **stepped**
knob it is a lie - the steps sit 54 degrees apart on the effect selector, so eleven of the thirteen
ticks pointed at positions the knob can never be on, and only the value marker was ever true.

## Resolution

- **A stepped knob gets one tick per slot.** `count = steps`, `step = 2700 / (steps - 1)` tenths of
  a degree, so six steps are six ticks 54 degrees apart, both ends included, and the value marker
  lands on a tick at every position.
- **A continuous knob keeps the 22.5 degree grid** (thirteen lines): where there are no steps there
  is no claim to match, and a finer grid would read as one.
- The painter decides from the knob, not from a flag the panel sets: `sweep == STEPPED` is the
  same fact `knob_step()` and `knob_cycle()` already read.

## Considered options

- **Ticks per mark** (the labelled positions rather than the slots). Rejected: a range knob's two
  ends would then have two ticks and an empty ring between them, and the slots are the positions
  the knob can actually take.
- **No graduations on stepped knobs at all.** Rejected: the ring is the dial's scale, and a
  selector with six labels and no ticks is a dial that cannot show where it is between two words.
- **One tick per slot for every knob.** Rejected: a continuous knob has no slots, and inventing
  a hundred ticks would fill the ring solid.

## Consequences

- A stepped knob's ring now has as many ticks as it has values - six for the effect selector,
  which is why the labels' 54 degree spacing and the ticks agree.
- `knob_cell` reads `sweep` and `steps` directly to size the grid, so a panel that changes a
  selector's step count gets the right ticks without touching the view.
- The two-ended plaques (a range's -26 and +6) are unaffected: they are placed on the ring at
  their values' angles, and a range knob's grid is still the 22.5 degree one.

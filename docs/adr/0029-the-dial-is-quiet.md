# The dial is quiet: no blink, and a selector is named by its positions

**Amended by [ADR 0030](./0030-the-labels-sit-in-a-box.md):** the plaques no longer sit on the ring.
A knob with more than two labelled positions prints them in a box instead - three stacked each side
and one above, at most seven - and the container is 456 px wide because the columns define it.

The owner, after using the Pad-driven dial:

- "Quitemos el parpadeo del knob, no tiene sentido."
- "Bajemos un poco el knob grande como 20 pixeles para alejarlo del label superior."
- "Para los knobs con valores textuales como los de seleccion efecto rgb, quiero ver un pillow
  label al lado de cada posicion y quitar entonces el central de arriba."

Three presentation decisions, one subject: the dial is a **reading**, so everything on it either
says where the value is or gets out of the way.

## Resolution

- **No blink.** The cap's border used to breathe (100 % → 27 % → 100 %, 600 ms each way) while a
  knob was open, on the small cap and the big one. It said "you are here" when the Pad already
  says it, it animated the surface the user is *not* touching, and since [ADR
  0028](./0028-the-pad-drives-the-knob.md) took the bar for the Pad the small cap is not even on
  screen while the dial is up - so the animation was drawing attention to a reading with a pulse.
  Removed with its two targets: the `lv_anim`, `BLINK_MS`/`BLINK_MIN_OPA`, `knob_cell_disc()`, and
  the bank's `cell_disc` hook.
- **The dial drops 20 px.** The pillow's row ended at y=124 and the twelve o'clock mark began at
  y=132, so the marker read as part of the pillow's furniture rather than as the dial's own line.
  `BIG_DROP = 20`, the box grows to 276 px tall, and the gap is now 28 px - a finger's width. The
  plaques move with the dial: their ring is measured from the dial's centre, not from the box's.
- **The dial's container is wider than the dial.** A plaque at 9 or 3 o'clock reaches half its
  width past the 256 px box, and LVGL clips a child to its parent's bounds - `OVERFLOW_VISIBLE`
  only adds the parent's shadow margin, it does not let a child out. The container is 360 px wide
  for that reason, and two of the effect's names were losing their first letter to it before.
- **A selector has no title pillow; it has one plaque per position.** A stepped knob's values are
  *words* (OFF, SOLID, BREATHE, RAINBOW, CHASE, LEVEL), and a single centre pillow naming the
  current one was saying the same thing as the ring, once. So the RGB effect knob's marks are
  plaqued (`plaque = true`), each name sits beside its own position, and the pillow is hidden for
  any stepped knob - live value included, because the filled plaque is the "which one" indicator.
  A continuous knob keeps its pillow: its value has no name, and `GAIN` is what the dial is.

## Considered options

- **Blink only the small cap.** Rejected: it is deleted with the group's cells while the Pad owns
  the bar, so it never showed.
- **Blink the pillow instead.** Rejected: the pillow is text, and a pulsing word is worse than a
  pulsing ring.
- **Keep the pillow and add the plaques** for a selector. Rejected by the owner: with six names
  beside their positions the centre word is a duplicate.
- **Move only the marks down, leaving the cap where it was.** Rejected: the gap the owner saw is
  between the pillow and the *ring*, and the ring is what the marks and the value line live on;
  moving the whole dial keeps the pointer's pivot concentric with it.
- **Measure each plaque's text to size it.** Deferred: a two-width rule (52 px up to four
  characters, 100 px beyond) is enough for the build's strings, and a font metrics call per
  layout is a cost with no user-visible win yet.

## Consequences

- The dial has no `lv_anim` of its own, so an idle editor costs one static frame: the screen's
  refresh budget goes to the cassette and the meter.
- Nothing in the build blinks, and `knob_cell` no longer needs to hand out its cap.
- A selector's dial is now the widest thing on screen: six plaques up to 100 px wide on a 256 px
  dial reach past the dial's box into the window's dimmed area. That is deliberate - the names are
  the dial's scale - but the RGB group's own rework owns whether six words stay the right answer.
- `CONFIG_APP_UI_DUMP_SCENE=7` exists so that selector's dial can be photographed headless, which
  is how this was checked.

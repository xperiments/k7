# The DJ block: three bands, a trim, and the only transport arrows

**Amended by [ADR 0027](./0027-the-knobs-are-dragged.md):** the dial's crown is gone - a ring of
short lines, no dots - and every knob rides one 270 degree arc with the cut at 7:30 and the boost
at 4:30, which reverses this record's "the right side cuts and the left side boosts". A knob's
value moves by a vertical drag, so there is no click-to-angle and the ring is not a target. The
pillow moved from under the 3x dial to above it and carries the knob's label, or the live value
while a drag is in progress; the two ends are printed as plaques at their own angles.

**Amended by [ADR 0026](./0026-the-mode-bar-and-the-right-bars-groups.md):** the block is the
**EQ group** now, and it shares the right bar with the Empty grille and the RGB group. Its six
cells are `[Back][HI] / [VU][MID] / [Gain][LOW]`: the M cell is Back, the R placeholder's cell is
the VU meter, Gain moved down to the inside column's bottom cell, and the transport arrows and
R are deleted. The knobs' circle, crown, line, ranges, edit gesture and tone_eq path are
unchanged.

**Amended by the control move:** the left drawer's `<` and `>` are the transport arrows
now; the DJ block's two freed cells keep their carcases and become the Filebrowser and Rec
placeholders (the owner's open folder and two-circle icons; TBD). The EQ knobs and their
behaviour are unchanged.

**Amended by [ADR 0022](./0022-the-menu-is-a-controls-mode.md):** the M cell opens the Menu;
R is still a placeholder. While the Menu is up the block's six cells give way to the Pad.
The owner also moved the first column 1 px right: GAIN, M and R sit at 594..690, so the
block is 190x284 and the Pad takes the same footprint.

**Amended by the current geometry and [ADR 0024](./0024-the-bars-and-their-panels.md):** the
block's drawn ink is 594..784, 190 wide, and its first column is nudged 2 px from the 592 base
(the shared 2 px border), not 1; the decision text's 593..784 / 191 / 1 px is superseded. The
six cells are the right bar's DJ panel; the strip, its entrance and its page ink belong to the
bar.

The owner wants the cassette screen to carry a DJ channel strip, not a spectrum analyser:
HI, MID and LOW plus a GAIN, in the same line-art language as the left drawer's P/</>. The
block has six cells because it sits on a 2x3 grid; the four cells the strip needs leave two,
and those become the previous/next arrows. The screen's tap and the BOOT button stop
skipping tracks: next and previous belong to those two arrows (and to the MCP, which is a
control API rather than a gesture).

The output is the Icecast stream, so the EQ is a broadcast tone control: it changes what
every listener hears, and there is no local audio path to fight with. The PCM seam is the
1024-frame chunk the encoder is about to take.

## Decision

- **The block is at the right edge.** Six 96x96 carcases in two touching columns at
  x=593..784 (the first column nudged 1 px right, the second stays at 688), three rows at
  y=76/170/264, so the block is 191x284 and leaves 16 px to the
  screen. The 16 px is the inset of the 64 px circle inside each carcase, not a gap. The
  left column is GAIN above the `<` and `>` arrows; the right column is HI, MID and LOW.
- **The knobs are a small circle, an outside crown and a centre line.** The arrows keep
  the buttons' 64 px circle; the knobs use 44 px (30% smaller). The 24-dot crown is at
  r=32, outside that circle, and a line runs from the circle's centre towards its rim.
  0 dB is at twelve o'clock, the right side cuts and the left side boosts; the arc lights
  from twelve to the line on the side it points. The twelve o'clock dot is the 0 dB tick.
- **The ranges are the DJ ones.** GAIN -12..+12 dB, default 0; bands -26..+6 dB, default
  0, at 200 Hz (low shelf), 1 kHz (bell, Q 0.7) and 4 kHz (high shelf). The trim is applied
  before the bands. The int16 clamp is the last stage.
- **Edit mode is modal, on a 3x virtual knob.** A tap on a knob opens it after 260 ms; the
  second tap of a double tap lands on the 3x knob and resets it there. The 256 px (90% of
  96+94+94) virtual knob sits at x=220, its top at y=90; under the dial a rounded primary
  pillow carries the knob's internal name (GAIN, HI, MID, LOW) in the light cap ink; there
  is no outer square, just the 118 px
  circle with its 2 px stroke, the crown and the centre line. A transparent 800x480
  overlay owns the touch. The cassette cuts out and the knob appears; on close the knob
  disappears and the cassette cuts back in. Only the big knob's centre circle closes. A
  click on the ring - the 256 px area around that circle - is the target point: the knob
  jumps to that angle, 0 dB at twelve, clockwise cutting and counter-clockwise boosting. A
  click outside does nothing; there is no drag and no relative rotation. A double tap on any
  small knob resets it to 0 dB and cancels the pending open, the mixer's centre-detent
  gesture, so the virtual knob never appears. While it is up nothing else responds - the
  the left drawer's buttons, the right placeholders and the peek are behind the overlay -
  and the view drops every
  screen intent too, because the touch adapter's two-finger VU arrives directly. Both
  circles blink their border 100% -> 27% -> 100%, 600 ms each way. The MCP is not a gesture
  or a click and is untouched.
- **Only the left drawer's `<` and `>` do next/previous on the screen.** The screen's tap
  no longer skips a track (`SCREEN_INTENT_TAP` is removed) and the BOOT button no longer
  skips either. The DJ block's two freed cells keep their carcases and become the
  Filebrowser and Rec placeholders (amended: the two cells are M, the Menu's door and now
  the right bar's chrome, and R, the EQ's record toggle; the Filebrowser/Rec placeholders
  were dropped); the drawer's P remains a placeholder.
- **The cassette enters at full scale; the controls get the 134/256 pose.** A right swipe
  brings the transport drawer and the DJ block in and scales the cassette to 134/256,
  148..550, so the drawer's 96 px column on the left and the block on the right both have
  room. A left swipe puts them away and restores the full entry view. The block is hidden at
  rest, shown and hidden with the drawer, sliding in from the right edge over the same 260 ms,
  and chrome above the Level sheets. The peek's 200/256 is the other room request and comes
  back when the drawer is away.

## Considered options

- **A 5-8 band graphic EQ.** Rejected: at 96 px per cell, eight columns are 12 px each, and
  the spectrum analysis (or six biquads) costs more than the DJ strip; the owner wanted the
  DJ vocabulary.
- **Six bands instead of three plus a trim.** Rejected: the owner asked for HI/MID/LOW and a
  GAIN, the DJ channel strip.
- **A literal `lv_spinner`.** Rejected: it is an indefinite animation with no value. The
  arc/crown gives the same visual language and a value that can be set.
- **Move P/R/W to the right and put the knobs on the left.** Rejected: the owner keeps the
  transport drawer on the left; the right edge was free.
- **A fixed pre-gain instead of a GAIN knob.** Rejected: the owner wants the trim as a
  knob. The knob starts at its 0 dB neutral, like the three bands, and the owner lowers it
  when a hot source needs headroom.
- **A limiter after the EQ.** Not built: a hot trim plus a boosted band can reach the clamp,
  and that is the mixer's behaviour. A limiter is a later decision, not a silent one.

## Consequences

- `components/portable/tone_eq` is a new leaf component: three RBJ biquads, a trim, a 2 dB
  per-chunk slew and an int16 clamp. `mini_icecast` calls it in `emit_chunk()` and on the
  partial tail in `flush_pipeline()`, both before `measure_level()`, so the trim and bands
  shape the last 23 ms of a track too and the VU is the level that goes on the wire.
  `host/tests/t17_tone_eq` pins the unity case, the trim, the three bands and the clamp.
- `mini_icecast_eq_set()` and `mini_icecast_gain_set()` are the public targets; the filter
  state and the coefficient rebuild stay on the broadcast task.
- `cassette_eq` owns the block, the overlay, the blink and the audio calls. The view creates
  it after the Levels, shows and hides it with the drawer, raises it with the drawer, and
  drops intents while `is_editing()`.
- `screen_input` loses `SCREEN_INTENT_TAP` and the click handler; `appliance` loses the
  BOOT-button skip. The drawer stays at the left edge, its P is a placeholder and its `<`/`>`
  are the transport's arrows.
- The block is hidden at rest and shown with the drawer, including over the Label and
  Artwork sheets while the controls are out. At the peek's 200/256 (drawer away) the block
  is hidden, so the entry and peek views are unaffected. Whether the block should hide on
  those Levels while the drawer is out is still open; this record keeps it chrome.
- The MCP keeps `player_skip` and `player_prev`: it is the parallel control surface and the
  one exception to "only these buttons".

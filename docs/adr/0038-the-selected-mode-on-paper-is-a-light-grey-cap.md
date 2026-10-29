# On paper the selected mode is a light grey cap

The owner, on the white page's mode bar - three messages, two of them correcting the first:

- "button ring color in white screen is primary and must be 0xBBBBBB."
- "queria poner el disco interno en 0xBBBBBB y no tocar el borde."
- "mostly dome is 0xBBBBBB by default and What I like is dome white by default as before and
  0xBBBBBB when selected."

## Decision

- **The idle cap stays the palette's neutral.** On the white page an unselected button's cap is
  `k_col_dome` (0x929292), the spec's `cap.face` - exactly what it was before this record.
- **The selected mode's cap is the light grey.** On that page the latch fills the cap with
  `k_col_button_cap`, `0xBBBBBB`, instead of `k_col_primary`: a lit mode is a lighter disc on
  paper, not an orange one. The glyph stays `k_col_bg` in both states, which reads on either face.
- **Nothing else moves.** The cap's 4 px rim stays `k_col_dome` - so the selected disc keeps a
  grey edge, slightly darker than its face - the carcase's ring stays `k_col_button_rect`, and the
  *dark* page's latch is untouched: it draws no cap, and there a lit mode is still a filled
  `k_col_primary` disc.
- **Spec data does not grow.** `cassette_cap_t` keeps `{face, letter}` for the idle cap; the
  selected face is the module's ink, the same way the dark page's primary latch already is.

## Considered options

- **The carcase's ring** (the first cut). Rejected by the owner: the frame is the chrome's line,
  not a button's ink.
- **0xBBBBBB as the idle face** (the second cut). Rejected by the owner: "dome white by default as
  before", the grey is what a selected mode looks like.
- **Primary on paper, as it was before this record.** Rejected: the owner asked for the grey.
- **The cap's rim.** Rejected: `CASSETTE_CAP_RIM_W` of `k_col_dome` is what keeps a grey edge round
  any cap, selected or not.
- **A third field in `cassette_cap_t`** (`selected_face`). Deferred: the module already owns the
  latch's ink on the dark page, and a per-panel selected face would be a spec field every host has
  to fill for one state on one page.

## Consequences

- On the white page the mode bar's "on" is a lighter disc - 0xBBBBBB against the page's 0x909090
  and the idle 0x929292 - so the cue is quieter than the dark page's orange face. The orange left
  on paper belongs to the playing ink: the timeline's times and track, the knobs' marks, the
  meter's dots.
- The palette gains `k_col_button_cap`, used for one state on one page, and `k_col_dome` keeps its
  two jobs: the idle cap's face and every cap's rim.
- The press, the pulse, the tap guard and the latch's own ordering are unchanged.

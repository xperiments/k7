# The mode bar puts the dial away

**Amends [ADR 0028](./0028-the-pad-drives-the-knob.md).** That record made the mode bar inert while
a dial was up - each of its three callbacks returned early on `knob_editor_is_editing()` - which
left the Pad's leftward swipe as the only way out of an edit.

The owner, after using it:

- "Actualmente cuando modificamos un knob solo podemos salir con swipe left."
- "Necesito que clicar cualquier elemento de left-bar tambien cancele la edicion."

## Decision

- **A tap on any of the mode bar's three buttons ends the edit.** The dial's container spans
  x=120..576, so the left bar (x=16..112) is the one piece of chrome the editor leaves uncovered.
  It is where a finger goes when the Pad's swipe is not the thing being thought of, and "any
  element of the left bar" is the owner's whole scope: the cassette's door, EQ and RGB alike - a
  door that alone stayed inert would be a hole in the surface.
- **The tap is consumed.** It ends the edit and runs nothing else: it does not open the Menu,
  select a group or drop the active one. A tap that dismisses a mode must not also be a command,
  and the mode's own tap is then the next one.
- **One helper, three callbacks.** `left_bar_dismiss()` in `cassette_view.c` answers "a dial is
  up" by calling `knob_editor_close()` - the same `knob_edit_close()` the panel swap and the Pad's
  Back already use - and returns true so the three callbacks return. The editor learns nothing
  new: it still knows no bar and no button (ADR 0028's seam).
- **Everything else about an edit is unchanged**: the Pad keeps its ratchet, its cycle and its
  leftward Back, the cassette still cuts out, and the group's cells still come back on close.

## Considered options

- **Dismiss and run the mode in the same tap** (tap RGB while the gain dial is up: close the dial
  and show RGB). Rejected: the group buttons are also the active group's toggle, so the tap that
  dismissed an EQ dial would put the EQ group away - a dismissal with an invisible second effect.
  One tap, one meaning.
- **Only the two group buttons dismiss.** Rejected by the owner's scope; the door is the first
  element of the bar a thumb reaches.
- **Let a leftward swipe anywhere on the screen close the dial.** Rejected: the bar keeps gestures
  local and the screen's sink owns the ladder's own swipes (ADR 0025); a third leftward meaning at
  screen level is one the two existing ones would have to be told apart from.
- **Keep the early return and find another exit.** There is not one: the dial covers the cassette,
  and the right bar is already the Pad.

## Consequences

- The mode bar is a live surface while a dial is up. ADR 0028's "the mode bar is inert then" is
  reversed, with the two `if` statements that implemented it; its note that the buttons "were
  already inert in practice - the overlay ate the press" is history, not a state.
- The button still pulses before its action runs, so a tap on the bar answers the finger even
  though all it does is put the dial away.
- Editing ends the same ways it did - the Pad's swipe, a bank swap, the bar sliding out, the
  double tap on the open knob - plus this one.
- The host tier cannot reach it: the decision is one `if` in the view's callbacks and the tests are
  LVGL-free, so this is verified on the panel.

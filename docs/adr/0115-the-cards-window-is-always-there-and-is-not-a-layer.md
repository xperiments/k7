# The Card's window is always there, and it is not a layer

A K7Art v2 carries the Card's window as the label section's optional `aperture`: a placement
aid that must be absent or exactly `(95, 101, 556, 166, r 83)`. The editor showed it as a
rectangle the designer had to add, size and flag as **Aperture** in Appearance, and it was an
object in the layer stack - one that could be left out, moved, or mistaken for art. The window
is the Card's, not the design's: every design has it, it never moves, and the only thing a
designer can do with the object is get it wrong. **The request is the same treatment as the
paper: the window is always there, and it is not a layer.**

## Decision

- **The window is fixed.** It is `(95, 101) 556x166, radius 83`, the Card's own hole, from the
  vocabulary (`assets/labels/vocabulary.json`'s `aperture`). A design cannot move, size or omit
  it.
- **The editor always shows it, black and on top.** In Design and in Preview the window is an
  opaque black rounded layer painted over the print, so nothing drawn under it is seen. It is a
  guide; it is not selectable, and it is not a prim.
- **No aperture layer and no switch.** `Obj.ap`, `apOf`, the `Aperture` switch, the aperture
  rectangle in the stack and the "covers the whole aperture" refusal go. The window's facts sit
  beside the paper in Design → Paper.
- **The editor always writes `aperture`.** The file keeps the field and its shape
  (`{ x, y, w, h, r }`, always the Card's window); a file without it opens with it, and a
  stored document's aperture layer is dropped. The sheet never paints it: the Card's mask cuts
  the window on the device, in both layouts.
- **Nothing downstream changes.** The sheet writer, the packer, the firmware and the App are
  untouched; the five factory K7Arts keep their `aperture` and round-trip byte for byte.

## Considered options

- **Keep the aperture rectangle and only refuse a wrong one.** That is the status quo. The
  designer still adds a guide, the guide can be moved or forgotten, and the editor keeps a
  validation rule about a window the design cannot choose.
- **Cut the window and leave the canvas behind it.** The hole would take the colour of the
  screen or the paper's edge, so a dark or busy design would blur where the window is. The
  request names black, and black is the one colour nothing in the design can be mistaken for.
- **Ship a black prim in the sheet.** A prim is painted, not cut: the sheet would print over
  the tape the window is there to show.
- **Drop `aperture` from the file.** That would express a constant by removing it, at the cost
  of a format version and a re-export of the factory set. The editor always writes it, as it
  always writes `mask`.

## Consequences

- **The window is a fact of the Card, not a design element.** The layer stack is art only; the
  window is drawn for every design, in the same place, and cannot be selected.
- **The black is the editor's.** The device still cuts the window with the Card's mask; the
  label sheet still carries no aperture, and a pack's bytes do not move.
- **A stored document loses its aperture layer.** It was a guide; the window is still shown,
  and Export writes it.
- **The specs and guides say the window is fixed.** The aperture is still the file's word for
  it; `k7art-read` still returns it, and the packer still validates it.
- **ADR 0114's paper stands.** Its sentence that "the aperture stays a rectangle flagged in
  Appearance" is superseded; the paper, the default and the always-written `mask` are not.

## Supersedes

ADR 0114's sentence that the aperture stays a rectangle flagged in Appearance. The rest of
ADR 0114 stands.

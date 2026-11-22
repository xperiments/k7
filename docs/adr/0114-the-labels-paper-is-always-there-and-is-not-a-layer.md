# The label's paper is always there, and it is not a layer

A K7Art v2 makes the print's paper the label section's `mask`, and the editor showed it as a
**LayerMask**: a row in the layer stack with a count, an add/remove control, its own inspector,
and a membership flag whose layers had to stay one contiguous run. Every design the editor
writes has one, and the five factory designs all do. To the person designing a label the row
reads as a strange layer that must always be present: it is not art, it cannot be left out, and
a group that crosses it is refused for a reason the design does not have. **The request is to
remove the LayerMask.**

## Decision

- **The label always has paper.** It is the print's own box (748x359) painted in a `#RRGGBB`
  colour and clipped to a corner radius: the section's `mask`, as in
  [ADR 0113](./0113-a-k7arts-colours-are-literal.md). Every layer prints on it.
- **The editor has no LayerMask.** No row, no add/remove, no selection, no inspector panel, no
  Into/Out of LayerMask, no contiguous-run rule. A group only has to be a contiguous run of its
  surface's stack.
- **The paper is edited in Design → Paper.** With no layer selected the inspector shows its
  colour and corner radius; it is present in every document and cannot be removed. The aperture
  stays a rectangle flagged in Appearance, which marks the Card's window.
- **The editor always writes `mask`.** The K7Art file keeps its field and shape
  (`mask: { color, r }`); a stored document or a file without one opens with the default paper
  (`#c7a867`, radius 10), and Export writes it back. A hand-written file may still omit the
  field, and the packer still reads that as a print with no paper.
- **Nothing downstream changes.** The sheet writers put the paper's rect first exactly as
  before; the label sheet, the packer, the firmware and the App are untouched. The factory
  designs' bytes and sheets are unchanged.

## Considered options

- **Keep the LayerMask row but make it non-removable.** The control would stop lying about
  being optional, but the layer would still appear in the stack with a count, and the
  membership rules - the part that constrains groups and drags - would remain.
- **Remove the paper too.** A label could be printed on the panel's page. Every factory design
  would need a full-print rect or a new base colour, and the sheet would lose the one prim that
  papers a design; the paperless case is precisely what no one asked for.
- **Make `mask` required and the file v3.** The editor already writes it for every design; a
  version bump would change the packer, the firmware and the App, and refuse today's files, to
  express a property the writer never omits.
- **Teach the LayerMask as the paper.** Renaming the row would leave the membership and the
  stack depth in place; the request is to remove the concept, not to explain it.

## Consequences

- **A simpler layer model.** The label and the J-card are both one stack; groups, drags and
  reorders have one set of rules. The mask-specific refusals, `maskRange`, `selMask` and the
  membership flag leave the editor.
- **Paper is edited where a design is.** It is a property of the print, not a layer, so the
  Design panel is its home, beside the canvas and the fields.
- **A paperless file is a hand-written file.** A v1 design or a stored document without a
  full-print paper rect is given the default paper on open; a v2 file without `mask` opens with
  it and exports it. There is no way to author one in the editor.
- **The guides say paper.** The specs, the editor's README, labels-for-the-k7 and the agent
  skill stop calling it a LayerMask; `maskColor`, `maskRadius` and the file's `mask` keep their
  names because they are the file's vocabulary.
- **ADR 0113 stands for colours.** Its paper-as-mask field and the sheet's version 5 are
  unchanged; its "absent means the design has no paper" narrows to the hand-written file the
  editor does not produce.

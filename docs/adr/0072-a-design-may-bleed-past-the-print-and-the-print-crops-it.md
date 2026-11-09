# A design may bleed past the print, and the print crops it

The labels are drawn in a 748x359 print, and the owner designs them the way a sleeve is
designed: the bands run the full width of the page and past it, a tick sits above the top edge,
the paper is wider than the box. The packer refused any prim outside the print, the editor
refused it at Export, and the reader refused the whole sheet on the board — so a design that
looked right on the canvas could not be saved, and when one was forced through the device fell
back to the print's flat fill. Three walls for one drawing fact: the print crops what does not
fit, which is what a print is.

## Decision

- **Position is not validated.** A prim may sit anywhere the sheet's record can hold it: `x` and
  `y` are int16, `w` and `h` uint16, and those four ranges are the only positional refusal
  (a value the record cannot represent). Everything else - negative coordinates, a box wider
  than the page, a prim entirely off it - is written and packed.
- **The print crops.** The label container is the print's box and LVGL clips its children to it,
  so the corner of a band above `y = 0` is simply not painted. The panel's output is the design
  intersected with the print; that is the same rule the Card's mask already applies to the
  window.
- **The editor marks, it does not refuse.** A layer that leaves the print gets a grey dashed
  outline on the canvas and a grey row in the layer list - information, like a margin guide, so
  the bleed can be seen while it is drawn. Export writes it; nothing is hidden or snapped.
- **The reader accepts it.** `label.c` checks a rect's record (a height, no text offset) and a
  text's fields, and no longer compares a coordinate with the space.

## Considered options

- **Refuse outside prims (where this was).** It guards against a design that promises more than
  the panel does, but it refuses the way a sleeve is actually drawn, and a designer cannot see
  why a band that looks right will not save. Removed.
- **Clip on export.** Writing only the part of each prim inside the print would keep the sheets
  tidy and lose the design: move the page edge later and the clipped art is gone. A design is
  the record, so it is written whole.
- **Snap the prim to the edge.** Changes what the designer drew, silently, and makes the bleed
  invisible rather than cropped.

## Consequences

- A design can print less than it draws - the crop is visible on the canvas (with Outside on)
  and on the panel, and the grey mark says which layers are affected.
- A design can print nothing at all: a page of prims entirely outside the box is valid, packs,
  and shows the print's paper. That is a design mistake the canvas makes obvious, not a sheet
  the tool will catch.
- The sheet's coordinates are signed for this reason; a reader that assumed non-negative values
  would be wrong, and `label.c` is the one place that must stay signed.
- A rotated prim keeps its own rotation about its own centre; the crop is the print's box, not a
  rotated one, exactly as the panel draws it.

## Supersedes

The rule in `docs/specs/label-templates.md` §1 that every prim must be inside 748x359, and the
refusal rows that followed from it.

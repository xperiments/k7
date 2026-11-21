import type { Obj } from "./types";

/**
 * Scaling a selection as one. A single layer's handle changes that layer's box; a group's
 * handle has to move every member and grow every member by the same amount, or the design
 * comes apart. The factor is one number for both axes, and the members scale about the corner
 * opposite the handle.
 *
 * One factor, not two, because the print has no shear: a rotated layer under a non-uniform
 * scale would no longer be a rotated rectangle, and a circle's radius is `min(w, h) / 2`, so a
 * circle stretched on one axis would print as a stadium (docs/specs/label-templates.md §1).
 *
 * A text's box scales with the rest and its type does not: a label's type is one of the sheet's
 * three compiled pairs, and a J-card's is one of the sheet's three sizes, so no layer carries a
 * size of its own to scale (docs/specs/label-templates.md §2, docs/specs/k7art.md §4).
 */

/** The floor a layer may be scaled to: the same 8 px the single-layer resize keeps. */
export const SCALE_MIN = 8;
/** The gesture's own floor and ceiling, so a slip of the pointer is not a broken design. */
const FACTOR_MIN = 0.05;
const FACTOR_MAX = 32;

/** One member of a scale gesture, and its box when the drag began. */
export interface ScaleMember {
  o: Obj;
  x: number;
  y: number;
  w: number;
  /** The box's height: a text's is its line box, because a text stores no height. */
  h: number;
  /** The corner radius: a shape's is scaled with the shape. */
  r: number;
}

/** What a gesture holds still: the anchor (the corner the handle is not), and the box it began
 *  in. The factor is measured against that box, not against the live bounds, so a drag does not
 *  feed on itself. */
export interface ScaleFrame {
  ax: number;
  ay: number;
  w: number;
  h: number;
}

/** The frame of a selection's bounds, or null for fewer than two layers: one layer has its own
 *  handle, which scales its box rather than a group's. */
export function frameOf(members: ScaleMember[]): ScaleFrame | null {
  if (members.length < 2) return null;
  const ax = Math.min(...members.map((m) => m.x));
  const ay = Math.min(...members.map((m) => m.y));
  const x1 = Math.max(...members.map((m) => m.x + m.w));
  const y1 = Math.max(...members.map((m) => m.y + m.h));
  return { ax, ay, w: Math.max(1, x1 - ax), h: Math.max(1, y1 - ay) };
}

/** The factor a pointer puts the frame at. The grabbed corner is the pointer's target, so each
 *  axis carries the drag in proportion to how far the pointer moved along it: a drag down one
 *  axis tracks it exactly, a diagonal drag is one factor for both, and nothing jumps when the
 *  drag crosses from one to the other. */
export function scaleFactor(frame: ScaleFrame, p: { x: number; y: number }): number {
  const dw = (p.x - frame.ax) / frame.w - 1;
  const dh = (p.y - frame.ay) / frame.h - 1;
  const moved = Math.abs(dw) + Math.abs(dh);
  const f = moved < 1e-6 ? 1 : 1 + (dw * Math.abs(dw) + dh * Math.abs(dh)) / moved;
  return Math.min(Math.max(f, FACTOR_MIN), FACTOR_MAX);
}

/** Put every member at the factor, about the frame's anchor. */
export function scaleMembers(members: ScaleMember[], frame: ScaleFrame, f: number): void {
  const round = Math.round;
  for (const m of members) {
    const o = m.o;
    o.x = round(frame.ax + (m.x - frame.ax) * f);
    o.y = round(frame.ay + (m.y - frame.ay) * f);
    o.w = Math.max(SCALE_MIN, round(m.w * f));
    if (o.type !== "text") o.h = Math.max(SCALE_MIN, round(m.h * f));
    // A circle has no radius of its own: it derives it from the square it has become.
    if (m.r && o.type !== "circle") o.r = Math.max(0, round(m.r * f));
  }
}

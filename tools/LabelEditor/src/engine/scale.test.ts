import { describe, expect, it } from "vitest";

import { frameOf, SCALE_MIN, scaleFactor, scaleMembers, type ScaleMember } from "./scale";
import type { Obj } from "./types";

/** A layer to scale, and the box the gesture remembers for it. A text stores no height; its
 *  member box carries the line box the selection's bounds measure. */
function member(o: Partial<Obj> & Pick<Obj, "id" | "x" | "y" | "w">, h = 0, r = 0): ScaleMember {
  const obj = { type: "rect", name: "", ...o } as Obj;
  if (obj.type !== "text") obj.h = obj.h ?? h;
  return { o: obj, x: obj.x, y: obj.y, w: obj.w, h: obj.h ?? h, r };
}

describe("frameOf", () => {
  it("is the selection's bounds, anchored at the top-left", () => {
    const f = frameOf([member({ id: 1, x: 40, y: 100, w: 200 }, 50), member({ id: 2, x: 300, y: 20, w: 80 }, 40)]);
    expect(f).toEqual({ ax: 40, ay: 20, w: 340, h: 130 });
  });

  it("needs two layers: one layer has a handle of its own", () => {
    expect(frameOf([member({ id: 1, x: 0, y: 0, w: 10 }, 10)])).toBeNull();
    expect(frameOf([])).toBeNull();
  });
});

describe("scaleFactor", () => {
  const frame = { ax: 40, ay: 20, w: 340, h: 130 };

  it("is one at the corner the handle was grabbed from", () => {
    expect(scaleFactor(frame, { x: 380, y: 150 })).toBe(1);
  });

  it("tracks the axis the pointer drags", () => {
    // The corner is the pointer's target, so pulling it halfway in on either axis is half.
    expect(scaleFactor(frame, { x: 210, y: 150 })).toBeCloseTo(0.5, 3);
    expect(scaleFactor(frame, { x: 380, y: 85 })).toBeCloseTo(0.5, 3);
    expect(scaleFactor(frame, { x: 210, y: 85 })).toBeCloseTo(0.5, 3);
  });

  it("mixes the axes by how far each moved", () => {
    // Half the width and a tenth of the height is mostly the width's drag.
    expect(scaleFactor(frame, { x: 210, y: 137 })).toBeCloseTo(0.567, 3);
  });

  it("has a floor and a ceiling, so a slip of the pointer is not a design", () => {
    expect(scaleFactor(frame, { x: -10_000, y: -10_000 })).toBe(0.05);
    expect(scaleFactor(frame, { x: 10_000, y: 10_000 })).toBe(32);
  });
});

describe("scaleMembers", () => {
  const frame = { ax: 40, ay: 20, w: 340, h: 130 };

  it("moves and grows every member about the anchor", () => {
    const a = member({ id: 1, x: 40, y: 100, w: 200 }, 50);
    const b = member({ id: 2, x: 300, y: 20, w: 80 }, 40);
    scaleMembers([a, b], frame, 2);
    expect([a.o.x, a.o.y, a.o.w, a.o.h]).toEqual([40, 180, 400, 100]);
    expect([b.o.x, b.o.y, b.o.w, b.o.h]).toEqual([560, 20, 160, 80]);
  });

  it("scales a text's box and leaves its height to its type", () => {
    const t = member({ id: 3, type: "text", x: 100, y: 100, w: 120 }, 22);
    t.o.static = true;
    t.o.text = "Side A";
    scaleMembers([t], frame, 0.5);
    expect([t.o.x, t.o.y, t.o.w]).toEqual([70, 60, 60]);
    expect(t.o.h).toBeUndefined();
  });

  it("scales a corner radius with the shape, and keeps a layer's own floor", () => {
    const r = member({ id: 4, x: 40, y: 20, w: 100 }, 40, 10);
    const tiny = member({ id: 5, x: 380, y: 150, w: 10 }, 10);
    scaleMembers([r, tiny], frame, 0.01);
    expect(r.o.r).toBe(0);
    expect(r.o.w).toBe(SCALE_MIN);
    expect(tiny.o.w).toBe(SCALE_MIN);
  });

  it("leaves rotation alone: a turned layer stays turned", () => {
    const turned = member({ id: 6, x: 60, y: 40, w: 50 }, 50);
    turned.o.rot = 30;
    scaleMembers([turned], frame, 3);
    expect(turned.o.rot).toBe(30);
    expect([turned.o.x, turned.o.y, turned.o.w]).toEqual([100, 80, 150]);
  });
});

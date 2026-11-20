import { describe, expect, it } from "vitest";

import {
  buildTemplate,
  canvasOf,
  DEFAULT_CANVAS,
  defaultTpl,
  importTemplate,
  syncSlots,
  tplText,
} from "./tpl";
import { WIN } from "./vocab";
import type { Mask, Obj, Tpl } from "./types";

const mask: Mask = { color: "#c7a867", radius: 10, collapsed: false };

const objs: Obj[] = [
  { id: 1, type: "rect", name: "", x: 40, y: 30, w: 200, h: 120, r: 10, role: "band.2", rot: 15 },
  {
    id: 2,
    type: "text",
    name: "song_title",
    x: 300,
    y: 40,
    w: 380,
    fr: "title",
    role: "band.1",
    align: "right",
    sim: "Neon Skyline",
  },
  {
    id: 3,
    type: "text",
    name: "",
    static: true,
    text: "STATIC",
    x: 300,
    y: 120,
    w: 380,
    fr: "numeral",
    role: "ink",
    align: "center",
    rot: -5,
  },
];

const design = (): Tpl => {
  const t = defaultTpl();
  t.id = "label.probe";
  t.name = "Probe";
  return t;
};

describe("buildTemplate", () => {
  it("carries the colourway, the faces, the slots and one prim per layer", () => {
    const tpl = design();
    const { doc, errs } = buildTemplate(objs, mask, tpl);
    expect(errs).toEqual([]);
    expect(doc.id).toBe("label.probe");
    expect(doc.space).toEqual({ w: 748, h: 359 });
    expect(doc.inks).toEqual({
      paper: "#c7a867",
      ink: "#151515",
      rule: "#8e7e4a",
      band: tpl.inks.band,
    });
    expect(doc.fonts.title).toEqual({ ff: "Archivo Narrow 600", px: 32 });
    expect(doc.prims).toHaveLength(4);
    expect(doc.prims[0]).toMatchObject({ fill: "paper", w: 748, h: 359 });
    expect(doc.prims[1]).toMatchObject({ t: "rect", fill: "band.2", rot: 15 });
    expect(doc.prims[2]).toMatchObject({ t: "text", slot: "song_title", align: "right" });
    expect(doc.prims[3]).toMatchObject({ t: "text", text: "STATIC", rot: -5 });
    expect(doc.slots).toEqual({ song_title: { max: 96 } });
  });

  it("writes the file's key order", () => {
    const { doc } = buildTemplate(objs, mask, design());
    expect(Object.keys(doc)).toEqual([
      "v",
      "id",
      "name",
      "space",
      "canvas",
      "slots",
      "inks",
      "fonts",
      "prims",
    ]);
  });

  it("carries the canvas presets and the active one", () => {
    const { doc, errs } = buildTemplate(objs, mask, design(), {
      backgrounds: ["#101010", "#f5f1e6"],
      active: 1,
    });
    expect(errs).toEqual([]);
    expect(doc.canvas).toEqual({ backgrounds: ["#101010", "#f5f1e6"], active: 1 });
  });

  it("refuses a canvas with no background, over the cap, or a bad colour", () => {
    expect(
      buildTemplate(objs, mask, design(), { backgrounds: [], active: 0 }).errs.join("\n"),
    ).toContain("canvas: 0 backgrounds");
    expect(
      buildTemplate(objs, mask, design(), {
        backgrounds: Array.from({ length: 9 }, () => "#101010"),
        active: 0,
      }).errs.join("\n"),
    ).toContain("canvas: 9 backgrounds");
    expect(
      buildTemplate(objs, mask, design(), {
        backgrounds: ["#nope", "#101010"],
        active: 0,
      }).errs.join("\n"),
    ).toContain('canvas.backgrounds[0] "#nope" is not #RRGGBB');
  });

  it("refuses an active background outside the list", () => {
    expect(
      buildTemplate(objs, mask, design(), {
        backgrounds: ["#101010", "#f5f1e6"],
        active: 2,
      }).errs.join("\n"),
    ).toContain("canvas.active 2 is not an index into 2 backgrounds");
  });

  it("refuses an id that does not look like label.my-name", () => {
    const tpl = design();
    tpl.id = "Probe";
    expect(buildTemplate(objs, mask, tpl).errs).toContain(
      'id "Probe" must look like label.my-name',
    );
  });

  it("refuses the Card's window drawn at another size", () => {
    const ap: Obj = {
      id: 9,
      type: "rect",
      name: "",
      x: WIN.x,
      y: WIN.y,
      w: WIN.w,
      h: WIN.h,
      r: 0,
      ap: true,
    };
    const errs = buildTemplate([...objs, ap], mask, design()).errs;
    expect(errs.join("\n")).toContain("aperture: the Card's window is");
  });

  it("does not draw the aperture as a prim", () => {
    const ap: Obj = {
      id: 9,
      type: "rect",
      name: "",
      x: WIN.x,
      y: WIN.y,
      w: WIN.w,
      h: WIN.h,
      r: WIN.r,
      ap: true,
    };
    const { doc, errs } = buildTemplate([...objs, ap], mask, design());
    expect(errs).toEqual([]);
    expect(doc.aperture).toEqual({ x: WIN.x, y: WIN.y, w: WIN.w, h: WIN.h, r: WIN.r });
    expect(doc.prims).toHaveLength(4);
  });

  it("refuses a dynamic text that names no field", () => {
    const loose: Obj = { id: 4, type: "text", name: "", x: 0, y: 0, w: 100, fr: "caption" };
    const errs = buildTemplate([...objs, loose], mask, design()).errs;
    expect(errs.join("\n")).toContain("needs a slot name");
  });

  it("refuses a static text with nothing to print", () => {
    const empty: Obj = {
      id: 4,
      type: "text",
      name: "",
      static: true,
      text: "   ",
      x: 0,
      y: 0,
      w: 100,
      fr: "caption",
    };
    const errs = buildTemplate([...objs, empty], mask, design()).errs;
    expect(errs.join("\n")).toContain("static text is empty");
  });

  it("drops a slot nothing prints: the slots follow the text layers", () => {
    const tpl = design();
    syncSlots(objs, tpl);
    tpl.slots.genre = { max: 96 };
    const { doc, errs } = buildTemplate(objs, mask, tpl);
    expect(errs).toEqual([]);
    expect(doc.slots).toEqual({ song_title: { max: 96 } });
  });

  it("refuses more prims than the sheet holds", () => {
    const many: Obj[] = Array.from({ length: 49 }, (_, i) => ({
      id: 100 + i,
      type: "rect" as const,
      name: "",
      x: i,
      y: i,
      w: 10,
      h: 10,
      role: "band.0",
    }));
    const errs = buildTemplate([...objs, ...many], mask, design()).errs;
    expect(errs.join("\n")).toContain("and the sheet holds 48 per design");
  });
});

describe("importTemplate", () => {
  it("round-trips a design byte for byte", () => {
    const tpl = design();
    const { doc } = buildTemplate(objs, mask, tpl);
    const back = importTemplate(tplText(doc), "#c7a867");
    const again = buildTemplate(back.objs, back.mask, back.tpl, back.canvas);
    expect(again.errs).toEqual([]);
    expect(tplText(again.doc)).toBe(tplText(doc));
  });

  it("carries the canvas back, and refuses a malformed one", () => {
    const { doc } = buildTemplate(objs, mask, design(), {
      backgrounds: ["#101010", "#f5f1e6"],
      active: 1,
    });
    expect(importTemplate(tplText(doc), "#c7a867").canvas).toEqual({
      backgrounds: ["#101010", "#f5f1e6"],
      active: 1,
    });
    doc.canvas = { backgrounds: ["#101010"], active: 3 };
    expect(() => importTemplate(JSON.stringify(doc), "#c7a867")).toThrow(
      "canvas.active 3 is not an index into 1 backgrounds",
    );
    doc.canvas = { backgrounds: ["#101010", "#nope"], active: 0 };
    expect(() => importTemplate(JSON.stringify(doc), "#c7a867")).toThrow(
      'canvas.backgrounds[1] "#nope" is not #RRGGBB',
    );
  });

  it("opens a design without a canvas on the panel's page", () => {
    const { doc } = buildTemplate(objs, mask, design());
    delete doc.canvas;
    expect(importTemplate(tplText(doc), "#c7a867").canvas).toEqual({
      backgrounds: [DEFAULT_CANVAS],
      active: 0,
    });
  });

  it("refuses a space that is not the print", () => {
    const { doc } = buildTemplate(objs, mask, design());
    doc.space = { w: 800, h: 480 };
    expect(() => importTemplate(JSON.stringify(doc), "#c7a867")).toThrow(
      "space is 800x480, not 748x359",
    );
  });

  it("refuses a face the sheet does not carry", () => {
    const { doc } = buildTemplate(objs, mask, design());
    doc.fonts.title = { ff: "Montserrat", px: 32 };
    expect(() => importTemplate(JSON.stringify(doc), "#c7a867")).toThrow(
      'font role title is "Montserrat" 32 px',
    );
  });

  it("refuses an aperture with no paper to cut", () => {
    const ap: Obj = {
      id: 9,
      type: "rect",
      name: "",
      x: WIN.x,
      y: WIN.y,
      w: WIN.w,
      h: WIN.h,
      r: WIN.r,
      ap: true,
    };
    const { doc } = buildTemplate([...objs, ap], mask, design());
    const noPaper = { ...doc, prims: doc.prims.filter((p) => p.fill !== "paper") };
    expect(() => importTemplate(JSON.stringify(noPaper), "#c7a867")).toThrow(
      "aperture without a paper",
    );
  });
});

describe("canvasOf", () => {
  it("turns a document's single bg into one preset", () => {
    expect(canvasOf({ bg: "#ffffff" })).toEqual({
      backgrounds: ["#ffffff"],
      active: 0,
    });
  });

  it("keeps a preset list and clamps an active index that is past it", () => {
    expect(canvasOf({ bgs: ["#101010", "#f5f1e6"], bgi: 5 })).toEqual({
      backgrounds: ["#101010", "#f5f1e6"],
      active: 1,
    });
  });

  it("starts a document with neither on the panel's page", () => {
    expect(canvasOf({})).toEqual({ backgrounds: [DEFAULT_CANVAS], active: 0 });
  });
});

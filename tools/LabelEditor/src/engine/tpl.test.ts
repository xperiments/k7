import { describe, expect, it } from "vitest";

import {
  buildTemplate,
  canvasOf,
  DEFAULT_CANVAS,
  DEFAULT_PAPER,
  defaultTpl,
  importTemplate,
  migrateDocData,
  migrateId,
  syncSlots,
  tplText,
  type RetiredTemplateDoc,
} from "./tpl";
import {
  JCARD_FIELD_CHOICES,
  JCARD_FIELDS,
  JCARD_PARTS,
  JCARD_SIMS,
  JCARD_SPACE,
  starterJCard,
} from "./jcard";
import { SLOTS, WIN } from "./vocab";
import type { DocData, Mask, Obj, Tpl } from "./types";

const INK = "#151515";
const BAND1 = "#a24e2c";
const BAND2 = "#8a5d34";
const RULE = "#8e7e4a";

const mask: Mask = { color: "#c7a867", radius: 10, collapsed: false };

const objs: Obj[] = [
  { id: 1, type: "rect", name: "", x: 40, y: 30, w: 200, h: 120, r: 10, color: BAND2, rot: 15 },
  {
    id: 2,
    type: "text",
    name: "song_title",
    x: 300,
    y: 40,
    w: 380,
    fr: "title",
    color: BAND1,
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
    color: INK,
    align: "center",
    rot: -5,
  },
];

const design = (): Tpl => {
  const t = defaultTpl();
  t.id = "k7.probe";
  t.name = "Probe";
  return t;
};

/**
 * The label section the design above writes, in the section's own key order. The bytes are
 * the regression: a v2 section has the mask as a field and a colour on every prim.
 */
const expectedLabel = {
  space: { w: 748, h: 359 },
  mask: { color: "#c7a867", r: 10 },
  canvas: { backgrounds: [DEFAULT_CANVAS], active: 0 },
  fonts: {
    title: { ff: "Archivo Narrow 600", px: 32 },
    numeral: { ff: "Barlow Condensed 700", px: 78 },
    caption: { ff: "IBM Plex Mono 500", px: 18 },
  },
  slots: { song_title: { max: 96 } },
  prims: [
    { t: "rect", x: 40, y: 30, w: 200, h: 120, r: 10, rot: 15, fill: BAND2, still: true },
    {
      t: "text",
      slot: "song_title",
      x: 300,
      y: 40,
      w: 380,
      font: "title",
      color: BAND1,
      align: "right",
      preview: "Neon Skyline",
    },
    {
      t: "text",
      x: 300,
      y: 120,
      w: 380,
      rot: -5,
      font: "numeral",
      color: INK,
      align: "center",
      text: "STATIC",
    },
  ],
};

describe("buildTemplate", () => {
  it("writes one K7Art v2: the container, the label section and the J-card", () => {
    const { doc, errs } = buildTemplate(objs, mask, design());
    expect(errs).toEqual([]);
    expect(doc.format).toBe("k7art");
    expect(doc.v).toBe(2);
    expect(doc.id).toBe("k7.probe");
    expect(doc.name).toBe("Probe");
    expect(doc).not.toHaveProperty("inks");
    expect(doc.label.space).toEqual({ w: 748, h: 359 });
    expect(doc.label.mask).toEqual({ color: "#c7a867", r: 10 });
    expect(doc.label.fonts.title).toEqual({ ff: "Archivo Narrow 600", px: 32 });
    expect(doc.label.prims).toHaveLength(3);
    expect(doc.label.prims[0]).toMatchObject({ t: "rect", fill: BAND2, rot: 15 });
    expect(doc.label.prims[1]).toMatchObject({ t: "text", slot: "song_title", align: "right" });
    expect(doc.label.prims[2]).toMatchObject({ t: "text", text: "STATIC", rot: -5 });
    expect(doc.label.slots).toEqual({ song_title: { max: 96 } });
    expect(doc.jcard.space).toEqual({ w: 1300, h: 1280, unit: 320 });
  });

  it("writes the file's key order, and each section's", () => {
    const { doc } = buildTemplate(objs, mask, design());
    expect(Object.keys(doc)).toEqual(["format", "v", "id", "name", "label", "jcard"]);
    expect(Object.keys(doc.label)).toEqual([
      "space",
      "mask",
      "canvas",
      "fonts",
      "slots",
      "prims",
    ]);
    expect(Object.keys(doc.label.fonts)).toEqual(["title", "numeral", "caption"]);
    expect(Object.keys(doc.jcard)).toEqual([
      "space",
      "parts",
      "background",
      "fonts",
      "prims",
    ]);
    expect(Object.keys(doc.jcard.space)).toEqual(["w", "h", "unit"]);
    expect(Object.keys(doc.jcard.parts)).toEqual(["flap", "spine", "cover"]);
    expect(Object.keys(doc.jcard.fonts)).toEqual(["title", "numeral", "caption"]);
  });

  it("writes the label section byte for byte", () => {
    const { doc } = buildTemplate(objs, mask, design());
    expect(tplText(doc.label)).toBe(tplText(expectedLabel));
  });

  it("puts the aperture between the mask and the canvas, in the section's order", () => {
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
    expect(Object.keys(doc.label)).toEqual([
      "space",
      "mask",
      "aperture",
      "canvas",
      "fonts",
      "slots",
      "prims",
    ]);
    expect(doc.label.aperture).toEqual({ x: WIN.x, y: WIN.y, w: WIN.w, h: WIN.h, r: WIN.r });
    expect(doc.label.prims).toHaveLength(3);
  });

  it("carries the canvas presets and the active one", () => {
    const { doc, errs } = buildTemplate(objs, mask, design(), {
      backgrounds: ["#101010", "#f5f1e6"],
      active: 1,
    });
    expect(errs).toEqual([]);
    expect(doc.label.canvas).toEqual({ backgrounds: ["#101010", "#f5f1e6"], active: 1 });
  });

  it("writes the mask's colour and radius, and no mask without one", () => {
    expect(buildTemplate(objs, mask, design()).doc.label.mask).toEqual({
      color: "#c7a867",
      r: 10,
    });
    const bare = buildTemplate(objs, null, design());
    expect(bare.errs).toEqual([]);
    expect(bare.doc.label).not.toHaveProperty("mask");
    expect(bare.doc.jcard.background).toBe(DEFAULT_PAPER);
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

  it("refuses an id that does not look like k7.my-name", () => {
    const tpl = design();
    tpl.id = "probe";
    expect(buildTemplate(objs, mask, tpl).errs.join("\n")).toContain(
      'id "probe" must look like k7.my-name',
    );
  });

  it("refuses the Card's window drawn at another size", () => {
    const ap: Obj = {
      id: 9,
      type: "rect",
      name: "",
      x: 0,
      y: 0,
      w: WIN.w,
      h: WIN.h,
      r: WIN.r,
      ap: true,
    };
    expect(buildTemplate([...objs, ap], mask, design()).errs.join("\n")).toContain(
      "aperture: the Card's window",
    );
  });

  it("refuses a dynamic text that names no field", () => {
    const nameless: Obj = { id: 4, type: "text", name: "", x: 0, y: 0, w: 100, fr: "title" };
    expect(buildTemplate([...objs, nameless], mask, design()).errs.join("\n")).toContain(
      "dynamic text needs a slot name",
    );
  });

  it("refuses a static text with nothing to print", () => {
    const empty: Obj = {
      id: 4,
      type: "text",
      name: "",
      static: true,
      text: "  ",
      x: 0,
      y: 0,
      w: 100,
      fr: "title",
    };
    expect(buildTemplate([...objs, empty], mask, design()).errs.join("\n")).toContain(
      "static text is empty",
    );
  });

  it("drops a slot nothing prints: the slots follow the text layers", () => {
    const tpl = design();
    syncSlots(objs, tpl);
    tpl.slots.genre = { max: 96 };
    const { doc, errs } = buildTemplate(objs, mask, tpl);
    expect(errs).toEqual([]);
    expect(doc.label.slots).toEqual({ song_title: { max: 96 } });
  });

  it("counts the mask against the sheet's prim cap", () => {
    const many: Obj[] = Array.from({ length: 48 }, (_, i) => ({
      id: 100 + i,
      type: "rect" as const,
      name: "",
      x: i,
      y: i,
      w: 10,
      h: 10,
      color: "#c68b2e",
    }));
    const errs = buildTemplate([...objs, ...many], mask, design()).errs;
    expect(errs.join("\n")).toContain("the mask's rect included");
    expect(errs.join("\n")).toContain("and the sheet holds 48 per design");
  });

  it("refuses a design with no prims at all", () => {
    expect(buildTemplate([], null, design()).errs.join("\n")).toContain(
      "no layers: a design needs at least one prim",
    );
  });
});

describe("the J-card section", () => {
  it("starts with the sheet, the folds and the three fields at the defaults", () => {
    const { doc } = buildTemplate(objs, mask, design());
    expect(doc.jcard.space).toEqual({ w: 1300, h: 1280, unit: 320 });
    expect(doc.jcard.parts).toEqual({
      flap: { x: 0, y: 0, w: 320, h: 1280 },
      spine: { x: 320, y: 0, w: 160, h: 1280 },
      cover: { x: 480, y: 0, w: 820, h: 1280 },
    });
    expect(doc.jcard.background).toBe("#c7a867");
    expect(doc.jcard.fonts).toEqual({
      title: { ff: "Archivo Narrow 600", size: 56 },
      numeral: { ff: "Barlow Condensed 700", size: 136 },
      caption: { ff: "IBM Plex Mono 500", size: 31 },
    });
    expect(doc.jcard.prims).toHaveLength(3);
    expect(doc.jcard.prims[0]).toMatchObject({
      t: "text",
      slot: "title",
      color: INK,
      preview: "Neon Skyline",
    });
  });

  it("takes the J-card's background and faces from the container", () => {
    const tpl = design();
    const jc = starterJCard(tpl, "#102030");
    expect(jc.fonts.title.ff).toBe(tpl.fonts.title.ff);
    expect(jc.background).toBe("#102030");
    const doc = buildTemplate(objs, mask, tpl).doc;
    expect(doc.jcard.background).toBe("#c7a867");
  });

  it("writes a static J-card text and one bound to a field", () => {
    const jcard = starterJCard(design());
    jcard.prims.push(
      {
        id: 5,
        type: "text",
        name: "",
        static: true,
        text: "SIDE A",
        x: 500,
        y: 100,
        w: 200,
        fr: "caption",
        color: INK,
        align: "center",
      },
      {
        id: 6,
        type: "text",
        name: "meta",
        x: 500,
        y: 60,
        w: 200,
        fr: "caption",
        color: "#6b7a2f",
        align: "right",
      },
    );
    const { doc, errs } = buildTemplate(objs, mask, design(), undefined, jcard);
    expect(errs).toEqual([]);
    expect(doc.jcard.prims[3]).toEqual({
      t: "text",
      x: 500,
      y: 100,
      w: 200,
      font: "caption",
      color: INK,
      align: "center",
      text: "SIDE A",
    });
    expect(doc.jcard.prims[4]).toEqual({
      t: "text",
      slot: "meta",
      x: 500,
      y: 60,
      w: 200,
      font: "caption",
      color: "#6b7a2f",
      align: "right",
      preview: "Side A · Side B",
    });
  });

  it("refuses a field, a face, a size or a colour the pair does not carry", () => {
    const jcard = starterJCard(design());
    jcard.prims[0] = { ...jcard.prims[0], name: "year" };
    expect(buildTemplate(objs, mask, design(), undefined, jcard).errs.join("\n")).toContain(
      "is not a J-card field",
    );

    const font = starterJCard(design());
    font.fonts.title = { ff: "Montserrat", size: 56 };
    expect(buildTemplate(objs, mask, design(), undefined, font).errs.join("\n")).toContain(
      'font role title is "Montserrat"',
    );

    const size = starterJCard(design());
    size.fonts.caption.size = 2;
    expect(buildTemplate(objs, mask, design(), undefined, size).errs.join("\n")).toContain(
      "outside 4..640 units",
    );

    const role = starterJCard(design());
    role.prims[0] = { ...role.prims[0], color: "paper" };
    expect(
      buildTemplate(objs, mask, design(), undefined, role).errs.join("\n"),
    ).toContain("is not #RRGGBB");
  });

  it("refuses a J-card layer outside the sheet's +/-4096 units, and over 64 prims", () => {
    const far = starterJCard(design());
    far.prims[1] = { ...far.prims[1], x: 4000, w: 700 };
    expect(buildTemplate(objs, mask, design(), undefined, far).errs.join("\n")).toContain(
      "leaves the sheet's +/-4096 units",
    );

    const many = starterJCard(design());
    many.prims = Array.from({ length: 65 }, (_, i) => ({
      id: i + 1,
      type: "rect" as const,
      name: "",
      x: 0,
      y: 0,
      w: 10,
      h: 10,
      color: INK,
    }));
    expect(buildTemplate(objs, mask, design(), undefined, many).errs.join("\n")).toContain(
      "and the App draws 64",
    );
  });
});

describe("importTemplate", () => {
  it("round-trips a K7Art byte for byte, the label section included", () => {
    const tpl = design();
    const { doc } = buildTemplate(objs, mask, tpl);
    const back = importTemplate(tplText(doc), "#c7a867");
    const again = buildTemplate(back.objs, back.mask, back.tpl, back.canvas, back.jcard);
    expect(again.errs).toEqual([]);
    expect(tplText(again.doc)).toBe(tplText(doc));
    expect(tplText(again.doc.label)).toBe(tplText(doc.label));
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
    doc.label.canvas = { backgrounds: ["#101010"], active: 3 };
    expect(() => importTemplate(JSON.stringify(doc), "#c7a867")).toThrow(
      "canvas.active 3 is not an index into 1 backgrounds",
    );
    doc.label.canvas = { backgrounds: ["#101010", "#nope"], active: 0 };
    expect(() => importTemplate(JSON.stringify(doc), "#c7a867")).toThrow(
      'canvas.backgrounds[1] "#nope" is not #RRGGBB',
    );
  });

  it("opens a design without a canvas on the panel's page", () => {
    const { doc } = buildTemplate(objs, mask, design());
    delete doc.label.canvas;
    expect(importTemplate(tplText(doc), "#c7a867").canvas).toEqual({
      backgrounds: [DEFAULT_CANVAS],
      active: 0,
    });
  });

  it("refuses a space that is not the print", () => {
    const { doc } = buildTemplate(objs, mask, design());
    doc.label.space = { w: 800, h: 480 };
    expect(() => importTemplate(JSON.stringify(doc), "#c7a867")).toThrow(
      "space is 800x480, not 748x359",
    );
  });

  it("refuses a face the sheet does not carry", () => {
    const { doc } = buildTemplate(objs, mask, design());
    doc.label.fonts.title = { ff: "Montserrat", px: 32 };
    expect(() => importTemplate(JSON.stringify(doc), "#c7a867")).toThrow(
      'font role title is "Montserrat" 32 px',
    );
  });

  it("refuses an aperture with no mask to cut", () => {
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
    const noMask = JSON.parse(tplText(doc));
    delete noMask.label.mask;
    expect(() => importTemplate(JSON.stringify(noMask), "#c7a867")).toThrow(
      "aperture without a mask",
    );
  });

  it("refuses a file that is not a K7Art, and an id that is not k7.<slug>", () => {
    expect(() => importTemplate(JSON.stringify({ hello: 1 }), "#c7a867")).toThrow(
      "not a K7Art",
    );
    const { doc } = buildTemplate(objs, mask, design());
    expect(() => importTemplate(JSON.stringify({ ...doc, format: "label" }), "#c7a867")).toThrow(
      'format is "label"',
    );
    expect(() => importTemplate(JSON.stringify({ ...doc, id: "label.probe" }), "#c7a867")).toThrow(
      'id "label.probe" must look like k7.my-name',
    );
    // A v1 K7Art is the editor's to migrate; one without inks cannot be resolved.
    expect(() => importTemplate(JSON.stringify({ ...doc, v: 1 }), "#c7a867")).toThrow(
      "no inks",
    );
  });

  it("reads a v1 file's roles through its own inks", () => {
    const v1 = {
      format: "k7art",
      v: 1,
      id: "k7.old-probe",
      name: "Old Probe",
      inks: { paper: "#f5f1e6", ink: "#241a12", rule: RULE, band: ["#c68b2e", "#a24e2c"] },
      label: {
        space: { w: 748, h: 359 },
        canvas: { backgrounds: ["#101010"], active: 0 },
        fonts: {
          title: { ff: "Archivo Narrow 600", px: 32 },
          numeral: { ff: "Barlow Condensed 700", px: 78 },
          caption: { ff: "IBM Plex Mono 500", px: 18 },
        },
        slots: { song_title: { max: 96 } },
        prims: [
          { t: "rect", x: 0, y: 0, w: 748, h: 359, r: 10, fill: "paper", still: true },
          { t: "rect", x: 40, y: 30, w: 200, h: 120, fill: "band.1", still: true },
          { t: "text", slot: "song_title", x: 300, y: 40, w: 380, font: "title",
            color: "ink", align: "right", preview: "Neon Skyline" },
        ],
      },
      jcard: {
        space: { w: 1300, h: 1280, unit: 320 },
        parts: JCARD_PARTS,
        paper: "#f5f1e6",
        fonts: {
          title: { ff: "Archivo Narrow 600", size: 56 },
          numeral: { ff: "Barlow Condensed 700", size: 136 },
          caption: { ff: "IBM Plex Mono 500", size: 31 },
        },
        prims: [
          { t: "rect", x: 0, y: 0, w: 1300, h: 1280, fill: "paper" },
          { t: "text", slot: "title", x: 540, y: 900, w: 700, font: "numeral",
            color: "ink", align: "left", preview: "Neon Skyline" },
        ],
      },
    };
    const back = importTemplate(JSON.stringify(v1), "#c7a867");
    expect(back.tpl.id).toBe("k7.old-probe");
    expect(back.mask?.color).toBe("#f5f1e6");
    expect(back.objs.find((o) => o.type === "rect")?.color).toBe("#a24e2c");
    expect(back.objs.find((o) => o.type === "text")?.color).toBe("#241a12");
    expect(back.jcard.background).toBe("#f5f1e6");
    expect(back.jcard.prims[0].color).toBe("#f5f1e6");
    expect(back.jcard.prims[1].color).toBe("#241a12");
  });

  it("refuses a design without a J-card, or one whose sheet is not the case's", () => {
    const { doc } = buildTemplate(objs, mask, design());
    const noJcard: Partial<typeof doc> = { ...doc };
    delete noJcard.jcard;
    expect(() => importTemplate(JSON.stringify(noJcard), "#c7a867")).toThrow("jcard is missing");

    const badSpace = JSON.parse(tplText(doc));
    badSpace.jcard.space = { w: 1300, h: 1280, unit: 300 };
    expect(() => importTemplate(JSON.stringify(badSpace), "#c7a867")).toThrow(
      "space is 1300x1280 at 300 units per inch",
    );

    const badPart = JSON.parse(tplText(doc));
    badPart.jcard.parts.spine = { x: 320, y: 0, w: 200, h: 1280 };
    expect(() => importTemplate(JSON.stringify(badPart), "#c7a867")).toThrow(
      'part "spine"',
    );
  });

  it("refuses a J-card field, a role colour or more prims than the App draws", () => {
    const { doc } = buildTemplate(objs, mask, design());
    const badField = JSON.parse(tplText(doc));
    badField.jcard.prims[0].slot = "year";
    expect(() => importTemplate(JSON.stringify(badField), "#c7a867")).toThrow(
      "is not a J-card field",
    );

    const role = JSON.parse(tplText(doc));
    role.jcard.prims[0].color = "ink";
    expect(() => importTemplate(JSON.stringify(role), "#c7a867")).toThrow("is not #RRGGBB");

    const many = JSON.parse(tplText(doc));
    many.jcard.prims = Array.from({ length: 65 }, () => ({
      t: "rect",
      x: 0,
      y: 0,
      w: 10,
      h: 10,
      fill: "#111111",
    }));
    expect(() => importTemplate(JSON.stringify(many), "#c7a867")).toThrow(
      "more than the 64 the App draws",
    );
  });
});

describe("the retired label-template v1", () => {
  const legacy = (): RetiredTemplateDoc => ({
    v: 1,
    id: "label.probe",
    name: "Probe",
    space: { w: 748, h: 359 },
    canvas: { backgrounds: [DEFAULT_CANVAS], active: 0 },
    slots: { song_title: { max: 96 } },
    inks: { paper: "#c7a867", ink: INK, rule: RULE, band: ["#c68b2e", BAND1, BAND2] },
    fonts: design().fonts,
    prims: [
      { t: "rect", x: 0, y: 0, w: 748, h: 359, r: 10, fill: "paper", still: true },
      { t: "rect", x: 40, y: 30, w: 200, h: 120, r: 10, rot: 15, fill: "band.2", still: true },
      {
        t: "text",
        slot: "song_title",
        x: 300,
        y: 40,
        w: 380,
        font: "title",
        color: "band.1",
        align: "right",
        preview: "Neon Skyline",
      },
      {
        t: "text",
        x: 300,
        y: 120,
        w: 380,
        rot: -5,
        font: "numeral",
        color: "ink",
        align: "center",
        text: "STATIC",
      },
    ],
  });

  it("migrates into a K7Art v2: the label section, the id and a starter J-card", () => {
    const back = importTemplate(JSON.stringify(legacy()), "#c7a867");
    expect(back.tpl.id).toBe("k7.probe");
    expect(back.tpl.name).toBe("Probe");
    expect(back.jcard).toEqual(starterJCard(back.tpl, "#c7a867"));
    const { doc, errs } = buildTemplate(
      back.objs,
      back.mask,
      back.tpl,
      back.canvas,
      back.jcard,
    );
    expect(errs).toEqual([]);
    expect(doc.v).toBe(2);
    expect(doc.id).toBe("k7.probe");
    expect(doc.label.mask).toEqual({ color: "#c7a867", r: 10 });
    expect(tplText(doc.label)).toBe(tplText(expectedLabel));
    expect(doc.jcard.prims).toHaveLength(3);
  });

  it("keeps an aperture and its mask, and the label section's bytes", () => {
    const old = legacy();
    old.aperture = { x: WIN.x, y: WIN.y, w: WIN.w, h: WIN.h, r: WIN.r };
    const back = importTemplate(JSON.stringify(old), "#c7a867");
    expect(back.mask).toEqual({ color: "#c7a867", radius: 10, collapsed: false });
    const ap = back.objs.find((o) => o.ap);
    expect(ap).toMatchObject({ x: WIN.x, y: WIN.y, w: WIN.w, h: WIN.h, r: WIN.r });
    const { doc } = buildTemplate(back.objs, back.mask, back.tpl, back.canvas, back.jcard);
    expect(doc.label.mask).toEqual({ color: "#c7a867", r: 10 });
    expect(tplText(doc.label)).toBe(
      tplText({
        space: expectedLabel.space,
        mask: { color: "#c7a867", r: 10 },
        aperture: { x: WIN.x, y: WIN.y, w: WIN.w, h: WIN.h, r: WIN.r },
        canvas: expectedLabel.canvas,
        fonts: expectedLabel.fonts,
        slots: expectedLabel.slots,
        prims: expectedLabel.prims,
      }),
    );
  });
});

describe("migrateDocData", () => {
  const stored = (over: Record<string, unknown> = {}): DocData =>
    ({
      w: 748,
      h: 359,
      bg: DEFAULT_CANVAS,
      bgs: [DEFAULT_CANVAS],
      bgi: 0,
      objs: [],
      nid: 0,
      mask: { color: "#c7a867", radius: 10, collapsed: false },
      tpl: {
        ...defaultTpl(),
        id: "label.old",
        inks: { paper: "#c7a867", ink: INK, rule: RULE, band: ["#c68b2e", BAND1] },
        ...over,
      },
    }) as DocData;

  it("gives a stored document its K7Art id and a starter J-card", () => {
    const data = migrateDocData(stored());
    expect(data.tpl!.id).toBe("k7.old");
    expect(data.jcard).toEqual(starterJCard(data.tpl!, "#c7a867"));
    expect(data.tpl).not.toHaveProperty("inks");
  });

  it("resolves a stored layer's role through the document's inks", () => {
    const data = stored();
    data.objs = [
      { id: 1, type: "rect", name: "", x: 0, y: 0, w: 10, h: 10, role: "band.1" } as Obj,
      { id: 2, type: "text", name: "", x: 0, y: 0, w: 10, role: "ink" } as Obj,
    ];
    const out = migrateDocData(data);
    expect(out.objs[0].color).toBe(BAND1);
    expect(out.objs[1].color).toBe(INK);
    expect(out.objs[0]).not.toHaveProperty("role");
  });

  it("is idempotent, and never moves an id twice", () => {
    const once = migrateDocData(stored());
    const first = tplText(once.jcard);
    expect(migrateId("label.neon")).toBe("k7.neon");
    expect(migrateId("k7.neon")).toBe("k7.neon");
    expect(migrateId("Probe")).toBe("Probe");
    migrateDocData(once);
    expect(tplText(once.jcard)).toBe(first);
  });

  it("keeps the mask's colour, and defaults one without it", () => {
    const masked = migrateDocData(stored());
    expect(masked.mask!.color).toBe("#c7a867");
    const bare = stored();
    bare.mask = null;
    expect(migrateDocData(bare).jcard!.background).toBe(DEFAULT_PAPER);
  });

  it("keeps a J-card that is already there", () => {
    const data = stored();
    const jcard = starterJCard(data.tpl!, "#c7a867");
    jcard.fonts.title.size = 90;
    data.jcard = jcard;
    expect(migrateDocData(data).jcard).toMatchObject({
      fonts: { title: { size: 90 } },
    });
  });
});

describe("the J-card's own vocabulary", () => {
  it("binds to the label's Slots as well as its own three fields", () => {
    expect(JCARD_FIELDS).toEqual([...SLOTS.map((slot) => slot.id), "title", "artist", "meta"]);
    expect(JCARD_FIELD_CHOICES).toEqual([...SLOTS.map((slot) => slot.id), "meta"]);
    expect(JCARD_SIMS.title).toBe("Neon Skyline");
    expect(JCARD_SIMS.artist).toBe("The Analog Sons");
    expect(JCARD_SIMS.meta).toBe("Side A · Side B");
    expect(JCARD_SIMS.bpm).toBe(SLOTS.find((slot) => slot.id === "bpm")?.sim);
    expect(JCARD_PARTS.cover.x + JCARD_PARTS.cover.w).toBe(JCARD_SPACE.w);
    expect(JCARD_PARTS.flap.w + JCARD_PARTS.spine.w + JCARD_PARTS.cover.w).toBe(
      JCARD_SPACE.w,
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

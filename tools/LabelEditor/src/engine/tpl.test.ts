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
  JCARD_FIELDS,
  JCARD_PARTS,
  JCARD_SIMS,
  JCARD_SPACE,
  starterJCard,
} from "./jcard";
import { WIN } from "./vocab";
import type { DocData, Mask, Obj, Tpl } from "./types";

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
  t.id = "k7.probe";
  t.name = "Probe";
  return t;
};

/**
 * The label section the retired builder wrote for the design above, in the section's own key
 * order. The bytes are the regression: the container changed where the section lives and
 * where its identity and inks went, not a byte of what the sheet reads.
 */
const expectedLabel = {
  space: { w: 748, h: 359 },
  canvas: { backgrounds: [DEFAULT_CANVAS], active: 0 },
  fonts: {
    title: { ff: "Archivo Narrow 600", px: 32 },
    numeral: { ff: "Barlow Condensed 700", px: 78 },
    caption: { ff: "IBM Plex Mono 500", px: 18 },
  },
  slots: { song_title: { max: 96 } },
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
};

describe("buildTemplate", () => {
  it("writes one K7Art: the container, the label section and the J-card", () => {
    const tpl = design();
    const { doc, errs } = buildTemplate(objs, mask, tpl);
    expect(errs).toEqual([]);
    expect(doc.format).toBe("k7art");
    expect(doc.v).toBe(1);
    expect(doc.id).toBe("k7.probe");
    expect(doc.name).toBe("Probe");
    expect(doc.inks).toEqual({
      paper: "#c7a867",
      ink: "#151515",
      rule: "#8e7e4a",
      band: tpl.inks.band,
    });
    expect(doc.label.space).toEqual({ w: 748, h: 359 });
    expect(doc.label.fonts.title).toEqual({ ff: "Archivo Narrow 600", px: 32 });
    expect(doc.label.prims).toHaveLength(4);
    expect(doc.label.prims[0]).toMatchObject({ fill: "paper", w: 748, h: 359 });
    expect(doc.label.prims[1]).toMatchObject({ t: "rect", fill: "band.2", rot: 15 });
    expect(doc.label.prims[2]).toMatchObject({ t: "text", slot: "song_title", align: "right" });
    expect(doc.label.prims[3]).toMatchObject({ t: "text", text: "STATIC", rot: -5 });
    expect(doc.label.slots).toEqual({ song_title: { max: 96 } });
    expect(doc.jcard.space).toEqual({ w: 1300, h: 1280, unit: 320 });
  });

  it("writes the file's key order, and each section's", () => {
    const { doc } = buildTemplate(objs, mask, design());
    expect(Object.keys(doc)).toEqual([
      "format",
      "v",
      "id",
      "name",
      "inks",
      "label",
      "jcard",
    ]);
    expect(Object.keys(doc.inks)).toEqual(["paper", "ink", "rule", "band"]);
    expect(Object.keys(doc.label)).toEqual([
      "space",
      "canvas",
      "fonts",
      "slots",
      "prims",
    ]);
    expect(Object.keys(doc.label.fonts)).toEqual(["title", "numeral", "caption"]);
    expect(Object.keys(doc.jcard)).toEqual(["space", "parts", "fonts", "prims"]);
    expect(Object.keys(doc.jcard.space)).toEqual(["w", "h", "unit"]);
    expect(Object.keys(doc.jcard.parts)).toEqual(["flap", "spine", "cover"]);
    expect(Object.keys(doc.jcard.fonts)).toEqual(["title", "numeral", "caption"]);
  });

  it("keeps the label section byte for byte what label-template v1 wrote", () => {
    const { doc } = buildTemplate(objs, mask, design());
    expect(tplText(doc.label)).toBe(tplText(expectedLabel));
  });

  it("puts the aperture between the space and the canvas, in the section's order", () => {
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
      "aperture",
      "canvas",
      "fonts",
      "slots",
      "prims",
    ]);
    expect(doc.label.aperture).toEqual({ x: WIN.x, y: WIN.y, w: WIN.w, h: WIN.h, r: WIN.r });
    expect(doc.label.prims).toHaveLength(4);
  });

  it("carries the canvas presets and the active one", () => {
    const { doc, errs } = buildTemplate(objs, mask, design(), {
      backgrounds: ["#101010", "#f5f1e6"],
      active: 1,
    });
    expect(errs).toEqual([]);
    expect(doc.label.canvas).toEqual({ backgrounds: ["#101010", "#f5f1e6"], active: 1 });
  });

  it("takes the container's paper from the mask, and the default without one", () => {
    expect(buildTemplate(objs, mask, design()).doc.inks.paper).toBe("#c7a867");
    const bare = buildTemplate(objs, null, design());
    expect(bare.errs).toEqual([]);
    expect(bare.doc.inks.paper).toBe(DEFAULT_PAPER);
    expect(bare.doc.label.prims).toHaveLength(3);
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
    tpl.id = "Probe";
    expect(buildTemplate(objs, mask, tpl).errs).toContain(
      'id "Probe" must look like k7.my-name',
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
    expect(doc.label.slots).toEqual({ song_title: { max: 96 } });
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
    expect(doc.jcard.fonts).toEqual({
      title: { ff: "Archivo Narrow 600", size: 56 },
      numeral: { ff: "Barlow Condensed 700", size: 136 },
      caption: { ff: "IBM Plex Mono 500", size: 31 },
    });
    expect(doc.jcard.prims).toEqual([
      { t: "rect", x: 0, y: 0, w: 1300, h: 1280, fill: "paper" },
      {
        t: "text",
        slot: "title",
        x: 540,
        y: 900,
        w: 700,
        font: "numeral",
        color: "ink",
        align: "left",
        preview: "Neon Skyline",
      },
      {
        t: "text",
        slot: "artist",
        x: 540,
        y: 1060,
        w: 700,
        font: "title",
        color: "ink",
        align: "left",
        preview: "The Analog Sons",
      },
      {
        t: "text",
        slot: "meta",
        x: 540,
        y: 1120,
        w: 700,
        font: "caption",
        color: "ink",
        align: "left",
        preview: "Side A · Side B",
      },
    ]);
  });

  it("takes the J-card's paper role and faces from the container", () => {
    const tpl = design();
    const jc = starterJCard(tpl);
    expect(jc.fonts.title.ff).toBe(tpl.fonts.title.ff);
    expect(jc.prims[0]).toMatchObject({ role: "paper" });
    const paper = buildTemplate(objs, mask, tpl).doc.jcard.prims[0] as {
      fill: string;
    };
    expect(paper.fill).toBe("paper");
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
        role: "ink",
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
        role: "band.3",
        align: "right",
      },
    );
    const { doc, errs } = buildTemplate(objs, mask, design(), undefined, jcard);
    expect(errs).toEqual([]);
    expect(doc.jcard.prims[4]).toEqual({
      t: "text",
      x: 500,
      y: 100,
      w: 200,
      font: "caption",
      color: "ink",
      align: "center",
      text: "SIDE A",
    });
    expect(doc.jcard.prims[5]).toEqual({
      t: "text",
      slot: "meta",
      x: 500,
      y: 60,
      w: 200,
      font: "caption",
      color: "band.3",
      align: "right",
      preview: "Side A · Side B",
    });
  });

  it("refuses a field, a face, a size or a colour the pair does not carry", () => {
    const jcard = starterJCard(design());
    jcard.prims[1] = { ...jcard.prims[1], name: "year" };
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

    const paperText = starterJCard(design());
    paperText.prims[1] = { ...paperText.prims[1], role: "paper" };
    expect(
      buildTemplate(objs, mask, design(), undefined, paperText).errs.join("\n"),
    ).toContain("paper has nothing to read against it");
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
      role: "ink",
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
    const noPaper = {
      ...doc,
      label: { ...doc.label, prims: doc.label.prims.filter((p) => p.fill !== "paper") },
    };
    expect(() => importTemplate(JSON.stringify(noPaper), "#c7a867")).toThrow(
      "aperture without a paper",
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
    expect(() => importTemplate(JSON.stringify({ ...doc, v: 2 }), "#c7a867")).toThrow(
      "not a K7Art v1",
    );
    expect(() => importTemplate(JSON.stringify({ ...doc, id: "label.probe" }), "#c7a867")).toThrow(
      'id "label.probe" must look like k7.my-name',
    );
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

  it("refuses a J-card field, a literal colour or a text on the paper", () => {
    const { doc } = buildTemplate(objs, mask, design());
    const badField = JSON.parse(tplText(doc));
    badField.jcard.prims[1].slot = "year";
    expect(() => importTemplate(JSON.stringify(badField), "#c7a867")).toThrow(
      "is not a J-card field",
    );

    const literal = JSON.parse(tplText(doc));
    literal.jcard.prims[1].color = "#ff0000";
    expect(() => importTemplate(JSON.stringify(literal), "#c7a867")).toThrow(
      "literal colour",
    );

    const onPaper = JSON.parse(tplText(doc));
    onPaper.jcard.prims[1].color = "paper";
    expect(() => importTemplate(JSON.stringify(onPaper), "#c7a867")).toThrow(
      "paper has nothing to read against it",
    );

    const many = JSON.parse(tplText(doc));
    many.jcard.prims = Array.from({ length: 65 }, () => ({
      t: "rect",
      x: 0,
      y: 0,
      w: 10,
      h: 10,
      fill: "ink",
    }));
    expect(() => importTemplate(JSON.stringify(many), "#c7a867")).toThrow(
      "more than the 64 the App draws",
    );
  });

  it("reads a four-band design once, with the editor's eight", () => {
    const tpl = design();
    const { doc } = buildTemplate(objs, mask, tpl);
    doc.inks.band = doc.inks.band.slice(0, 4);
    const back = importTemplate(JSON.stringify(doc), "#c7a867");
    expect(back.tpl.inks.band).toHaveLength(8);
    expect(back.tpl.inks.band.slice(0, 4)).toEqual(doc.inks.band);
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
    inks: {
      paper: "#c7a867",
      ink: "#151515",
      rule: "#8e7e4a",
      band: design().inks.band,
    },
    fonts: design().fonts,
    prims: expectedLabel.prims as Array<Record<string, unknown>>,
  });

  it("migrates into a K7Art: the label section, the id and a starter J-card", () => {
    const back = importTemplate(JSON.stringify(legacy()), "#c7a867");
    expect(back.tpl.id).toBe("k7.probe");
    expect(back.tpl.name).toBe("Probe");
    expect(back.jcard).toEqual(starterJCard(back.tpl));
    const { doc, errs } = buildTemplate(
      back.objs,
      back.mask,
      back.tpl,
      back.canvas,
      back.jcard,
    );
    expect(errs).toEqual([]);
    expect(doc.format).toBe("k7art");
    expect(doc.id).toBe("k7.probe");
    expect(doc.inks).toEqual(legacy().inks);
    expect(tplText(doc.label)).toBe(tplText(expectedLabel));
    expect(doc.jcard.prims).toHaveLength(4);
  });

  it("keeps an aperture and its paper, and the label section's bytes", () => {
    const old = legacy();
    old.aperture = { x: WIN.x, y: WIN.y, w: WIN.w, h: WIN.h, r: WIN.r };
    old.prims = [
      { t: "rect", x: 0, y: 0, w: 748, h: 359, r: 10, fill: "paper", still: true },
      ...(expectedLabel.prims.slice(1) as Array<Record<string, unknown>>),
    ];
    const back = importTemplate(JSON.stringify(old), "#c7a867");
    expect(back.mask).toEqual({ color: "#c7a867", radius: 10, collapsed: false });
    const ap = back.objs.find((o) => o.ap);
    expect(ap).toMatchObject({ x: WIN.x, y: WIN.y, w: WIN.w, h: WIN.h, r: WIN.r });
    const { doc } = buildTemplate(back.objs, back.mask, back.tpl, back.canvas, back.jcard);
    expect(tplText(doc.label)).toBe(
      tplText({
        space: expectedLabel.space,
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
  const stored = (over: Partial<Tpl> = {}): DocData =>
    ({
      w: 748,
      h: 359,
      bg: DEFAULT_CANVAS,
      bgs: [DEFAULT_CANVAS],
      bgi: 0,
      objs: [],
      nid: 0,
      mask: { color: "#c7a867", radius: 10, collapsed: false },
      tpl: { ...defaultTpl(), id: "label.old", ...over },
    }) as DocData;

  it("gives a stored document its K7Art id and a starter J-card", () => {
    const data = migrateDocData(stored());
    expect(data.tpl!.id).toBe("k7.old");
    expect(data.jcard).toEqual(starterJCard(data.tpl!));
    expect(data.jcard!.prims[0]).toMatchObject({ role: "paper" });
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

  it("takes the paper from the mask, and defaults one without it", () => {
    const masked = migrateDocData(stored());
    expect(masked.tpl!.inks.paper).toBe("#c7a867");
    const bare = stored();
    bare.mask = null;
    bare.tpl = { ...bare.tpl!, inks: { ...bare.tpl!.inks, paper: "" } };
    expect(migrateDocData(bare).tpl!.inks.paper).toBe(DEFAULT_PAPER);
  });

  it("keeps a J-card that is already there", () => {
    const data = stored();
    const jcard = starterJCard(data.tpl!);
    jcard.fonts.title.size = 90;
    data.jcard = jcard;
    expect(migrateDocData(data).jcard).toMatchObject({
      fonts: { title: { size: 90 } },
    });
  });
});

describe("the J-card's own vocabulary", () => {
  it("names the three App fields and their simulations", () => {
    expect(JCARD_FIELDS).toEqual(["title", "artist", "meta"]);
    expect(JCARD_SIMS).toEqual({
      title: "Neon Skyline",
      artist: "The Analog Sons",
      meta: "Side A · Side B",
    });
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

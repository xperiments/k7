import { describe, expect, it } from "vitest";

import {
  buildJCard,
  importJCard,
  JCARD_BYTES_MAX,
  JCARD_PARTS,
  JCARD_PRIM_MAX,
  JCARD_SPACE,
  migrateJCard,
  starterJCard,
} from "./jcard";
import { defaultTpl } from "./tpl";
import type { JCardDoc, Tpl } from "./types";

const tpl = (): Tpl => {
  const t = defaultTpl();
  t.id = "k7.probe";
  t.name = "Probe";
  return t;
};

const INK = "#151515";
const PAPER = "#c7a867";

describe("buildJCard", () => {
  it("writes the sheet, the folds, the background, the fonts and the prims", () => {
    const { doc, errs } = buildJCard(starterJCard(tpl(), PAPER));
    expect(errs).toEqual([]);
    expect(doc.space).toEqual({ w: 1300, h: 1280, unit: 320 });
    expect(doc.parts).toEqual(JCARD_PARTS);
    expect(Object.keys(doc)).toEqual(["space", "parts", "background", "fonts", "prims"]);
    expect(doc.background).toBe(PAPER);
    expect(doc.fonts.numeral).toEqual({ ff: "Barlow Condensed 700", size: 136 });
    // The sheet's colour paints the background; there is no full-sheet paper rect.
    expect(doc.prims).toHaveLength(3);
    expect(doc.prims.every((p) => p.t === "text")).toBe(true);
  });

  it("writes a rect's radius and rotation, and a circle as its half-size", () => {
    const jcard = starterJCard(tpl());
    jcard.prims.push(
      {
        id: 5, type: "rect", name: "", x: 10, y: 20, w: 100, h: 60, r: 8, rot: 15,
        color: "#c68b2e",
      },
      { id: 6, type: "circle", name: "", x: 300, y: 20, w: 80, h: 80, color: "#8e7e4a" },
    );
    const { doc, errs } = buildJCard(jcard);
    expect(errs).toEqual([]);
    expect(doc.prims[3]).toEqual({
      t: "rect",
      x: 10,
      y: 20,
      w: 100,
      h: 60,
      r: 8,
      rot: 15,
      fill: "#c68b2e",
    });
    expect(doc.prims[4]).toEqual({
      t: "rect", x: 300, y: 20, w: 80, h: 80, r: 40, fill: "#8e7e4a",
    });
  });

  it("gives a dynamic text the field's default preview until one is set", () => {
    const jcard = starterJCard(tpl());
    delete jcard.prims[0].sim;
    const { doc } = buildJCard(jcard);
    expect(doc.prims[0]).toMatchObject({ preview: "Neon Skyline" });
  });

  it("writes the label's test values, and ignores a layer's own", () => {
    const jcard = starterJCard(tpl());
    delete jcard.prims[0].sim;
    /* The label's test values are the design's: the J-card reads the same ones. */
    const container = tpl();
    container.sim = { ...container.sim, title: "Case Study" };
    expect(buildJCard(jcard, container).doc.prims[0]).toMatchObject({ preview: "Case Study" });

    /* A design read from an older file may still carry a layer's own; it is not the preview. */
    jcard.prims[0].sim = "Layer Value";
    expect(buildJCard(jcard, container).doc.prims[0]).toMatchObject({ preview: "Case Study" });
  });

  it("refuses a colour that is not #RRGGBB, and a rect without one", () => {
    const jcard = starterJCard(tpl());
    jcard.prims.push({
      id: 5,
      type: "rect",
      name: "",
      x: 0,
      y: 0,
      w: 10,
      h: 10,
      color: "band.2",
    });
    expect(buildJCard(jcard).errs.join("\n")).toContain("is not #RRGGBB");

    const missing = starterJCard(tpl());
    missing.prims.push({ id: 5, type: "rect", name: "", x: 0, y: 0, w: 10, h: 10 });
    expect(buildJCard(missing).errs.join("\n")).toContain("is not #RRGGBB");

    const text = starterJCard(tpl());
    text.prims[0].color = "ink";
    expect(buildJCard(text).errs.join("\n")).toContain("is not #RRGGBB");
  });

  it("refuses a J-card over the App's byte cap", () => {
    const jcard = starterJCard(tpl());
    jcard.prims.push({
      id: 5,
      type: "text",
      name: "",
      static: true,
      text: "x".repeat(JCARD_BYTES_MAX + 100),
      x: 0,
      y: 0,
      w: 700,
      fr: "caption",
      color: INK,
    });
    expect(buildJCard(jcard).errs.join("\n")).toContain("over the 16384 the App bundles");
  });
});

describe("importJCard", () => {
  it("round-trips the starter byte for byte", () => {
    const start = starterJCard(tpl(), PAPER);
    const { doc } = buildJCard(start);
    const back = importJCard(doc, null);
    const again = buildJCard(back);
    expect(again.errs).toEqual([]);
    expect(JSON.stringify(again.doc)).toBe(JSON.stringify(doc));
  });

  it("reads the prims back as the editor's layers, and the fields' values", () => {
    const back = importJCard(buildJCard(starterJCard(tpl(), PAPER)).doc, null);
    expect(back.prims.map((o) => o.id)).toEqual([1, 2, 3]);
    expect(back.background).toBe(PAPER);
    expect(back.sims).toMatchObject({
      title: "Neon Skyline",
      artist: "The Analog Sons",
      meta: "Side A · Side B",
    });
    expect(back.prims[0]).toMatchObject({
      type: "text",
      name: "title",
      fr: "numeral",
      color: INK,
      sim: "Neon Skyline",
    });
    expect(back.nid).toBe(3);
  });

  it("reads a v1 section's paper and roles through its inks", () => {
    const v1 = {
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
    };
    const inks = { paper: "#f5f1e6", ink: "#241a12", rule: "#8e7e4a", band: ["#c68b2e"] };
    const back = importJCard(v1, inks);
    expect(back.background).toBe("#f5f1e6");
    expect(back.prims[0].color).toBe("#f5f1e6");
    expect(back.prims[1].color).toBe("#241a12");
  });

  it("refuses a face, a size, a field and an alignment the pair does not carry", () => {
    const base = buildJCard(starterJCard(tpl())).doc;
    const face = JSON.parse(JSON.stringify(base));
    face.fonts.title.ff = "Jura";
    expect(() => importJCard(face, null)).toThrow("not one of");

    const size = JSON.parse(JSON.stringify(base));
    size.fonts.caption.size = 641;
    expect(() => importJCard(size, null)).toThrow("outside 4..640 units");

    const field = JSON.parse(JSON.stringify(base));
    field.prims[0].slot = "year";
    expect(() => importJCard(field, null)).toThrow("is not a J-card field");

    const align = JSON.parse(JSON.stringify(base));
    align.prims[0].align = "justify";
    expect(() => importJCard(align, null)).toThrow("is not left, center or right");
  });

  it("refuses a prim outside the sheet, a role, and a missing background", () => {
    const base = buildJCard(starterJCard(tpl())).doc;
    const far = JSON.parse(JSON.stringify(base));
    far.prims[0].x = -5000;
    expect(() => importJCard(far, null)).toThrow("leaves the sheet's +/-4096 units");

    const role = JSON.parse(JSON.stringify(base));
    role.prims[0].color = "paper";
    expect(() => importJCard(role, null)).toThrow("is not #RRGGBB");

    const rect = JSON.parse(JSON.stringify(base));
    rect.prims.push({ t: "rect", x: 0, y: 0, w: 10, h: 10, fill: "ink" });
    expect(() => importJCard(rect, null)).toThrow("is not #RRGGBB");

    const noBackground = JSON.parse(JSON.stringify(base));
    delete noBackground.background;
    expect(() => importJCard(noBackground, null)).toThrow("background");
  });

  it("refuses a J-card over the prim cap, or over the byte cap", () => {
    const many = JSON.parse(JSON.stringify(buildJCard(starterJCard(tpl())).doc));
    many.prims = Array.from({ length: JCARD_PRIM_MAX + 1 }, () => ({
      t: "rect",
      x: 0,
      y: 0,
      w: 10,
      h: 10,
      fill: "#111111",
    }));
    expect(() => importJCard(many, null)).toThrow(`more than the ${JCARD_PRIM_MAX}`);

    const long = JSON.parse(JSON.stringify(buildJCard(starterJCard(tpl())).doc));
    long.prims[0].text = "x".repeat(JCARD_BYTES_MAX + 100);
    delete long.prims[0].slot;
    expect(() => importJCard(long, null)).toThrow("over the 16384 the App bundles");
  });
});

describe("migrateJCard", () => {
  it("starts a document without one from the label's faces", () => {
    const migrated = migrateJCard(undefined, tpl(), null, null);
    expect(migrated).toEqual(starterJCard(tpl()));
  });

  it("migrates a v1 section's paper and roles through the document's inks", () => {
    const v1 = {
      paper: "#f5f1e6",
      fonts: {
        title: { ff: "Archivo Narrow 600", size: 56 },
        numeral: { ff: "Barlow Condensed 700", size: 136 },
        caption: { ff: "IBM Plex Mono 500", size: 31 },
      },
      prims: [
        { id: 1, type: "rect", name: "", x: 0, y: 0, w: 1300, h: 1280, role: "paper" },
        { id: 2, type: "text", name: "title", x: 540, y: 900, w: 700, fr: "numeral",
          role: "ink", align: "left", sim: "Neon Skyline" },
      ],
    } as unknown as JCardDoc;
    const inks = { paper: "#f5f1e6", ink: "#241a12", rule: "#8e7e4a", band: [] };
    const migrated = migrateJCard(v1, tpl(), inks, null);
    expect(migrated.background).toBe("#f5f1e6");
    expect(migrated.prims[0].color).toBe("#f5f1e6");
    expect(migrated.prims[1].color).toBe("#241a12");
    expect(migrated.prims[1]).not.toHaveProperty("role");
  });

  it("repairs a damaged font record and keeps the prims", () => {
    const damaged = starterJCard(tpl()) as unknown as JCardDoc;
    damaged.fonts.title = { ff: "Jura", size: 1 };
    const migrated = migrateJCard(damaged, tpl(), null, null);
    expect(migrated.fonts.title).toEqual({ ff: "Archivo Narrow 600", size: 56 });
    expect(migrated.prims).toHaveLength(3);
  });

  it("replaces a section with no prims with the starter", () => {
    const empty: JCardDoc = { ...starterJCard(tpl()), prims: [] };
    expect(migrateJCard(empty, tpl(), null, null).prims).toHaveLength(3);
  });

  it("recovers a document's field values from its prims when it has none", () => {
    const old = starterJCard(tpl()) as unknown as JCardDoc;
    delete old.sims;
    old.prims[0].sim = "Old Title";
    const migrated = migrateJCard(old, tpl(), null, null);
    expect(migrated.sims?.title).toBe("Old Title");
    expect(migrated.sims?.artist).toBe("The Analog Sons");
  });

  it("keeps the sheet constants out of the document", () => {
    // The document stores the layers and the sizes; the space and the folds are the case's.
    const jcard = starterJCard(tpl());
    expect(jcard).not.toHaveProperty("space");
    expect(jcard).not.toHaveProperty("parts");
    expect(JCARD_SPACE.w).toBe(1300);
    expect(JCARD_PARTS.cover.w).toBe(820);
  });
});

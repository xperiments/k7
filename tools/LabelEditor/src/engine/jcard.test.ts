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

describe("buildJCard", () => {
  it("writes the sheet, the folds, the fonts and the prims", () => {
    const { doc, errs } = buildJCard(starterJCard(tpl()));
    expect(errs).toEqual([]);
    expect(doc.space).toEqual({ w: 1300, h: 1280, unit: 320 });
    expect(doc.parts).toEqual(JCARD_PARTS);
    expect(Object.keys(doc)).toEqual(["space", "parts", "paper", "fonts", "prims"]);
    expect(doc.fonts.numeral).toEqual({ ff: "Barlow Condensed 700", size: 136 });
    expect(doc.prims).toHaveLength(4);
  });

  it("writes a rect's radius and rotation, and a circle as its half-size", () => {
    const jcard = starterJCard(tpl());
    jcard.prims.push(
      { id: 5, type: "rect", name: "", x: 10, y: 20, w: 100, h: 60, r: 8, rot: 15, role: "band.2" },
      { id: 6, type: "circle", name: "", x: 300, y: 20, w: 80, h: 80, role: "rule" },
    );
    const { doc, errs } = buildJCard(jcard);
    expect(errs).toEqual([]);
    expect(doc.prims[4]).toEqual({
      t: "rect",
      x: 10,
      y: 20,
      w: 100,
      h: 60,
      r: 8,
      rot: 15,
      fill: "band.2",
    });
    expect(doc.prims[5]).toEqual({ t: "rect", x: 300, y: 20, w: 80, h: 80, r: 40, fill: "rule" });
  });

  it("gives a dynamic text the field's default preview until one is set", () => {
    const jcard = starterJCard(tpl());
    delete jcard.prims[1].sim;
    const { doc } = buildJCard(jcard);
    expect(doc.prims[1]).toMatchObject({ preview: "Neon Skyline" });
  });

  it("writes the Fields panel's value, and a layer's own test value over it", () => {
    const jcard = starterJCard(tpl());
    delete jcard.prims[1].sim;
    jcard.sims = { ...jcard.sims, title: "Case Study" };
    expect(buildJCard(jcard).doc.prims[1]).toMatchObject({ preview: "Case Study" });

    jcard.prims[1].sim = "Layer Value";
    expect(buildJCard(jcard).doc.prims[1]).toMatchObject({ preview: "Layer Value" });
  });

  it("refuses a literal colour, an unknown role and a rect without one", () => {
    const jcard = starterJCard(tpl());
    jcard.prims.push({
      id: 5,
      type: "rect",
      name: "",
      x: 0,
      y: 0,
      w: 10,
      h: 10,
      role: "#ff0000",
    });
    expect(buildJCard(jcard).errs.join("\n")).toContain("literal colour");

    jcard.prims[4].role = "nope";
    expect(buildJCard(jcard).errs.join("\n")).toContain("not one of the container's inks");

    delete jcard.prims[4].role;
    expect(buildJCard(jcard).errs.join("\n")).toContain("(none)");
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
      role: "ink",
    });
    expect(buildJCard(jcard).errs.join("\n")).toContain("over the 16384 the App bundles");
  });
});

describe("importJCard", () => {
  it("round-trips the starter byte for byte", () => {
    const start = starterJCard(tpl());
    const { doc } = buildJCard(start);
    const back = importJCard(doc, tpl());
    const again = buildJCard(back);
    expect(again.errs).toEqual([]);
    expect(JSON.stringify(again.doc)).toBe(JSON.stringify(doc));
  });

  it("reads the prims back as the editor's layers, and the fields' values", () => {
    const back = importJCard(buildJCard(starterJCard(tpl())).doc, tpl());
    expect(back.prims.map((o) => o.id)).toEqual([1, 2, 3, 4]);
    expect(back.sims).toMatchObject({
      title: "Neon Skyline",
      artist: "The Analog Sons",
      meta: "Side A · Side B",
    });
    expect(back.prims[1]).toMatchObject({
      type: "text",
      name: "title",
      fr: "numeral",
      role: "ink",
      sim: "Neon Skyline",
    });
    expect(back.nid).toBe(4);
  });

  it("refuses a face, a size, a field and an alignment the pair does not carry", () => {
    const base = buildJCard(starterJCard(tpl())).doc;
    const face = JSON.parse(JSON.stringify(base));
    face.fonts.title.ff = "Jura";
    expect(() => importJCard(face, tpl())).toThrow("not one of");

    const size = JSON.parse(JSON.stringify(base));
    size.fonts.caption.size = 641;
    expect(() => importJCard(size, tpl())).toThrow("outside 4..640 units");

    const field = JSON.parse(JSON.stringify(base));
    field.prims[1].slot = "year";
    expect(() => importJCard(field, tpl())).toThrow("is not a J-card field");

    const align = JSON.parse(JSON.stringify(base));
    align.prims[1].align = "justify";
    expect(() => importJCard(align, tpl())).toThrow("is not left, center or right");
  });

  it("refuses a prim outside the sheet, a literal colour and a text on the paper", () => {
    const base = buildJCard(starterJCard(tpl())).doc;
    const far = JSON.parse(JSON.stringify(base));
    far.prims[1].x = -5000;
    expect(() => importJCard(far, tpl())).toThrow("leaves the sheet's +/-4096 units");

    const literal = JSON.parse(JSON.stringify(base));
    literal.prims[0].fill = "#ff0000";
    expect(() => importJCard(literal, tpl())).toThrow("literal colour");

    const paper = JSON.parse(JSON.stringify(base));
    paper.prims[2].color = "paper";
    expect(() => importJCard(paper, tpl())).toThrow("paper has nothing to read against it");
  });

  it("refuses a J-card over the prim cap, or over the byte cap", () => {
    const many = JSON.parse(JSON.stringify(buildJCard(starterJCard(tpl())).doc));
    many.prims = Array.from({ length: JCARD_PRIM_MAX + 1 }, () => ({
      t: "rect",
      x: 0,
      y: 0,
      w: 10,
      h: 10,
      fill: "ink",
    }));
    expect(() => importJCard(many, tpl())).toThrow(`more than the ${JCARD_PRIM_MAX}`);

    const long = JSON.parse(JSON.stringify(buildJCard(starterJCard(tpl())).doc));
    long.prims[1].text = "x".repeat(JCARD_BYTES_MAX + 100);
    delete long.prims[1].slot;
    expect(() => importJCard(long, tpl())).toThrow("over the 16384 the App bundles");
  });
});

describe("migrateJCard", () => {
  it("starts a document without one from the label's faces", () => {
    const migrated = migrateJCard(undefined, tpl());
    expect(migrated).toEqual(starterJCard(tpl()));
  });

  it("repairs a damaged font record and keeps the prims", () => {
    const damaged = starterJCard(tpl()) as unknown as JCardDoc;
    damaged.fonts.title = { ff: "Jura", size: 1 };
    const migrated = migrateJCard(damaged, tpl());
    expect(migrated.fonts.title).toEqual({ ff: "Archivo Narrow 600", size: 56 });
    expect(migrated.prims).toHaveLength(4);
  });

  it("replaces a section with no prims with the starter", () => {
    const empty: JCardDoc = { ...starterJCard(tpl()), prims: [] };
    expect(migrateJCard(empty, tpl()).prims).toHaveLength(4);
  });

  it("recovers a document's field values from its prims when it has none", () => {
    const old = starterJCard(tpl()) as unknown as JCardDoc;
    delete old.sims;
    old.prims[1].sim = "Old Title";
    const migrated = migrateJCard(old, tpl());
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

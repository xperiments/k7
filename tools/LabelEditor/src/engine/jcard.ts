import { VOCAB } from "./vocab.generated";
import { FROLES, ROLES, SLOTS, TEXT_ROLES } from "./vocab";
import { DEFAULT_PAPER } from "./tpl";
import type { FontRole, JCardDoc, JObj, Tpl } from "./types";

/**
 * The J-card section of a K7Art (docs/specs/k7art.md §4): one printed side of a cassette
 * case, 4.0625x4 in at 320 units per inch, scored into a flap, a spine and a cover. The
 * packer never draws it; the editor and the webapp do. This module is the section's format:
 * its constants, its starter, its writer and its reader.
 */
export const JCARD_W = 1300;
export const JCARD_H = 1280;
/** Units per inch: 1300 / 320 = 4.0625 in wide, 1280 / 320 = 4 in tall. */
export const JCARD_UNIT = 320;

export const JCARD_SPACE = { w: JCARD_W, h: JCARD_H, unit: JCARD_UNIT } as const;

/** The three panels, in fold order: flap 0-320, spine 320-480, cover 480-1300. */
export const JCARD_PARTS = {
  flap: { x: 0, y: 0, w: 320, h: JCARD_H },
  spine: { x: 320, y: 0, w: 160, h: JCARD_H },
  cover: { x: 480, y: 0, w: 820, h: JCARD_H },
} as const;
export type JCardPart = keyof typeof JCARD_PARTS;

/**
 * The album card is square, so the editor marks the central 820x820 of the cover - x 480-1300,
 * y 230-1050 - as the region the card shows (docs/specs/k7art.md §4). A guide, not a clip.
 */
export const JCARD_SAFE = { x: 480, y: 230, w: 820, h: 820 } as const;

/**
 * The fields a J-card text may bind to: the label's eleven Slots and the J-card's own three.
 * A J-card text binds to the same vocabulary as the label's, so an album's case can print a
 * tempo or a key where the surface has one; `title`, `artist` and `meta` are the J-card's own
 * words — the album's name and byline, and the cassette line the App passes.
 */
export const JCARD_OWN_FIELDS: string[] = ["title", "artist", "meta"];
export const JCARD_SLOT_FIELDS: string[] = SLOTS.map((slot) => slot.id);
export const JCARD_FIELDS: string[] = [...JCARD_SLOT_FIELDS, ...JCARD_OWN_FIELDS];
/**
 * What the panel offers for a text: the label's Slots and `meta`. `title` and `artist` are
 * accepted — the factory designs and every design made before this — but no longer offered:
 * the Slot beside each is the same field with the label's name.
 */
export const JCARD_FIELD_CHOICES = [...JCARD_SLOT_FIELDS, "meta"];
/** The J-card's field names are a vocabulary, not a closed set: a design may name any. */
export type JCardField = string;
export const JCARD_FIELD_LABELS: Record<string, string> = Object.fromEntries([
  ...SLOTS.map((slot) => [slot.id, slot.label] as const),
  ["title", "Title"],
  ["artist", "Artist"],
  ["meta", "Meta"],
]);

/** The editor's simulations, which are the App's own default words (§4). */
export const JCARD_SIMS: Record<string, string> = Object.fromEntries([
  ...SLOTS.map((slot) => [slot.id, slot.sim] as const),
  ["title", "Neon Skyline"],
  ["artist", "The Analog Sons"],
  ["meta", "Side A · Side B"],
]);

/** The three test values a document carries, defaulted for a section stored without them. */
export function jcardSims(jcard: JCardDoc | null | undefined): Record<string, string> {
  const out: Record<string, string> = { ...JCARD_SIMS };
  const stored = jcard && jcard.sims;
  if (stored && typeof stored === "object") {
    for (const field of JCARD_FIELDS) {
      if (typeof stored[field] === "string") out[field] = stored[field];
    }
  }
  return out;
}

/** What a J-card field previews: the editor's value, then the default. */
export const jcardSim = (jcard: JCardDoc, field: JCardField): string =>
  jcardSims(jcard)[field] ?? JCARD_SIMS[field];

/** The faces the J-card may wear: the container's three, one per type role. */
export const JCARD_FACES: string[] = FROLES.map((role) => VOCAB.defaults.fonts[role].ff);
/** The J-card's default sizes in units: 0.175 in, 0.425 in and 0.097 in on the sheet. */
export const JCARD_SIZES: Record<FontRole, number> = { title: 56, numeral: 136, caption: 31 };
export const JCARD_SIZE_MIN = 4;
export const JCARD_SIZE_MAX = 640;
export const JCARD_PRIM_MAX = 64;
export const JCARD_BYTES_MAX = 16 * 1024;
export const JCARD_BOUND = 4096;

/** The section as it is written to a K7Art: the file's key order and its records. */
export interface JCardFile {
  space: { w: number; h: number; unit: number };
  parts: Record<JCardPart, { x: number; y: number; w: number; h: number }>;
  /** The sheet's own colour; the label's paper is its own and does not move with this. */
  paper: string;
  fonts: Record<FontRole, { ff: string; size: number }>;
  prims: Array<Record<string, unknown>>;
}

const isObject = (v: unknown): v is Record<string, unknown> =>
  typeof v === "object" && v !== null && !Array.isArray(v);

const isNum = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);

const isInt = (v: unknown): v is number => Number.isInteger(v);

/** The J-card's fonts: the label's own three faces at the J-card's default sizes. */
export function jcardFonts(tpl: Tpl): JCardDoc["fonts"] {
  const fonts = {} as JCardDoc["fonts"];
  for (const role of FROLES) {
    const face = tpl.fonts?.[role]?.ff;
    fonts[role] = {
      ff: JCARD_FACES.includes(face) ? face : VOCAB.defaults.fonts[role].ff,
      size: JCARD_SIZES[role],
    };
  }
  return fonts;
}

/**
 * The J-card a design starts with, and the one a document or a retired label template is
 * migrated with: the paper, and the three fields at the defaults (§8). The fold guides are
 * the editor's, not prims. The paper is the sheet's own colour: it starts at the label's
 * paper, and thereafter the two move independently.
 */
export function starterJCard(tpl: Tpl): JCardDoc {
  const texts: Array<[JCardField, FontRole, number]> = [
    ["title", "numeral", 900],
    ["artist", "title", 1060],
    ["meta", "caption", 1120],
  ];
  return {
    paper: tpl.inks.paper,
    fonts: jcardFonts(tpl),
    prims: [
      { id: 1, type: "rect", name: "", x: 0, y: 0, w: JCARD_W, h: JCARD_H, role: "paper" },
      ...texts.map(([field, fr, y], i) => ({
        id: i + 2,
        type: "text" as const,
        name: field,
        x: 540,
        y,
        w: 700,
        fr,
        role: "ink",
        align: "left" as const,
        sim: JCARD_SIMS[field],
      })),
    ],
    sims: { ...JCARD_SIMS },
    nid: texts.length + 1,
  };
}

/** A J-card layer's name in a refusal: its field, its static string, or its place in the stack. */
function tagOf(o: JObj, i: number): string {
  if (o.type !== "text") return `jcard layer ${i + 1} (${o.type})`;
  if (o.static) return `jcard text "${(o.text || "").slice(0, 24)}"`;
  return o.name ? `jcard text (field "${o.name}")` : `jcard text ${i + 1}`;
}

/**
 * The `jcard` section from the document. It refuses what docs/specs/k7art.md §7 refuses, so a
 * design the App cannot draw says so at Export rather than at the App's import.
 */
export function buildJCard(jcard: JCardDoc, tpl?: Tpl): { doc: JCardFile; errs: string[] } {
  const errs: string[] = [];

  const fonts = {} as JCardFile["fonts"];
  for (const role of FROLES) {
    const f = jcard.fonts?.[role] || ({} as { ff?: string; size?: number });
    const ff = typeof f.ff === "string" ? f.ff : "";
    const size = f.size;
    if (!JCARD_FACES.includes(ff))
      errs.push(
        `jcard: font role ${role} is "${ff || "(none)"}", and the J-card wears ${JCARD_FACES.join(", ")}`,
      );
    if (!isInt(size) || size < JCARD_SIZE_MIN || size > JCARD_SIZE_MAX)
      errs.push(
        `jcard: font role ${role} size ${size ?? "(none)"} is outside ${JCARD_SIZE_MIN}..${JCARD_SIZE_MAX} units`,
      );
    fonts[role] = {
      ff: JCARD_FACES.includes(ff) ? ff : JCARD_FACES[0],
      size:
        isInt(size) && size >= JCARD_SIZE_MIN && size <= JCARD_SIZE_MAX
          ? size
          : JCARD_SIZES[role],
    };
  }

  /* A hidden layer is in the document and in the panel, but not on the sheet: the App draws
   * what the editor shows, so the file carries the visible prims only. */
  const list = (Array.isArray(jcard.prims) ? jcard.prims : []).filter((o) => !o.hidden);
  if (list.length > JCARD_PRIM_MAX)
    errs.push(
      `jcard: ${list.length} prims, and the App draws ${JCARD_PRIM_MAX} on a sheet`,
    );

  const prims: Array<Record<string, unknown>> = [];
  list.forEach((o, i) => {
    const tag = tagOf(o, i);
    const X = Math.round(o.x);
    const Y = Math.round(o.y);
    const W = Math.round(o.w);
    const inSheet = (x: number, y: number, w: number, h: number) =>
      x >= -JCARD_BOUND &&
      y >= -JCARD_BOUND &&
      x + w <= JCARD_BOUND &&
      y + h <= JCARD_BOUND;
    if (!isNum(o.x) || !isNum(o.y) || !isNum(o.w) || W < 1)
      errs.push(`${tag}: is ${o.w}x${o.h ?? 0} at ${o.x},${o.y}, and a prim needs a position and a width`);
    else if (!inSheet(X, Y, W, o.type === "text" ? 0 : Math.round(o.h || 0)))
      errs.push(`${tag}: its box leaves the sheet's +/-${JCARD_BOUND} units`);

    if (o.type === "text") {
      const role = o.role ?? "";
      if (!TEXT_ROLES.includes(role))
        errs.push(
          `${tag}: colour ${role ? `"${role}"` : "(none)"} is not ink or one of the eight bands; the paper has nothing to read against it`,
        );
      if (!FROLES.includes(o.fr as FontRole))
        errs.push(`${tag}: font role must be title, numeral or caption`);
      const pr: Record<string, unknown> = o.static
        ? { t: "text", x: X, y: Y, w: W }
        : { t: "text", slot: o.name || "", x: X, y: Y, w: W };
      if (o.rot !== undefined) pr.rot = Math.round(o.rot);
      pr.font = FROLES.includes(o.fr as FontRole) ? o.fr : "caption";
      pr.color = TEXT_ROLES.includes(role) ? role : "ink";
      pr.align = o.align === "center" || o.align === "right" ? o.align : "left";
      if (o.static) {
        if (!(o.text || "").trim()) errs.push(`${tag}: static text is empty`);
        pr.text = o.text || "";
      } else {
        if (!JCARD_FIELDS.includes(o.name as JCardField))
          errs.push(
            `${tag}: "${o.name || ""}" is not a J-card field (${JCARD_FIELDS.join(", ")})`,
          );
        // A dynamic text always carries a preview: the layer's own test value, the Fields
        // panel's value for the field, or the App's default word.
        const field = o.name as JCardField;
        /* A label Slot's test value is the label's — one field, one test value — so the
         * preview for one comes from the container when the caller has it. */
        const slotSim = JCARD_SLOT_FIELDS.includes(field)
          ? (tpl?.sim as Record<string, string> | undefined)?.[field]
          : undefined;
        pr.preview =
          slotSim !== undefined && slotSim !== ""
            ? slotSim
            : JCARD_FIELDS.includes(field)
              ? jcardSim(jcard, field)
              : "";
      }
      prims.push(pr);
      return;
    }

    const role = o.role ?? "";
    if (role.startsWith("#"))
      errs.push(`${tag}: fill ${role} is a literal colour; the J-card is role-coloured`);
    else if (!ROLES.includes(role))
      errs.push(`${tag}: fill ${role ? `"${role}"` : "(none)"} is not one of the container's inks`);
    const pr: Record<string, unknown> = { t: "rect", x: X, y: Y, w: W, h: Math.round(o.h || 0) };
    if (o.type === "circle") pr.r = Math.floor(Math.min(W, Math.round(o.h || 0)) / 2);
    else if (o.r !== undefined) pr.r = Math.round(o.r);
    if (o.rot !== undefined) pr.rot = Math.round(o.rot);
    pr.fill = ROLES.includes(role) ? role : "paper";
    prims.push(pr);
  });

  const doc: JCardFile = {
    space: { ...JCARD_SPACE },
    parts: {
      flap: { ...JCARD_PARTS.flap },
      spine: { ...JCARD_PARTS.spine },
      cover: { ...JCARD_PARTS.cover },
    },
    paper: jcard.paper ?? DEFAULT_PAPER,
    fonts,
    prims,
  };
  // The App bundles the J-cards, so the section has a byte cap of its own (k7art.md §7).
  const bytes = new TextEncoder().encode(JSON.stringify(doc)).length;
  if (bytes > JCARD_BYTES_MAX)
    errs.push(
      `jcard: the section is ${bytes} B, over the ${JCARD_BYTES_MAX} the App bundles`,
    );
  return { doc, errs };
}

/** A J-card role that is one of the container's, or a paper a text cannot read against. */
function readRole(
  role: unknown,
  allowPaper: boolean,
  bandCount: number,
  what: string,
): string {
  if (typeof role !== "string")
    throw new Error(`jcard: ${what} ${role === undefined ? "is missing" : "is not a role"}`);
  if (role.startsWith("#"))
    throw new Error(
      `jcard: ${what} ${role} is a literal colour; the J-card is role-coloured`,
    );
  if (!ROLES.includes(role))
    throw new Error(`jcard: ${what} "${role}" is not one of the container's inks`);
  if (role === "paper" && !allowPaper)
    throw new Error(
      `jcard: ${what} paper has nothing to read against it`,
    );
  if (/^band\.[0-7]$/.test(role) && +role.slice(5) >= bandCount)
    throw new Error(`jcard: ${what} ${role} is past this design's ${bandCount} bands`);
  return role;
}

function readPart(raw: unknown, part: JCardPart): void {
  const want = JCARD_PARTS[part];
  const got = isObject(raw)
    ? [raw.x, raw.y, raw.w, raw.h]
    : [undefined, undefined, undefined, undefined];
  if (
    got[0] !== want.x ||
    got[1] !== want.y ||
    got[2] !== want.w ||
    got[3] !== want.h
  )
    throw new Error(
      `jcard: part "${part}" is ${JSON.stringify(got)}, not the case's ${want.x},${want.y} ${want.w}x${want.h}`,
    );
}

/**
 * Read a K7Art's `jcard` section into the editor's document. It throws what the packer and
 * the App would refuse, so Open is as strict as Export (docs/specs/k7art.md §7).
 */
export function importJCard(raw: unknown, tpl: Tpl): JCardDoc {
  if (!isObject(raw)) throw new Error("jcard is missing -- the design is the pair");

  const space = raw.space;
  const got = isObject(space) ? [space.w, space.h, space.unit] : undefined;
  if (
    !got ||
    got[0] !== JCARD_SPACE.w ||
    got[1] !== JCARD_SPACE.h ||
    got[2] !== JCARD_SPACE.unit
  )
    throw new Error(
      `jcard: space is ${got ? `${got[0]}x${got[1]} at ${got[2]} units per inch` : "missing"}, not ${JCARD_SPACE.w}x${JCARD_SPACE.h} at ${JCARD_SPACE.unit} units per inch`,
    );

  const parts = raw.parts;
  if (!isObject(parts)) throw new Error("jcard: parts is missing -- the fold lines are the case's");
  for (const part of Object.keys(JCARD_PARTS) as JCardPart[]) readPart(parts[part], part);
  const extra = Object.keys(parts).filter((k) => !(k in JCARD_PARTS));
  if (extra.length)
    throw new Error(
      `jcard: parts names ${extra.join(", ")}; the case is flap, spine and cover`,
    );

  const rawFonts = raw.fonts;
  if (!isObject(rawFonts)) throw new Error("jcard: fonts is missing -- the three faces are the pair's");
  const fonts = {} as JCardDoc["fonts"];
  for (const role of FROLES) {
    const f = rawFonts[role];
    if (!isObject(f))
      throw new Error(`jcard: font role ${role} is missing`);
    if (!JCARD_FACES.includes(f.ff as string))
      throw new Error(
        `jcard: font role ${role} is ${JSON.stringify(f.ff)}, not one of ${JCARD_FACES.join(", ")}`,
      );
    if (!isInt(f.size) || f.size < JCARD_SIZE_MIN || f.size > JCARD_SIZE_MAX)
      throw new Error(
        `jcard: font role ${role} size ${JSON.stringify(f.size)} is outside ${JCARD_SIZE_MIN}..${JCARD_SIZE_MAX} units`,
      );
    fonts[role] = { ff: f.ff as string, size: f.size };
  }

  const prims = raw.prims;
  if (!Array.isArray(prims)) throw new Error("jcard: prims is not a list");
  if (prims.length > JCARD_PRIM_MAX)
    throw new Error(
      `jcard: ${prims.length} prims, more than the ${JCARD_PRIM_MAX} the App draws`,
    );

  const bandCount = Array.isArray(tpl.inks.band) ? tpl.inks.band.length : 0;
  const out: JObj[] = [];
  const sims: Record<string, string> = { ...JCARD_SIMS };
  prims.forEach((prim, i) => {
    if (!isObject(prim)) throw new Error(`jcard prim ${i + 1}: not an object`);
    const kind = prim.t;
    if (kind !== "rect" && kind !== "text")
      throw new Error(`jcard prim ${i + 1}: t must be rect or text`);

    const x = prim.x;
    const y = prim.y;
    const w = prim.w;
    if (!isNum(x) || !isNum(y) || !isNum(w))
      throw new Error(`jcard prim ${i + 1}: x, y and w must be numbers`);
    if (w < 1 || w > JCARD_BOUND)
      throw new Error(`jcard prim ${i + 1}: w ${w} is outside 1..${JCARD_BOUND}`);
    let H = 0;
    if (kind === "rect") {
      if (!isNum(prim.h) || prim.h < 1 || prim.h > JCARD_BOUND)
        throw new Error(
          `jcard prim ${i + 1}: h ${JSON.stringify(prim.h)} is outside 1..${JCARD_BOUND}`,
        );
      H = prim.h;
    }
    if (
      x < -JCARD_BOUND ||
      y < -JCARD_BOUND ||
      x + w > JCARD_BOUND ||
      y + H > JCARD_BOUND
    )
      throw new Error(
        `jcard prim ${i + 1}: the box leaves the sheet's +/-${JCARD_BOUND} units`,
      );
    if (prim.rot !== undefined && !isNum(prim.rot))
      throw new Error(`jcard prim ${i + 1}: rot must be a number`);

    const o: JObj = { id: i + 1, type: kind, name: "", x, y, w };
    if (kind === "rect") {
      o.h = H;
      if (prim.r !== undefined) {
        if (!isNum(prim.r) || prim.r < 0)
          throw new Error(`jcard prim ${i + 1}: r must be zero or more units`);
        o.r = prim.r;
      }
      o.role = readRole(prim.fill, true, bandCount, `prim ${i + 1}: fill`);
    } else {
      const font = prim.font;
      if (!FROLES.includes(font as FontRole))
        throw new Error(
          `jcard prim ${i + 1}: font ${JSON.stringify(font)} is not title, numeral or caption`,
        );
      o.fr = font as FontRole;
      const align = prim.align ?? "left";
      if (align !== "left" && align !== "center" && align !== "right")
        throw new Error(
          `jcard prim ${i + 1}: align ${JSON.stringify(align)} is not left, center or right`,
        );
      o.align = align as JObj["align"];
      o.role = readRole(prim.color ?? "ink", false, bandCount, `prim ${i + 1}: color`);
      if ("slot" in prim) {
        const field = prim.slot;
        if (!JCARD_FIELDS.includes(field as JCardField))
          throw new Error(
            `jcard prim ${i + 1}: ${JSON.stringify(field)} is not a J-card field (${JCARD_FIELDS.join(", ")})`,
          );
        o.name = field as string;
        if (prim.preview !== undefined) {
          if (typeof prim.preview !== "string")
            throw new Error(`jcard prim ${i + 1}: preview is not a string`);
          o.sim = prim.preview;
          sims[field as string] = prim.preview;
        }
      } else if ("text" in prim) {
        if (typeof prim.text !== "string" || !prim.text.trim())
          throw new Error(`jcard prim ${i + 1}: a static text is empty`);
        o.static = true;
        o.text = prim.text;
      } else {
        throw new Error(`jcard prim ${i + 1}: a text needs a slot or a text`);
      }
    }
    if (prim.rot !== undefined) o.rot = prim.rot;
    out.push(o);
  });

  // The cap is on the section's UTF-8 bytes, exactly as the App's bundle counts them.
  const bytes = new TextEncoder().encode(JSON.stringify(raw)).length;
  if (bytes > JCARD_BYTES_MAX)
    throw new Error(
      `jcard: the section is ${bytes} B, over the ${JCARD_BYTES_MAX} the App bundles`,
    );

  return {
    paper: typeof raw.paper === "string" ? raw.paper : tpl.inks.paper,
    fonts,
    prims: out,
    nid: Math.max(0, ...out.map((o) => o.id)),
    sims,
  };
}

/**
 * A stored document's J-card: a document saved before the J-card existed gets the starter
 * from its own label's paper and faces (docs/specs/k7art.md §8). A damaged section is
 * repaired rather than losing the label with it.
 */
export function migrateJCard(
  jcard: JCardDoc | null | undefined,
  tpl: Tpl,
): JCardDoc {
  const starter = starterJCard(tpl);
  if (!isObject(jcard)) return starter;
  const doc = jcard as unknown as Record<string, unknown>;
  const paper = typeof doc.paper === "string" ? doc.paper : starter.paper;

  const fonts = {} as JCardDoc["fonts"];
  const rawFonts = (isObject(doc.fonts) ? doc.fonts : {}) as Record<string, unknown>;
  for (const role of FROLES) {
    const f = isObject(rawFonts[role]) ? (rawFonts[role] as Record<string, unknown>) : {};
    fonts[role] = {
      ff:
        typeof f.ff === "string" && JCARD_FACES.includes(f.ff)
          ? f.ff
          : starter.fonts[role].ff,
      size:
        isInt(f.size) && f.size >= JCARD_SIZE_MIN && f.size <= JCARD_SIZE_MAX
          ? f.size
          : JCARD_SIZES[role],
    };
  }

  const raw = Array.isArray(doc.prims) ? doc.prims : [];
  const prims: JObj[] = [];
  raw.forEach((p, i) => {
    if (!isObject(p)) return;
    prims.push({ ...(p as unknown as JObj), id: isInt(p.id) ? p.id : i + 1 });
  });
  // A document stored before the Fields panel carried its own values keeps each field's
  // first text preview; the starter's own words fill the rest.
  const sims = jcardSims(doc as unknown as JCardDoc);
  if (!isObject(doc.sims)) {
    prims.forEach((o) => {
      if (!o.static && o.name && o.sim !== undefined && o.sim !== "") sims[o.name] = o.sim;
    });
  }
  if (!prims.length) return { ...starter, paper, fonts, sims };
  return { paper, fonts, prims, sims, nid: Math.max(0, ...prims.map((o) => o.id)) };
}

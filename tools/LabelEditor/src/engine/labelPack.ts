import { VOCAB } from "./vocab.generated";
import { FROLES } from "./vocab";
import { JCARD_FIELDS } from "./jcard";
import { crc32 } from "./crc";
import type { K7ArtDoc } from "./tpl";

/**
 * The browser packer (docs/specs/art-packs.md §3): a TypeScript port of
 * `tools/label_pack.py` for the label sheet, and the `.k7artpack` header around it.
 *
 * The sheet's bytes are the contract: `labelPack.test.ts` shells the Python packer over
 * every `assets/k7art/*.json` and compares the two outputs byte for byte. Keep this file
 * and `tools/label_pack.py` in step; the Python packer stays the build's, this one is the
 * editor's.
 */

export const SHEET_MAGIC = new Uint8Array([0x4b, 0x37, 0x4c, 0x42, 0x4c, 0x00, 0x00, 0x00]); // K7LBL\0\0\0
export const PACK_MAGIC = new Uint8Array([0x4b, 0x37, 0x41, 0x52, 0x54, 0x00, 0x00, 0x00]); // K7ART\0\0\0
export const SHEET_VERSION = 5;
export const SHEET_HEADER_SIZE = 32;
export const TEMPLATE_SIZE = 16;
export const PRIM_SIZE = 24;
export const SLOT_SIZE = 8;
export const CANVAS_MAX = VOCAB.canvas;
export const TEMPLATE_MAX = VOCAB.limits.templates;
export const PACK_VERSION = 1;
export const PACK_HEADER_SIZE = 128;
export const PACK_TOTAL_MAX = 1024 * 1024;
export const JCARD_BYTES_MAX = 16 * 1024;
export const PACK_ID_MAX = 16;
export const PACK_NAME_MAX = 32;
export const PACK_AUTHOR_MAX = 32;

const ALIGN_INDEX: Record<string, number> = { left: 0, center: 1, right: 2 };
const SLOT_NAMES = new Set<string>(VOCAB.slots.map((s) => s.id));
const HEX = /^#[0-9A-Fa-f]{6}$/;
const ID = /^k7\.[a-z0-9][a-z0-9-]*$/;

/** A font table record: `assets/labels/vocabulary.json`'s `fonts`. */
export interface PackFont {
  name: string;
  px: number;
  symbol?: string;
}

/** A K7Art as the packer reads it: the editor's file, or a parsed `assets/k7art/*.json`. */
export type PackDesign = K7ArtDoc | Record<string, unknown>;

/** The packer's refusals, all at once and by name. */
export class PackError extends Error {
  errors: string[];
  constructor(errors: string[]) {
    super(errors.join("\n"));
    this.name = "PackError";
    this.errors = errors;
  }
}

const isObject = (v: unknown): v is Record<string, unknown> =>
  typeof v === "object" && v !== null && !Array.isArray(v);
const isInt = (v: unknown): v is number => typeof v === "number" && Number.isInteger(v);

function hex(value: unknown, what: string, errors: string[], allowNone = false): number | null {
  if (value === undefined || value === null) {
    if (allowNone) return null;
    errors.push(`${what}: ${JSON.stringify(value)} is not #RRGGBB`);
    return 0;
  }
  if (typeof value !== "string" || !HEX.test(value)) {
    errors.push(`${what}: ${JSON.stringify(value)} is not #RRGGBB`);
    return 0;
  }
  return parseInt(value.slice(1), 16);
}

function int(
  value: unknown,
  what: string,
  errors: string[],
  lo?: number,
  hi?: number,
  def?: number,
): number {
  if (value === undefined && def !== undefined) return def;
  if (!isInt(value)) {
    errors.push(`${what}: ${JSON.stringify(value)} is not an integer`);
    return 0;
  }
  if ((lo !== undefined && value < lo) || (hi !== undefined && value > hi)) {
    errors.push(`${what}: ${value} is outside ${lo}..${hi}`);
    return 0;
  }
  return value;
}

/** The sheet's interned string pool: every distinct string once, NUL-terminated. */
class Pool {
  private bytes: number[] = [];
  private at = new Map<string, number>();
  add(text: string): number {
    const known = this.at.get(text);
    if (known !== undefined) return known;
    const offset = this.bytes.length;
    this.at.set(text, offset);
    for (const b of new TextEncoder().encode(text)) this.bytes.push(b);
    this.bytes.push(0);
    return offset;
  }
  data(): Uint8Array {
    return Uint8Array.from(this.bytes);
  }
  get size(): number {
    return this.bytes.length;
  }
}

/**
 * The J-card's own refusals, ported from `jcard_errors` in tools/label_pack.py. The packer
 * never puts a J-card in the sheet; it refuses one the App could not draw.
 */
function jcardErrors(jcard: Record<string, unknown>, did: string): string[] {
  const errors: string[] = [];
  const add = (line: string) => errors.push(`${did}: jcard ${line}`);

  const space = jcard.space;
  const got = isObject(space) ? [space.w, space.h, space.unit] : undefined;
  if (!got || got[0] !== 1300 || got[1] !== 1280 || got[2] !== 320)
    add(`space is ${got ? `${got[0]}x${got[1]} at ${got[2]} units per inch` : "missing"}, not 1300x1280 at 320 units per inch`);

  const want: Record<string, [number, number, number, number]> = {
    flap: [0, 0, 320, 1280],
    spine: [320, 0, 160, 1280],
    cover: [480, 0, 820, 1280],
  };
  const parts = jcard.parts;
  if (!isObject(parts)) add("parts is missing -- the fold lines are the case's");
  else {
    for (const [part, rect] of Object.entries(want)) {
      const p = parts[part];
      const gotRect = isObject(p) ? [p.x, p.y, p.w, p.h] : undefined;
      if (!gotRect || gotRect.some((v, i) => v !== rect[i]))
        add(`part "${part}" is ${JSON.stringify(gotRect)}, not ${JSON.stringify(rect)}`);
    }
    const extra = Object.keys(parts).filter((k) => !(k in want));
    if (extra.length) add(`parts names ${extra.join(", ")}; the case is flap, spine and cover`);
  }

  const background = jcard.background;
  if (typeof background !== "string" || !HEX.test(background))
    add(`background ${JSON.stringify(background)} is not #RRGGBB`);

  const faces: string[] = FROLES.map((role) => VOCAB.defaults.fonts[role].ff);
  const fonts = jcard.fonts;
  if (!isObject(fonts)) add("fonts is missing -- the three faces are the pair's");
  else {
    for (const role of FROLES) {
      const f = isObject(fonts[role]) ? (fonts[role] as Record<string, unknown>) : {};
      if (typeof f.ff !== "string" || !faces.includes(f.ff))
        add(`font role ${role} is ${JSON.stringify(f.ff)}, not one of ${faces.join(", ")}`);
      if (!isInt(f.size) || f.size < 4 || f.size > 640)
        add(`font role ${role} size ${JSON.stringify(f.size)} is outside 4..640 units`);
    }
  }

  const prims = jcard.prims;
  const list = Array.isArray(prims) ? prims : [];
  if (!Array.isArray(prims)) add("prims is not a list");
  if (list.length > 64) add(`${list.length} prims, more than the 64 the App draws`);

  list.forEach((prim, i) => {
    if (!isObject(prim)) {
      add(`prim ${i + 1}: not an object`);
      return;
    }
    const kind = prim.t;
    if (kind !== "rect" && kind !== "text") {
      add(`prim ${i + 1}: t must be rect or text`);
      return;
    }
    const x = int(prim.x, `${did}: jcard prim ${i + 1}: x`, errors, -4096, 4096);
    const y = int(prim.y, `${did}: jcard prim ${i + 1}: y`, errors, -4096, 4096);
    const w = int(prim.w, `${did}: jcard prim ${i + 1}: w`, errors, 1, 4096);
    int(prim.rot ?? 0, `${did}: jcard prim ${i + 1}: rot`, errors, undefined, undefined, 0);
    const colourOf = (what: "fill" | "color") => {
      const colour = what === "fill" ? prim.fill : prim.color;
      if (typeof colour !== "string" || !HEX.test(colour))
        add(`prim ${i + 1}: ${what} ${JSON.stringify(colour)} is not #RRGGBB`);
    };
    let h = 0;
    if (kind === "rect") {
      h = int(prim.h, `${did}: jcard prim ${i + 1}: h`, errors, 1, 4096);
      int(prim.r ?? 0, `${did}: jcard prim ${i + 1}: r`, errors, 0, undefined, 0);
      colourOf("fill");
    } else {
      if (prim.align !== undefined && prim.align !== "left" && prim.align !== "center" && prim.align !== "right")
        add(`prim ${i + 1}: align ${JSON.stringify(prim.align)} is not left, center or right`);
      if (typeof prim.font !== "string" || !FROLES.includes(prim.font as never))
        add(`prim ${i + 1}: font ${JSON.stringify(prim.font)} is not title, numeral or caption`);
      colourOf("color");
      if ("slot" in prim) {
        if (!JCARD_FIELDS.includes(String(prim.slot)))
          add(`prim ${i + 1}: "${String(prim.slot)}" is not a J-card field (a Slot, title, artist or meta)`);
        if ("preview" in prim && typeof prim.preview !== "string")
          add(`prim ${i + 1}: preview is not a string`);
      } else if ("text" in prim) {
        if (typeof prim.text !== "string" || !prim.text.trim())
          add(`prim ${i + 1}: a static text is empty`);
      } else add(`prim ${i + 1}: a text needs a slot or a text`);
    }
    if (!(x >= -4096 && x + w <= 4096 && y >= -4096 && y + h <= 4096))
      add(`prim ${i + 1}: the box leaves the sheet's +/-4096 units`);
  });

  const size = new TextEncoder().encode(JSON.stringify(jcard)).length;
  if (size > JCARD_BYTES_MAX) add(`the section is ${size} B, over the ${JCARD_BYTES_MAX} the App bundles`);
  return errors;
}

interface PackedDesign {
  name: string;
  nameOff: number;
  primStart: number;
  primCount: number;
  slotStart: number;
  slotCount: number;
  font: number[];
  canvases: number[];
}

const tagOf = (prim: Record<string, unknown>, i: number): string => {
  if (typeof prim?.slot === "string" && prim.slot) return `slot "${prim.slot}"`;
  if (typeof prim?.text === "string" && prim.text) return `static text "${prim.text.slice(0, 24)}"`;
  return `prim ${i}`;
};

/**
 * One K7Art -> its template record, prims and slots. Raises PackError; the pool is shared,
 * so its string order is the packer's: each prim's text, then the sorted slots, then the
 * design's name.
 */
function packDesign(
  design: Record<string, unknown>,
  fontIndex: Map<string, number>,
  pool: Pool,
): { fields: Omit<PackedDesign, "primStart" | "slotStart">; prims: Uint8Array[]; slots: Uint8Array[] } {
  const errors: string[] = [];
  const did = typeof design.id === "string" ? design.id : String(design.id);

  if (design.format !== "k7art") errors.push(`${did}: format is ${JSON.stringify(design.format)}, not "k7art"`);
  if (design.v !== 2)
    errors.push(`${did}: v is not 2 -- open a v1 design in the editor and export it`);

  let designName = typeof design.name === "string" ? design.name : "";
  if (!designName.trim()) {
    errors.push(`${did}: no name -- the sheet's template is named for the log`);
    designName = did;
  }

  const label = isObject(design.label) ? design.label : {};
  if (!isObject(design.label)) errors.push(`${did}: no label -- the design is the pair`);

  if (!isObject(design.jcard)) errors.push(`${did}: no jcard -- the design is the pair`);
  else errors.push(...jcardErrors(design.jcard, did));

  const space = isObject(label.space) ? label.space : {};
  if (space.w !== VOCAB.space.w || space.h !== VOCAB.space.h)
    errors.push(`${did}: space is ${space.w}x${space.h}, not ${VOCAB.space.w}x${VOCAB.space.h}`);

  let maskColour: number | null = null;
  let maskRadius = 0;
  const mask = label.mask;
  if (mask !== undefined) {
    if (!isObject(mask)) errors.push(`${did}: mask must be an object with color and r`);
    else {
      maskColour = hex(mask.color, `${did}: mask.color`, errors);
      maskRadius = int(mask.r ?? 0, `${did}: mask.r`, errors, 0, undefined, 0);
    }
  }

  const aperture = label.aperture;
  if (aperture !== undefined) {
    const a = isObject(aperture) ? aperture : {};
    const got = [a.x, a.y, a.w, a.h, a.r ?? 0];
    const want = [VOCAB.aperture.x, VOCAB.aperture.y, VOCAB.aperture.w, VOCAB.aperture.h, VOCAB.aperture.r];
    if (got.some((v, i) => v !== want[i]))
      errors.push(`${did}: aperture ${JSON.stringify(got)} is not the Card's window ${JSON.stringify(want)}`);
    if (mask === undefined)
      errors.push(`${did}: aperture without a mask -- the window is cut out of the paper`);
  }

  const fonts = isObject(label.fonts) ? label.fonts : {};
  if (!isObject(label.fonts))
    errors.push(`${did}: no fonts -- a design without faces is type with no glyphs`);
  const font = FROLES.map((role) => {
    const f = isObject(fonts[role]) ? (fonts[role] as Record<string, unknown>) : {};
    const key = `${String(f.ff)}\u0000${String(f.px)}`;
    const found = fontIndex.get(key);
    if (found === undefined) {
      errors.push(
        `${did}: font role ${role} is ${JSON.stringify(f.ff)} ${JSON.stringify(f.px)}, which assets/labels/vocabulary.json does not carry`,
      );
      return 0;
    }
    return found;
  });

  let canvases: number[] = [];
  if (label.canvas !== undefined) {
    if (!isObject(label.canvas))
      errors.push(`${did}: canvas must be an object with backgrounds and active`);
    else {
      const rawBg = label.canvas.backgrounds;
      const bg: unknown[] = Array.isArray(rawBg) ? rawBg : [];
      if (!Array.isArray(rawBg) || rawBg.length < 1 || rawBg.length > CANVAS_MAX) {
        errors.push(`${did}: canvas.backgrounds must be one to ${CANVAS_MAX} colours`);
      }
      canvases = bg.map((c, i) => hex(c, `${did}: canvas.backgrounds[${i}]`, errors) ?? 0);
      const active = label.canvas.active;
      if (!isInt(active) || active < 0 || active >= canvases.length)
        errors.push(`${did}: canvas.active ${JSON.stringify(active)} is not an index into backgrounds`);
    }
  }

  const slots = isObject(label.slots) ? label.slots : {};
  if (!isObject(label.slots)) errors.push(`${did}: slots must be an object`);
  if (Object.keys(slots).length > VOCAB.limits.slots)
    errors.push(`${did}: ${Object.keys(slots).length} slots, more than the ${VOCAB.limits.slots} allowed`);
  const slotMax = new Map<string, number>();
  for (const [name, spec] of Object.entries(slots)) {
    if (!SLOT_NAMES.has(name)) {
      errors.push(`${did}: slot "${name}" is not in the vocabulary`);
      continue;
    }
    const max = isInt(isObject(spec) ? spec.max : undefined)
      ? ((spec as Record<string, unknown>).max as number)
      : 0;
    if (max < 1 || max > VOCAB.value) {
      errors.push(`${did}: slot "${name}".max ${JSON.stringify(isObject(spec) ? spec.max : undefined)} is not 1..${VOCAB.value}`);
      continue;
    }
    slotMax.set(name, max);
  }

  let prims = Array.isArray(label.prims) ? label.prims : [];
  if (!Array.isArray(label.prims) || !label.prims.length) errors.push(`${did}: no prims`);
  const sheetPrims = prims.length + (mask !== undefined ? 1 : 0);
  if (sheetPrims > VOCAB.limits.prims)
    errors.push(`${did}: ${sheetPrims} prims (the mask's rect included), more than the ${VOCAB.limits.prims} allowed`);

  const outPrims: Uint8Array[] = [];
  if (mask !== undefined && maskColour !== null)
    outPrims.push(primRecord(0, 0, 0, 0, 0, VOCAB.space.w, VOCAB.space.h, maskRadius, 0, 0xffffffff, maskColour));
  const usedSlots = new Set<string>();

  prims.forEach((raw, i) => {
    const prim = isObject(raw) ? raw : {};
    const tag = `${did}: ${tagOf(prim, i + 1)}`;
    const kind = prim.t;
    const x = int(prim.x, `${tag}: x`, errors, VOCAB.coords.x[0], VOCAB.coords.x[1]);
    const y = int(prim.y, `${tag}: y`, errors, VOCAB.coords.y[0], VOCAB.coords.y[1]);
    const w = int(prim.w, `${tag}: w`, errors, VOCAB.coords.w[0], VOCAB.coords.w[1]);
    const rot = int(prim.rot ?? 0, `${tag}: rot`, errors, -360, 360, 0);
    if (kind === "rect") {
      const h = int(prim.h, `${tag}: h`, errors, VOCAB.coords.h[0], VOCAB.coords.h[1]);
      const r = int(prim.r ?? 0, `${tag}: r`, errors, 0, undefined, 0);
      const colour = hex(prim.fill, `${tag}: fill`, errors) ?? 0;
      outPrims.push(
        primRecord(0, 0, 0, x, y, w, h, r, rot, 0xffffffff, colour),
      );
      return;
    }
    if (kind !== "text") {
      errors.push(`${tag}: t must be rect or text`);
      return;
    }
    const colour = hex(prim.color, `${tag}: colour`, errors) ?? 0;
    const role = prim.font;
    const fontIndexValue =
      typeof role === "string" ? FROLES.indexOf(role as (typeof FROLES)[number]) : -1;
    if (fontIndexValue < 0) errors.push(`${tag}: font ${JSON.stringify(role)} is not title, numeral or caption`);
    const alignValue = typeof prim.align === "string" ? ALIGN_INDEX[prim.align] : 0;
    if (prim.align !== undefined && alignValue === undefined)
      errors.push(`${tag}: align ${JSON.stringify(prim.align)} is not left, center or right`);

    let textOff: number;
    let kindIndex: number;
    if ("slot" in prim) {
      const name = String(prim.slot);
      if (!SLOT_NAMES.has(name)) errors.push(`${tag}: "${name}" is not a slot in the vocabulary`);
      else if (!slotMax.has(name)) errors.push(`${tag}: slot "${name}" is not declared`);
      else usedSlots.add(name);
      textOff = pool.add(name);
      kindIndex = 2;
    } else if ("text" in prim) {
      const text = typeof prim.text === "string" ? prim.text : "";
      if (!text.trim()) errors.push(`${tag}: a static text is empty`);
      textOff = pool.add(text);
      kindIndex = 1;
    } else {
      errors.push(`${tag}: a text needs a slot or a text`);
      return;
    }
    outPrims.push(
      primRecord(
        kindIndex,
        Math.max(fontIndexValue, 0),
        alignValue ?? 0,
        x,
        y,
        w,
        0,
        0,
        rot,
        textOff,
        colour,
      ),
    );
  });

  for (const name of slotMax.keys())
    if (!usedSlots.has(name)) errors.push(`${did}: slot "${name}" is declared but no text uses it`);

  if (errors.length) throw new PackError(errors);

  const slotNames = [...slotMax.keys()].sort();
  const slotsOut = slotNames.map((name) => {
    const rec = new DataView(new ArrayBuffer(SLOT_SIZE));
    rec.setUint32(0, pool.add(name), true);
    rec.setUint16(4, slotMax.get(name)!, true);
    return new Uint8Array(rec.buffer);
  });
  const nameOff = pool.add(designName);

  return {
    fields: {
      name: designName,
      nameOff,
      primCount: outPrims.length,
      slotCount: slotsOut.length,
      font,
      canvases,
    },
    prims: outPrims,
    slots: slotsOut,
  };
}

function primRecord(
  kind: number,
  font: number,
  align: number,
  x: number,
  y: number,
  w: number,
  h: number,
  r: number,
  rot: number,
  textOff: number,
  colour: number,
): Uint8Array {
  const dv = new DataView(new ArrayBuffer(PRIM_SIZE));
  dv.setUint8(0, kind);
  dv.setUint8(1, 0);
  dv.setUint8(2, font);
  dv.setUint8(3, align);
  dv.setInt16(4, x, true);
  dv.setInt16(6, y, true);
  dv.setUint16(8, w, true);
  dv.setUint16(10, h, true);
  dv.setUint16(12, r, true);
  dv.setInt16(14, (((Math.round(rot * 10) % 3600) + 3600) % 3600), true);
  dv.setUint32(16, textOff >>> 0, true);
  dv.setUint32(20, colour >>> 0, true);
  return new Uint8Array(dv.buffer);
}

const concat = (parts: Uint8Array[]): Uint8Array => {
  const total = parts.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(total);
  let at = 0;
  for (const p of parts) {
    out.set(p, at);
    at += p.length;
  }
  return out;
};

/** The sheet's font table, exactly what `label_pack.load_fonts` reads. */
export function packFonts(): PackFont[] {
  return VOCAB.fonts.map((f) => ({ name: f.name, px: f.px, symbol: "symbol" in f ? f.symbol : undefined }));
}

/**
 * The version-4 label sheet from a set of K7Arts (docs/specs/label-templates.md §3).
 * Throws PackError with every refusal, so Publish names the design before a pack exists.
 */
export function buildLabelSheet(
  designs: readonly PackDesign[],
  fonts: readonly PackFont[] = packFonts(),
): Uint8Array {
  const errors: string[] = [];
  if (!designs.length) throw new PackError(["no designs: there is nothing to pack"]);
  if (designs.length > TEMPLATE_MAX)
    throw new PackError([`${designs.length} designs, more than the ${TEMPLATE_MAX} a sheet holds`]);

  const fontIndex = new Map(fonts.map((f, i) => [`${f.name}\u0000${f.px}`, i] as const));
  const pool = new Pool();
  const ids = new Set<string>();
  const ordered = [...designs].sort((a, b) => {
    const ai = String((a as Record<string, unknown>).id);
    const bi = String((b as Record<string, unknown>).id);
    return ai < bi ? -1 : ai > bi ? 1 : 0;
  });
  for (const design of ordered) {
    const did = (design as Record<string, unknown>).id;
    if (typeof did !== "string" || !ID.test(did)) errors.push(`${did}: id must look like k7.my-name`);
    if (ids.has(String(did))) errors.push(`${did}: duplicate id`);
    ids.add(String(did));
  }

  const templates: PackedDesign[] = [];
  const prims: Uint8Array[] = [];
  const slots: Uint8Array[] = [];
  for (const design of ordered) {
    try {
      const packed = packDesign(design as Record<string, unknown>, fontIndex, pool);
      templates.push({
        ...packed.fields,
        primStart: prims.length,
        slotStart: slots.length,
      });
      prims.push(...packed.prims);
      slots.push(...packed.slots);
    } catch (err) {
      if (err instanceof PackError) errors.push(...err.errors);
      else throw err;
    }
  }
  if (errors.length) throw new PackError(errors);

  const poolBytes = pool.data();
  if (poolBytes.length > 0xffff)
    throw new PackError([`the string pool is ${poolBytes.length} B, over the 65535 a sheet holds`]);
  const total =
    SHEET_HEADER_SIZE +
    TEMPLATE_SIZE * templates.length +
    CANVAS_MAX * 4 * templates.length +
    PRIM_SIZE * prims.length +
    SLOT_SIZE * slots.length +
    poolBytes.length;

  const body = new Uint8Array(
    total - SHEET_HEADER_SIZE,
  );
  const dv = new DataView(body.buffer);
  let at = 0;
  for (const t of templates) {
    dv.setUint32(at, t.nameOff, true);
    dv.setUint16(at + 4, t.primStart, true);
    dv.setUint16(at + 6, t.primCount, true);
    dv.setUint16(at + 8, t.slotStart, true);
    dv.setUint16(at + 10, t.slotCount, true);
    dv.setUint8(at + 12, t.font[0]);
    dv.setUint8(at + 13, t.font[1]);
    dv.setUint8(at + 14, t.font[2]);
    dv.setUint8(at + 15, t.canvases.length);
    at += TEMPLATE_SIZE;
  }
  for (const t of templates) {
    for (let i = 0; i < CANVAS_MAX; i++)
      dv.setUint32(at + i * 4, i < t.canvases.length ? t.canvases[i] >>> 0 : 0, true);
    at += CANVAS_MAX * 4;
  }
  for (const p of prims) {
    body.set(p, at);
    at += PRIM_SIZE;
  }
  for (const s of slots) {
    body.set(s, at);
    at += SLOT_SIZE;
  }
  body.set(poolBytes, at);

  const header = new Uint8Array(SHEET_HEADER_SIZE);
  const hv = new DataView(header.buffer);
  header.set(SHEET_MAGIC, 0);
  hv.setUint16(8, SHEET_VERSION, true);
  hv.setUint16(10, SHEET_HEADER_SIZE, true);
  hv.setUint32(12, total, true);
  hv.setUint32(16, crc32(body), true);
  hv.setUint16(20, VOCAB.space.w, true);
  hv.setUint16(22, VOCAB.space.h, true);
  hv.setUint16(24, templates.length, true);
  hv.setUint16(26, prims.length, true);
  hv.setUint16(28, slots.length, true);
  hv.setUint16(30, poolBytes.length, true);
  return concat([header, body]);
}

export interface PackMeta {
  packId: string;
  name: string;
  author: string;
}

export interface ArtPack {
  bytes: Uint8Array;
  sheet: Uint8Array;
  jcards: Uint8Array;
  filename: string;
  designIds: string[];
  designCount: number;
  sheetVersion: number;
  fontCount: number;
  crc: number;
  totalSize: number;
}

function fixedString(target: Uint8Array, at: number, cap: number, value: string, what: string): void {
  const bytes = new TextEncoder().encode(value);
  if (bytes.length > cap) throw new PackError([`${what} is ${bytes.length} B, over the ${cap} the pack holds`]);
  target.set(bytes, at);
}

/** The J-cards' JSON: each design's own section, in the sheet's id order. */
export function jcardsJson(designs: readonly PackDesign[]): Uint8Array {
  const ordered = [...designs].sort((a, b) => {
    const ai = String((a as Record<string, unknown>).id);
    const bi = String((b as Record<string, unknown>).id);
    return ai < bi ? -1 : ai > bi ? 1 : 0;
  });
  const body = {
    v: 2,
    designs: ordered.map((design) => {
      const d = design as Record<string, unknown>;
      return {
        id: d.id,
        name: d.name,
        jcard: d.jcard,
      };
    }),
  };
  const bytes = new TextEncoder().encode(JSON.stringify(body));
  for (const design of ordered) {
    const d = design as Record<string, unknown>;
    const size = new TextEncoder().encode(JSON.stringify(d.jcard)).length;
    if (size > JCARD_BYTES_MAX)
      throw new PackError([
        `${String(d.id)}: jcard is ${size} B, over the ${JCARD_BYTES_MAX} the App bundles`,
      ]);
  }
  return bytes;
}

/**
 * One `.k7artpack` (docs/specs/art-packs.md §1): the 128-byte header, the sheet, the
 * J-cards and the CRC over both.
 */
export function buildArtPack(
  designs: readonly PackDesign[],
  meta: PackMeta,
  fonts: readonly PackFont[] = packFonts(),
): ArtPack {
  const errors: string[] = [];
  if (!designs.length) errors.push("no designs: a pack carries at least one");
  if (designs.length > TEMPLATE_MAX)
    errors.push(`${designs.length} designs, more than the ${TEMPLATE_MAX} a pack holds`);
  if (!/^[a-z0-9][a-z0-9-]*$/.test(meta.packId || ""))
    errors.push(`pack id ${JSON.stringify(meta.packId)} is a slug: lowercase letters, digits and dashes`);
  for (const [what, value, cap] of [
    ["pack id", meta.packId, PACK_ID_MAX],
    ["name", meta.name, PACK_NAME_MAX],
    ["author", meta.author, PACK_AUTHOR_MAX],
  ] as const) {
    const bytes = new TextEncoder().encode(value || "");
    if (!bytes.length) errors.push(`${what} is empty`);
    if (bytes.length > cap) errors.push(`${what} is ${bytes.length} B, over the ${cap} the header holds`);
  }
  if (errors.length) throw new PackError(errors);

  const sheet = buildLabelSheet(designs, fonts);
  const jcards = jcardsJson(designs);
  const total = PACK_HEADER_SIZE + sheet.length + jcards.length;
  if (total > PACK_TOTAL_MAX)
    throw new PackError([`the pack is ${total} B, over the ${PACK_TOTAL_MAX} the art partition holds`]);

  const sheetDesigns = new DataView(sheet.buffer, sheet.byteOffset).getUint16(24, true);
  if (sheetDesigns !== designs.length)
    throw new PackError([`the sheet carries ${sheetDesigns} designs, the pack ${designs.length}`]);

  const payload = concat([sheet, jcards]);
  const bytes = new Uint8Array(total);
  const dv = new DataView(bytes.buffer);
  bytes.set(PACK_MAGIC, 0);
  dv.setUint16(8, PACK_VERSION, true);
  dv.setUint16(10, PACK_HEADER_SIZE, true);
  dv.setUint32(12, total, true);
  dv.setUint32(16, crc32(payload), true);
  dv.setUint16(20, designs.length, true);
  dv.setUint16(22, SHEET_VERSION, true);
  dv.setUint16(24, fonts.length, true);
  dv.setUint16(26, 0, true);
  dv.setUint32(28, PACK_HEADER_SIZE, true);
  dv.setUint32(32, sheet.length, true);
  dv.setUint32(36, PACK_HEADER_SIZE + sheet.length, true);
  dv.setUint32(40, jcards.length, true);
  fixedString(bytes, 44, PACK_ID_MAX, meta.packId, "pack id");
  fixedString(bytes, 60, PACK_NAME_MAX, meta.name, "name");
  fixedString(bytes, 92, PACK_AUTHOR_MAX, meta.author, "author");
  dv.setUint32(124, 0, true);
  bytes.set(sheet, PACK_HEADER_SIZE);
  bytes.set(jcards, PACK_HEADER_SIZE + sheet.length);

  return {
    bytes,
    sheet,
    jcards,
    filename: `${meta.packId}.k7artpack`,
    designIds: [...designs]
      .sort((a, b) => {
        const ai = String((a as Record<string, unknown>).id);
        const bi = String((b as Record<string, unknown>).id);
        return ai < bi ? -1 : ai > bi ? 1 : 0;
      })
      .map((d) => String((d as Record<string, unknown>).id)),
    designCount: designs.length,
    sheetVersion: SHEET_VERSION,
    fontCount: fonts.length,
    crc: crc32(payload),
    totalSize: total,
  };
}

/** Base64, chunked so a sheet of a few KB does not overflow a call's argument list. */
export function toBase64(bytes: Uint8Array): string {
  let out = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk)
    out += String.fromCharCode(...bytes.subarray(i, i + chunk));
  return btoa(out);
}

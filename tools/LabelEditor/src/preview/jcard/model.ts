/**
 * The J-card section of a K7Art (docs/specs/k7art.md §4): one printed side of a cassette
 * case, 4.0625x4 in at 320 units per inch, scored into a flap, a spine and a cover.
 *
 * The packer never draws it. This module is the section as the App can draw it: the
 * constants, the records, and the reader that keeps only what the DOM renderer can actually
 * paint. A real design is validated at Export and at the pack's install (art-packs.md §4);
 * this reader is the last gate before a prim becomes an element.
 */

/** The sheet's space: 1300 / 320 = 4.0625 in wide, 1280 / 320 = 4 in tall. */
export const JCARD_WIDTH = 1300;
export const JCARD_HEIGHT = 1280;
export const JCARD_UNITS_PER_INCH = 320;
export const JCARD_SPACE = {
  w: JCARD_WIDTH,
  h: JCARD_HEIGHT,
  unit: JCARD_UNITS_PER_INCH,
} as const;

/** The three panels, in fold order: flap 0-320, spine 320-480, cover 480-1300. */
export const JCARD_PARTS = {
  flap: { x: 0, y: 0, w: 320, h: JCARD_HEIGHT },
  spine: { x: 320, y: 0, w: 160, h: JCARD_HEIGHT },
  cover: { x: 480, y: 0, w: 820, h: JCARD_HEIGHT },
} as const;
export type JcardPart = keyof typeof JCARD_PARTS;

/**
 * The album card is square, so the central 820x820 of the cover - x 480-1300, y 230-1050 -
 * is the region it shows (k7art.md §4). It is the cover centre-cropped, not a clip the design
 * can move; the editor draws it as a guide and this renderer reaches it by covering a square
 * surface with the whole cover part.
 */
export const JCARD_SAFE = { x: 480, y: 230, w: 820, h: 820 } as const;

/** The three fields the App fills: an album's or Track's title, byline and cassette line. */
export const JCARD_FIELDS = ['title', 'artist', 'meta'] as const;
export type JcardField = (typeof JCARD_FIELDS)[number];
export type JcardFields = Record<JcardField, string>;

/** The three faces the container's fonts name, and the J-card's own sizes per role. */
export const JCARD_FACES = [
  'Archivo Narrow 600',
  'Barlow Condensed 700',
  'IBM Plex Mono 500',
] as const;
export type JcardFace = (typeof JCARD_FACES)[number];

export const JCARD_FONT_ROLES = ['title', 'numeral', 'caption'] as const;
export type JcardFontRole = (typeof JCARD_FONT_ROLES)[number];

/** A colour is seven characters: # and six hex digits, either case. */
const COLOUR = /^#[0-9a-f]{6}$/i;

export const JCARD_SIZE_MIN = 4;
export const JCARD_SIZE_MAX = 640;
export const JCARD_PRIM_MAX = 64;
export const JCARD_BOUND = 4096;

export interface JcardFont {
  ff: string;
  size: number;
}

export interface JcardRect {
  t: 'rect';
  x: number;
  y: number;
  w: number;
  h: number;
  r?: number;
  rot?: number;
  /** The shape's own colour, #RRGGBB. */
  fill: string;
}

export interface JcardText {
  t: 'text';
  x: number;
  y: number;
  w: number;
  rot?: number;
  font: JcardFontRole;
  /** The text's own colour, #RRGGBB. */
  color: string;
  align: 'left' | 'center' | 'right';
  slot?: JcardField;
  text?: string;
}

export type JcardPrim = JcardRect | JcardText;

export interface JcardSection {
  space: { w: number; h: number; unit: number };
  /** The sheet's own colour, painted under the prims. */
  background: string;
  fonts: Record<JcardFontRole, JcardFont>;
  prims: JcardPrim[];
}

/**
 * One design the pool can draw. The label section is not here: the App never draws it, and
 * the compact pack's J-cards carry only this (art-packs.md §1).
 */
export interface JcardDesign {
  id: string;
  name: string;
  jcard: JcardSection;
}

const isObject = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value);
const isNumber = (value: unknown): value is number =>
  typeof value === 'number' && Number.isFinite(value);
const isInteger = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value);
const isString = (value: unknown): value is string => typeof value === 'string';

function isColour(value: unknown): value is string {
  return typeof value === 'string' && COLOUR.test(value);
}

function isField(value: unknown): value is JcardField {
  return typeof value === 'string' && (JCARD_FIELDS as readonly string[]).includes(value);
}

/**
 * One prim, or null for one the DOM cannot draw. This is deliberately per-prim: a bad layer
 * is dropped, and the design's other layers still make its face. The section is the one
 * refused whole by {@link readJcardDesign} when its sheet, parts or fonts are not the case's.
 */
function readPrim(raw: unknown): JcardPrim | null {
  if (!isObject(raw)) return null;

  const { x, y, w, rot: rawRot } = raw;
  if (!isNumber(x) || !isNumber(y) || !isNumber(w)) return null;
  if (w <= 0 || w > JCARD_BOUND) return null;
  if (x < -JCARD_BOUND || y < -JCARD_BOUND || x + w > JCARD_BOUND) return null;
  const rot = isNumber(rawRot) ? rawRot : undefined;

  if (raw.t === 'rect') {
    const { h, r: rawR, fill } = raw;
    if (!isNumber(h) || h <= 0 || h > JCARD_BOUND) return null;
    if (y + h > JCARD_BOUND) return null;
    if (!isColour(fill)) return null;
    const r = isNumber(rawR) && rawR >= 0 ? rawR : undefined;
    return {
      t: 'rect',
      x,
      y,
      w,
      h,
      ...(r === undefined ? {} : { r }),
      ...(rot === undefined ? {} : { rot }),
      fill,
    };
  }

  if (raw.t === 'text') {
    const font = raw.font;
    if (typeof font !== 'string' || !(JCARD_FONT_ROLES as readonly string[]).includes(font)) {
      return null;
    }
    // A text has no declared height; the sheet's bottom is the only bound it can leave.
    if (y > JCARD_BOUND) return null;
    const color = raw.color;
    if (!isColour(color)) return null;
    const rawAlign = raw.align ?? 'left';
    const align: JcardText['align'] =
      rawAlign === 'center' || rawAlign === 'right' ? rawAlign : 'left';
    const common = {
      t: 'text' as const,
      x,
      y,
      w,
      ...(rot === undefined ? {} : { rot }),
      font: font as JcardFontRole,
      color,
      align,
    };
    if (isString(raw.slot)) {
      // A dynamic text names a J-card field; the editor's `preview` is not drawable here.
      if (!isField(raw.slot)) return null;
      return { ...common, slot: raw.slot };
    }
    if (isString(raw.text) && raw.text.trim()) return { ...common, text: raw.text };
    return null;
  }

  return null;
}

function readsSpace(raw: unknown): JcardSection['space'] | null {
  if (!isObject(raw)) return null;
  if (
    raw.w !== JCARD_SPACE.w ||
    raw.h !== JCARD_SPACE.h ||
    raw.unit !== JCARD_SPACE.unit
  ) {
    return null;
  }
  return { w: raw.w, h: raw.h, unit: raw.unit };
}

function readsParts(raw: unknown): boolean {
  if (!isObject(raw)) return false;
  const names = Object.keys(raw);
  if (names.length !== Object.keys(JCARD_PARTS).length) return false;
  return (Object.keys(JCARD_PARTS) as JcardPart[]).every((part) => {
    const got = raw[part];
    const want = JCARD_PARTS[part];
    return (
      isObject(got) &&
      got.x === want.x &&
      got.y === want.y &&
      got.w === want.w &&
      got.h === want.h
    );
  });
}

function readsFonts(raw: unknown): Record<JcardFontRole, JcardFont> | null {
  if (!isObject(raw)) return null;
  const fonts = {} as Record<JcardFontRole, JcardFont>;
  for (const role of JCARD_FONT_ROLES) {
    const font = raw[role];
    if (!isObject(font)) return null;
    const ff = font.ff;
    const size = font.size;
    if (typeof ff !== 'string' || !(JCARD_FACES as readonly string[]).includes(ff)) {
      return null;
    }
    if (!isInteger(size) || size < JCARD_SIZE_MIN || size > JCARD_SIZE_MAX) return null;
    fonts[role] = { ff, size };
  }
  return fonts;
}

/**
 * A K7Art, or one design from a pack's J-cards, as the renderer needs it. The container's
 * `format`/`v` are refused when present and wrong; a pack entry has neither (art-packs.md §1).
 */
export function readJcardDesign(raw: unknown): JcardDesign | null {
  if (!isObject(raw)) return null;
  if ('format' in raw && raw.format !== 'k7art') return null;
  if ('v' in raw && raw.v !== 2) return null;

  const id = raw.id;
  if (!isString(id) || !/^k7\.[a-z0-9][a-z0-9-]*$/.test(id)) return null;
  const name = isString(raw.name) && raw.name.trim() ? raw.name : id;

  const jcard = raw.jcard;
  if (!isObject(jcard)) return null;
  const space = readsSpace(jcard.space);
  if (!space) return null;
  if (!readsParts(jcard.parts)) return null;
  const fonts = readsFonts(jcard.fonts);
  if (!fonts) return null;
  if (!Array.isArray(jcard.prims) || jcard.prims.length > JCARD_PRIM_MAX) return null;
  const background = jcard.background;
  if (!isColour(background)) return null;

  const prims: JcardPrim[] = [];
  for (const prim of jcard.prims) {
    const read = readPrim(prim);
    if (read) prims.push(read);
  }

  return { id, name, jcard: { space, background, fonts, prims } };
}

/**
 * The J-cards of a `.k7artpack` (art-packs.md §1): its `{ v, designs }` object, or the design
 * list alone. Every design the renderer can draw is kept; a bad one is left out rather than
 * taking the good ones with it. The pack's header and CRC are the install's (APP-65).
 */
export function readJcardPack(raw: unknown): JcardDesign[] | null {
  let list: unknown[] | null = null;
  if (Array.isArray(raw)) list = raw;
  else if (isObject(raw) && Array.isArray(raw.designs)) {
    if ('v' in raw && raw.v !== 2) return null;
    list = raw.designs;
  }
  if (!list) return null;
  const designs: JcardDesign[] = [];
  for (const entry of list) {
    const design = readJcardDesign(entry);
    if (design) designs.push(design);
  }
  return designs;
}

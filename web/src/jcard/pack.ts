import { readJcardDesign, type JcardDesign } from './model';

/**
 * One `.k7artpack`, read and checked (`art-packs.md` §1).
 *
 * The file is little-endian: a 128-byte header, the label sheet the packer writes, and the
 * designs' J-cards as UTF-8 JSON. The App is not the authority on whether a pack *works* —
 * the device's `label_pack_open_why()` is — but the App is where a person sees what they
 * picked before anything is uploaded, so it repeats every check the browser can make:
 * the header's fields and bounds, the CRC over the payload, the sheet's own open (magic,
 * version, space, counts, layout and checksum) and the J-cards' shape.
 *
 * A refusal names the part that failed, and reading is all-or-nothing: a pack with one bad
 * design is not a smaller pack, because the sheet and the J-cards are one list in id order.
 */

export const PACK_MAGIC = 'K7ART\0\0\0';
export const PACK_VERSION = 1;
export const PACK_HEADER_SIZE = 128;
export const PACK_TOTAL_MAX = 1024 * 1024;
export const PACK_DESIGN_MAX = 32;
export const PACK_JCARD_MAX = 16 * 1024;
export const PACK_ID_MAX = 16;
export const PACK_NAME_MAX = 32;
export const PACK_AUTHOR_MAX = 32;

export const SHEET_MAGIC = 'K7LBL\0\0\0';
export const SHEET_VERSION = 5;
export const SHEET_HEADER_SIZE = 32;
/** Version 5 is the literal colours: a template record carries no inks. */
export const SHEET_TEMPLATE_SIZE = 16;
export const SHEET_PRIM_SIZE = 24;
export const SHEET_SLOT_SIZE = 8;
export const SHEET_SPACE = { w: 748, h: 359 } as const;
export const SHEET_TEMPLATE_MAX = 32;
export const SHEET_PRIM_MAX = 48;
export const SHEET_SLOT_MAX = 16;
export const SHEET_CANVAS_MAX = 8;
export const SHEET_VALUE_MAX = 96;
export const SHEET_PRIMS_MAX = SHEET_TEMPLATE_MAX * SHEET_PRIM_MAX;
export const SHEET_SLOTS_MAX = SHEET_TEMPLATE_MAX * SHEET_SLOT_MAX;

/** Which part of the file a refusal is about: the sentence the page shows starts here. */
export type ArtPackPart = 'header' | 'checksum' | 'sheet' | 'jcards' | 'bounds';

export class ArtPackError extends Error {
  constructor(
    readonly part: ArtPackPart,
    message: string,
  ) {
    super(message);
    this.name = 'ArtPackError';
  }
}

/**
 * The plain line the Art packs page shows for a sheet from another version: an old pack is
 * refused, not read, and its designs are published again from the editor (art-packs.md §1).
 */
function sheetVersionError(version: number, part: ArtPackPart): ArtPackError {
  if (version === 4) {
    return new ArtPackError(
      part,
      "The pack's label sheet is from an older editor (version 4); publish the designs again from /k7art.",
    );
  }
  return new ArtPackError(
    part,
    `The pack's label sheet is version ${version}, not ${SHEET_VERSION}; publish the designs again from /k7art.`,
  );
}

/** The pack as the App can use it: the header's facts, its J-cards, and its bytes. */
export interface ArtPack {
  /** An owned copy, so a caller may drop its buffer and an upload may still send it. */
  bytes: Uint8Array<ArrayBuffer>;
  packId: string;
  name: string;
  author: string;
  designCount: number;
  sheetVersion: number;
  fontCount: number;
  /** The sheet's own CRC (its header's), not the pack's. */
  sheetCrc: number;
  /** The CRC-32/IEEE over the payload, and the key the App's cache is named with. */
  packCrc: number;
  size: number;
  /**
   * The J-cards document itself (`{ v: 2, designs: [...] }`), kept as it was parsed so the
   * IndexedDB cache stores the payload rather than a second shape of it.
   */
  jcards: unknown;
  designs: JcardDesign[];
}

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let byte = 0; byte < 256; byte += 1) {
    let value = byte;
    for (let bit = 0; bit < 8; bit += 1) {
      value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
    }
    table[byte] = value >>> 0;
  }
  return table;
})();

/** CRC-32/IEEE, the same polynomial as the sheet's and the cover archive's. */
export function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function u16(bytes: Uint8Array, at: number): number {
  return bytes[at]! | (bytes[at + 1]! << 8);
}

function u32(bytes: Uint8Array, at: number): number {
  return (bytes[at]! | (bytes[at + 1]! << 8) | (bytes[at + 2]! << 16) | (bytes[at + 3]! << 24)) >>> 0;
}

function startsWith(bytes: Uint8Array, at: number, text: string): boolean {
  if (at + text.length > bytes.length) return false;
  for (let index = 0; index < text.length; index += 1) {
    if (bytes[at + index] !== text.charCodeAt(index)) return false;
  }
  return true;
}

/** A NUL-padded UTF-8 field: the bytes before the first NUL, or the field's whole width. */
function fixedString(bytes: Uint8Array, at: number, cap: number, what: string): string {
  let end = at;
  while (end < at + cap && bytes[end] !== 0) end += 1;
  try {
    return new TextDecoder('utf-8', { fatal: true }).decode(bytes.subarray(at, end));
  } catch {
    throw new ArtPackError('header', `The pack's ${what} is not UTF-8.`);
  }
}

/** A string offset is inside the pool only when a NUL ends it there. */
function hasString(pool: Uint8Array, offset: number): boolean {
  if (offset >= pool.length) return false;
  for (let at = offset; at < pool.length; at += 1) {
    if (pool[at] === 0) return true;
  }
  return false;
}

/**
 * The sheet's own open, as much of it as a browser can check: the magic, the version, the
 * space, the counts, the layout that follows from them, the checksum, and each template's,
 * prim's and slot's index into the arrays around them. Returns the sheet's CRC, which the
 * device's status also reports.
 */
function readSheet(sheet: Uint8Array, designCount: number, fontCount: number): number {
  if (sheet.length < SHEET_HEADER_SIZE) {
    throw new ArtPackError('sheet', 'The label sheet is shorter than its header.');
  }
  if (!startsWith(sheet, 0, SHEET_MAGIC)) {
    throw new ArtPackError('sheet', "The label sheet's magic is not K7LBL.");
  }
  if (u16(sheet, 8) !== SHEET_VERSION) {
    throw sheetVersionError(u16(sheet, 8), 'sheet');
  }
  if (u16(sheet, 10) !== SHEET_HEADER_SIZE) {
    throw new ArtPackError('sheet', "The label sheet's header size is not 32.");
  }
  if (u16(sheet, 20) !== SHEET_SPACE.w || u16(sheet, 22) !== SHEET_SPACE.h) {
    throw new ArtPackError(
      'sheet',
      `The label sheet's space is not ${SHEET_SPACE.w}x${SHEET_SPACE.h}.`,
    );
  }

  const templates = u16(sheet, 24);
  const prims = u16(sheet, 26);
  const slots = u16(sheet, 28);
  const poolSize = u16(sheet, 30);

  if (templates === 0 || templates > SHEET_TEMPLATE_MAX) {
    throw new ArtPackError('sheet', 'The label sheet carries no designs, or more than 32.');
  }
  if (prims > SHEET_PRIMS_MAX) {
    throw new ArtPackError('sheet', 'The label sheet carries more prims than any sheet can hold.');
  }
  if (slots > SHEET_SLOTS_MAX) {
    throw new ArtPackError('sheet', 'The label sheet carries more slots than any sheet can hold.');
  }

  const layout =
    SHEET_HEADER_SIZE +
    templates * SHEET_TEMPLATE_SIZE +
    templates * SHEET_CANVAS_MAX * 4 +
    prims * SHEET_PRIM_SIZE +
    slots * SHEET_SLOT_SIZE +
    poolSize;
  if (u32(sheet, 12) !== layout || layout !== sheet.length) {
    throw new ArtPackError(
      'sheet',
      "The label sheet's total size disagrees with its parts and its bytes.",
    );
  }
  if (crc32(sheet.subarray(SHEET_HEADER_SIZE)) !== u32(sheet, 16)) {
    throw new ArtPackError('sheet', "The label sheet's checksum does not match its bytes.");
  }
  if (templates !== designCount) {
    throw new ArtPackError(
      'sheet',
      `The pack says ${designCount} ${designCount === 1 ? 'design' : 'designs'}; its label sheet carries ${templates}.`,
    );
  }

  const templateBase = SHEET_HEADER_SIZE;
  const canvasBase = templateBase + templates * SHEET_TEMPLATE_SIZE;
  const primBase = canvasBase + templates * SHEET_CANVAS_MAX * 4;
  const slotBase = primBase + prims * SHEET_PRIM_SIZE;
  const poolBase = slotBase + slots * SHEET_SLOT_SIZE;
  const pool = sheet.subarray(poolBase, poolBase + poolSize);

  for (let template = 0; template < templates; template += 1) {
    const at = templateBase + template * SHEET_TEMPLATE_SIZE;
    const name = u32(sheet, at);
    if (!hasString(pool, name)) {
      throw new ArtPackError('sheet', `Template ${template + 1}'s name is outside the string pool.`);
    }

    const primStart = u16(sheet, at + 4);
    const primCount = u16(sheet, at + 6);
    if (primCount === 0 || primCount > SHEET_PRIM_MAX) {
      throw new ArtPackError('sheet', `Template ${template + 1} has no prims, or more than 48.`);
    }
    if (primStart + primCount > prims) {
      throw new ArtPackError('sheet', `Template ${template + 1}'s prims are outside the sheet.`);
    }

    const slotStart = u16(sheet, at + 8);
    const slotCount = u16(sheet, at + 10);
    if (slotCount > SHEET_SLOT_MAX || slotStart + slotCount > slots) {
      throw new ArtPackError('sheet', `Template ${template + 1}'s slots are outside the sheet.`);
    }

    for (let role = 0; role < 3; role += 1) {
      if (sheet[at + 12 + role]! >= fontCount) {
        throw new ArtPackError(
          'sheet',
          `Template ${template + 1} names a font the pack's table does not carry.`,
        );
      }
    }

    const canvasCount = sheet[at + 15]!;
    if (canvasCount > SHEET_CANVAS_MAX) {
      throw new ArtPackError('sheet', `Template ${template + 1} names too many canvases.`);
    }
    for (let canvas = 0; canvas < SHEET_CANVAS_MAX; canvas += 1) {
      const colour = u32(sheet, canvasBase + template * SHEET_CANVAS_MAX * 4 + canvas * 4);
      if (canvas < canvasCount ? colour > 0xffffff : colour !== 0) {
        throw new ArtPackError('sheet', `Template ${template + 1}'s canvas list is not a colour list.`);
      }
    }
  }

  for (let index = 0; index < prims; index += 1) {
    const at = primBase + index * SHEET_PRIM_SIZE;
    const kind = sheet[at]!;
    const reserved = sheet[at + 1]!;
    const font = sheet[at + 2]!;
    const align = sheet[at + 3]!;
    const rotation = u16(sheet, at + 14);
    if (kind > 2 || reserved !== 0 || font > 2 || align > 2 || rotation > 3599) {
      throw new ArtPackError('sheet', `Prim ${index + 1} is out of range.`);
    }
    if (u32(sheet, at + 20) > 0xffffff) {
      throw new ArtPackError('sheet', `Prim ${index + 1}'s colour is not an RGB888 value.`);
    }
    if (kind === 0) {
      if (u16(sheet, at + 10) === 0 || u32(sheet, at + 16) !== 0xffffffff) {
        throw new ArtPackError('sheet', `Prim ${index + 1} is a rect with no height or a text offset.`);
      }
    } else if (!hasString(pool, u32(sheet, at + 16))) {
      throw new ArtPackError('sheet', `Prim ${index + 1}'s text is outside the string pool.`);
    }
  }

  for (let index = 0; index < slots; index += 1) {
    const at = slotBase + index * SHEET_SLOT_SIZE;
    const max = sheet[at + 4]!;
    if (max === 0 || max > SHEET_VALUE_MAX) {
      throw new ArtPackError('sheet', `Slot ${index + 1}'s length is not 1..${SHEET_VALUE_MAX}.`);
    }
    if (!hasString(pool, u32(sheet, at))) {
      throw new ArtPackError('sheet', `Slot ${index + 1}'s name is outside the string pool.`);
    }
  }

  return u32(sheet, 16);
}

/**
 * Read one `.k7artpack`. Throws {@link ArtPackError} naming the part that failed; a pack
 * that reads is the one that may be shown, cached and uploaded.
 */
export function readArtPack(input: Uint8Array): ArtPack {
  /* One owned image: every offset below is then relative to zero, and the bytes an upload
   * sends are the bytes that were checked. */
  const bytes = new Uint8Array(input);

  if (bytes.length < PACK_HEADER_SIZE) {
    throw new ArtPackError('header', 'The pack is shorter than its 128-byte header.');
  }
  if (bytes.length > PACK_TOTAL_MAX) {
    throw new ArtPackError(
      'bounds',
      `The pack is ${bytes.length} bytes; the art partition holds ${PACK_TOTAL_MAX}.`,
    );
  }
  if (!startsWith(bytes, 0, PACK_MAGIC)) {
    throw new ArtPackError('header', 'This is not a K7ART pack: its magic is not K7ART.');
  }
  if (u16(bytes, 8) !== PACK_VERSION) {
    throw new ArtPackError('header', `The pack is not version ${PACK_VERSION}.`);
  }
  if (u16(bytes, 10) !== PACK_HEADER_SIZE) {
    throw new ArtPackError('header', "The pack's header size is not 128.");
  }

  const totalSize = u32(bytes, 12);
  if (totalSize !== bytes.length) {
    throw new ArtPackError(
      'header',
      `The pack says it is ${totalSize} bytes; the file is ${bytes.length}.`,
    );
  }

  const packCrc = u32(bytes, 16);
  const designCount = u16(bytes, 20);
  const sheetVersion = u16(bytes, 22);
  const fontCount = u16(bytes, 24);
  const sheetOffset = u32(bytes, 28);
  const sheetSize = u32(bytes, 32);
  const jcardsOffset = u32(bytes, 36);
  const jcardsSize = u32(bytes, 40);

  if (u16(bytes, 26) !== 0 || u32(bytes, 124) !== 0) {
    throw new ArtPackError('header', 'A reserved field in the pack header is not zero.');
  }
  if (designCount === 0 || designCount > PACK_DESIGN_MAX) {
    throw new ArtPackError(
      'bounds',
      `The pack carries ${designCount} designs; a pack carries 1 to ${PACK_DESIGN_MAX}.`,
    );
  }
  if (sheetVersion !== SHEET_VERSION) {
    throw sheetVersionError(sheetVersion, 'header');
  }
  if (fontCount === 0 || fontCount > 255) {
    throw new ArtPackError('header', "The pack's font count is not a table a firmware carries.");
  }
  if (sheetOffset !== PACK_HEADER_SIZE) {
    throw new ArtPackError('header', 'The label sheet is not at its offset.');
  }
  if (sheetSize === 0) {
    throw new ArtPackError('header', 'The pack carries no label sheet.');
  }
  if (jcardsOffset !== PACK_HEADER_SIZE + sheetSize) {
    throw new ArtPackError('header', 'The J-cards are not at their offset.');
  }
  if (jcardsSize === 0) {
    throw new ArtPackError('header', 'The pack carries no J-cards.');
  }
  if (jcardsOffset + jcardsSize !== totalSize) {
    throw new ArtPackError(
      'header',
      'The pack total disagrees with its sheet and its J-cards.',
    );
  }
  if (crc32(bytes.subarray(PACK_HEADER_SIZE)) !== packCrc) {
    throw new ArtPackError('checksum', "The pack's checksum does not match its bytes.");
  }

  const packId = fixedString(bytes, 44, PACK_ID_MAX, 'pack id');
  if (!/^[a-z0-9][a-z0-9-]*$/.test(packId)) {
    throw new ArtPackError(
      'header',
      `The pack id ${JSON.stringify(packId)} is not a slug: lowercase letters, digits and dashes.`,
    );
  }
  const name = fixedString(bytes, 60, PACK_NAME_MAX, 'name');
  const author = fixedString(bytes, 92, PACK_AUTHOR_MAX, 'author');

  const sheet = bytes.subarray(sheetOffset, sheetOffset + sheetSize);
  const sheetCrc = readSheet(sheet, designCount, fontCount);

  let text: string;
  try {
    text = new TextDecoder('utf-8', { fatal: true }).decode(
      bytes.subarray(jcardsOffset, jcardsOffset + jcardsSize),
    );
  } catch {
    throw new ArtPackError('jcards', 'The J-cards are not UTF-8.');
  }

  let jcards: unknown;
  try {
    jcards = JSON.parse(text) as unknown;
  } catch {
    throw new ArtPackError('jcards', 'The J-cards are not JSON.');
  }
  if (!isObject(jcards) || jcards.v !== 2 || !Array.isArray(jcards.designs)) {
    throw new ArtPackError('jcards', 'The J-cards are not a version 2 { designs } document.');
  }
  if (jcards.designs.length !== designCount) {
    throw new ArtPackError(
      'jcards',
      `The pack says ${designCount} ${designCount === 1 ? 'design' : 'designs'}; its J-cards carry ${jcards.designs.length}.`,
    );
  }

  const designs: JcardDesign[] = [];
  let previous = '';
  for (let index = 0; index < jcards.designs.length; index += 1) {
    const entry: unknown = jcards.designs[index];
    const id =
      isObject(entry) && typeof entry.id === 'string' ? entry.id : `design ${index + 1}`;
    if (!isObject(entry) || typeof entry.id !== 'string') {
      throw new ArtPackError('jcards', `The J-cards' ${id} has no id.`);
    }
    if (entry.id <= previous) {
      throw new ArtPackError(
        'jcards',
        'The J-cards are not in id order, so they cannot match the sheet.',
      );
    }
    previous = entry.id;

    const design = readJcardDesign(entry);
    if (!design) {
      throw new ArtPackError(
        'jcards',
        `The design ${entry.id} is not a J-card this App can draw.`,
      );
    }
    const size = new TextEncoder().encode(JSON.stringify(entry.jcard)).length;
    if (size > PACK_JCARD_MAX) {
      throw new ArtPackError(
        'bounds',
        `The design ${entry.id}'s J-card is ${size} bytes, over the ${PACK_JCARD_MAX} a pack holds.`,
      );
    }
    designs.push(design);
  }

  return {
    bytes,
    packId,
    name,
    author,
    designCount,
    sheetVersion,
    fontCount,
    sheetCrc,
    packCrc,
    size: totalSize,
    jcards,
    designs,
  };
}

/** The cache key: a pack's CRC and size name one immutable file. */
export function artPackKey(crc: number, size: number): string {
  return `${crc.toString(16)}:${size}`;
}

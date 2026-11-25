import { readArtPack, crc32, type ArtPack } from '../../src/jcard/pack';

/**
 * A `.k7artpack` built in the test: a real factory J-card, and the smallest label sheet the
 * packer's format allows (one rect per design, no slots). The App's reader checks the sheet's
 * layout, so the fixture has to write it as faithfully as the editor's packer does; it is not
 * a mock of the format.
 *
 * The options exist so a test can break one field and prove the reader names that part.
 */

/** The factory J-cards as the files write them: the pack's reader wants the raw section,
 *  `parts` and all, not the App's parsed copy of it. */
const FACTORY = import.meta.glob('../../../assets/k7art/*.json', {
  eager: true,
  import: 'jcard',
}) as Record<string, Record<string, unknown>>;

function firstFactoryJcard(): Record<string, unknown> {
  const key = Object.keys(FACTORY).sort()[0];
  const jcard = key ? FACTORY[key] : undefined;
  if (!jcard) throw new Error('the factory has no J-card to clone');
  return structuredClone(jcard);
}

export interface DesignSeed {
  id: string;
  name: string;
  jcard: Record<string, unknown>;
}

export interface PackFixtureOptions {
  /** The J-cards' designs, in id order. Defaults to one factory clone. */
  designs?: DesignSeed[];
  packId?: string;
  name?: string;
  author?: string;
  /** The header's design count; defaults to the design list's length. */
  headerDesignCount?: number;
  /** The sheet's template count; defaults to the header's. */
  sheetDesignCount?: number;
  /** How many designs the J-cards document carries; defaults to the header's. */
  jcardsDesignCount?: number;
  fontCount?: number;
  packVersion?: number;
  packMagic?: string;
  headerSize?: number;
  /** Overrides the header's total_size without changing the file. */
  totalSize?: number;
  /** The pack header's compatibility field, not the sheet's own version. */
  headerSheetVersion?: number;
  /** The sheet's own version. */
  sheetVersion?: number;
  sheetSpace?: { w: number; h: number };
  sheetMagic?: string;
  corruptSheetCrc?: boolean;
  corruptPackCrc?: boolean;
  /** Replaces the `{ v, designs }` document, for a shape the packer would not write. */
  jcardsOverride?: unknown;
  /** Replaces the J-cards' bytes, for a document that is not JSON at all. */
  jcardsRaw?: string;
  /** Gives the first J-card a text big enough to pass the 16 KB cap. */
  oversizedJcard?: boolean;
}

/** `count` clones of the first factory J-card, in the id order a pack writes them. */
export function seedDesigns(count = 1): DesignSeed[] {
  return Array.from({ length: count }, (_, index) => ({
    id: `k7.test-${String(index + 1).padStart(2, '0')}`,
    name: `Test design ${index + 1}`,
    jcard: firstFactoryJcard(),
  }));
}

const encoder = new TextEncoder();

function writeFixed(target: Uint8Array, at: number, text: string, cap: number): void {
  target.set(encoder.encode(text).subarray(0, cap), at);
}

function buildSheet(
  options: PackFixtureOptions,
  names: readonly string[],
): Uint8Array {
  const strings: number[] = [];
  const offsets: number[] = [];
  for (const name of names) {
    offsets.push(strings.length);
    strings.push(...encoder.encode(name), 0);
  }
  const pool = Uint8Array.from(strings);

  const templates = names.length;
  const prims = templates;
  const slots = 0;
  /* Version 5: a 16-byte template record, the canvas row, 24-byte prim records. */
  const total =
    32 + templates * 16 + templates * 8 * 4 + prims * 24 + slots * 8 + pool.length;
  const sheet = new Uint8Array(total);
  const view = new DataView(sheet.buffer);

  writeFixed(sheet, 0, options.sheetMagic ?? 'K7LBL\0\0\0', 8);
  view.setUint16(8, options.sheetVersion ?? 5, true);
  view.setUint16(10, 32, true);
  view.setUint32(12, total, true);
  // 16..20 is the sheet's CRC and is stamped below.
  view.setUint16(20, options.sheetSpace?.w ?? 748, true);
  view.setUint16(22, options.sheetSpace?.h ?? 359, true);
  view.setUint16(24, templates, true);
  view.setUint16(26, prims, true);
  view.setUint16(28, slots, true);
  view.setUint16(30, pool.length, true);

  const templateBase = 32;
  const canvasBase = templateBase + templates * 16;
  const primBase = canvasBase + templates * 8 * 4;
  const slotBase = primBase + prims * 24;
  const poolBase = slotBase + slots * 8;

  for (let index = 0; index < templates; index += 1) {
    const at = templateBase + index * 16;
    view.setUint32(at, offsets[index] ?? 0, true);
    view.setUint16(at + 4, index, true);
    view.setUint16(at + 6, 1, true);
    view.setUint16(at + 8, 0, true);
    view.setUint16(at + 10, 0, true);
    // Three roles, all the first face; canvas_count is 0 and the list stays zero.
    view.setUint8(at + 12, 0);
    view.setUint8(at + 13, 0);
    view.setUint8(at + 14, 0);
    view.setUint8(at + 15, 0);

    const prim = primBase + index * 24;
    view.setUint8(prim, 0); // a rect
    view.setUint8(prim + 1, 0); // reserved
    view.setUint8(prim + 2, 0);
    view.setUint8(prim + 3, 0);
    view.setInt16(prim + 4, 0, true);
    view.setInt16(prim + 6, 0, true);
    view.setUint16(prim + 8, 100, true);
    view.setUint16(prim + 10, 100, true);
    view.setUint16(prim + 12, 0, true);
    view.setUint16(prim + 14, 0, true);
    view.setUint32(prim + 16, 0xffffffff, true);
    // The prim's own colour: a literal RGB888.
    view.setUint32(prim + 20, 0x142538, true);
  }

  sheet.set(pool, poolBase);
  if (!options.corruptSheetCrc) {
    view.setUint32(16, crc32(sheet.subarray(32)), true);
  }
  return sheet;
}

/** Build one pack, faithful to the editor's header and sheet. */
export function buildPackBytes(options: PackFixtureOptions = {}): Uint8Array<ArrayBuffer> {
  const designs = options.designs ?? seedDesigns();
  const headerDesignCount = options.headerDesignCount ?? designs.length;
  const sheetDesignCount = options.sheetDesignCount ?? headerDesignCount;
  const jcardsDesignCount = options.jcardsDesignCount ?? headerDesignCount;
  const fontCount = options.fontCount ?? 3;

  const names = Array.from(
    { length: sheetDesignCount },
    (_, index) => designs[index % designs.length]?.id ?? `k7.blank-${index + 1}`,
  );
  const sheet = buildSheet(options, names);

  const entries = designs.slice(0, jcardsDesignCount).map((design, index) => ({
    id: design.id,
    name: design.name,
    jcard:
      options.oversizedJcard && index === 0
        ? {
            ...design.jcard,
            prims: [
              ...((design.jcard.prims as unknown[]) ?? []),
              {
                t: 'text',
                x: 0,
                y: 0,
                w: 100,
                font: 'title',
                color: '#242424',
                align: 'left',
                text: 'x'.repeat(17 * 1024),
              },
            ],
          }
        : design.jcard,
  }));
  const document = options.jcardsOverride ?? { v: 2, designs: entries };
  const jcards = encoder.encode(options.jcardsRaw ?? JSON.stringify(document));

  const total = 128 + sheet.length + jcards.length;
  const bytes = new Uint8Array(total);
  const view = new DataView(bytes.buffer);

  writeFixed(bytes, 0, options.packMagic ?? 'K7ART\0\0\0', 8);
  view.setUint16(8, options.packVersion ?? 1, true);
  view.setUint16(10, options.headerSize ?? 128, true);
  view.setUint32(12, options.totalSize ?? total, true);
  view.setUint16(20, headerDesignCount, true);
  view.setUint16(22, options.headerSheetVersion ?? 5, true);
  view.setUint16(24, fontCount, true);
  view.setUint16(26, 0, true);
  view.setUint32(28, 128, true);
  view.setUint32(32, sheet.length, true);
  view.setUint32(36, 128 + sheet.length, true);
  view.setUint32(40, jcards.length, true);
  writeFixed(bytes, 44, options.packId ?? 'test-pack', 16);
  writeFixed(bytes, 60, options.name ?? 'Test pack', 32);
  writeFixed(bytes, 92, options.author ?? 'A tester', 32);
  view.setUint32(124, 0, true);

  bytes.set(sheet, 128);
  bytes.set(jcards, 128 + sheet.length);
  if (!options.corruptPackCrc) {
    view.setUint32(16, crc32(bytes.subarray(128)), true);
  }
  return bytes;
}

/** A valid pack, already read: the shape the cache and the Art packs page start from. */
export function buildPack(options: PackFixtureOptions = {}): ArtPack {
  return readArtPack(buildPackBytes(options));
}

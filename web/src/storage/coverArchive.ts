/**
 * `k7.covers`: the card's copy of the browser's preprocessed cover cache.
 *
 * One content-addressed file at the card root, beside `k7.db`. Every distinct source picture
 * appears once, as the 422px AVIF the browser draws (or the source itself when the codec is
 * unavailable), keyed by the SHA-256 of the source bytes. A media row's `coverHash` names one
 * of these records; nothing about a Track's cover is stored twice.
 *
 * Layout, little-endian:
 *
 *   header 32 B   magic "K7COVR", version, sizes, count, total, CRC
 *   records       count x 52 B, sorted by hash
 *   payloads      each record's bytes, in record order
 *
 * A record is hash[32], width u16, height u16, format u8, 3 reserved, offset u32, length u32,
 * payload CRC32. `offset` is absolute in the file. The header CRC covers [32, total_size) and
 * the per-record CRC covers the payload alone, so a single damaged cover can be named rather
 * than taking the archive down.
 */

export interface ArchiveCover {
  /** Lowercase SHA-256 hex of the source picture bytes. */
  hash: string;
  bytes: Uint8Array<ArrayBuffer>;
  format: string;
  width: number;
  height: number;
}

const MAGIC = 'K7COVR\0\0';
const HEADER_SIZE = 32;
const RECORD_SIZE = 52;
const VERSION = 1;
const MAX_COVERS = 10_000;

const FORMATS: readonly string[] = [
  'image/avif',
  'image/jpeg',
  'image/png',
  'image/webp',
  'image/gif',
  'image/bmp',
  'application/octet-stream',
];

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

function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function formatCode(format: string): number {
  const index = FORMATS.indexOf(format);
  return index < 0 ? FORMATS.length - 1 : index;
}

function codeFormat(code: number): string {
  return FORMATS[code] ?? 'application/octet-stream';
}

function hashBytes(hash: string): Uint8Array {
  if (!/^[0-9a-f]{64}$/.test(hash)) throw new Error(`"${hash}" is not a SHA-256 hex digest`);
  const out = new Uint8Array(32);
  for (let i = 0; i < 32; i += 1) out[i] = Number.parseInt(hash.slice(i * 2, i * 2 + 2), 16);
  return out;
}

function bytesHash(bytes: Uint8Array): string {
  let out = '';
  for (const byte of bytes) out += byte.toString(16).padStart(2, '0');
  return out;
}

/** Build a deterministic archive: records sorted by hash, payloads in that same order. */
export function buildCoverArchive(covers: readonly ArchiveCover[]): Uint8Array<ArrayBuffer> {
  const unique = new Map<string, ArchiveCover>();
  for (const cover of covers) {
    if (!cover.hash || unique.has(cover.hash)) continue;
    unique.set(cover.hash, cover);
  }
  const records = [...unique.values()].sort((left, right) =>
    left.hash < right.hash ? -1 : left.hash > right.hash ? 1 : 0,
  );
  if (records.length > MAX_COVERS) throw new Error(`the archive holds more than ${MAX_COVERS} covers`);

  const payloadSize = records.reduce((total, cover) => total + cover.bytes.length, 0);
  const total = HEADER_SIZE + records.length * RECORD_SIZE + payloadSize;
  const image = new Uint8Array(total);
  const view = new DataView(image.buffer);

  for (let index = 0; index < MAGIC.length; index += 1) view.setUint8(index, MAGIC.charCodeAt(index));
  view.setUint16(8, VERSION, true);
  view.setUint16(10, HEADER_SIZE, true);
  view.setUint16(12, RECORD_SIZE, true);
  view.setUint16(14, 0, true);
  view.setUint32(16, records.length, true);
  view.setUint32(20, total, true);
  // 24..28 is the header CRC and 28..32 is reserved; the CRC is stamped last.

  let payloadAt = HEADER_SIZE + records.length * RECORD_SIZE;
  records.forEach((cover, index) => {
    const at = HEADER_SIZE + index * RECORD_SIZE;
    image.set(hashBytes(cover.hash), at);
    view.setUint16(at + 32, cover.width, true);
    view.setUint16(at + 34, cover.height, true);
    view.setUint8(at + 36, formatCode(cover.format));
    view.setUint32(at + 40, payloadAt, true);
    view.setUint32(at + 44, cover.bytes.length, true);
    view.setUint32(at + 48, crc32(cover.bytes), true);
    image.set(cover.bytes, payloadAt);
    payloadAt += cover.bytes.length;
  });

  view.setUint32(24, crc32(image.subarray(HEADER_SIZE, total)), true);
  return image;
}

/** Parse and validate an archive. Throws a sentence naming the first broken rule. */
export function parseCoverArchive(bytes: Uint8Array): ArchiveCover[] {
  if (bytes.length < HEADER_SIZE) throw new Error('the archive is shorter than its header');
  const magic = String.fromCharCode(...bytes.subarray(0, 8));
  if (magic !== MAGIC) throw new Error('this is not a cover archive');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint16(8, true) !== VERSION) throw new Error('unknown cover archive version');
  if (view.getUint16(10, true) !== HEADER_SIZE) throw new Error('bad cover archive header size');
  if (view.getUint16(12, true) !== RECORD_SIZE) throw new Error('bad cover record size');
  if (view.getUint16(14, true) !== 0) throw new Error('reserved header bytes are not zero');

  const count = view.getUint32(16, true);
  const total = view.getUint32(20, true);
  if (count > MAX_COVERS) throw new Error('the archive holds too many covers');
  if (total !== bytes.length) throw new Error('the archive is not its stated length');
  if (view.getUint32(24, true) !== crc32(bytes.subarray(HEADER_SIZE))) {
    throw new Error('the cover archive checksum does not match');
  }
  if (HEADER_SIZE + count * RECORD_SIZE > total) throw new Error('the records are out of range');

  const covers: ArchiveCover[] = [];
  for (let index = 0; index < count; index += 1) {
    const at = HEADER_SIZE + index * RECORD_SIZE;
    const hash = bytesHash(bytes.subarray(at, at + 32));
    const width = view.getUint16(at + 32, true);
    const height = view.getUint16(at + 34, true);
    const format = codeFormat(view.getUint8(at + 36));
    const offset = view.getUint32(at + 40, true);
    const length = view.getUint32(at + 44, true);
    const payloadCrc = view.getUint32(at + 48, true);
    if (offset < HEADER_SIZE + count * RECORD_SIZE || offset + length > total) {
      throw new Error(`cover ${index} is outside the payload area`);
    }
    const payload = bytes.slice(offset, offset + length);
    if (crc32(payload) !== payloadCrc) throw new Error(`cover ${index} failed its checksum`);
    covers.push({ hash, bytes: payload, format, width, height });
  }

  // Sorted and unique, so two archives built from the same set are byte-identical.
  for (let index = 1; index < covers.length; index += 1) {
    if (covers[index].hash <= covers[index - 1].hash) {
      throw new Error('cover records are not sorted by hash');
    }
  }
  return covers;
}

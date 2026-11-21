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
 *   album index   version 2: count u32, then (hash[32], path_len u16, path) sorted by path
 *
 * The album index is what lets a browser with no store of its own draw a cover at all: a
 * media row's `coverHash` is the *mapping* between an album and a picture, and a cleared
 * browser has lost it. The archive carries it, so "remember from the player" restores the
 * names from the image and the covers from the archive together.
 *
 * A record is hash[32], width u16, height u16, format u8, 3 reserved, offset u32, length u32,
 * payload CRC32. `offset` is absolute in the file. The header CRC covers [32, total_size) and
 * the per-record CRC covers the payload alone, so a single damaged cover can be named rather
 * than taking the archive down.
 */

export interface ArchiveAlbum {
  /** The album's folder, below the card's `music`: what a row's `coverHash` is keyed to. */
  path: string;
  /** The hash of the picture that album wears. */
  hash: string;
}

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
const VERSION = 2;
/** v1 is the same file without the album index: read, never written. */
const VERSION_NO_INDEX = 1;
const MAX_ALBUM_PATH = 512;
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
export function buildCoverArchive(
  covers: readonly ArchiveCover[],
  albums: readonly ArchiveAlbum[] = [],
): Uint8Array<ArrayBuffer> {
  const unique = new Map<string, ArchiveCover>();
  for (const cover of covers) {
    if (!cover.hash || unique.has(cover.hash)) continue;
    unique.set(cover.hash, cover);
  }
  const records = [...unique.values()].sort((left, right) =>
    left.hash < right.hash ? -1 : left.hash > right.hash ? 1 : 0,
  );
  if (records.length > MAX_COVERS) throw new Error(`the archive holds more than ${MAX_COVERS} covers`);

  /* Only albums whose picture is in this archive, and one entry per folder: the index is a
   * set, so two rows of one album cannot disagree. An empty index is v1, byte for byte. */
  const known = new Set(records.map((cover) => cover.hash));
  const index = new Map<string, string>();
  for (const album of albums) {
    const path = album.path.trim();
    if (!path || !known.has(album.hash) || index.has(path)) continue;
    if (new TextEncoder().encode(path).length > MAX_ALBUM_PATH) continue;
    index.set(path, album.hash);
  }
  const entries = [...index.entries()].sort((left, right) =>
    left[0] < right[0] ? -1 : left[0] > right[0] ? 1 : 0,
  );
  const version = entries.length > 0 ? VERSION : VERSION_NO_INDEX;
  const indexSize = entries.length > 0 ? 4 + entries.reduce((n, [path]) => n + 32 + 2 + new TextEncoder().encode(path).length, 0) : 0;

  const payloadSize = records.reduce((total, cover) => total + cover.bytes.length, 0);
  const total = HEADER_SIZE + records.length * RECORD_SIZE + payloadSize + indexSize;
  const image = new Uint8Array(total);
  const view = new DataView(image.buffer);

  for (let index = 0; index < MAGIC.length; index += 1) view.setUint8(index, MAGIC.charCodeAt(index));
  view.setUint16(8, version, true);
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

  if (entries.length > 0) {
    view.setUint32(payloadAt, entries.length, true);
    let at = payloadAt + 4;
    for (const [path, hash] of entries) {
      const bytes = new TextEncoder().encode(path);
      image.set(hashBytes(hash), at);
      view.setUint16(at + 32, bytes.length, true);
      image.set(bytes, at + 34);
      at += 34 + bytes.length;
    }
  }

  view.setUint32(24, crc32(image.subarray(HEADER_SIZE, total)), true);
  return image;
}

/** Parse and validate an archive: its pictures, and (v2) which album wears which. */
export function readCoverArchive(bytes: Uint8Array): {
  covers: ArchiveCover[];
  albums: Map<string, string>;
} {
  if (bytes.length < HEADER_SIZE) throw new Error('the archive is shorter than its header');
  const magic = String.fromCharCode(...bytes.subarray(0, 8));
  if (magic !== MAGIC) throw new Error('this is not a cover archive');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const version = view.getUint16(8, true);
  if (version !== VERSION && version !== VERSION_NO_INDEX) {
    throw new Error('unknown cover archive version');
  }
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

  const albums = new Map<string, string>();
  if (version === VERSION_NO_INDEX) return { covers, albums };

  /* The index begins where the payloads end: the records say where each payload is, so the
   * last end is the first byte the index may use. */
  let payloadEnd = HEADER_SIZE + count * RECORD_SIZE;
  for (let i = 0; i < count; i += 1) {
    const offset = view.getUint32(HEADER_SIZE + i * RECORD_SIZE + 40, true);
    const length = view.getUint32(HEADER_SIZE + i * RECORD_SIZE + 44, true);
    payloadEnd = Math.max(payloadEnd, offset + length);
  }
  const indexAt = payloadEnd;
  if (indexAt + 4 > total) {
    if (indexAt !== total) throw new Error('the album index is out of range');
    return { covers, albums };
  }
  const entries = view.getUint32(indexAt, true);
  if (entries > MAX_COVERS) throw new Error('the album index holds too many entries');
  const decoder = new TextDecoder();
  const hashes = new Set(covers.map((cover) => cover.hash));
  let cursor = indexAt + 4;
  let previous = '';
  for (let i = 0; i < entries; i += 1) {
    if (cursor + 34 > total) throw new Error('the album index is truncated');
    const hash = bytesHash(bytes.subarray(cursor, cursor + 32));
    const length = view.getUint16(cursor + 32, true);
    if (cursor + 34 + length > total) throw new Error('the album index is truncated');
    const path = decoder.decode(bytes.subarray(cursor + 34, cursor + 34 + length));
    if (!hashes.has(hash)) throw new Error(`the album index names a cover the archive does not hold`);
    if (path <= previous) throw new Error('the album index is not sorted by path');
    previous = path;
    albums.set(path, hash);
    cursor += 34 + length;
  }
  if (cursor !== total) throw new Error('the album index does not end the file');
  return { covers, albums };
}

/** The pictures alone, for the callers that have no use for the index. */
export function parseCoverArchive(bytes: Uint8Array): ArchiveCover[] {
  return readCoverArchive(bytes).covers;
}

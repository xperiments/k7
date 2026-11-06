import type { Analysis, MoodTag } from './analysis';

/**
 * The device mounts the card's `music` directory as its Music directory. The
 * browser hands over paths from the card root, so the volume name in front of
 * `music` is whatever the card was formatted with and cannot be counted on.
 */
const MUSIC_DIRECTORY = 'music';

const HEADER_SIZE = 80;
const FORMAT_VERSION = 3;
const ROW_SIZE = 32;
const MAX_IMAGE_BYTES = 2 * 1024 * 1024;

/** The list record, list row, and the caps the format puts on them. */
const LIST_SIZE = 32;
const LIST_ROW_SIZE = 4;
const MAX_LISTS = 64;
const MAX_LIST_ROWS = 8192;
/** A name is written NUL-terminated, so its UTF-8 bytes must stay under 64. */
const MAX_LIST_NAME_BYTES = 63;

/** Percentages are seven bits: `0..100`, with 127 as the one "unknown". */
const UNKNOWN_PERCENT = 127;

/** A row's four offsets are 24-bit, so the pool they index ends at `0xFFFFFF`. */
const MAX_POOL_SIZE = 0xffffff;

/** The folded tempo range the format accepts, in BPM and in BPM x 10. */
const MIN_BPM = 70;
const MAX_BPM = 180;

const UINT64_MASK = 0xffffffffffffffffn;

/**
 * The path the device hashes: everything below the selection's `music`
 * directory, with `/` separators. Case and Unicode below that segment are left
 * as they arrived, because both sides hash the bytes they were given.
 *
 * The Music directory's own case is not information: a card is FAT32, where
 * `MUSIC` and `music` are one directory, and the App already says so when it
 * decides what a card is (`card/mark.ts` K1). Matching it exactly here would
 * accept such a card and then give every track on it no Snapshot path at all.
 */
export function snapshotPath(relativePath: string): string | null {
  const segments = relativePath.split('/');
  // The selection is the card root, so `music` is either the only root segment or the
  // card's child. A `music` further down is a directory inside the library, not the
  // Music directory, and is not a reason to accept the path.
  const isMusic = (segment: string | undefined) =>
    segment !== undefined && segment.toLowerCase() === MUSIC_DIRECTORY;
  const music = isMusic(segments[0]) ? 0 : isMusic(segments[1]) ? 1 : -1;
  if (music === -1) return null;

  const below = segments.slice(music + 1);
  if (below.length === 0 || below.includes('')) return null;
  return below.join('/');
}

/** FNV-1a, 64-bit, over the bytes exactly as given. */
export function fnv1a64(bytes: Uint8Array): bigint {
  const FNV_OFFSET_BASIS = 0xcbf29ce484222325n;
  const FNV_PRIME = 0x100000001b3n;

  let hash = FNV_OFFSET_BASIS;
  for (const byte of bytes) {
    hash ^= BigInt(byte);
    hash = (hash * FNV_PRIME) & UINT64_MASK;
  }
  return hash;
}

/**
 * A Playlist's initial id, as §10 describes it: FNV-1a 64 over the name and the
 * creation time, newline-joined. Compute it **once**, when the list is made, and
 * keep it: renaming must not move it, because it is what `select` and the device's
 * NVS key carry. Zero is remapped to one, since the format refuses an id of zero.
 */
export function playlistId(name: string, createdAtSeconds: number): bigint {
  const created = generationValue(createdAtSeconds);
  const id = fnv1a64(new TextEncoder().encode(`${name}\n${created}`));
  return id === 0n ? 1n : id;
}

export interface SnapshotTrack {
  /** Browser path under the card root, e.g. `MYCARD/music/Artist/track.mp3`. */
  path: string;
  /** File size in bytes; part of the binding between a row and its file. */
  size: number;
  analysis: Analysis;
  title: string;
  artist: string;
  album: string;
  genre: string;
}

export interface SnapshotInput {
  tracks: SnapshotTrack[];
  generationSeconds: number;
  analyserVersion: number;
  appSchema: number;
  modelSet: bigint;
}

/**
 * A **Playlist** to carry in the image (format §10, list record).
 *
 * `id` is opaque, 64-bit and non-zero; it is what `select` and the device's NVS
 * key carry, so renaming a list must not change it. `createdAt` is Unix seconds
 * and is informational.
 *
 * `rows` are **Snapshot paths**, in the order the list plays them: each is a
 * Track's location below the card's `music` directory (`Artist/Album/track.mp3`),
 * never the browser path it was found at. A row names a Track by that path alone —
 * there is no fingerprint and no size — so the file the Library holds at that path
 * is what plays, and a path the Library no longer has is skipped.
 */
export interface SnapshotPlaylist {
  id: bigint | string;
  /** NUL-terminated in the pool, so its UTF-8 encoding must be under 64 bytes. */
  name: string;
  createdAt: number;
  rows: readonly string[];
}

/** A list after validation and sorting: what the writer interns and emits. */
interface PreparedList {
  id: bigint;
  createdAt: bigint;
  /** The name, UTF-8, without its NUL terminator. */
  name: Uint8Array;
  /** Each row's path, UTF-8 and NUL-free, in the list's own order. */
  paths: Uint8Array[];
}

interface SnapshotRow {
  fingerprint: bigint;
  size: number;
  bpmX10: number;
  key: number;
  energy: number;
  danceability: number;
  valence: number;
  arousal: number;
  moodBits: number;
  confidence: number;
  sourceFlags: number;
  /** Title, artist, album and genre, UTF-8, in the format's order. */
  strings: readonly Uint8Array[];
}

/** Half away from zero, which differs from `Math.round` for negative values. */
function roundHalfAwayFromZero(value: number): number {
  return value < 0 ? -Math.round(-value) : Math.round(value);
}

function clampPercent(value: number): number {
  return Math.min(100, Math.max(0, value));
}

/** A 0-1 measurement as a percent. */
function unitPercent(value: number): number {
  const unit = Math.min(1, Math.max(0, value));
  return clampPercent(roundHalfAwayFromZero(unit * 100));
}

/**
 * Energy is 0-1 when measured and may already be 0-100 when it came from a
 * tag. Above 1 there is no 0-1 reading left to make, so it is a percentage.
 */
function energyPercent(value: number): number {
  return clampPercent(roundHalfAwayFromZero(value > 1 ? value : value * 100));
}

function measuredPercent(value: number | null): number {
  return value === null || !Number.isFinite(value) ? UNKNOWN_PERCENT : unitPercent(value);
}

function energyField(value: number | null): number {
  return value === null || !Number.isFinite(value) ? UNKNOWN_PERCENT : energyPercent(value);
}

/**
 * The folded BPM the analysis presents, times ten. The analysis stores BPM
 * already folded into 70-180, so a value outside that range is a bug upstream
 * rather than something this writer should fold again.
 */
function bpmField(bpm: number): number {
  if (bpm === 0) return 0;
  if (!Number.isFinite(bpm) || bpm < MIN_BPM || bpm > MAX_BPM) {
    throw new Error(`BPM ${bpm} folds outside ${MIN_BPM}-${MAX_BPM}`);
  }
  return roundHalfAwayFromZero(bpm * 10);
}

/** 1A..12A are 1..12, 1B..12B are 13..24; anything else is unknown. */
function keyField(camelot: string): number {
  const match = /^(1[0-2]|[1-9])([AB])$/.exec(camelot);
  if (!match) return 0;
  const number = Number(match[1]);
  return match[2] === 'B' ? number + 12 : number;
}

const MOOD_MASKS: ReadonlyArray<readonly [MoodTag, number]> = [
  ['happy', 0x01],
  ['sad', 0x02],
  ['aggressive', 0x04],
  ['relaxed', 0x08],
  ['party', 0x10],
];

/** `danceable` is deliberately absent: it has its own field. */
function moodBitsField(tags: Record<MoodTag, number> | null): number {
  if (!tags) return 0;
  let bits = 0;
  for (const [tag, mask] of MOOD_MASKS) {
    if (tags[tag] >= 0.5) bits |= mask;
  }
  return bits;
}

function sourceFlagsField(analysis: Analysis): number {
  let flags = 0;
  if (analysis.tempo.source === 'metadata') flags |= 0x01;
  if (analysis.key.source === 'metadata') flags |= 0x02;
  if (analysis.mood.source === 'metadata') flags |= 0x04;
  if (analysis.energySource === 'metadata') flags |= 0x08;
  if (analysis.mood.valence !== null && analysis.mood.arousal !== null) flags |= 0x10;
  if (analysis.tempo.escalated) flags |= 0x20;
  return flags;
}

/**
 * A Map key that is the bytes exactly: one character per byte, so two strings
 * that differ only where UTF-16 and UTF-8 disagree stay distinct.
 */
function byteKey(bytes: Uint8Array): string {
  let key = '';
  for (const byte of bytes) key += String.fromCharCode(byte);
  return key;
}

/** A 24-bit little-endian field: a row's four pool offsets. */
function writeUint24(bytes: Uint8Array, at: number, value: number): void {
  bytes[at] = value & 0xff;
  bytes[at + 1] = (value >>> 8) & 0xff;
  bytes[at + 2] = (value >>> 16) & 0xff;
}

/** The row's `measurements` word, packed from bit 0 as format §4 lays it out. */
function measurementWord(row: SnapshotRow): bigint {
  let word = 0n;
  word |= BigInt(row.bpmX10 === 0 ? 0 : row.bpmX10 - 699) & 0x7ffn;
  word |= (BigInt(row.key) & 0x1fn) << 11n;
  word |= (BigInt(row.energy) & 0x7fn) << 16n;
  word |= (BigInt(row.danceability) & 0x7fn) << 23n;
  word |= (BigInt(row.valence) & 0x7fn) << 30n;
  word |= (BigInt(row.arousal) & 0x7fn) << 37n;
  word |= (BigInt(row.confidence) & 0x7fn) << 44n;
  word |= (BigInt(row.moodBits) & 0x1fn) << 51n;
  word |= (BigInt(row.sourceFlags) & 0x3fn) << 56n;
  return word;
}

function generationValue(seconds: number): bigint {
  return Number.isFinite(seconds) ? BigInt(Math.trunc(seconds)) & UINT64_MASK : 0n;
}

const CRC32_TABLE = (() => {
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

/** CRC-32/IEEE, the reflected variant `zlib.crc32` produces. */
function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc = CRC32_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function toRow(track: SnapshotTrack, encoder: TextEncoder): SnapshotRow {
  const path = snapshotPath(track.path);
  if (path === null) {
    throw new Error(`"${track.path}" is not inside the music directory`);
  }

  return {
    fingerprint: fnv1a64(encoder.encode(path)),
    size: track.size,
    bpmX10: bpmField(track.analysis.tempo.bpm),
    key: keyField(track.analysis.key.camelot),
    energy: energyField(track.analysis.energy),
    danceability:
      track.analysis.mood.tags === null
        ? UNKNOWN_PERCENT
        : measuredPercent(track.analysis.mood.tags.danceable),
    valence: measuredPercent(track.analysis.mood.valence),
    arousal: measuredPercent(track.analysis.mood.arousal),
    moodBits: moodBitsField(track.analysis.mood.tags),
    confidence: measuredPercent(track.analysis.tempo.confidence),
    sourceFlags: sourceFlagsField(track.analysis),
    strings: [track.title ?? '', track.artist ?? '', track.album ?? '', track.genre ?? ''].map(
      (value) => encoder.encode(value),
    ),
  };
}

/** Compare two 64-bit ids without going through `Number`. */
function compareUint64(left: bigint, right: bigint): number {
  return left < right ? -1 : left > right ? 1 : 0;
}

/**
 * A 64-bit unsigned value, from a `bigint` or from the textual forms a caller may
 * hold it in: `0x`-prefixed hex, decimal, or up to sixteen hex digits that contain
 * a letter (so a bare string of digits is always decimal).
 */
function parseUint64(value: bigint | string, field: string): bigint {
  let parsed: bigint;
  if (typeof value === 'bigint') {
    parsed = value;
  } else {
    const text = value.trim();
    if (/^0[xX][0-9a-fA-F]{1,16}$/.test(text)) parsed = BigInt(text);
    else if (/^[0-9]{1,20}$/.test(text)) parsed = BigInt(text);
    else if (/^[0-9a-fA-F]{1,16}$/.test(text)) parsed = BigInt(`0x${text}`);
    else throw new Error(`${field} "${value}" is not a 64-bit unsigned integer`);
  }
  if (parsed < 0n || parsed > UINT64_MASK) {
    throw new Error(`${field} ${parsed} is outside the 64-bit unsigned range`);
  }
  return parsed;
}

/**
 * A Playlist row names a Track the Library has, so its path must be one the device
 * can look up: relative to the Music directory, `/`-separated, no empty, `.` or
 * `..` segment, and no backslash (which `library_find` refuses).
 */
function assertListPath(path: string, where: string): void {
  if (path.length === 0) throw new Error(`${where}: the path is empty`);
  if (path.includes('\0')) throw new Error(`${where}: the path contains a NUL byte`);
  if (path.startsWith('/')) {
    throw new Error(`${where}: "${path}" is absolute; a Playlist row is a path below music/`);
  }
  if (path.includes('\\')) {
    throw new Error(`${where}: "${path}" contains a backslash, which the Library refuses`);
  }
  for (const segment of path.split('/')) {
    if (segment === '' || segment === '.' || segment === '..') {
      throw new Error(`${where}: "${path}" has an empty, "." or ".." segment`);
    }
  }
}

/**
 * Validate and order the lists. Refusing here rather than writing a truncated or
 * out-of-order image is the format's rule (§8, §9, §10): the device checks these
 * invariants and would reject the whole image anyway.
 */
function prepareLists(
  lists: readonly SnapshotPlaylist[],
  encoder: TextEncoder,
): PreparedList[] {
  if (lists.length > MAX_LISTS) {
    throw new Error(`snapshot has ${lists.length} Playlists; the format allows ${MAX_LISTS}`);
  }

  const prepared = lists.map((list, listIndex): PreparedList => {
    const where = `Playlist ${listIndex}${list.name ? ` ("${list.name}")` : ''}`;
    const id = parseUint64(list.id, `${where}: id`);
    if (id === 0n) throw new Error(`${where}: an id of zero is not a Playlist`);

    if (list.name.includes('\0')) {
      throw new Error(`${where}: the name contains a NUL byte`);
    }
    const name = encoder.encode(list.name);
    if (name.length > MAX_LIST_NAME_BYTES) {
      throw new Error(
        `${where}: the name is ${name.length} bytes; the format allows ${MAX_LIST_NAME_BYTES}`,
      );
    }

    const paths = list.rows.map((path, rowIndex) => {
      assertListPath(path, `${where}, row ${rowIndex}`);
      return encoder.encode(path);
    });

    return { id, createdAt: generationValue(list.createdAt), name, paths };
  });

  prepared.sort((left, right) => compareUint64(left.id, right.id));

  for (let index = 1; index < prepared.length; index += 1) {
    if (prepared[index].id === prepared[index - 1].id) {
      throw new Error(
        `duplicate Playlist id ${prepared[index].id.toString(16)}: two lists share one id`,
      );
    }
  }

  const listRows = prepared.reduce((count, list) => count + list.paths.length, 0);
  if (listRows > MAX_LIST_ROWS) {
    throw new Error(`snapshot has ${listRows} Playlist rows; the format allows ${MAX_LIST_ROWS}`);
  }

  return prepared;
}

/**
 * Builds a version 3 image, with or without Playlists. Rows are sorted by fingerprint
 * and their four strings are interned by exact bytes in first-use order; the list
 * area follows and is written even when there are no lists (`list_count` 0).
 */
export function buildSnapshot(
  input: SnapshotInput,
  lists: readonly SnapshotPlaylist[] = [],
): Uint8Array<ArrayBuffer> {
  const encoder = new TextEncoder();
  const rows = input.tracks.map((track) => toRow(track, encoder));

  rows.sort((left, right) => compareUint64(left.fingerprint, right.fingerprint));

  for (let index = 1; index < rows.length; index += 1) {
    if (rows[index].fingerprint === rows[index - 1].fingerprint) {
      throw new Error(
        `duplicate fingerprint ${rows[index].fingerprint.toString(16)}: two tracks share one relative path`,
      );
    }
  }

  /* §7 and §10: one copy of each distinct string, in first-use order over the
   * fingerprint-sorted rows (title, artist, album, genre for each row), keyed by the
   * exact bytes. The row's four 3-byte offsets are final once this pass ends. */
  const poolOffsets = new Map<string, number>();
  const poolParts: Uint8Array[] = [];
  let rowPoolSize = 0;
  const rowBytes = new Uint8Array(rows.length * ROW_SIZE);
  const rowView = new DataView(rowBytes.buffer);

  rows.forEach((row, index) => {
    const at = index * ROW_SIZE;
    rowView.setBigUint64(at, row.fingerprint, true);
    rowView.setUint32(at + 8, row.size, true);

    row.strings.forEach((string, field) => {
      const key = byteKey(string);
      let offset = poolOffsets.get(key);
      if (offset === undefined) {
        offset = rowPoolSize;
        poolOffsets.set(key, offset);
        poolParts.push(string);
        rowPoolSize += string.length + 1;
        if (rowPoolSize > MAX_POOL_SIZE) {
          throw new Error(
            `the row string pool is ${rowPoolSize} bytes; 24-bit offsets end at ${MAX_POOL_SIZE}`,
          );
        }
      }
      // Row bytes 12, 15, 18 and 21: title, artist, album, genre.
      writeUint24(rowBytes, at + 12 + field * 3, offset);
    });

    rowView.setBigUint64(at + 24, measurementWord(row), true);
  });

  return assembleImage({
    generationSeconds: input.generationSeconds,
    analyserVersion: input.analyserVersion,
    appSchema: input.appSchema,
    modelSet: input.modelSet,
    rowCount: rows.length,
    rowBytes,
    rowPool: concatNulTerminated(poolParts),
    lists,
  });
}

/** Concatenate the strings, each followed by its NUL terminator. */
function concatNulTerminated(parts: readonly Uint8Array[]): Uint8Array {
  const out = new Uint8Array(parts.reduce((n, part) => n + part.length + 1, 0));
  let at = 0;
  for (const part of parts) {
    out.set(part, at);
    // The terminator is the zero the array was allocated with.
    at += part.length + 1;
  }
  return out;
}

interface Assembly {
  generationSeconds: number;
  analyserVersion: number;
  appSchema: number;
  modelSet: bigint;
  rowCount: number;
  /** `rowCount * 32` bytes, with the four 3-byte offsets already in place. */
  rowBytes: Uint8Array;
  /** `[0, row_pool_size)`: every row's interned strings, without list strings. */
  rowPool: Uint8Array;
  lists: readonly SnapshotPlaylist[];
}

/**
 * Write a version 3 image from parts that are already encoded. `buildSnapshot` uses
 * it after measuring, and `patchListArea` uses it to give an existing image new
 * lists without disturbing its rows: the list area lives at the end, so everything
 * before it is written verbatim and only the sizes and the CRC change.
 */
export function assembleImage(assembly: Assembly): Uint8Array<ArrayBuffer> {
  const encoder = new TextEncoder();
  const prepared = prepareLists(assembly.lists, encoder);

  if (assembly.rowBytes.length !== assembly.rowCount * ROW_SIZE) {
    throw new Error(
      `the rows are ${assembly.rowBytes.length} bytes; ${assembly.rowCount} rows are ${assembly.rowCount * ROW_SIZE}`,
    );
  }

  /* §7: the list strings follow the row pool and are interned among themselves —
   * never against a row string, which occupies the region before them. */
  const rowPoolSize = assembly.rowPool.length;
  const listPoolOffsets = new Map<string, number>();
  const listPoolParts: Uint8Array[] = [];
  let listPoolSize = 0;
  const listRecords = prepared.map((list) => {
    const intern = (bytes: Uint8Array): number => {
      const key = byteKey(bytes);
      const existing = listPoolOffsets.get(key);
      if (existing !== undefined) return existing;
      const offset = rowPoolSize + listPoolSize;
      listPoolOffsets.set(key, offset);
      listPoolParts.push(bytes);
      listPoolSize += bytes.length + 1;
      return offset;
    };
    return { nameOffset: intern(list.name), pathOffsets: list.paths.map(intern) };
  });

  const poolSize = rowPoolSize + listPoolSize;
  const listRowCount = prepared.reduce((count, list) => count + list.paths.length, 0);
  const totalSize =
    HEADER_SIZE +
    assembly.rowCount * ROW_SIZE +
    poolSize +
    prepared.length * LIST_SIZE +
    listRowCount * LIST_ROW_SIZE;
  if (totalSize > MAX_IMAGE_BYTES) {
    throw new Error(`snapshot is ${totalSize} bytes, larger than ${MAX_IMAGE_BYTES}`);
  }

  const image = new Uint8Array(totalSize);
  const view = new DataView(image.buffer);
  const poolStart = HEADER_SIZE + assembly.rowCount * ROW_SIZE;
  const listsStart = poolStart + poolSize;
  const listRowsStart = listsStart + prepared.length * LIST_SIZE;

  /* The magic field is eight bytes (format spec §3): the six-letter name and two NULs.
   * Written out in full so the device's eight-byte compare sees exactly one value. */
  const magic = 'K7SNAP\0\0';
  for (let index = 0; index < magic.length; index += 1) {
    view.setUint8(index, magic.charCodeAt(index));
  }
  view.setUint16(8, FORMAT_VERSION, true);
  view.setUint16(10, HEADER_SIZE, true);
  view.setUint32(12, totalSize, true);
  view.setUint32(16, assembly.rowCount, true);
  view.setUint32(20, ROW_SIZE, true);
  view.setBigUint64(24, generationValue(assembly.generationSeconds), true);
  view.setUint32(32, poolSize, true);
  view.setUint32(40, assembly.analyserVersion, true);
  view.setUint32(44, assembly.appSchema, true);
  view.setBigUint64(48, assembly.modelSet & UINT64_MASK, true);
  // `reserved` at 56..64 stays zero; the buffer starts zeroed and is never
  // written there.

  // §3, bytes 64..80: the list counts and `row_pool_size`.
  view.setUint16(64, prepared.length, true);
  view.setUint16(66, LIST_SIZE, true);
  view.setUint16(68, LIST_ROW_SIZE, true);
  // `reserved` at 70..72 stays zero.
  view.setUint32(72, listRowCount, true);
  view.setUint32(76, rowPoolSize, true);

  image.set(assembly.rowBytes, HEADER_SIZE);
  image.set(assembly.rowPool, poolStart);
  image.set(concatNulTerminated(listPoolParts), poolStart + rowPoolSize);

  let listRowStart = 0;
  for (let index = 0; index < prepared.length; index += 1) {
    const list = prepared[index];
    const record = listsStart + index * LIST_SIZE;

    view.setBigUint64(record, list.id, true);
    view.setUint32(record + 8, listRecords[index].nameOffset, true);
    view.setUint32(record + 12, listRowStart, true);
    view.setUint32(record + 16, list.paths.length, true);
    view.setBigUint64(record + 20, list.createdAt, true);
    // `reserved` at 28..32 stays zero.

    for (let rowIndex = 0; rowIndex < list.paths.length; rowIndex += 1) {
      const at = listRowsStart + (listRowStart + rowIndex) * LIST_ROW_SIZE;
      view.setUint32(at, listRecords[index].pathOffsets[rowIndex], true);
    }

    listRowStart += list.paths.length;
  }

  // The CRC covers everything after the header, never the header that carries it.
  view.setUint32(36, crc32(image.subarray(HEADER_SIZE, totalSize)), true);
  return image;
}

/** What an image says about one track: the numbers the player answers with. */
export interface ImageMeasurement {
  bpm: number | null;
  key: string | null;
}

/** The key an image row is found by: the path's fingerprint and the file's size. */
export function imageRowKey(path: string, size: number): string {
  return `${fnv1a64(new TextEncoder().encode(path)).toString(16)}:${size}`;
}

/** The key a row's number names, written the way the player writes it: `8A`, `12B`. */
function camelotOf(key: number): string | null {
  if (key < 1 || key > 24) return null;
  return `${((key - 1) % 12) + 1}${key > 12 ? 'B' : 'A'}`;
}

/**
 * Reads the measurements back out of an image this browser wrote.
 *
 * The album page asks the device for a track's numbers, and the device answers from the image
 * it last applied — so with the card in the computer there is no answer at all, even though
 * this browser wrote the image the card holds. The rows are on disk here, and this turns them
 * back into the same two numbers, keyed exactly as the device keys them: the path's
 * fingerprint, the file's size, and nothing else.
 *
 * It refuses what the player refuses: a wrong magic, a version it does not know, a header of
 * the wrong size, a length that is not the one stated.
 */
export function readImageMeasurements(image: Uint8Array): Map<string, ImageMeasurement> {
  const out = new Map<string, ImageMeasurement>();
  if (image.length < HEADER_SIZE) throw new Error('the image is shorter than its header');

  const magic = String.fromCharCode(...image.subarray(0, 8));
  if (magic !== 'K7SNAP\0\0') throw new Error('this is not a Snapshot');

  const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
  const version = view.getUint16(8, true);
  if (version !== FORMAT_VERSION) throw new Error(`unknown format version ${version}`);
  const headerSize = view.getUint16(10, true);
  if (headerSize !== HEADER_SIZE) {
    throw new Error(`a version ${version} image has a ${headerSize}-byte header`);
  }
  if (view.getUint32(12, true) !== image.length) throw new Error('the image is not its stated length');
  if (view.getUint16(20, true) !== ROW_SIZE) throw new Error('unexpected row size');

  const rowCount = view.getUint32(16, true);
  const poolSize = view.getUint32(32, true);
  if (headerSize + rowCount * ROW_SIZE + poolSize > image.length) {
    throw new Error('the rows and string pool are out of range');
  }

  for (let index = 0; index < rowCount; index += 1) {
    const at = headerSize + index * ROW_SIZE;
    const fingerprint = view.getBigUint64(at, true);
    const size = view.getUint32(at + 8, true);
    const word = view.getBigUint64(at + 24, true);
    const bpmCode = Number(word & 0x7ffn);
    const key = Number((word >> 11n) & 0x1fn);
    out.set(`${fingerprint.toString(16)}:${size}`, {
      bpm: bpmCode === 0 ? null : (bpmCode + 699) / 10,
      key: camelotOf(key),
    });
  }
  return out;
}

/**
 * The producer's identity for a model set: FNV-1a 64 over the sorted `name:sha256`
 * descriptors, newline-joined. The device reports it in `library_stats` and never
 * interprets it, so it only has to be stable for a given set of weights.
 */
export function modelSetId(models: readonly { name: string; sha256: string }[]): bigint {
  const text = models
    .map((model) => `${model.name}:${model.sha256}`)
    .sort()
    .join('\n');
  return fnv1a64(new TextEncoder().encode(text));
}

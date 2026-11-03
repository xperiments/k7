import type { Analysis, MoodTag } from './analysis';

/**
 * The device mounts the card's `music` directory as its Music directory. The
 * browser hands over paths from the card root, so the volume name in front of
 * `music` is whatever the card was formatted with and cannot be counted on.
 */
const MUSIC_DIRECTORY = 'music';

const HEADER_SIZE_V1 = 64;
const HEADER_SIZE_V2 = 80;
const ROW_SIZE = 32;
const FORMAT_VERSION_V1 = 1;
const FORMAT_VERSION_V2 = 2;
const MAX_IMAGE_BYTES = 2 * 1024 * 1024;

/** Version 2's list record, list row, and the caps the format puts on them. */
const LIST_SIZE = 32;
const LIST_ROW_SIZE = 4;
const MAX_LISTS = 64;
const MAX_LIST_ROWS = 8192;
/** A name is written NUL-terminated, so its UTF-8 bytes must stay under 64. */
const MAX_LIST_NAME_BYTES = 63;

const UNKNOWN_PERCENT = 0xff;

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

/** A list after validation, sorting and pool encoding: what the writer emits. */
interface PreparedList {
  id: bigint;
  createdAt: bigint;
  /** `name\0path\0path\0...`, UTF-8, Tracks in the list's own order. */
  strings: Uint8Array;
  /** Each row's `path_offset`, relative to the start of this list's block. */
  pathOffsets: number[];
  rowCount: number;
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
  /** `title\0artist\0album\0genre\0`, already UTF-8. */
  strings: Uint8Array;
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

function encodeStrings(encoder: TextEncoder, values: readonly string[]): Uint8Array {
  const parts = values.map((value) => encoder.encode(value));
  const bytes = new Uint8Array(parts.reduce((length, part) => length + part.length + 1, 0));
  let offset = 0;
  for (const part of parts) {
    bytes.set(part, offset);
    // The terminator is the zero the array was allocated with.
    offset += part.length + 1;
  }
  return bytes;
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
    strings: encodeStrings(encoder, [
      track.title ?? '',
      track.artist ?? '',
      track.album ?? '',
      track.genre ?? '',
    ]),
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

/** Concatenate already-encoded strings, each followed by its NUL terminator. */
function joinNulTerminated(parts: readonly Uint8Array[]): Uint8Array {
  const bytes = new Uint8Array(
    parts.reduce((length, part) => length + part.length + 1, 0),
  );
  let offset = 0;
  for (const part of parts) {
    bytes.set(part, offset);
    // The terminator is the zero the array was allocated with.
    offset += part.length + 1;
  }
  return bytes;
}

/**
 * Validate, order and encode the lists. Refusing here rather than writing a
 * truncated or out-of-order image is the format's rule (§10, §12): the device
 * checks these invariants and would reject the whole image anyway.
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
    const nameBytes = encoder.encode(list.name);
    if (nameBytes.length > MAX_LIST_NAME_BYTES) {
      throw new Error(
        `${where}: the name is ${nameBytes.length} bytes; the format allows ${MAX_LIST_NAME_BYTES}`,
      );
    }

    const parts: Uint8Array[] = [nameBytes];
    const pathOffsets: number[] = [];
    // The name comes first in the block, so the first path starts after it.
    let offset = nameBytes.length + 1;

    list.rows.forEach((path, rowIndex) => {
      assertListPath(path, `${where}, row ${rowIndex}`);
      const pathBytes = encoder.encode(path);
      parts.push(pathBytes);
      pathOffsets.push(offset);
      offset += pathBytes.length + 1;
    });

    return {
      id,
      createdAt: generationValue(list.createdAt),
      strings: joinNulTerminated(parts),
      pathOffsets,
      rowCount: list.rows.length,
    };
  });

  prepared.sort((left, right) => compareUint64(left.id, right.id));

  for (let index = 1; index < prepared.length; index += 1) {
    if (prepared[index].id === prepared[index - 1].id) {
      throw new Error(
        `duplicate Playlist id ${prepared[index].id.toString(16)}: two lists share one id`,
      );
    }
  }

  const listRows = prepared.reduce((count, list) => count + list.rowCount, 0);
  if (listRows > MAX_LIST_ROWS) {
    throw new Error(`snapshot has ${listRows} Playlist rows; the format allows ${MAX_LIST_ROWS}`);
  }

  return prepared;
}

/**
 * Builds the image. `lists` is the second argument rather than a field of
 * `SnapshotInput` so that a caller with no Playlists keeps the old call and the old
 * bytes: with none, the output is byte-identical version 1 (`header_size` 64, no list
 * area); with at least one, it is version 2 (`header_size` 80, §2 of the format).
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

  return assembleImage({
    generationSeconds: input.generationSeconds,
    analyserVersion: input.analyserVersion,
    appSchema: input.appSchema,
    modelSet: input.modelSet,
    rowCount: rows.length,
    rowStringOffsets: (() => {
      const offsets: number[] = [];
      let at = 0;
      for (const row of rows) {
        offsets.push(at);
        at += row.strings.length;
      }
      return offsets;
    })(),
    rowBytes: rows.length
      ? (() => {
          const bytes = new Uint8Array(rows.length * ROW_SIZE);
          const rowView = new DataView(bytes.buffer);
          rows.forEach((row, index) => {
            const at = index * ROW_SIZE;
            rowView.setBigUint64(at, row.fingerprint, true);
            rowView.setUint32(at + 8, row.size, true);
            rowView.setUint16(at + 16, row.bpmX10, true);
            rowView.setUint8(at + 18, row.key);
            rowView.setUint8(at + 19, row.energy);
            rowView.setUint8(at + 20, row.danceability);
            rowView.setUint8(at + 21, row.valence);
            rowView.setUint8(at + 22, row.arousal);
            rowView.setUint8(at + 23, row.moodBits);
            rowView.setUint8(at + 24, row.confidence);
            rowView.setUint8(at + 25, row.sourceFlags);
          });
          return bytes;
        })()
      : new Uint8Array(0),
    rowPool: rows.length ? concatBytes(rows.map((row) => row.strings)) : new Uint8Array(0),
    lists,
  });
}

/** The rows' strings, in order, as one buffer. */
function concatBytes(parts: readonly Uint8Array[]): Uint8Array {
  const out = new Uint8Array(parts.reduce((n, part) => n + part.length, 0));
  let at = 0;
  for (const part of parts) {
    out.set(part, at);
    at += part.length;
  }
  return out;
}

interface Assembly {
  generationSeconds: number;
  analyserVersion: number;
  appSchema: number;
  modelSet: bigint;
  /** `rowCount * 32` bytes, with `string_offset` left for the writer to fill in. */
  rowBytes: Uint8Array;
  /** Each row's `string_offset`: its strings' position in `rowPool`. */
  rowStringOffsets: readonly number[];
  rowCount: number;
  /** Every row's four strings, concatenated in row order. */
  rowPool: Uint8Array;
  lists: readonly SnapshotPlaylist[];
}

/**
 * Write an image from parts that are already encoded. `buildSnapshot` uses it after
 * measuring, and `patchListArea` uses it to give an existing image new lists without
 * disturbing its rows: the list area lives at the end, so everything before it can be
 * copied verbatim and only the header's sizes and the CRC change.
 */
export function assembleImage(assembly: Assembly): Uint8Array<ArrayBuffer> {
  const encoder = new TextEncoder();
  const prepared = prepareLists(assembly.lists, encoder);
  const version = prepared.length > 0 ? FORMAT_VERSION_V2 : FORMAT_VERSION_V1;
  const headerSize = prepared.length > 0 ? HEADER_SIZE_V2 : HEADER_SIZE_V1;

  const listsPoolSize = prepared.reduce((size, list) => size + list.strings.length, 0);
  const poolSize = assembly.rowPool.length + listsPoolSize;
  const listRowCount = prepared.reduce((count, list) => count + list.rowCount, 0);
  const totalSize =
    headerSize +
    assembly.rowCount * ROW_SIZE +
    poolSize +
    prepared.length * LIST_SIZE +
    listRowCount * LIST_ROW_SIZE;
  if (totalSize > MAX_IMAGE_BYTES) {
    throw new Error(`snapshot is ${totalSize} bytes, larger than ${MAX_IMAGE_BYTES}`);
  }

  const image = new Uint8Array(totalSize);
  const view = new DataView(image.buffer);
  const poolStart = headerSize + assembly.rowCount * ROW_SIZE;
  const listsStart = poolStart + poolSize;
  const listRowsStart = listsStart + prepared.length * LIST_SIZE;

  /* The magic field is eight bytes (format spec §3): the six-letter name and two NULs.
   * Written out in full so the device's eight-byte compare sees exactly one value. */
  const magic = 'K7SNAP\0\0';
  for (let index = 0; index < magic.length; index += 1) {
    view.setUint8(index, magic.charCodeAt(index));
  }
  view.setUint16(8, version, true);
  view.setUint16(10, headerSize, true);
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

  if (version === FORMAT_VERSION_V2) {
    // §3.1: the list counts, then `reserved` at 70..72 and 76..80, both zero.
    view.setUint16(64, prepared.length, true);
    view.setUint16(66, LIST_SIZE, true);
    view.setUint16(68, LIST_ROW_SIZE, true);
    view.setUint32(72, listRowCount, true);
  }

  image.set(assembly.rowBytes, headerSize);
  assembly.rowStringOffsets.forEach((offset, index) => {
    view.setUint32(headerSize + index * ROW_SIZE + 12, offset, true);
  });
  image.set(assembly.rowPool, poolStart);

  // §7: the pool continues with each list's name and its rows' paths, in list order.
  let listPoolOffset = assembly.rowPool.length;
  let listRowStart = 0;
  for (let index = 0; index < prepared.length; index += 1) {
    const list = prepared[index];
    const record = listsStart + index * LIST_SIZE;

    view.setBigUint64(record, list.id, true);
    view.setUint32(record + 8, listPoolOffset, true);
    view.setUint32(record + 12, listRowStart, true);
    view.setUint32(record + 16, list.rowCount, true);
    view.setBigUint64(record + 20, list.createdAt, true);
    // `reserved` at 28..32 stays zero.

    image.set(list.strings, poolStart + listPoolOffset);

    for (let rowIndex = 0; rowIndex < list.rowCount; rowIndex += 1) {
      const at = listRowsStart + (listRowStart + rowIndex) * LIST_ROW_SIZE;
      view.setUint32(at, listPoolOffset + list.pathOffsets[rowIndex], true);
    }

    listRowStart += list.rowCount;
    listPoolOffset += list.strings.length;
  }

  // The CRC covers everything after the header, never the header that carries it.
  view.setUint32(36, crc32(image.subarray(headerSize, totalSize)), true);
  return image;
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

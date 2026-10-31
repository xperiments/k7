import type { Analysis, MoodTag } from './analysis';

/**
 * The device mounts the card's `music` directory as its Music directory. The
 * browser hands over paths from the card root, so the volume name in front of
 * `music` is whatever the card was formatted with and cannot be counted on.
 */
const MUSIC_DIRECTORY = 'music';

const HEADER_SIZE = 64;
const ROW_SIZE = 32;
const FORMAT_VERSION = 1;
const MAX_IMAGE_BYTES = 2 * 1024 * 1024;

const UNKNOWN_PERCENT = 0xff;

/** The folded tempo range the format accepts, in BPM and in BPM x 10. */
const MIN_BPM = 70;
const MAX_BPM = 180;

const UINT64_MASK = 0xffffffffffffffffn;

/**
 * The path the device hashes: everything below the selection's `music`
 * directory, with `/` separators. Case and Unicode are left as they arrived,
 * because both sides hash the bytes they were given.
 */
export function snapshotPath(relativePath: string): string | null {
  const segments = relativePath.split('/');
  // The selection is the card root, so `music` is either the only root segment or the
  // card's child. A `music` further down is a directory inside the library, not the
  // Music directory, and is not a reason to accept the path.
  const music = segments[0] === MUSIC_DIRECTORY ? 0 : segments[1] === MUSIC_DIRECTORY ? 1 : -1;
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

export function buildSnapshot(input: SnapshotInput): Uint8Array<ArrayBuffer> {
  const encoder = new TextEncoder();
  const rows = input.tracks.map((track) => toRow(track, encoder));

  rows.sort((left, right) =>
    left.fingerprint < right.fingerprint ? -1 : left.fingerprint > right.fingerprint ? 1 : 0,
  );

  for (let index = 1; index < rows.length; index += 1) {
    if (rows[index].fingerprint === rows[index - 1].fingerprint) {
      throw new Error(
        `duplicate fingerprint ${rows[index].fingerprint.toString(16)}: two tracks share one relative path`,
      );
    }
  }

  const poolSize = rows.reduce((size, row) => size + row.strings.length, 0);
  const totalSize = HEADER_SIZE + rows.length * ROW_SIZE + poolSize;
  if (totalSize > MAX_IMAGE_BYTES) {
    throw new Error(`snapshot is ${totalSize} bytes, larger than ${MAX_IMAGE_BYTES}`);
  }

  const image = new Uint8Array(totalSize);
  const view = new DataView(image.buffer);
  const poolStart = HEADER_SIZE + rows.length * ROW_SIZE;

  const magic = 'XTAPESNP';
  for (let index = 0; index < magic.length; index += 1) {
    view.setUint8(index, magic.charCodeAt(index));
  }
  view.setUint16(8, FORMAT_VERSION, true);
  view.setUint16(10, HEADER_SIZE, true);
  view.setUint32(12, totalSize, true);
  view.setUint32(16, rows.length, true);
  view.setUint32(20, ROW_SIZE, true);
  view.setBigUint64(24, generationValue(input.generationSeconds), true);
  view.setUint32(32, poolSize, true);
  view.setUint32(40, input.analyserVersion, true);
  view.setUint32(44, input.appSchema, true);
  view.setBigUint64(48, input.modelSet & UINT64_MASK, true);
  // `reserved` at 56..64 stays zero; the buffer starts zeroed and is never
  // written there.

  let poolOffset = 0;
  for (let index = 0; index < rows.length; index += 1) {
    const row = rows[index];
    const offset = HEADER_SIZE + index * ROW_SIZE;

    view.setBigUint64(offset, row.fingerprint, true);
    view.setUint32(offset + 8, row.size, true);
    view.setUint32(offset + 12, poolOffset, true);
    view.setUint16(offset + 16, row.bpmX10, true);
    view.setUint8(offset + 18, row.key);
    view.setUint8(offset + 19, row.energy);
    view.setUint8(offset + 20, row.danceability);
    view.setUint8(offset + 21, row.valence);
    view.setUint8(offset + 22, row.arousal);
    view.setUint8(offset + 23, row.moodBits);
    view.setUint8(offset + 24, row.confidence);
    view.setUint8(offset + 25, row.sourceFlags);
    // `reserved` at 26..32 stays zero for the same reason as the header's.

    image.set(row.strings, poolStart + poolOffset);
    poolOffset += row.strings.length;
  }

  // The CRC covers the rows and the pool, never the header that carries it.
  view.setUint32(36, crc32(image.subarray(HEADER_SIZE, totalSize)), true);
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

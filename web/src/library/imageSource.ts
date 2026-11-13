import {
  McpError,
  type McpAnalysis,
  type McpSearch,
  type McpTrack,
  type SearchArgs,
} from '../device/mcp';
import { fnv1a64, type AxisProfile, type LibraryProfile } from '../domain/snapshot';
import type { CachedMedia } from '../storage/media';

/**
 * The **Last image** as a Library source: the rows of `k7.db` joined to the browser's media
 * index by the Snapshot path's fingerprint. It answers `library_search` with the same filters
 * and sort as the device's `snapshot_matches` (components/portable/snapshot/snapshot.c), so a
 * virtual card, or a card the player is not holding, can be searched without the player.
 *
 * The image stores no paths: the join's other half is `CachedMedia`, keyed by path. A media
 * row with no image row is a Track the image never measured; it matches a name search on its
 * path and no range, exactly as the device's null row does.
 */

const HEADER_SIZE = 80;
const ROW_SIZE = 32;
const MAGIC = 'K7SNAP\0\0';
const UNKNOWN_PCT = 127;
const DEFAULT_LIMIT = 50;

export interface ImageRow {
  fingerprint: bigint;
  size: number;
  /** Folded BPM × 10; 0 unknown. */
  bpmX10: number;
  /** 0 unknown, otherwise 1..24 (1A..12A, 1B..12B). */
  key: number;
  energy: number;
  danceability: number;
  valence: number;
  arousal: number;
  confidence: number;
  moodBits: number;
  sourceFlags: number;
  title: string;
  artist: string;
  album: string;
  genre: string;
}

/** The rows of a version 3 image, or an `McpError` when the bytes are not one. */
export function readImageRows(bytes: Uint8Array): ImageRow[] {
  if (bytes.length < HEADER_SIZE) throw new McpError('the image is shorter than its header');
  const magic = String.fromCharCode(...bytes.subarray(0, 8));
  if (magic !== MAGIC) throw new McpError('this is not a Snapshot');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint16(8, true) !== 3) throw new McpError('this Snapshot is not format 3');
  const rowCount = view.getUint32(16, true);
  if (view.getUint16(20, true) !== ROW_SIZE) throw new McpError('unexpected row size');
  const poolSize = view.getUint32(32, true);
  const poolAt = HEADER_SIZE + rowCount * ROW_SIZE;
  if (poolAt + poolSize > bytes.length) throw new McpError('the image is shorter than its rows');

  const pool = bytes.subarray(poolAt, poolAt + poolSize);
  const decoder = new TextDecoder();
  const stringAt = (offset: number): string => {
    if (offset >= pool.length) return '';
    let end = offset;
    while (end < pool.length && pool[end] !== 0) end += 1;
    return decoder.decode(pool.subarray(offset, end));
  };
  const offset3 = (at: number): number => bytes[at] | (bytes[at + 1] << 8) | (bytes[at + 2] << 16);
  const bits = (word: bigint, shift: bigint, mask: bigint): number =>
    Number((word >> shift) & mask);

  const rows: ImageRow[] = [];
  for (let index = 0; index < rowCount; index += 1) {
    const at = HEADER_SIZE + index * ROW_SIZE;
    const word = view.getBigUint64(at + 24, true);
    const bpmCode = bits(word, 0n, 0x7ffn);
    rows.push({
      fingerprint: view.getBigUint64(at, true),
      size: view.getUint32(at + 8, true),
      bpmX10: bpmCode === 0 ? 0 : bpmCode + 699,
      key: bits(word, 11n, 0x1fn),
      energy: bits(word, 16n, 0x7fn),
      danceability: bits(word, 23n, 0x7fn),
      valence: bits(word, 30n, 0x7fn),
      arousal: bits(word, 37n, 0x7fn),
      confidence: bits(word, 44n, 0x7fn),
      moodBits: bits(word, 51n, 0x1fn),
      sourceFlags: bits(word, 56n, 0x3fn),
      title: stringAt(offset3(at + 12)),
      artist: stringAt(offset3(at + 15)),
      album: stringAt(offset3(at + 18)),
      genre: stringAt(offset3(at + 21)),
    });
  }
  return rows;
}

/** Pitch class to Camelot number, the device's two twelve-entry wheels. */
const MINOR_WHEEL = [5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10];
const MAJOR_WHEEL = [8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1];
const NOTE_PC: Record<string, number> = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };

/**
 * The device's key parser (`snapshot_key_parse`): a Camelot code or a note name, 0 when
 * neither. Kept word for word so the image source accepts exactly what the device accepts.
 */
export function keyCode(text: string): number {
  const value = text.trim();
  const camelot = /^(\d{1,2})([AB])$/i.exec(value);
  if (camelot) {
    const number = Number(camelot[1]);
    if (number < 1 || number > 12) return 0;
    return number + (camelot[2].toUpperCase() === 'B' ? 12 : 0);
  }

  const note = /^([A-Ga-g])([#b]?)\s*(.*)$/.exec(value);
  if (!note) return 0;
  let pc = NOTE_PC[note[1].toUpperCase()];
  if (pc === undefined) return 0;
  if (note[2] === '#') pc = (pc + 1) % 12;
  else if (note[2] === 'b') pc = (pc + 11) % 12;

  const mode = note[3].toLowerCase();
  if (mode === '') return MAJOR_WHEEL[pc] + 12;
  if (mode === 'm' || mode === 'mi' || mode === 'min' || mode === 'mino' || mode === 'minor') {
    return MINOR_WHEEL[pc];
  }
  if (mode === 'ma' || mode === 'maj' || mode === 'majo' || mode === 'major') {
    return MAJOR_WHEEL[pc] + 12;
  }
  return 0;
}

/** A Camelot number as the code the device prints: 1..12 A, 13..24 B. */
export function camelotName(code: number): string | null {
  if (code < 1 || code > 24) return null;
  return `${((code - 1) % 12) + 1}${code > 12 ? 'B' : 'A'}`;
}

const MOOD_NAMES = ['happy', 'sad', 'aggressive', 'relaxed', 'party'] as const;
const PROFILE_GENRE_CAP = 40;

function axisOf(values: readonly number[]): AxisProfile | null {
  if (values.length === 0) return null;
  const sorted = [...values].sort((a, b) => a - b);
  const at = (q: number) => sorted[Math.round(q * (sorted.length - 1))];
  return { min: sorted[0], p25: at(0.25), p50: at(0.5), p75: at(0.75), max: sorted[sorted.length - 1] };
}

/**
 * A Library's aggregates straight from the image's rows. The stored profile is preferred, but
 * an image written before profiles existed still answers Ask: the rows carry every value the
 * profile needs, and no track, artist or path is involved beyond the aggregate.
 */
export function profileFromRows(rows: readonly ImageRow[]): LibraryProfile {
  const genres = new Map<string, { name: string; count: number }>();
  const keys = new Set<string>();
  const moods = new Set<string>();
  const bpm: number[] = [];
  const energy: number[] = [];
  const danceability: number[] = [];
  const valence: number[] = [];
  const arousal: number[] = [];

  for (const row of rows) {
    const genre = row.genre.trim();
    if (genre) {
      const key = genre.toLowerCase();
      const entry = genres.get(key);
      if (entry) entry.count += 1;
      else genres.set(key, { name: genre, count: 1 });
    }
    const key = camelotName(row.key);
    if (key) keys.add(key);
    for (let index = 0; index < MOOD_NAMES.length; index += 1) {
      if ((row.moodBits & (1 << index)) !== 0) moods.add(MOOD_NAMES[index]);
    }
    if (row.bpmX10) bpm.push(row.bpmX10 / 10);
    if (row.energy !== UNKNOWN_PCT) energy.push(row.energy);
    if (row.danceability !== UNKNOWN_PCT) danceability.push(row.danceability);
    if (row.valence !== UNKNOWN_PCT) valence.push(row.valence);
    if (row.arousal !== UNKNOWN_PCT) arousal.push(row.arousal);
  }

  return {
    tracks: rows.length,
    genres: [...genres.values()]
      .sort((a, b) => b.count - a.count || a.name.localeCompare(b.name))
      .slice(0, PROFILE_GENRE_CAP)
      .map((entry) => entry.name),
    keys: [...keys].sort((a, b) => {
      const order = (code: string) => Number(code.slice(0, -1)) + (code.endsWith('B') ? 12 : 0);
      return order(a) - order(b);
    }),
    moods: MOOD_NAMES.filter((mood) => moods.has(mood)),
    bpm: axisOf(bpm),
    energy: axisOf(energy),
    danceability: axisOf(danceability),
    valence: axisOf(valence),
    arousal: axisOf(arousal),
  };
}
const TAGGED_NAMES = ['bpm', 'key', 'mood', 'energy'] as const;

function analysisOf(row: ImageRow): McpAnalysis {
  const percent = (value: number) => (value === UNKNOWN_PCT ? null : value);
  return {
    bpm: row.bpmX10 ? row.bpmX10 / 10 : null,
    key: camelotName(row.key),
    energy: percent(row.energy),
    danceability: percent(row.danceability),
    valence: percent(row.valence),
    arousal: percent(row.arousal),
    mood: MOOD_NAMES.filter((_, index) => (row.moodBits & (1 << index)) !== 0),
    confidence: percent(row.confidence),
    tagged: TAGGED_NAMES.filter((_, index) => (row.sourceFlags & (1 << index)) !== 0),
    title: row.title || null,
    artist: row.artist || null,
    album: row.album || null,
    genre: row.genre || null,
  };
}

interface Joined {
  media: CachedMedia;
  row: ImageRow | null;
}

function titleOf(joined: Joined): string {
  return joined.row?.title ?? '';
}

function sortValue(joined: Joined, field: 'bpmX10' | 'energy' | 'danceability'): number {
  if (!joined.row) return -1;
  const value = joined.row[field];
  if (value === UNKNOWN_PCT) return -1;
  return value;
}

function passesRange(
  row: ImageRow | null,
  value: number | undefined,
  min: number | undefined,
  max: number | undefined,
): boolean {
  const bounded = min !== undefined || max !== undefined;
  if (!bounded) return true;
  if (!row || value === undefined || value === UNKNOWN_PCT) return false;
  if (min !== undefined && value < min) return false;
  if (max !== undefined && value > max) return false;
  return true;
}

function compare(a: Joined, b: Joined, sort: SearchArgs['sort']): number {
  switch (sort) {
    case 'title': {
      const at = titleOf(a);
      const bt = titleOf(b);
      if (!at && !bt) return a.media.path < b.media.path ? -1 : 1;
      if (!at) return 1;
      if (!bt) return -1;
      const order = at.localeCompare(bt);
      return order !== 0 ? order : a.media.path < b.media.path ? -1 : 1;
    }
    case 'bpm':
    case 'energy':
    case 'danceability': {
      const field = sort === 'bpm' ? 'bpmX10' : sort;
      const order = sortValue(a, field) - sortValue(b, field);
      return order !== 0 ? order : a.media.path < b.media.path ? -1 : 1;
    }
    case 'path':
    default:
      return a.media.path < b.media.path ? -1 : a.media.path > b.media.path ? 1 : 0;
  }
}

function trackOf(joined: Joined): McpTrack {
  const name = joined.media.path.split('/').pop() ?? joined.media.path;
  const lower = name.toLowerCase();
  return {
    name,
    path: joined.media.path,
    size: joined.row?.size ?? joined.media.size ?? 0,
    format: lower.endsWith('.flac') ? 'flac' : lower.endsWith('.mp3') ? 'mp3' : '',
    analysis: joined.row ? analysisOf(joined.row) : null,
  };
}

/** One `library_search` over the image's rows and the media index. Synchronous and pure. */
export function searchImage(
  rows: readonly ImageRow[],
  media: readonly CachedMedia[],
  args: SearchArgs = {},
): McpSearch {
  const byFingerprint = new Map<string, ImageRow>();
  for (const row of rows) byFingerprint.set(row.fingerprint.toString(), row);

  const query = args.query?.trim().toLowerCase() ?? '';
  const genre = args.genre?.trim().toLowerCase() ?? '';

  const keys = args.key && args.key.length > 0 ? args.key.map(keyCode) : [];
  const keyMask = keys.reduce((mask, code) => mask | (1 << (code - 1)), 0);
  if (keys.some((code) => code === 0)) {
    throw new McpError(
      'key must be a list of Camelot codes or note names; mood a list of happy, sad, aggressive, relaxed, party',
    );
  }

  const moods = (args.mood ?? []).map((mood) => MOOD_NAMES.indexOf(mood.toLowerCase() as never));
  const moodMask = moods.reduce((mask, index) => mask | (1 << index), 0);
  if (moods.some((index) => index < 0)) {
    throw new McpError(
      'key must be a list of Camelot codes or note names; mood a list of happy, sad, aggressive, relaxed, party',
    );
  }

  const bpmMin = args.bpmMin !== undefined ? Math.round(args.bpmMin * 10) : 0;
  const bpmMax = args.bpmMax !== undefined ? Math.round(args.bpmMax * 10) : 0;

  const matched: Joined[] = [];
  const encoder = new TextEncoder();
  for (const mediaRow of media) {
    let row = byFingerprint.get(fnv1a64(encoder.encode(mediaRow.path)).toString()) ?? null;
    if (row && mediaRow.size !== undefined && row.size !== mediaRow.size) row = null;

    if (query) {
      const haystack = row
        ? [mediaRow.path, row.title, row.artist, row.album, row.genre]
        : [mediaRow.path];
      if (!haystack.some((value) => value.toLowerCase().includes(query))) continue;
    }
    if (genre) {
      if (!row || !row.genre.toLowerCase().includes(genre)) continue;
    }
    if (bpmMin || bpmMax) {
      if (!row || row.bpmX10 === 0) continue;
      if (bpmMin && row.bpmX10 < bpmMin) continue;
      if (bpmMax && row.bpmX10 > bpmMax) continue;
    }
    if (keyMask) {
      if (!row || row.key < 1 || row.key > 24 || ((keyMask >> (row.key - 1)) & 1) === 0) continue;
    }
    if (moodMask) {
      if (!row || (row.moodBits & moodMask) === 0) continue;
    }
    if (!passesRange(row, row?.energy, args.energyMin, args.energyMax)) continue;
    if (!passesRange(row, row?.danceability, args.danceabilityMin, args.danceabilityMax)) continue;
    if (!passesRange(row, row?.valence, args.valenceMin, args.valenceMax)) continue;
    if (!passesRange(row, row?.arousal, args.arousalMin, args.arousalMax)) continue;

    matched.push({ media: mediaRow, row });
  }

  matched.sort((a, b) => compare(a, b, args.sort));
  const offset = args.offset ?? 0;
  const limit = args.limit ?? DEFAULT_LIMIT;
  return {
    query: args.query ?? '',
    matched: matched.length,
    truncated: matched.length > offset + limit,
    tracks: matched.slice(offset, offset + limit).map(trackOf),
  };
}

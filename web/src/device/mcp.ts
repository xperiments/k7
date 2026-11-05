/**
 * The device's library, read through MCP.
 *
 * The four `library_*` tools carry the titles and measurements a card's tags held, which is
 * the only place the device knows them (`/api/library` carries paths and sizes only). It is
 * one `POST /mcp` with a JSON-RPC body and no key, answered through the `Device` seam, so
 * this module never fetches.
 *
 * The parsers are exported because the local transport uses them: a tool's text block is
 * JSON off a device, and nothing unvalidated reaches a view (O5).
 */

import { getDevice } from './device';
import { DeviceError } from './transport';

export interface McpAnalysis {
  bpm: number | null;
  key: string | null;
  energy: number | null;
  danceability: number | null;
  valence: number | null;
  arousal: number | null;
  mood: string[];
  confidence: number | null;
  tagged: string[];
  title: string | null;
  artist: string | null;
  album: string | null;
  genre: string | null;
}

export interface McpTrack {
  name: string;
  /** Relative to the Music directory: the same string a Playlist row carries. */
  path: string;
  size: number;
  format: string;
  /** null when the Snapshot has no row for this track (not measured). */
  analysis: McpAnalysis | null;
}

export interface McpSearch {
  query: string;
  matched: number;
  tracks: McpTrack[];
  truncated: boolean;
}

export interface McpCoverage {
  tracks: number;
  analysed: number;
  percent: number;
}

/** The Snapshot block of `library_stats`. `coverage` and `tagged` live *inside* it, and
 * the block is absent when the device has no library at all (`{"available":false}`) —
 * the shape is docs/specs/snapshot.md M3, and the integration suite asserts it. */
export interface McpSnapshot {
  state: string;
  format_version?: number;
  generation?: number;
  rows?: number;
  analyser?: number;
  model_set?: string;
  reason?: string;
  coverage?: McpCoverage;
  tagged?: { bpm: number; key: number; mood: number; energy: number };
}

export interface McpStats {
  available: boolean;
  /** The Library's own state: `ok`, `no_card`, ... Not the Snapshot's. */
  state: string;
  tracks: number;
  directories: number;
  generation: number;
  snapshot?: McpSnapshot;
}

/** The coverage block, or null when this firmware does not report one. One guarded read,
 * because a region that assumes a field it did not receive takes the whole page with it. */
export function statsCoverage(stats: McpStats | null): McpCoverage | null {
  const coverage = stats?.snapshot?.coverage;
  if (!coverage) return null;
  if (
    typeof coverage.tracks !== 'number' ||
    typeof coverage.analysed !== 'number' ||
    typeof coverage.percent !== 'number'
  ) {
    return null;
  }
  return coverage;
}

export class McpError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'McpError';
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function numberOrNull(value: unknown): number | null {
  return typeof value === 'number' && Number.isFinite(value) ? value : null;
}

function stringOrNull(value: unknown): string | null {
  return typeof value === 'string' ? value : null;
}

function strings(value: unknown): string[] {
  return Array.isArray(value) ? value.filter((item): item is string => typeof item === 'string') : [];
}

function number(value: unknown, fallback: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

function parseAnalysis(value: unknown): McpAnalysis | null {
  if (!isRecord(value)) return null;
  return {
    bpm: numberOrNull(value.bpm),
    key: stringOrNull(value.key),
    energy: numberOrNull(value.energy),
    danceability: numberOrNull(value.danceability),
    valence: numberOrNull(value.valence),
    arousal: numberOrNull(value.arousal),
    mood: strings(value.mood),
    confidence: numberOrNull(value.confidence),
    tagged: strings(value.tagged),
    title: stringOrNull(value.title),
    artist: stringOrNull(value.artist),
    album: stringOrNull(value.album),
    genre: stringOrNull(value.genre),
  };
}

/** One row of a library answer. A row without a name and a path is not a Track. */
export function parseTrack(value: unknown): McpTrack | null {
  if (!isRecord(value)) return null;
  if (typeof value.name !== 'string' || typeof value.path !== 'string') return null;
  return {
    name: value.name,
    path: value.path,
    size: number(value.size, 0),
    format: typeof value.format === 'string' ? value.format : '',
    analysis:
      value.analysis === null || value.analysis === undefined
        ? null
        : parseAnalysis(value.analysis),
  };
}

/** `library_search`'s answer. A body without a `tracks` array is not an answer. */
export function parseSearch(value: unknown): McpSearch | null {
  if (!isRecord(value) || !Array.isArray(value.tracks)) return null;
  const tracks: McpTrack[] = [];
  for (const entry of value.tracks) {
    const track = parseTrack(entry);
    if (track !== null) tracks.push(track);
  }
  return {
    query: typeof value.query === 'string' ? value.query : '',
    matched: number(value.matched, tracks.length),
    tracks,
    truncated: value.truncated === true,
  };
}

export interface LibraryEntry {
  name: string;
  path: string;
  type: 'dir' | 'track';
  size?: number;
  format?: string;
}

export interface LibraryListing {
  path: string;
  entries: LibraryEntry[];
  truncated?: boolean;
}

function parseEntry(value: unknown): LibraryEntry | null {
  if (!isRecord(value)) return null;
  if (typeof value.name !== 'string' || typeof value.path !== 'string') return null;
  if (value.type !== 'dir' && value.type !== 'track') return null;
  const entry: LibraryEntry = { name: value.name, path: value.path, type: value.type };
  if (typeof value.size === 'number' && Number.isFinite(value.size)) entry.size = value.size;
  if (typeof value.format === 'string') entry.format = value.format;
  return entry;
}

/** `library_list`'s answer. `media/albums.ts` walks it for track paths. */
export function parseListing(value: unknown): LibraryListing | null {
  if (!isRecord(value) || !Array.isArray(value.entries)) return null;
  const entries: LibraryEntry[] = [];
  for (const entry of value.entries) {
    const parsed = parseEntry(entry);
    if (parsed !== null) entries.push(parsed);
  }
  const listing: LibraryListing = {
    path: typeof value.path === 'string' ? value.path : '',
    entries,
  };
  if (value.truncated === true) listing.truncated = true;
  return listing;
}

function parseSnapshotBlock(value: unknown): McpSnapshot | undefined {
  if (!isRecord(value)) return undefined;
  const block: McpSnapshot = {
    state: typeof value.state === 'string' ? value.state : 'unknown',
  };
  if (typeof value.format_version === 'number') block.format_version = value.format_version;
  if (typeof value.generation === 'number') block.generation = value.generation;
  if (typeof value.rows === 'number') block.rows = value.rows;
  if (typeof value.analyser === 'number') block.analyser = value.analyser;
  if (typeof value.model_set === 'string') block.model_set = value.model_set;
  if (typeof value.reason === 'string') block.reason = value.reason;
  if (isRecord(value.coverage)) {
    const coverage = value.coverage;
    if (
      typeof coverage.tracks === 'number' &&
      typeof coverage.analysed === 'number' &&
      typeof coverage.percent === 'number'
    ) {
      block.coverage = {
        tracks: coverage.tracks,
        analysed: coverage.analysed,
        percent: coverage.percent,
      };
    }
  }
  if (isRecord(value.tagged)) {
    const tagged = value.tagged;
    if (
      typeof tagged.bpm === 'number' &&
      typeof tagged.key === 'number' &&
      typeof tagged.mood === 'number' &&
      typeof tagged.energy === 'number'
    ) {
      block.tagged = { bpm: tagged.bpm, key: tagged.key, mood: tagged.mood, energy: tagged.energy };
    }
  }
  return block;
}

/** `library_stats`'s answer. `available` and `state` are the two facts every read needs. */
export function parseStats(value: unknown): McpStats | null {
  if (!isRecord(value)) return null;
  if (typeof value.available !== 'boolean') return null;
  const stats: McpStats = {
    available: value.available,
    state: typeof value.state === 'string' ? value.state : 'unknown',
    tracks: number(value.tracks, 0),
    directories: number(value.directories, 0),
    generation: number(value.generation, 0),
  };
  const snapshot = parseSnapshotBlock(value.snapshot);
  if (snapshot !== undefined) stats.snapshot = snapshot;
  return stats;
}

/** The seam's `DeviceError` in this module's vocabulary, so `McpError` checks keep working. */
function mcpError(error: unknown): McpError {
  if (error instanceof DeviceError) return new McpError(error.message);
  return new McpError(error instanceof Error ? error.message : String(error));
}

export interface SearchArgs {
  query?: string;
  genre?: string;
  bpmMin?: number;
  bpmMax?: number;
  energyMin?: number;
  energyMax?: number;
  danceabilityMin?: number;
  danceabilityMax?: number;
  valenceMin?: number;
  valenceMax?: number;
  arousalMin?: number;
  arousalMax?: number;
  key?: string[];
  mood?: string[];
  sort?: 'path' | 'title' | 'bpm' | 'energy' | 'danceability';
  offset?: number;
  limit?: number;
}

export async function searchTracks(args: SearchArgs = {}): Promise<McpSearch> {
  try {
    return await getDevice().request('library.search', args);
  } catch (error) {
    throw mcpError(error);
  }
}

export async function listLibrary(
  path = '',
  options: { depth?: number; limit?: number } = {},
): Promise<LibraryListing> {
  try {
    return await getDevice().request('library.list', {
      path,
      ...(options.depth !== undefined ? { depth: options.depth } : {}),
      ...(options.limit !== undefined ? { limit: options.limit } : {}),
    });
  } catch (error) {
    throw mcpError(error);
  }
}

export async function libraryStats(): Promise<McpStats> {
  try {
    return await getDevice().request('library.stats');
  } catch (error) {
    throw mcpError(error);
  }
}

export async function trackAt(path: string): Promise<McpTrack> {
  try {
    return await getDevice().request('library.track', { path });
  } catch (error) {
    throw mcpError(error);
  }
}

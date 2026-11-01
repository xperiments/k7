/**
 * The device's MCP endpoint, from the page.
 *
 * The device is on the same origin, so this is one `POST /mcp` with a JSON-RPC body; no
 * proxy, no CORS, no key. What it is used for here is reading: the library's rows carry
 * the titles and measurements a card's tags held, which is the only place the device
 * knows them (`/api/library` carries paths and sizes only).
 *
 * Every call is a fresh request and the device keeps no session, so `initialize` is sent
 * once per page load for a protocol version and to satisfy the handshake, and the calls
 * carry the version header after.
 */

const MCP_PATH = '/mcp';
const FALLBACK_PROTOCOL = '2025-11-25';

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

let version: string | null = null;
let handshake: Promise<void> | null = null;
let nextId = 1;

interface RpcReply {
  result?: { content?: { type: string; text?: string }[]; isError?: boolean };
  error?: { message?: string; code?: number };
}

async function post(body: unknown): Promise<RpcReply> {
  const response = await fetch(MCP_PATH, {
    method: 'POST',
    headers: {
      'content-type': 'application/json',
      accept: 'application/json',
      ...(version ? { 'MCP-Protocol-Version': version } : {}),
    },
    body: JSON.stringify(body),
  });
  const text = await response.text();
  if (!response.ok) throw new McpError(text || `MCP answered ${response.status}`);
  try {
    return JSON.parse(text) as RpcReply;
  } catch {
    throw new McpError('the device answered something that is not JSON');
  }
}

async function initialize(): Promise<void> {
  if (version) return;
  handshake ??= (async () => {
    const reply = await post({
      jsonrpc: '2.0',
      id: nextId++,
      method: 'initialize',
      params: {
        protocolVersion: FALLBACK_PROTOCOL,
        capabilities: {},
        clientInfo: { name: 'xtape-front-door', version: '1' },
      },
    });
    const echoed = (reply as unknown as { result?: { protocolVersion?: string } }).result
      ?.protocolVersion;
    version = typeof echoed === 'string' && echoed ? echoed : FALLBACK_PROTOCOL;
  })().catch((error: unknown) => {
    handshake = null;
    throw error;
  });
  await handshake;
}

/** Call one tool and parse the JSON it carries in its text block. */
export async function mcpCall<T>(tool: string, args: Record<string, unknown> = {}): Promise<T> {
  await initialize();
  const reply = await post({
    jsonrpc: '2.0',
    id: nextId++,
    method: 'tools/call',
    params: { name: tool, arguments: args },
  });
  if (reply.error) throw new McpError(reply.error.message ?? 'the device refused the call');
  const text = reply.result?.content?.find((part) => part.type === 'text')?.text;
  if (reply.result?.isError) throw new McpError(text ?? 'the tool failed');
  if (typeof text !== 'string') throw new McpError('the tool returned no text');
  try {
    return JSON.parse(text) as T;
  } catch {
    throw new McpError(`the tool returned something that is not JSON: ${text.slice(0, 80)}`);
  }
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

/** The device's own argument names, so one place translates the page's. */
function searchArguments(args: SearchArgs): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  const put = (to: string, value: unknown) => {
    if (value !== undefined && value !== null && value !== '') out[to] = value;
  };
  put('query', args.query);
  put('genre', args.genre);
  put('bpm_min', args.bpmMin);
  put('bpm_max', args.bpmMax);
  put('energy_min', args.energyMin);
  put('energy_max', args.energyMax);
  put('danceability_min', args.danceabilityMin);
  put('danceability_max', args.danceabilityMax);
  put('valence_min', args.valenceMin);
  put('valence_max', args.valenceMax);
  put('arousal_min', args.arousalMin);
  put('arousal_max', args.arousalMax);
  put('key', args.key);
  put('mood', args.mood);
  put('sort', args.sort);
  put('offset', args.offset);
  put('limit', args.limit);
  return out;
}

export async function searchTracks(args: SearchArgs = {}): Promise<McpSearch> {
  return mcpCall<McpSearch>('library_search', searchArguments(args));
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

export async function listLibrary(
  path = '',
  options: { depth?: number; limit?: number } = {},
): Promise<LibraryListing> {
  return mcpCall<LibraryListing>('library_list', {
    ...(path ? { path } : {}),
    ...(options.depth ? { depth: options.depth } : {}),
    ...(options.limit ? { limit: options.limit } : {}),
  });
}

export async function libraryStats(): Promise<McpStats> {
  return mcpCall<McpStats>('library_stats');
}

export async function trackAt(path: string): Promise<McpTrack> {
  return mcpCall<McpTrack>('library_track', { path });
}

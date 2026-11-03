/**
 * The direct Device: the App is the Device copy, so the device's own API is same-origin.
 *
 * Everything here is one `fetch` against a relative path — no proxy, no CORS, no key — and
 * the fifteen operations of bridge-protocol.md §3 are exactly the requests this file makes.
 * The parsing lives in `status.ts`, `playlists.ts`, `mcp.ts` and `upload.ts` and is shared
 * with the bridge, so the two ways of reaching a player accept and reject the same
 * documents (O5).
 *
 * The Device copy is at `http://k7.local/app`, and `/api/...` resolves to the device root
 * from there, which is why every path is absolute-path-relative rather than relative.
 *
 * `link()` is a constant here: the App can only be the Device copy by having been served by
 * the device, so there is nothing to connect and nothing to lose.
 */

import { OP_SPECS, type OpName } from '../bridge/protocol';
import {
  parseListing,
  parseSearch,
  parseStats,
  parseTrack,
  type LibraryListing,
  type McpSearch,
  type McpStats,
  type McpTrack,
  type SearchArgs,
} from './mcp';
import {
  parsePlaylistRows,
  parsePlaylists,
  parseSelectResult,
  type DevicePlaylistRows,
  type DevicePlaylists,
  type SelectResult,
} from './playlists';
import { parseStatus, type DeviceStatus } from './status';
import { DeviceError, type Device, type OpArgs, type OpData } from './transport';
import { parseUploadResult } from './upload';

/** A `fetch` this module can be handed in a test; the page's own is the default. */
export type FetchLike = (input: string, init?: RequestInit) => Promise<Response>;

/** The Device copy's own `<audio src="/stream">` (O3): the page that owns the audio drives it. */
export interface DirectAudio {
  playing: boolean;
  blocked: boolean;
  volume: number;
  /** `false` or a rejection means the browser refused: that is `blocked`, not a failure. */
  play(): void | boolean | Promise<void | boolean>;
  pause(): void | Promise<void>;
}

export interface DirectOptions {
  fetch?: FetchLike;
  /** `null` explicitly when this window has no audio; omitted means the DOM's own. */
  audio?: DirectAudio | null;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** The device's own `error` sentence in a refusal body, when it wrote one (E1). */
function replyDetail(text: string): string | null {
  try {
    const parsed: unknown = JSON.parse(text);
    if (isRecord(parsed) && typeof parsed.error === 'string' && parsed.error !== '') {
      return parsed.error;
    }
  } catch {
    /* the body is the device's error page; the status says enough */
  }
  return null;
}

/** The Device copy's own stream element, or null in a window that cannot carry audio. */
function domStreamAudio(): DirectAudio | null {
  if (typeof Audio === 'undefined') return null;
  const element = new Audio();
  element.preload = 'none';
  element.src = '/stream';
  const state: DirectAudio = {
    playing: false,
    blocked: false,
    get volume(): number {
      return element.volume;
    },
    set volume(level: number) {
      element.volume = level;
    },
    play() {
      return element.play().then(
        () => true,
        () => {
          state.blocked = true;
          return false;
        },
      );
    },
    pause() {
      element.pause();
    },
  };
  element.addEventListener('play', () => {
    state.playing = true;
    state.blocked = false;
  });
  element.addEventListener('pause', () => {
    state.playing = false;
  });
  return state;
}

/** The device's argument names for `library_search`, exactly as the Launcher translates them. */
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

const MCP_PATH = '/mcp';
const MCP_PROTOCOL = '2025-11-25';

export interface DirectDevice extends Device {
  readonly kind: 'direct';
}

/**
 * One device, one `fetch`. The object is stateless except for the MCP handshake, which the
 * device requires once per page load and which belongs to whichever document is asking.
 */
export function createDirectDevice(options: DirectOptions = {}): DirectDevice {
  const doFetch: FetchLike = options.fetch ?? ((input, init) => fetch(input, init));

  /* O3: the Device copy's own `<audio>`. It is made on the first stream operation rather
   * than at boot, because an element with a `src` may open a connection and the device
   * carries four of them. */
  let audio: DirectAudio | null | undefined;
  function streamAudio(): DirectAudio | null {
    if (audio === undefined) {
      audio = options.audio === undefined ? domStreamAudio() : options.audio;
    }
    return audio;
  }

  let mcpVersion: string | null = null;
  let handshake: Promise<void> | null = null;
  let nextRpcId = 1;

  async function call(
    op: OpName,
    path: string,
    init: RequestInit = {},
  ): Promise<Response> {
    const timeout = OP_SPECS[op].timeout;
    const controller = new AbortController();
    const handle = setTimeout(() => controller.abort(), timeout);
    try {
      return await doFetch(path, { ...init, signal: controller.signal });
    } catch {
      if (controller.signal.aborted) {
        throw new DeviceError(
          `the player did not answer within ${Math.round(timeout / 1000)} s`,
          'timeout',
          0,
        );
      }
      throw new DeviceError('the player did not answer; is it on the same network?', 'unreachable', 0);
    } finally {
      clearTimeout(handle);
    }
  }

  /**
   * One answer, read once and parsed by its caller. A refusal keeps the device's own
   * sentence and status, which is what the Playlists page shows for a 409 (E1).
   */
  async function read(op: OpName, path: string, init: RequestInit = {}): Promise<unknown> {
    const response = await call(op, path, init);
    let text: string;
    try {
      text = await response.text();
    } catch {
      throw new DeviceError('the player did not answer; is it on the same network?', 'unreachable', 0);
    }
    if (!response.ok) {
      throw new DeviceError(
        replyDetail(text) ?? `the device answered ${response.status}`,
        'refused',
        response.status,
      );
    }
    try {
      return JSON.parse(text) as unknown;
    } catch {
      throw new DeviceError('the player answered something that is not JSON', 'unreachable', 0);
    }
  }

  /** Parse or refuse: a document this page cannot read never reaches a view (O5). */
  function parsed<T>(value: unknown, parser: (raw: unknown) => T | null, what: string): T {
    const result = parser(value);
    if (result === null) {
      throw new DeviceError(`the player answered a ${what} this page could not read`, 'unreachable', 0);
    }
    return result;
  }

  const jsonInit = (body: unknown): RequestInit => ({
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body),
  });

  async function mcpPost(op: OpName, body: unknown): Promise<Record<string, unknown>> {
    const response = await call(op, MCP_PATH, {
      method: 'POST',
      headers: {
        'content-type': 'application/json',
        accept: 'application/json',
        ...(mcpVersion ? { 'MCP-Protocol-Version': mcpVersion } : {}),
      },
      body: JSON.stringify(body),
    });
    const text = await response.text();
    if (!response.ok) {
      throw new DeviceError(
        text || `the player's MCP endpoint answered ${response.status}`,
        'refused',
        response.status,
      );
    }
    let reply: unknown;
    try {
      reply = JSON.parse(text) as unknown;
    } catch {
      throw new DeviceError('the player answered something that is not JSON', 'unreachable', 0);
    }
    if (!isRecord(reply)) {
      throw new DeviceError('the player answered something that is not JSON-RPC', 'unreachable', 0);
    }
    return reply;
  }

  /** O1: the handshake is the device's, done once per document, then four fixed tools. */
  async function initialize(op: OpName): Promise<void> {
    if (mcpVersion) return;
    handshake ??= (async () => {
      const reply = await mcpPost(op, {
        jsonrpc: '2.0',
        id: nextRpcId++,
        method: 'initialize',
        params: {
          protocolVersion: MCP_PROTOCOL,
          capabilities: {},
          clientInfo: { name: 'k7-app', version: '1' },
        },
      });
      const result = reply.result;
      const echoed =
        isRecord(result) && typeof result.protocolVersion === 'string'
          ? result.protocolVersion
          : '';
      mcpVersion = echoed || MCP_PROTOCOL;
    })().catch((error: unknown) => {
      handshake = null;
      throw error;
    });
    await handshake;
  }

  /** One `tools/call`, and the JSON the tool carries in its text block. */
  async function mcpCall(op: OpName, tool: string, args: Record<string, unknown>): Promise<unknown> {
    await initialize(op);
    const reply = await mcpPost(op, {
      jsonrpc: '2.0',
      id: nextRpcId++,
      method: 'tools/call',
      params: { name: tool, arguments: args },
    });
    if (isRecord(reply.error)) {
      const message = typeof reply.error.message === 'string' ? reply.error.message : '';
      throw new DeviceError(message || 'the player refused the tool call', 'refused', 200);
    }
    const result = reply.result;
    if (!isRecord(result)) {
      throw new DeviceError('the tool returned no result', 'unreachable', 0);
    }
    let text: string | undefined;
    if (Array.isArray(result.content)) {
      for (const part of result.content) {
        if (isRecord(part) && part.type === 'text' && typeof part.text === 'string') {
          text = part.text;
          break;
        }
      }
    }
    if (result.isError === true) {
      throw new DeviceError(text ?? 'the tool failed', 'refused', 200);
    }
    if (typeof text !== 'string') {
      throw new DeviceError('the tool returned no text', 'unreachable', 0);
    }
    try {
      return JSON.parse(text) as unknown;
    } catch {
      throw new DeviceError('the tool returned something that is not JSON', 'unreachable', 0);
    }
  }

  /* ---------------------------------------------------------------- the audio ---- */

  function audioState() {
    const stream = streamAudio();
    return {
      playing: stream?.playing === true,
      volume: stream?.volume ?? 1,
      blocked: stream?.blocked === true,
    };
  }

  function requireAudio(): DirectAudio {
    const stream = streamAudio();
    if (!stream) {
      throw new DeviceError('this window has no stream to play', 'unreachable', 0);
    }
    return stream;
  }

  /* ------------------------------------------------------------ the fifteen ---- */

  const handlers: {
    [K in OpName]: (args: OpArgs[K]) => Promise<OpData[K]>;
  } = {
    async status() {
      return parsed<DeviceStatus>(
        await read('status', '/api/status', {
          cache: 'no-store',
          headers: { accept: 'application/json' },
        }),
        parseStatus,
        'status',
      );
    },

    async playlists() {
      return parsed<DevicePlaylists>(
        await read('playlists', '/api/playlists', {
          cache: 'no-store',
          headers: { accept: 'application/json' },
        }),
        parsePlaylists,
        'playlist index',
      );
    },

    async playlist(args: { id: string }) {
      return parsed<DevicePlaylistRows>(
        await read('playlist', `/api/playlists/${encodeURIComponent(args.id)}`, {
          cache: 'no-store',
          headers: { accept: 'application/json' },
        }),
        parsePlaylistRows,
        'playlist',
      );
    },

    async select(args: { id: string | null }) {
      return parsed<SelectResult>(
        await read('select', '/api/playlists/select', jsonInit({ id: args.id })),
        parseSelectResult,
        'selection',
      );
    },

    async snapshot(args: { bytes: Uint8Array<ArrayBuffer> }) {
      const response = await call('snapshot', '/api/snapshot', {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: args.bytes,
      });
      const text = await response.text();
      let document: unknown = {};
      try {
        document = text ? (JSON.parse(text) as unknown) : {};
      } catch {
        /* the body is the player's error page; the status says enough */
      }
      const detail = isRecord(document) && typeof document.error === 'string' ? document.error : null;
      if (!response.ok || !isRecord(document) || document.applied !== true) {
        throw new DeviceError(
          detail ?? `the player refused it (${response.status})`,
          'refused',
          response.status,
        );
      }
      const result = parseUploadResult(document, args.bytes.length);
      if (result === null) {
        throw new DeviceError(
          'the player answered a receipt this page could not read',
          'unreachable',
          0,
        );
      }
      return result;
    },

    async rescan() {
      const document = await read('rescan', '/api/rescan', { method: 'POST' });
      if (isRecord(document) && document.ok === true) return { ok: true };
      return { ok: false };
    },

    async play(args: { path: string }) {
      await read('play', '/api/play', jsonInit({ path: args.path }));
      return {};
    },

    async 'library.search'(args: SearchArgs) {
      return parsed<McpSearch>(
        await mcpCall('library.search', 'library_search', searchArguments(args)),
        parseSearch,
        'search',
      );
    },

    async 'library.list'(args: { path: string; depth?: number; limit?: number }) {
      return parsed<LibraryListing>(
        await mcpCall('library.list', 'library_list', {
          ...(args.path ? { path: args.path } : {}),
          ...(args.depth !== undefined ? { depth: args.depth } : {}),
          ...(args.limit !== undefined ? { limit: args.limit } : {}),
        }),
        parseListing,
        'listing',
      );
    },

    async 'library.stats'() {
      return parsed<McpStats>(await mcpCall('library.stats', 'library_stats', {}), parseStats, 'stats');
    },

    async 'library.track'(args: { path: string }) {
      return parsed<McpTrack>(
        await mcpCall('library.track', 'library_track', { path: args.path }),
        parseTrack,
        'track',
      );
    },

    async 'stream.play'() {
      const stream = requireAudio();
      const played = await stream.play();
      if (played === false || stream.blocked === true) {
        stream.blocked = true;
        throw new DeviceError(
          'the browser refused to start the stream; press play in the stream window',
          'blocked',
        );
      }
      stream.blocked = false;
      return audioState();
    },

    async 'stream.pause'() {
      await requireAudio().pause();
      return audioState();
    },

    async 'stream.volume'(args: { level: number }) {
      requireAudio().volume = args.level;
      return audioState();
    },

    async 'stream.state'() {
      return audioState();
    },
  };

  return {
    kind: 'direct',
    link: () => 'direct',
    /**
     * TypeScript cannot index a mapped type by a generic key, and the table above is the
     * proof that every operation is implemented with its own argument and answer types.
     */
    request<O extends OpName>(op: O, args?: OpArgs[O]): Promise<OpData[O]> {
      const handler = handlers[op] as (value: OpArgs[O] | undefined) => Promise<OpData[O]>;
      return handler(args);
    },
    onLinkChange() {
      /* The Device copy is direct for as long as the document lives. */
      return () => {};
    },
  };
}

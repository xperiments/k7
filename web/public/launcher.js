/**
 * The Launcher's half of the K7 bridge: `docs/specs/bridge-protocol.md`.
 *
 * This is plain JavaScript on purpose. The device serves it beside `launcher.html` with no
 * build step, so the page on the LAN is a few kilobytes of source, not a bundle. It is an
 * ES module with no imports, and it holds no DOM at module scope: `createLauncherBridge`
 * takes the window, `fetch`, origin, app-origin list, clock, timers and `<audio>` as
 * arguments, so the whole agent state machine runs in vitest under node with fakes.
 *
 * The page's own glue (the two faces, the status line, the transport controls) is at the
 * bottom and only runs when a `document` exists. It cannot live inline in the HTML: the
 * page's CSP is `default-src 'self'` with no `'unsafe-inline'` for scripts.
 *
 * The App's side is `src/bridge/protocol.ts`; `tests/bridge-contract.test.ts` holds the two
 * op tables equal.
 */

/** The protocol version, `v` on the wire. */
export const BRIDGE_VERSION = 1;

/** S4: a session silent for a minute is gone. Expiry is a disconnect, not an error. */
export const SESSION_IDLE_MS = 60_000;

/** §5: 256 KiB of binary args, 64 KiB of JSON args, 1 MiB of response. */
export const LIMITS = Object.freeze({
  request: 256 * 1024,
  json: 64 * 1024,
  response: 1024 * 1024,
});

const MCP_PATH = '/mcp';
const MCP_PROTOCOL = '2025-11-25';
const MCP_CLIENT = { name: 'k7-launcher', version: '1' };

const DEFAULT_APP_ORIGINS = ['https://k7.xperiments.dev'];

/**
 * L1/X2: the trusted App origins are compiled in. `window.K7_APP_ORIGINS` is the build-time
 * substitution point; a build that supplies nothing trusts the one shipping origin.
 */
function configuredOrigins() {
  const injected = typeof window === 'undefined' ? null : window.K7_APP_ORIGINS;
  const list = [];
  if (Array.isArray(injected)) {
    for (const item of injected) {
      if (typeof item === 'string' && item !== '' && item !== '*' && !list.includes(item)) list.push(item);
    }
  }
  return list.length > 0 ? list : DEFAULT_APP_ORIGINS.slice();
}

export const APP_ORIGINS = Object.freeze(configuredOrigins());

const DEFAULT_TIMERS = {
  setTimeout(handler, ms) {
    return setTimeout(handler, ms);
  },
  clearTimeout(handle) {
    clearTimeout(handle);
  },
};

function isRecord(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isVersion(value) {
  return typeof value === 'number' && Number.isInteger(value) && value >= 1;
}

function isFiniteNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

function isHex16(value) {
  return typeof value === 'string' && /^[0-9a-fA-F]{16}$/.test(value);
}

let encoder = null;

/** UTF-8 bytes, which is what the caps in §5 are counted in. */
function utf8Length(text) {
  encoder ??= new TextEncoder();
  return encoder.encode(text).length;
}

function jsonBytes(value) {
  try {
    const text = JSON.stringify(value);
    return typeof text === 'string' ? utf8Length(text) : null;
  } catch {
    return null;
  }
}

function noArgs() {
  return { value: {} };
}

function badArgs(message) {
  return { error: message };
}

/* -------------------------------------------------------------------------- args ---- */

function playlistArgs(args) {
  if (!isRecord(args) || !isHex16(args.id)) return badArgs('`id` must be 16 hex characters');
  return { value: { id: args.id } };
}

function selectArgs(args) {
  if (!isRecord(args) || !(args.id === null || isHex16(args.id))) {
    return badArgs('`id` must be 16 hex characters or null');
  }
  return { value: { id: args.id } };
}

function snapshotArgs(args) {
  if (!isRecord(args) || !(args.bytes instanceof Uint8Array)) return badArgs('`bytes` must be a Uint8Array');
  return { value: { bytes: args.bytes } };
}

function playArgs(args) {
  if (!isRecord(args) || typeof args.path !== 'string' || args.path === '') {
    return badArgs('`path` must be a non-empty string');
  }
  return { value: { path: args.path } };
}

const SEARCH_TEXT = ['query', 'genre'];
const SEARCH_NUMBERS = [
  'bpmMin',
  'bpmMax',
  'energyMin',
  'energyMax',
  'danceabilityMin',
  'danceabilityMax',
  'valenceMin',
  'valenceMax',
  'arousalMin',
  'arousalMax',
];
const SEARCH_LISTS = ['key', 'mood'];
const SEARCH_SORTS = ['path', 'title', 'bpm', 'energy', 'danceability'];

function searchArgs(args) {
  if (args === null) return { value: {} };
  if (!isRecord(args)) return badArgs('search arguments must be an object');
  const value = {};
  for (const name of SEARCH_TEXT) {
    if (args[name] === undefined) continue;
    if (typeof args[name] !== 'string') return badArgs(`\`${name}\` must be a string`);
    value[name] = args[name];
  }
  for (const name of SEARCH_NUMBERS) {
    if (args[name] === undefined) continue;
    if (!isFiniteNumber(args[name])) return badArgs(`\`${name}\` must be a number`);
    value[name] = args[name];
  }
  for (const name of SEARCH_LISTS) {
    if (args[name] === undefined) continue;
    const list = args[name];
    if (!Array.isArray(list) || list.some((item) => typeof item !== 'string')) {
      return badArgs(`\`${name}\` must be an array of strings`);
    }
    value[name] = list;
  }
  if (args.sort !== undefined) {
    if (!SEARCH_SORTS.includes(args.sort)) return badArgs('`sort` must be one of the documented sorts');
    value.sort = args.sort;
  }
  for (const name of ['offset', 'limit']) {
    if (args[name] === undefined) continue;
    if (!Number.isInteger(args[name]) || args[name] < 0) return badArgs(`\`${name}\` must be a non-negative integer`);
    value[name] = args[name];
  }
  return { value };
}

function listArgs(args) {
  if (args === null) return { value: {} };
  if (!isRecord(args)) return badArgs('list arguments must be an object');
  const value = {};
  if (args.path !== undefined) {
    if (typeof args.path !== 'string') return badArgs('`path` must be a string');
    value.path = args.path;
  }
  for (const name of ['depth', 'limit']) {
    if (args[name] === undefined) continue;
    if (!Number.isInteger(args[name]) || args[name] < 0) return badArgs(`\`${name}\` must be a non-negative integer`);
    value[name] = args[name];
  }
  return { value };
}

function trackArgs(args) {
  if (!isRecord(args) || typeof args.path !== 'string' || args.path === '') {
    return badArgs('`path` must be a non-empty string');
  }
  return { value: { path: args.path } };
}

function volumeArgs(args) {
  if (!isRecord(args) || !isFiniteNumber(args.level) || args.level < 0 || args.level > 1) {
    return badArgs('`level` must be a number from 0 to 1');
  }
  return { value: { level: args.level } };
}

/** The device's own argument names, exactly as `src/device/mcp.ts` translates them. */
function searchToolArgs(value) {
  const out = {};
  const put = (to, item) => {
    if (item !== undefined && item !== null && item !== '') out[to] = item;
  };
  put('query', value.query);
  put('genre', value.genre);
  put('bpm_min', value.bpmMin);
  put('bpm_max', value.bpmMax);
  put('energy_min', value.energyMin);
  put('energy_max', value.energyMax);
  put('danceability_min', value.danceabilityMin);
  put('danceability_max', value.danceabilityMax);
  put('valence_min', value.valenceMin);
  put('valence_max', value.valenceMax);
  put('arousal_min', value.arousalMin);
  put('arousal_max', value.arousalMax);
  put('key', value.key);
  put('mood', value.mood);
  put('sort', value.sort);
  put('offset', value.offset);
  put('limit', value.limit);
  return out;
}

function listToolArgs(value) {
  const out = {};
  if (typeof value.path === 'string' && value.path !== '') out.path = value.path;
  if (value.depth !== undefined) out.depth = value.depth;
  if (value.limit !== undefined) out.limit = value.limit;
  return out;
}

function trackToolArgs(value) {
  return { path: value.path };
}

/* ---------------------------------------------------------------------------- ops ---- */

/**
 * §3, one row per operation: the timeout, whether it carries binary, how its `args` are
 * validated and the one request it turns into. `stream.*` has no `build`: it never reaches
 * the network (O3).
 */
const OPS = {
  status: {
    timeout: 4_000,
    bytes: false,
    validate: noArgs,
    build: () => ({ path: '/api/status', method: 'GET' }),
  },
  playlists: {
    timeout: 6_000,
    bytes: false,
    validate: noArgs,
    build: () => ({ path: '/api/playlists', method: 'GET' }),
  },
  playlist: {
    timeout: 6_000,
    bytes: false,
    validate: playlistArgs,
    build: (value) => ({ path: `/api/playlists/${encodeURIComponent(value.id)}`, method: 'GET' }),
  },
  select: {
    timeout: 10_000,
    bytes: false,
    validate: selectArgs,
    build: (value) => ({ path: '/api/playlists/select', method: 'POST', body: JSON.stringify({ id: value.id }) }),
  },
  snapshot: {
    timeout: 30_000,
    bytes: true,
    validate: snapshotArgs,
    build: (value) => ({ path: '/api/snapshot', method: 'POST', body: value.bytes }),
  },
  rescan: {
    timeout: 10_000,
    bytes: false,
    validate: noArgs,
    build: () => ({ path: '/api/rescan', method: 'POST' }),
  },
  play: {
    timeout: 10_000,
    bytes: false,
    validate: playArgs,
    build: (value) => ({ path: '/api/play', method: 'POST', body: JSON.stringify({ path: value.path }) }),
  },
  'library.search': {
    timeout: 10_000,
    bytes: false,
    validate: searchArgs,
    mcp: 'library_search',
    mcpArgs: searchToolArgs,
  },
  'library.list': {
    timeout: 10_000,
    bytes: false,
    validate: listArgs,
    mcp: 'library_list',
    mcpArgs: listToolArgs,
  },
  'library.stats': {
    timeout: 10_000,
    bytes: false,
    validate: noArgs,
    mcp: 'library_stats',
    mcpArgs: () => ({}),
  },
  'library.track': {
    timeout: 10_000,
    bytes: false,
    validate: trackArgs,
    mcp: 'library_track',
    mcpArgs: trackToolArgs,
  },
  'stream.play': { timeout: 2_000, bytes: false, stream: true, validate: noArgs },
  'stream.pause': { timeout: 2_000, bytes: false, stream: true, validate: noArgs },
  'stream.volume': { timeout: 2_000, bytes: false, stream: true, validate: volumeArgs },
  'stream.state': { timeout: 2_000, bytes: false, stream: true, validate: noArgs },
};

/** The table as the contract test reads it: names, timeouts and the bytes flag. */
export const OP_SPECS = Object.freeze(
  Object.fromEntries(Object.entries(OPS).map(([name, spec]) => [name, { timeout: spec.timeout, bytes: spec.bytes }])),
);

/* ------------------------------------------------------------------------- messages ---- */

function welcomeMessage(audio) {
  return {
    t: 'welcome',
    v: BRIDGE_VERSION,
    role: 'launcher',
    audio: { playing: audio.playing === true, volume: volumeOf(audio) },
  };
}

function okMessage(id, data) {
  return { t: 'res', id, ok: true, data };
}

function failMessage(id, code, message, status) {
  return {
    t: 'res',
    id,
    ok: false,
    error: status === undefined ? { code, message } : { code, message, status },
  };
}

function byeMessage() {
  return { t: 'bye' };
}

function volumeOf(audio) {
  return isFiniteNumber(audio.volume) ? audio.volume : 1;
}

/**
 * The Launcher's own reader. It accepts an `op` it does not know so the session can answer
 * `unknown-op` (a response needs the `id`); everything else is strict, like the App's
 * `parseMessage`.
 */
function readMessage(data) {
  if (!isRecord(data)) return null;
  const t = data.t;
  if (t === 'hello') {
    if (!isVersion(data.v)) return null;
    if (data.role !== 'app' && data.role !== 'launcher') return null;
    if (data.app !== undefined && typeof data.app !== 'string') return null;
    return { t: 'hello', v: data.v, role: data.role };
  }
  if (t === 'req') {
    if (typeof data.id !== 'string' || data.id === '') return null;
    if (typeof data.op !== 'string' || data.op === '') return null;
    if (!(data.args === null || isRecord(data.args))) return null;
    return { t: 'req', id: data.id, op: data.op, args: data.args };
  }
  if (t === 'bye') return { t: 'bye' };
  return null;
}

/* ----------------------------------------------------------------------------- http ---- */

class BridgeFailure extends Error {
  constructor(code, message, status) {
    super(message);
    this.name = 'BridgeFailure';
    this.code = code;
    if (status !== undefined) this.status = status;
  }
}

function errorDetail(text) {
  if (typeof text !== 'string' || text.trim() === '') return null;
  try {
    const parsed = JSON.parse(text);
    if (isRecord(parsed) && typeof parsed.error === 'string' && parsed.error !== '') return parsed.error;
  } catch {
    /* the body is the device's error page */
  }
  const trimmed = text.trim();
  return trimmed.length <= 200 ? trimmed : null;
}

/** Read one answer, cap it, and turn a refusal into a `BridgeFailure`. */
async function readJson(response) {
  let text;
  try {
    text = await response.text();
  } catch {
    throw new BridgeFailure('unreachable', 'the player did not answer');
  }
  if (utf8Length(text) > LIMITS.response) {
    throw new BridgeFailure('too-large', 'the player answered with more than 1 MiB');
  }
  if (!response.ok) {
    throw new BridgeFailure(
      'refused',
      errorDetail(text) ?? `the player answered ${response.status}`,
      response.status,
    );
  }
  try {
    return JSON.parse(text);
  } catch {
    throw new BridgeFailure('unreachable', 'the player answered something that is not JSON');
  }
}

function requestInit(request) {
  const headers = { accept: 'application/json' };
  const init = { method: request.method, headers };
  if (typeof request.body === 'string') {
    headers['content-type'] = 'application/json';
    init.body = request.body;
  } else if (request.body instanceof Uint8Array) {
    headers['content-type'] = 'application/octet-stream';
    init.body = request.body;
  }
  if (request.method === 'GET') init.cache = 'no-store';
  return init;
}

function textPart(content) {
  if (!Array.isArray(content)) return undefined;
  for (const part of content) {
    if (isRecord(part) && part.type === 'text' && typeof part.text === 'string') return part.text;
  }
  return undefined;
}

/**
 * O1: four tools over `POST /mcp`, JSON-RPC `tools/call`, with the `initialize` handshake
 * performed once. This is not a passthrough: the tool names and arguments are fixed by the
 * ops table, and `control_play` is deliberately not reachable.
 */
function createMcpClient(fetchFn, origin) {
  let protocolVersion = null;
  let handshake = null;
  let nextId = 1;

  async function post(body) {
    const headers = { 'content-type': 'application/json', accept: 'application/json' };
    if (protocolVersion) headers['MCP-Protocol-Version'] = protocolVersion;
    let response;
    try {
      response = await fetchFn(`${origin}${MCP_PATH}`, {
        method: 'POST',
        headers,
        body: JSON.stringify(body),
      });
    } catch {
      throw new BridgeFailure('unreachable', 'the player did not answer');
    }
    return readJson(response);
  }

  async function initialize() {
    const reply = await post({
      jsonrpc: '2.0',
      id: nextId++,
      method: 'initialize',
      params: {
        protocolVersion: MCP_PROTOCOL,
        capabilities: {},
        clientInfo: MCP_CLIENT,
      },
    });
    const echoed =
      isRecord(reply) && isRecord(reply.result) && typeof reply.result.protocolVersion === 'string'
        ? reply.result.protocolVersion
        : '';
    protocolVersion = echoed || MCP_PROTOCOL;
  }

  async function ensure() {
    if (protocolVersion) return;
    if (!handshake) {
      handshake = initialize().catch((error) => {
        handshake = null;
        throw error;
      });
    }
    await handshake;
  }

  return {
    async call(tool, args) {
      await ensure();
      const reply = await post({
        jsonrpc: '2.0',
        id: nextId++,
        method: 'tools/call',
        params: { name: tool, arguments: args },
      });
      if (!isRecord(reply)) throw new BridgeFailure('unreachable', 'the player answered something that is not JSON-RPC');
      if (isRecord(reply.error)) {
        const message = typeof reply.error.message === 'string' && reply.error.message !== '' ? reply.error.message : '';
        throw new BridgeFailure('refused', message || 'the player refused the tool call', 200);
      }
      const result = reply.result;
      if (!isRecord(result)) throw new BridgeFailure('unreachable', 'the tool returned no result');
      const text = textPart(result.content);
      if (result.isError === true) throw new BridgeFailure('refused', text || 'the tool failed', 200);
      if (typeof text !== 'string') throw new BridgeFailure('unreachable', 'the tool returned no text');
      try {
        return JSON.parse(text);
      } catch {
        throw new BridgeFailure('unreachable', 'the tool returned something that is not JSON');
      }
    },
  };
}

/* -------------------------------------------------------------------------- factory ---- */

function silentAudio() {
  return {
    playing: false,
    blocked: false,
    volume: 1,
    play() {
      this.playing = true;
      this.blocked = false;
      return true;
    },
    pause() {
      this.playing = false;
    },
  };
}

/**
 * The agent side of the bridge.
 *
 * `start()` listens; `stop()` says `bye` to every live session and forgets them. `sessions()`
 * reports the table (S1) and `audio()` hands back the `<audio>` the page wired in, so the
 * transport controls and `stream.*` are the same object.
 */
export function createLauncherBridge(options) {
  if (!isRecord(options)) throw new TypeError('createLauncherBridge needs an options object');
  const win = options.win;
  const fetchFn = options.fetch;
  const originOption = options.origin;
  if (
    !win ||
    typeof win.addEventListener !== 'function' ||
    typeof win.removeEventListener !== 'function' ||
    typeof win.postMessage !== 'function'
  ) {
    throw new TypeError('createLauncherBridge needs a window-like `win`');
  }
  if (typeof fetchFn !== 'function') throw new TypeError('createLauncherBridge needs a `fetch`');
  if (typeof originOption !== 'string' || originOption === '') {
    throw new TypeError('createLauncherBridge needs the device `origin`');
  }
  const origin = (originOption.endsWith('/') ? originOption.replace(/\/+$/, '') : originOption) || originOption;
  const now = typeof options.now === 'function' ? options.now : Date.now;
  const timers = options.timers ?? DEFAULT_TIMERS;
  const audio = options.audio ?? silentAudio();
  const mcp = createMcpClient(fetchFn, origin);

  const trusted = [];
  if (Array.isArray(options.appOrigins)) {
    for (const item of options.appOrigins) {
      if (typeof item === 'string' && item !== '' && item !== '*' && !trusted.includes(item)) trusted.push(item);
    }
  }

  /** S1: `source WindowProxy -> {origin, version, lastSeen}`. */
  const sessions = new Map();
  /** X6: an id the App is still waiting on. Duplicate ids are `bad-message`. */
  const inFlight = new Set();
  /** Per-op deadline handles, so `stop()` can leave nothing armed. */
  const deadlines = new Set();

  let started = false;
  let tail = Promise.resolve();

  /**
   * W9: a reply goes to the window that spoke, with an explicit origin. Posting to `win`
   * here would send the answer to the Launcher itself — the channel would look alive in the
   * tests (which only record) and carry nothing in a browser.
   */
  function post(source, message, targetOrigin) {
    if (!started) return;
    if (!source || typeof source.postMessage !== 'function') return;
    if (typeof targetOrigin !== 'string' || targetOrigin === '' || targetOrigin === '*') return;
    source.postMessage(message, targetOrigin);
  }

  function dropSession(source) {
    const session = sessions.get(source);
    if (!session) return;
    if (session.timer !== undefined) timers.clearTimeout(session.timer);
    sessions.delete(source);
  }

  /** S4: every valid message refreshes the session, and expiry is a silent disconnect. */
  function touch(source, session) {
    session.lastSeen = now();
    if (session.timer !== undefined) timers.clearTimeout(session.timer);
    session.timer = timers.setTimeout(() => {
      const current = sessions.get(source);
      if (!current) return;
      if (now() - current.lastSeen >= SESSION_IDLE_MS) dropSession(source);
    }, SESSION_IDLE_MS);
  }

  function audioState() {
    return { playing: audio.playing === true, volume: volumeOf(audio) };
  }

  function streamState() {
    return { playing: audio.playing === true, volume: volumeOf(audio), blocked: audio.blocked === true };
  }

  function acceptHello(originName, source, message) {
    if (message.role !== 'app') return; // W7: the wrong role is dropped, not answered.
    const session = { origin: originName, version: message.v, lastSeen: now(), timer: undefined };
    sessions.set(source, session);
    touch(source, session);
    post(source, welcomeMessage(audioState()), originName);
  }

  function acceptRequest(originName, source, message) {
    const session = sessions.get(source);
    if (!session || session.origin !== originName) return; // S1, X4: no session, no request.
    touch(source, session);

    const id = message.id;
    // W8: a peer whose version we do not speak is answered, never executed. This comes
    // before the op table: an op this firmware has never heard of is the symptom, not the
    // diagnosis, when the App and the firmware disagree.
    if (session.version !== BRIDGE_VERSION) {
      post(source, 
        failMessage(id, 'version', `the player's bridge speaks version ${BRIDGE_VERSION}, not ${session.version}`),
        originName,
      );
      return;
    }
    if (!Object.hasOwn(OPS, message.op)) {
      post(source, failMessage(id, 'unknown-op', `the bridge has no operation named ${message.op}`), originName);
      return;
    }
    const op = message.op;
    const spec = OPS[op];
    if (inFlight.has(id)) {
      post(source, failMessage(id, 'bad-message', 'that request id is already in flight'), originName);
      return;
    }

    const validated = spec.validate(message.args);
    if (validated.error) {
      post(source, failMessage(id, 'bad-args', validated.error), originName);
      return;
    }
    const size = spec.bytes ? validated.value.bytes.byteLength : jsonBytes(message.args);
    const cap = spec.bytes ? LIMITS.request : LIMITS.json;
    if (size === null || size > cap) {
      post(source, 
        failMessage(id, 'too-large', `the request is over the ${Math.round(cap / 1024)} KiB cap`),
        originName,
      );
      return;
    }

    if (spec.stream) {
      runStream(id, originName, op, spec, validated.value, source);
      return;
    }
    runNetwork(id, originName, op, spec, validated.value, source);
  }

  function withDeadline(work, ms, onTimeout, onFailure) {
    return new Promise((resolve) => {
      let settled = false;
      const handle = timers.setTimeout(() => {
        if (settled) return;
        settled = true;
        deadlines.delete(handle);
        resolve(onTimeout());
      }, ms);
      deadlines.add(handle);
      work.then(
        (message) => {
          if (settled) return;
          settled = true;
          deadlines.delete(handle);
          timers.clearTimeout(handle);
          resolve(message);
        },
        () => {
          if (settled) return;
          settled = true;
          deadlines.delete(handle);
          timers.clearTimeout(handle);
          resolve(onFailure());
        },
      );
    });
  }

  async function executeStream(id, op, value) {
    if (op === 'stream.play') {
      try {
        const played = await audio.play();
        if (played === false || audio.blocked === true) {
          audio.blocked = true;
          return failMessage(id, 'blocked', 'the browser refused to start the stream; press play in the bridge window');
        }
        audio.blocked = false;
        return okMessage(id, { playing: audio.playing === true });
      } catch {
        audio.blocked = true;
        return failMessage(id, 'blocked', 'the browser refused to start the stream; press play in the bridge window');
      }
    }
    if (op === 'stream.pause') {
      await audio.pause();
      return okMessage(id, { playing: audio.playing === true });
    }
    if (op === 'stream.volume') {
      audio.volume = value.level;
      return okMessage(id, { volume: volumeOf(audio) });
    }
    return okMessage(id, streamState());
  }

  async function executeHttp(id, op, spec, value) {
    try {
      const data = spec.mcp ? await mcp.call(spec.mcp, spec.mcpArgs(value)) : await http(op, spec, value);
      return okMessage(id, data);
    } catch (error) {
      if (error instanceof BridgeFailure) return failMessage(id, error.code, error.message, error.status);
      return failMessage(id, 'unreachable', 'the player did not answer');
    }
  }

  async function http(op, spec, value) {
    const request = spec.build(value);
    let response;
    try {
      response = await fetchFn(`${origin}${request.path}`, requestInit(request));
    } catch {
      throw new BridgeFailure('unreachable', 'the player did not answer');
    }
    const data = await readJson(response);
    if (op === 'snapshot' && (!isRecord(data) || data.applied !== true)) {
      const detail = isRecord(data) && typeof data.error === 'string' ? data.error : null;
      throw new BridgeFailure('refused', detail || 'the player did not accept the image', response.status);
    }
    return data;
  }

  function runStream(id, originName, op, spec, value, source) {
    withDeadline(
      executeStream(id, op, value),
      spec.timeout,
      () => failMessage(id, 'timeout', 'the stream controls did not answer in time'),
      () => failMessage(id, 'blocked', 'the stream controls failed'),
    ).then((message) => post(source, message, originName));
  }

  /**
   * O4: one request in flight, the rest queued in arrival order. A timed-out request
   * answers `timeout` at once but keeps its slot until the device actually settles, so the
   * player never sees two of the bridge's requests at the same time.
   */
  function runNetwork(id, originName, op, spec, value, source) {
    inFlight.add(id);
    const job = async () => {
      const work = executeHttp(id, op, spec, value);
      const response = await withDeadline(
        work,
        spec.timeout,
        () => failMessage(id, 'timeout', `the player did not answer within ${Math.round(spec.timeout / 1000)} s`),
        () => failMessage(id, 'unreachable', 'the player did not answer'),
      );
      inFlight.delete(id);
      post(source, response, originName);
      await work;
    };
    tail = tail.then(job, job).catch(() => {});
  }

  function onMessage(event) {
    if (!started || !event) return;
    const originName = event.origin;
    if (typeof originName !== 'string' || !trusted.includes(originName)) return; // S3, X2.
    const source = event.source;
    if (source === null || source === undefined) return;
    const message = readMessage(event.data);
    if (!message) return;
    if (message.t === 'hello') {
      acceptHello(originName, source, message);
      return;
    }
    if (message.t === 'bye') {
      dropSession(source);
      return;
    }
    acceptRequest(originName, source, message);
  }

  function start() {
    if (started) return;
    started = true;
    win.addEventListener('message', onMessage);
  }

  function stop() {
    if (!started) return;
    for (const [source, session] of sessions) {
      // W10: best effort, to the window that opened the session, with its explicit origin.
      if (source && typeof source.postMessage === 'function') {
        source.postMessage(byeMessage(), session.origin);
      }
    }
    started = false;
    win.removeEventListener('message', onMessage);
    for (const source of Array.from(sessions.keys())) dropSession(source);
    for (const handle of deadlines) timers.clearTimeout(handle);
    deadlines.clear();
    inFlight.clear();
  }

  function currentSessions() {
    const at = now();
    const expired = [];
    const out = [];
    for (const [source, session] of sessions) {
      if (at - session.lastSeen >= SESSION_IDLE_MS) expired.push(source);
      else out.push({ origin: session.origin, version: session.version, lastSeen: session.lastSeen, source });
    }
    for (const source of expired) dropSession(source);
    return out;
  }

  return {
    start,
    stop,
    sessions: currentSessions,
    audio: () => audio,
  };
}

/* ----------------------------------------------------------------------------- page ---- */

/**
 * The two faces of `launcher.html`, kept out of the HTML because the page's CSP has no
 * `'unsafe-inline'` for scripts. Gateway: device facts, the library and stream links, and
 * the button that opens the App. Bridge: the `<audio>` the App drives and the sentence that
 * keeps the window open.
 */
function mountLauncherPage() {
  if (typeof window === 'undefined' || typeof document === 'undefined') return;
  const root = document.getElementById('k7');
  if (!root) return;
  const byId = (id) => document.getElementById(id);
  const gateway = byId('gateway');
  const bridgePanel = byId('bridge');
  const launchButton = byId('launch');
  const gatewayHint = byId('gateway-hint');
  const bridgeHint = byId('bridge-hint');
  const audioEl = byId('stream-audio');
  const playButton = byId('play');
  const pauseButton = byId('pause');
  const volumeInput = byId('volume');
  const originNote = byId('origin-note');
  if (
    !gateway ||
    !bridgePanel ||
    !launchButton ||
    !gatewayHint ||
    !bridgeHint ||
    !audioEl ||
    !playButton ||
    !pauseButton ||
    !volumeInput
  ) {
    return;
  }

  const setText = (id, value) => {
    const node = byId(id);
    if (node) node.textContent = value;
  };

  const audio = {
    playing: false,
    blocked: false,
    get volume() {
      return audioEl.volume;
    },
    set volume(level) {
      audioEl.volume = level;
      volumeInput.value = String(level);
    },
    play() {
      return audioEl.play().then(
        () => {
          audio.playing = true;
          audio.blocked = false;
        },
        (error) => {
          audio.playing = false;
          audio.blocked = Boolean(error) && error.name === 'NotAllowedError';
          throw error;
        },
      );
    },
    pause() {
      audioEl.pause();
      audio.playing = false;
    },
  };

  audioEl.addEventListener('play', () => {
    audio.playing = true;
    audio.blocked = false;
  });
  audioEl.addEventListener('pause', () => {
    audio.playing = false;
  });
  audioEl.addEventListener('volumechange', () => {
    volumeInput.value = String(audioEl.volume);
  });

  /* `/` is a doorbell, not a page: the App lives at its own origin and connects to this
   * device directly, so the only useful thing the device's address can do is send you there.
   * The Launcher itself lives on at `/bridge`, where the App opens it when a browser cannot
   * do the direct path — that window is the fallback, and this one would be a detour. */
  const path = typeof window === 'undefined' ? '/' : window.location.pathname;
  if (path === '/' || path === '/index.html') {
    if (typeof window !== 'undefined') window.location.replace(`${APP_ORIGINS[0]}/#/player`);
    return;
  }

  const bridge = createLauncherBridge({
    win: window,
    fetch: (url, init) => window.fetch(url, init),
    origin: window.location.origin,
    appOrigins: APP_ORIGINS,
    audio,
  });
  bridge.start();

  const showFace = (face) => {
    gateway.hidden = face !== 'gateway';
    bridgePanel.hidden = face !== 'bridge';
  };
  // W1: bridge mode follows a *trusted* hello, not the presence of an opener.
  showFace('gateway');

  if (originNote && window.location.hostname !== 'k7.local') {
    originNote.textContent = 'Connected mode needs http://k7.local. This page is at ' + window.location.hostname + '.';
    originNote.hidden = false;
  }

  async function pollStatus() {
    try {
      const response = await window.fetch('/api/status', {
        cache: 'no-store',
        headers: { accept: 'application/json' },
      });
      if (!response.ok) throw new Error(String(response.status));
      const status = await response.json();
      const playing =
        isRecord(status.playing) && typeof status.playing.title === 'string' ? status.playing.title : 'nothing';
      const order =
        status.order === 'list' && isRecord(status.playlist) && typeof status.playlist.name === 'string'
          ? 'list · ' + status.playlist.name
          : 'tree';
      setText('st-card', status.card === true ? 'card in' : 'no card');
      setText('st-tracks', typeof status.tracks === 'number' ? String(status.tracks) : '—');
      setText('st-playing', playing);
      setText('st-order', order);
      setText('bridge-status', (status.card === true ? 'card in' : 'no card') + ' · ' + playing);
    } catch {
      setText('st-card', 'unreachable');
      setText('st-tracks', '—');
      setText('st-playing', '—');
      setText('st-order', '—');
      setText('bridge-status', 'player unreachable');
    }
  }
  void pollStatus();
  window.setInterval(() => void pollStatus(), 5_000);

  launchButton.addEventListener('click', () => {
    const opened = window.open(APP_ORIGINS[0], 'k7-app');
    if (!opened) {
      gatewayHint.textContent = 'Allow popups for this page, then press Launch K7.';
      return;
    }
    gatewayHint.textContent = '';
    bridge.start();
    showFace('bridge');
  });

  playButton.addEventListener('click', () => {
    audio.play().then(
      () => {
        bridgeHint.textContent = 'Playing.';
      },
      () => {
        bridgeHint.textContent = 'The browser refused to play; press Play again.';
      },
    );
  });
  pauseButton.addEventListener('click', () => {
    audio.pause();
    bridgeHint.textContent = '';
  });
  volumeInput.addEventListener('input', () => {
    audio.volume = Number(volumeInput.value);
  });

  // S6: the App died, so there is no session to serve and nothing to keep open.
  window.setInterval(() => {
    if (window.opener && window.opener.closed) {
      bridge.stop();
      audio.pause();
      showFace('gateway');
      gatewayHint.textContent = 'The app closed; this window is a gateway again.';
    }
  }, 2_000);

  window.addEventListener('pagehide', () => {
    bridge.stop();
  });
}

if (typeof document !== 'undefined') {
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', mountLauncherPage, { once: true });
  } else {
    mountLauncherPage();
  }
}

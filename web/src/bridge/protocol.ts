/**
 * The wire between the App and the Launcher: `docs/specs/bridge-protocol.md`.
 *
 * This file is the App's side of the contract and holds no DOM, no `fetch` and no state, so
 * the same module runs in the tests, in the App at its HTTPS origin and in the Device copy.
 * The Launcher's side is `public/launcher.js`, hand-written JavaScript because the device
 * serves it with no build; `tests/bridge-contract.test.ts` holds the two op tables equal so
 * neither end can drift from the spec or from the other.
 *
 * Everything here is a pure function over a `postMessage` payload. `parseMessage` is the
 * only door: it validates shape and types strictly, drops unknown fields and returns null
 * for anything malformed. Semantic checks — role matching, version skew, duplicate ids —
 * belong to the state machines on both ends (W7, W8, X6), not to the parser.
 */

/** The protocol version, `v` on the wire. A peer that does not know it refuses `req`. */
export const BRIDGE_VERSION = 1;

export type Role = 'app' | 'launcher';

/** The fourteen operations. Nothing else is bridged (O2, X5). */
export type OpName =
  | 'status'
  | 'playlists'
  | 'playlist'
  | 'select'
  | 'rescan'
  | 'play'
  | 'library.search'
  | 'library.list'
  | 'library.stats'
  | 'library.track'
  | 'stream.play'
  | 'stream.pause'
  | 'stream.volume'
  | 'stream.state';

/** §4, closed: no operation invents an error code. */
export type ErrorCode =
  | 'bad-message'
  | 'unknown-op'
  | 'bad-args'
  | 'too-large'
  | 'timeout'
  | 'unreachable'
  | 'refused'
  | 'blocked'
  | 'version';

/** The per-op budget from the §3 table. */
export interface OpSpec {
  /** How long the caller waits for this op's answer, in milliseconds. */
  readonly timeout: number;
}

export const OP_SPECS: Readonly<Record<OpName, OpSpec>> = {
  status: { timeout: 4_000 },
  playlists: { timeout: 6_000 },
  playlist: { timeout: 6_000 },
  select: { timeout: 10_000 },
  rescan: { timeout: 10_000 },
  play: { timeout: 10_000 },
  'library.search': { timeout: 10_000 },
  'library.list': { timeout: 10_000 },
  'library.stats': { timeout: 10_000 },
  'library.track': { timeout: 10_000 },
  'stream.play': { timeout: 2_000 },
  'stream.pause': { timeout: 2_000 },
  'stream.volume': { timeout: 2_000 },
  'stream.state': { timeout: 2_000 },
};

/** §5: the request cap, the JSON cap and the response cap, in bytes. */
export const LIMITS = {
  /** `args` carrying JSON. */
  json: 64 * 1024,
  /** Any `data` the Launcher sends back. */
  response: 1024 * 1024,
} as const;

const OP_NAME_SET: ReadonlySet<string> = new Set(Object.keys(OP_SPECS));

export function isOpName(value: unknown): value is OpName {
  return typeof value === 'string' && OP_NAME_SET.has(value);
}

/** What the Launcher's `<audio>` reports in `welcome` and `stream.state`. */
export interface AudioState {
  readonly playing: boolean;
  readonly volume: number;
}

export interface HelloMessage {
  readonly t: 'hello';
  readonly v: number;
  readonly role: Role;
  /** The App's own origin, when the sender is the App. Informational; `event.origin` rules. */
  readonly app?: string;
}

export interface WelcomeMessage {
  readonly t: 'welcome';
  readonly v: number;
  readonly role: Role;
  readonly audio: AudioState;
}

/** `args` is `null` for the ops that carry nothing, an object for the rest. */
export type RequestArgs = Readonly<Record<string, unknown>> | null;

export interface RequestMessage {
  readonly t: 'req';
  readonly id: string;
  readonly op: OpName;
  readonly args: RequestArgs;
}

export interface BridgeError {
  readonly code: ErrorCode;
  readonly message: string;
  /** The device's HTTP status, when the refusal came with one. */
  readonly status?: number;
}

export interface OkResponse {
  readonly t: 'res';
  readonly id: string;
  readonly ok: true;
  readonly data: unknown;
}

export interface FailResponse {
  readonly t: 'res';
  readonly id: string;
  readonly ok: false;
  readonly error: BridgeError;
}

export type ResponseMessage = OkResponse | FailResponse;

export interface ByeMessage {
  readonly t: 'bye';
}

export type Message = HelloMessage | WelcomeMessage | RequestMessage | ResponseMessage | ByeMessage;

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isVersion(value: unknown): value is number {
  return typeof value === 'number' && Number.isInteger(value) && value >= 1;
}

function isRole(value: unknown): value is Role {
  return value === 'app' || value === 'launcher';
}

function isErrorCode(value: unknown): value is ErrorCode {
  switch (value) {
    case 'bad-message':
    case 'unknown-op':
    case 'bad-args':
    case 'too-large':
    case 'timeout':
    case 'unreachable':
    case 'refused':
    case 'blocked':
    case 'version':
      return true;
    default:
      return false;
  }
}

function parseHello(value: Record<string, unknown>): HelloMessage | null {
  if (!isVersion(value.v)) return null;
  if (!isRole(value.role)) return null;
  if (value.app !== undefined && typeof value.app !== 'string') return null;
  return value.app === undefined
    ? { t: 'hello', v: value.v, role: value.role }
    : { t: 'hello', v: value.v, role: value.role, app: value.app };
}

function parseWelcome(value: Record<string, unknown>): WelcomeMessage | null {
  if (!isVersion(value.v)) return null;
  if (!isRole(value.role)) return null;
  const audio = value.audio;
  if (!isRecord(audio)) return null;
  if (typeof audio.playing !== 'boolean') return null;
  if (typeof audio.volume !== 'number' || !Number.isFinite(audio.volume)) return null;
  return {
    t: 'welcome',
    v: value.v,
    role: value.role,
    audio: { playing: audio.playing, volume: audio.volume },
  };
}

function parseRequest(value: Record<string, unknown>): RequestMessage | null {
  if (typeof value.id !== 'string' || value.id.length === 0) return null;
  if (!isOpName(value.op)) return null;
  const args = value.args;
  if (args !== null && !isRecord(args)) return null;
  return { t: 'req', id: value.id, op: value.op, args };
}

function parseResponse(value: Record<string, unknown>): ResponseMessage | null {
  if (typeof value.id !== 'string' || value.id.length === 0) return null;
  if (value.ok === true) {
    if (!Object.hasOwn(value, 'data')) return null;
    return { t: 'res', id: value.id, ok: true, data: value.data };
  }
  if (value.ok !== false) return null;
  const error = value.error;
  if (!isRecord(error)) return null;
  if (!isErrorCode(error.code)) return null;
  if (typeof error.message !== 'string') return null;
  const status = error.status;
  if (status !== undefined) {
    if (typeof status !== 'number' || !Number.isInteger(status) || status <= 0) return null;
  }
  return {
    t: 'res',
    id: value.id,
    ok: false,
    error:
      status === undefined
        ? { code: error.code, message: error.message }
        : { code: error.code, message: error.message, status },
  };
}

/**
 * Validate one `postMessage` payload.
 *
 * Unknown fields are ignored (they are not copied into the result), every known field is
 * type-checked, an op outside §3 is rejected, and anything else malformed — a missing `t`,
 * a numeric `id`, an unknown error code — is `null`. Version skew is not malformed: a
 * `hello` with `v: 2` parses, because W8 makes the skew a refusal, not a bad message.
 */
export function parseMessage(value: unknown): Message | null {
  if (!isRecord(value)) return null;
  const t = value.t;
  if (typeof t !== 'string') return null;
  switch (t) {
    case 'hello':
      return parseHello(value);
    case 'welcome':
      return parseWelcome(value);
    case 'req':
      return parseRequest(value);
    case 'res':
      return parseResponse(value);
    case 'bye':
      return { t: 'bye' };
    default:
      return null;
  }
}

/**
 * W6: whoever has a handle says `hello` first. `app` is the App's origin, carried so the
 * Launcher can log it; the Launcher trusts `event.origin`, never this field (S3, X2).
 */
export function hello(role: Role, app?: string): HelloMessage {
  return app === undefined ? { t: 'hello', v: BRIDGE_VERSION, role } : { t: 'hello', v: BRIDGE_VERSION, role, app };
}

export function welcome(audio: AudioState): WelcomeMessage {
  return {
    t: 'welcome',
    v: BRIDGE_VERSION,
    role: 'launcher',
    audio: { playing: audio.playing, volume: audio.volume },
  };
}

export function req(id: string, op: OpName, args: RequestArgs = null): RequestMessage {
  return { t: 'req', id, op, args };
}

export function ok(id: string, data: unknown): OkResponse {
  return { t: 'res', id, ok: true, data };
}

export function fail(id: string, code: ErrorCode, message: string, status?: number): FailResponse {
  return {
    t: 'res',
    id,
    ok: false,
    error: status === undefined ? { code, message } : { code, message, status },
  };
}

export function bye(): ByeMessage {
  return { t: 'bye' };
}

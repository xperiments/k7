/**
 * The bridge Device: the App talks to a Launcher at `http://k7.local`, and the Launcher
 * talks to the device — `docs/specs/bridge-protocol.md`.
 *
 * This file is the App's state machine. It holds no `fetch` and no DOM state of its own:
 * the page's `window` is injected as a tiny port so the whole thing runs under node with a
 * fake window, the same way `public/launcher.js` does on the other end.
 *
 * The rules it enforces, each cited where it is applied:
 * - only `http://k7.local` and only the counterpart window are heard (S2, X3);
 * - `hello` first, and never a `req` before a `welcome` (W6);
 * - one id per request, the op's budget as the deadline, and the wire's `code`/`status`
 *   carried into the `DeviceError` (E1, §4);
 * - `bye` or a closed counterpart is a disconnect, not an error (S4, S6);
 * - every `data` is parsed by the same parsers the direct Device uses (O5).
 */

import {
  OP_SPECS,
  hello,
  parseMessage,
  req,
  welcome,
  type Message,
  type OpName,
  type ResponseMessage,
} from '../bridge/protocol';
import { DEVICE_ORIGIN, DEVICE_WINDOW } from '../config';
import { parseListing, parseSearch, parseStats, parseTrack } from './mcp';
import { parsePlaylistRows, parsePlaylists, parseSelectResult } from './playlists';
import { parseStatus } from './status';
import {
  DeviceError,
  NO_LINK,
  type AudioState,
  type Device,
  type Link,
  type OpArgs,
  type OpData,
} from './transport';
import { parseUploadResult } from './upload';

/** S4: the same 5 s cadence as the status poll, which is the protocol's only heartbeat. */
export const TICK_MS = 5_000;

/** A `WindowProxy`, which is the only thing this file knows how to post to (S2). */
export interface BridgeEndpoint {
  postMessage(message: unknown, targetOrigin: string, transfer?: Transferable[]): void;
  /** The one cross-origin-readable fact, and how a closed counterpart is noticed. */
  readonly closed?: boolean;
}

/**
 * The App's own window, as the bridge uses it: a message/visibility listener and the two
 * ways to get a handle on the Launcher (W1). A real `window` satisfies this; so does the
 * fake in `tests/bridge.test.ts`.
 */
export interface BridgeWindow {
  addEventListener(
    type: 'message' | 'focus' | 'visibilitychange',
    handler: (event: unknown) => void,
  ): void;
  removeEventListener(
    type: 'message' | 'focus' | 'visibilitychange',
    handler: (event: unknown) => void,
  ): void;
  /** Set when the Launcher opened the App; null for an installed PWA (A11). */
  readonly opener: unknown;
  open(url: string, target: string): unknown | null;
}

export interface BridgeOptions {
  window: BridgeWindow;
  /** The App's own origin, announced in `hello` and never trusted by the Launcher (S3). */
  appOrigin?: string;
}

export interface BridgeDevice extends Device {
  readonly kind: 'bridge';
  /** No-op from a view; the tests use it to leave no timers behind. */
  connect(): Promise<void>;
  dispose(): void;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** A `WindowProxy`, structurally: posting to it and reading `closed` are all we need. */
function endpointOf(handle: unknown): BridgeEndpoint | null {
  if (typeof handle !== 'object' || handle === null) return null;
  return handle as BridgeEndpoint;
}

function isAlive(handle: unknown): boolean {
  const endpoint = endpointOf(handle);
  return endpoint !== null && endpoint.closed !== true;
}

/**
 * `stream.*` answers carry only what changed: `welcome` gives `playing`/`volume`, a
 * `stream.play` answer gives `playing`, and `blocked` is the Launcher's autoplay fact (O3).
 */
function mergeAudio(current: AudioState, data: unknown): AudioState | null {
  if (!isRecord(data)) return null;
  if (data.playing !== undefined && typeof data.playing !== 'boolean') return null;
  if (
    data.volume !== undefined &&
    (typeof data.volume !== 'number' || !Number.isFinite(data.volume))
  ) {
    return null;
  }
  if (data.blocked !== undefined && typeof data.blocked !== 'boolean') return null;
  return {
    playing: typeof data.playing === 'boolean' ? data.playing : current.playing,
    volume: typeof data.volume === 'number' ? data.volume : current.volume,
    blocked: typeof data.blocked === 'boolean' ? data.blocked : false,
  };
}

interface Pending {
  readonly op: OpName;
  readonly resolve: (data: unknown) => void;
  readonly reject: (error: DeviceError) => void;
  readonly timer: ReturnType<typeof setTimeout>;
  /** The image's length as it was before the transfer detached it (C3). */
  readonly sentBytes: number;
}

export function createBridgeDevice(options: BridgeOptions): BridgeDevice {
  const win = options.window;
  const appOrigin =
    options.appOrigin ?? (typeof location === 'undefined' ? '' : location.origin);

  let state: Link = 'none';
  let audio: AudioState = { playing: false, volume: 1, blocked: false };
  /** The window this App opened for the Launcher, when it was the one who opened it (W1). */
  let opened: unknown | null = null;
  const pending = new Map<string, Pending>();
  const listeners = new Set<() => void>();
  /** Per instance, because two App tabs share one Launcher's in-flight table (X6, §8). */
  const stamp = Math.random().toString(36).slice(2, 10);
  let nextId = 1;
  let ticker: ReturnType<typeof setInterval> | null = null;

  function setLink(next: Link): void {
    if (state === next) return;
    state = next;
    for (const listener of [...listeners]) listener();
  }

  /** Every handle the App holds, dead or alive (W2: the opener is the only asymmetry). */
  function handles(): unknown[] {
    const found: unknown[] = [];
    if (win.opener !== null && win.opener !== undefined) found.push(win.opener);
    if (opened !== null) found.push(opened);
    return found;
  }

  function liveHandles(): unknown[] {
    return handles().filter(isAlive);
  }

  function postTo(handle: unknown, message: Message, transfer?: Transferable[]): void {
    const endpoint = endpointOf(handle);
    if (endpoint === null) return;
    endpoint.postMessage(message, DEVICE_ORIGIN, transfer);
  }

  /** W6: whoever has a handle announces itself, to every handle it has. */
  function announce(): void {
    const live = liveHandles();
    if (live.length === 0) return;
    const message = hello('app', appOrigin);
    for (const handle of live) postTo(handle, message);
  }

  /** S4/S6: a session that is gone leaves nothing waiting and no link. */
  function disconnect(message: string): void {
    setLink('none');
    for (const [id, entry] of [...pending]) {
      clearTimeout(entry.timer);
      pending.delete(id);
      entry.reject(new DeviceError(message, 'unreachable'));
    }
  }

  /**
   * The 5 s tick and the two moments a window comes back into view. A handle that is closed
   * is dropped; with no handle the link is gone; with a handle and no session, re-announce
   * (the Launcher reloaded, or its page finished loading after we first said hello).
   */
  function check(): void {
    if (opened !== null && !isAlive(opened)) opened = null;
    const live = liveHandles();
    if (live.length === 0) {
      if (state !== 'none') disconnect('the bridge window closed');
      return;
    }
    if (state === 'none') announce();
  }

  function startTicker(): void {
    if (ticker !== null) return;
    ticker = setInterval(check, TICK_MS);
  }

  function settle(message: ResponseMessage): void {
    const entry = pending.get(message.id);
    if (!entry) return;
    pending.delete(message.id);
    clearTimeout(entry.timer);
    if (message.ok) {
      const data = parseReply(entry.op, message.data, entry.sentBytes);
      if (data === null) {
        entry.reject(
          new DeviceError('the player answered something this page could not read', 'unreachable'),
        );
        return;
      }
      entry.resolve(data);
      return;
    }
    entry.reject(new DeviceError(message.error.message, message.error.code, message.error.status));
  }

  /**
   * O5: the answer is parsed, never trusted. Each op's reader is the one the direct Device
   * uses, so the two links accept and reject the same documents.
   */
  function parseReply(op: OpName, data: unknown, sentBytes: number): unknown | null {
    switch (op) {
      case 'status':
        return parseStatus(data);
      case 'playlists':
        return parsePlaylists(data);
      case 'playlist':
        return parsePlaylistRows(data);
      case 'select':
        return parseSelectResult(data);
      case 'snapshot':
        return parseUploadResult(data, sentBytes);
      case 'rescan':
        return isRecord(data) && typeof data.ok === 'boolean' ? { ok: data.ok } : null;
      case 'play':
        return isRecord(data) ? {} : null;
      case 'library.search':
        return parseSearch(data);
      case 'library.list':
        return parseListing(data);
      case 'library.stats':
        return parseStats(data);
      case 'library.track':
        return parseTrack(data);
      case 'stream.play':
      case 'stream.pause':
      case 'stream.volume':
      case 'stream.state': {
        const next = mergeAudio(audio, data);
        if (next !== null) audio = next;
        return next;
      }
      default:
        return null;
    }
  }

  function onMessage(event: unknown): void {
    if (!isRecord(event)) return;
    // S2, X3: the origin is the one string, and the source is a handle we hold.
    if (event.origin !== DEVICE_ORIGIN) return;
    const source = event.source;
    if (!handles().some((handle) => handle === source)) return;

    const message = parseMessage(event.data);
    if (message === null) return;

    switch (message.t) {
      case 'welcome':
        // W7: a peer claiming to be the App is not the Launcher and is dropped.
        if (message.role !== 'launcher') return;
        audio = { playing: message.audio.playing, volume: message.audio.volume, blocked: false };
        setLink('bridge');
        return;
      case 'hello':
        if (message.role !== 'launcher') return;
        // W6: a valid hello is answered with welcome; if we have no session yet, say hello
        // back so the Launcher can open one (S1) — a welcome never triggers another hello.
        postTo(source, welcome({ playing: audio.playing, volume: audio.volume }));
        if (state === 'none') announce();
        return;
      case 'res':
        settle(message);
        return;
      case 'bye':
        disconnect('the bridge window closed');
        return;
      case 'req':
        // W3: the Launcher never initiates a request, so this can only be a mistake.
        return;
      default:
        return;
    }
  }

  function onForeground(): void {
    check();
  }

  function request<O extends OpName>(op: O, args?: OpArgs[O]): Promise<OpData[O]> {
    if (state !== 'bridge') return Promise.reject(NO_LINK);
    const live = liveHandles();
    const target = live[0];
    if (target === undefined) return Promise.reject(NO_LINK);

    const id = `${stamp}-${nextId++}`;
    // The wire sends the argument object as it stands and the Launcher validates it (O5);
    // TypeScript cannot project a generic indexed access onto that index signature.
    const payload = args as Readonly<Record<string, unknown>> | undefined;
    const message = req(id, op, payload ?? null);

    // C3: the image's buffer travels, not a copy, so the App's own is detached.
    const bytes = op === 'snapshot' ? payload?.['bytes'] : undefined;
    const transfer =
      bytes instanceof Uint8Array && bytes.buffer instanceof ArrayBuffer ? [bytes.buffer] : undefined;
    const sentBytes = bytes instanceof Uint8Array ? bytes.byteLength : 0;

    const deadline = OP_SPECS[op].timeout;
    return new Promise<OpData[O]>((resolve, reject) => {
      const timer = setTimeout(() => {
        pending.delete(id);
        reject(
          new DeviceError(
            `the player did not answer within ${Math.round(deadline / 1000)} s`,
            'timeout',
          ),
        );
        /* S1/§8: a Launcher that reloaded kept the window and lost the session, so our
         * requests are dropped and this deadline is how the App finds out. `hello` is
         * idempotent (W6), so re-announcing costs one message and heals the session. */
        announce();
      }, deadline);
      pending.set(id, {
        op,
        // The closed op table is what makes this the answer to this request; `parseReply`
        // has already produced that op's `OpData` shape.
        resolve: (data) => resolve(data as OpData[O]),
        reject,
        timer,
        sentBytes,
      });
      postTo(target, message, transfer);
    });
  }

  async function connect(): Promise<void> {
    const openedWindow = win.open(`${DEVICE_ORIGIN}/`, DEVICE_WINDOW);
    if (openedWindow === null || openedWindow === undefined) {
      throw new DeviceError(
        'the browser blocked the bridge window. Allow popups for this site, then press Connect.',
        'unreachable',
      );
    }
    opened = openedWindow;
    startTicker();
    if (state === 'none') announce();
  }

  function dispose(): void {
    if (ticker !== null) {
      clearInterval(ticker);
      ticker = null;
    }
    win.removeEventListener('message', onMessage);
    win.removeEventListener('focus', onForeground);
    win.removeEventListener('visibilitychange', onForeground);
    for (const [id, entry] of [...pending]) {
      clearTimeout(entry.timer);
      pending.delete(id);
      entry.reject(new DeviceError('the bridge window closed', 'unreachable'));
    }
    listeners.clear();
  }

  win.addEventListener('message', onMessage);
  win.addEventListener('focus', onForeground);
  win.addEventListener('visibilitychange', onForeground);
  startTicker();
  if (liveHandles().length > 0) announce();

  return {
    kind: 'bridge',
    link: () => state,
    request,
    onLinkChange(listener: () => void) {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },
    connect,
    dispose,
  };
}

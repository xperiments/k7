import { describe, expect, it } from 'vitest';
import {
  BRIDGE_VERSION as APP_VERSION,
  LIMITS as APP_LIMITS,
  OP_SPECS as APP_OPS,
  isOpName,
  parseMessage,
  type Message,
} from '../src/bridge/protocol';
import {
  BRIDGE_VERSION as LAUNCHER_VERSION,
  LIMITS as LAUNCHER_LIMITS,
  OP_SPECS as LAUNCHER_OPS,
  createLauncherBridge,
} from '../public/launcher.js';
import type { LauncherFetch, LauncherEvent, LauncherWindow } from '../public/launcher.js';

/**
 * §3 again, by hand. Both implementations are compared against this table rather than
 * against each other, so the two ends cannot drift *together* to a wrong value.
 */
const SPEC: Record<string, { timeout: number; bytes: boolean }> = {
  status: { timeout: 4_000, bytes: false },
  playlists: { timeout: 6_000, bytes: false },
  playlist: { timeout: 6_000, bytes: false },
  select: { timeout: 10_000, bytes: false },
  snapshot: { timeout: 30_000, bytes: true },
  rescan: { timeout: 10_000, bytes: false },
  play: { timeout: 10_000, bytes: false },
  'library.search': { timeout: 10_000, bytes: false },
  'library.list': { timeout: 10_000, bytes: false },
  'library.stats': { timeout: 10_000, bytes: false },
  'library.track': { timeout: 10_000, bytes: false },
  'stream.play': { timeout: 2_000, bytes: false },
  'stream.pause': { timeout: 2_000, bytes: false },
  'stream.volume': { timeout: 2_000, bytes: false },
  'stream.state': { timeout: 2_000, bytes: false },
};

const NAMES = Object.keys(SPEC).sort();
const APP = 'https://k7.xperiments.dev';
const DEVICE = 'http://k7.local';

describe('the bridge contract', () => {
  it('has the same version on both ends', () => {
    expect(LAUNCHER_VERSION).toBe(APP_VERSION);
    expect(APP_VERSION).toBe(1);
  });

  it('declares exactly the spec operations, on both ends', () => {
    expect(Object.keys(APP_OPS).sort()).toEqual(NAMES);
    expect(Object.keys(LAUNCHER_OPS).sort()).toEqual(NAMES);
    expect(NAMES).toHaveLength(15);
  });

  it('agrees on every timeout and the one binary op', () => {
    for (const [name, spec] of Object.entries(APP_OPS)) {
      expect(spec, `App op ${name}`).toEqual(SPEC[name]);
      expect(LAUNCHER_OPS[name], `Launcher op ${name}`).toEqual(spec);
    }
  });

  it('names only ops the App can parse', () => {
    for (const name of Object.keys(LAUNCHER_OPS)) {
      expect(isOpName(name), `Launcher op ${name}`).toBe(true);
    }
  });

  it('agrees on the size caps in §5', () => {
    expect(LAUNCHER_LIMITS).toEqual(APP_LIMITS);
    expect(APP_LIMITS).toEqual({ request: 256 * 1024, json: 64 * 1024, response: 1024 * 1024 });
  });
});

/* -------------------------------------------------------------------------- runtime ---- */

class WindowProxyFake implements LauncherWindow {
  readonly posts: { message: unknown; targetOrigin: string }[] = [];
  private handlers: ((event: LauncherEvent) => void)[] = [];

  addEventListener(_type: 'message', handler: (event: LauncherEvent) => void): void {
    this.handlers.push(handler);
  }

  removeEventListener(_type: 'message', handler: (event: LauncherEvent) => void): void {
    const at = this.handlers.indexOf(handler);
    if (at >= 0) this.handlers.splice(at, 1);
  }

  postMessage(message: unknown, targetOrigin: string): void {
    this.posts.push({ message, targetOrigin });
  }

  deliver(origin: string, source: unknown, data: unknown): void {
    for (const handler of [...this.handlers]) handler({ origin, source, data });
  }
}

/** The App's window: replies land here, not in the Launcher's own window. */
function peer() {
  const posts: { message: unknown; targetOrigin: string }[] = [];
  return {
    posts,
    postMessage(message: unknown, targetOrigin: string): void {
      posts.push({ message, targetOrigin });
    },
  };
}

function parsedAt(posts: { message: unknown }[], at: number): Message {
  const message = parseMessage(posts[at]?.message);
  if (message === null) throw new Error(`post ${at} is not a bridge message`);
  return message;
}

const tick = (): Promise<void> =>
  new Promise((resolve) => {
    setImmediate(resolve);
  });

describe('the two ends on one wire', () => {
  it('emits welcome and res messages the App-side parser accepts', async () => {
    const win = new WindowProxyFake();
    const source = peer();
    const fetch: LauncherFetch = () =>
      Promise.resolve({ ok: true, status: 200, text: () => Promise.resolve('{"card":true,"tracks":3}') });
    const bridge = createLauncherBridge({
      win,
      fetch,
      origin: DEVICE,
      appOrigins: [APP],
      // Deadlines that never fire: this test is about shapes, not time.
      timers: { setTimeout: () => 0, clearTimeout: () => {} },
      audio: { playing: false, blocked: false, volume: 1, play: () => true, pause: () => {} },
    });
    bridge.start();

    win.deliver(APP, source, { t: 'hello', v: 1, role: 'app', app: APP });
    expect(parsedAt(source.posts, 0)).toEqual({
      t: 'welcome',
      v: APP_VERSION,
      role: 'launcher',
      audio: { playing: false, volume: 1 },
    });

    win.deliver(APP, source, { t: 'req', id: '1', op: 'status', args: null });
    await tick();
    expect(parsedAt(source.posts, 1)).toEqual({
      t: 'res',
      id: '1',
      ok: true,
      data: { card: true, tracks: 3 },
    });

    win.deliver(APP, source, { t: 'req', id: '2', op: 'library.delete', args: null });
    expect(parsedAt(source.posts, 2)).toEqual({
      t: 'res',
      id: '2',
      ok: false,
      error: { code: 'unknown-op', message: expect.any(String) },
    });

    win.deliver(APP, source, { t: 'req', id: '3', op: 'play', args: { path: 42 } });
    expect(parsedAt(source.posts, 3)).toEqual({
      t: 'res',
      id: '3',
      ok: false,
      error: { code: 'bad-args', message: expect.any(String) },
    });

    bridge.stop();
  });

  it('gives every message an explicit targetOrigin', () => {
    const win = new WindowProxyFake();
    const source = peer();
    const bridge = createLauncherBridge({
      win,
      fetch: () => Promise.resolve({ ok: true, status: 200, text: () => Promise.resolve('{}') }),
      origin: DEVICE,
      appOrigins: [APP],
      timers: { setTimeout: () => 0, clearTimeout: () => {} },
      audio: { playing: false, blocked: false, volume: 1, play: () => true, pause: () => {} },
    });
    bridge.start();
    win.deliver(APP, source, { t: 'hello', v: 1, role: 'app' });
    win.deliver(APP, source, { t: 'req', id: '1', op: 'stream.state', args: null });
    expect(source.posts.length).toBeGreaterThan(0);
    for (const post of source.posts) expect(post.targetOrigin).toBe(APP);
    bridge.stop();
  });
});

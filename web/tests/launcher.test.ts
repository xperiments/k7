import { describe, expect, it } from 'vitest';
import { createLauncherBridge } from '../public/launcher.js';
import type {
  LauncherAudio,
  LauncherBridge,
  LauncherEvent,
  LauncherFetch,
  LauncherRequestInit,
  LauncherResponse,
  LauncherTimers,
  LauncherWindow,
} from '../public/launcher.js';
import {
  bye,
  hello,
  ok,
  parseMessage,
  req,
  type OpName,
  type RequestArgs,
  type ResponseMessage,
} from '../src/bridge/protocol';

const APP = 'https://k7.xperiments.dev';
const DEVICE = 'http://k7.local';

/* --------------------------------------------------------------------------- fakes ---- */

class FakeWindow implements LauncherWindow {
  readonly posts: { readonly message: unknown; readonly targetOrigin: string }[] = [];
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

  get listening(): number {
    return this.handlers.length;
  }

  clear(): void {
    this.posts.length = 0;
  }
}

interface Clock {
  now(): number;
  timers: LauncherTimers;
  advance(ms: number): void;
}

/** A deterministic clock: timers fire in due order, and `now()` moves with them. */
function fakeClock(): Clock {
  let time = 0;
  let nextId = 1;
  const armed = new Map<unknown, { at: number; run: () => void }>();
  return {
    now: () => time,
    timers: {
      setTimeout(run, ms) {
        const handle = nextId++;
        armed.set(handle, { at: time + ms, run });
        return handle;
      },
      clearTimeout(handle) {
        armed.delete(handle);
      },
    },
    advance(ms) {
      const target = time + ms;
      for (;;) {
        let due: { handle: unknown; entry: { at: number; run: () => void } } | null = null;
        for (const [handle, entry] of armed) {
          if (entry.at <= target && (due === null || entry.at < due.entry.at)) due = { handle, entry };
        }
        if (due === null) break;
        armed.delete(due.handle);
        time = due.entry.at;
        due.entry.run();
      }
      time = target;
    },
  };
}

interface PendingCall {
  readonly url: string;
  readonly init: LauncherRequestInit | undefined;
  respond(response: LauncherResponse): void;
  reject(error: unknown): void;
}

function fakeNetwork(): { calls: PendingCall[]; fetch: LauncherFetch } {
  const calls: PendingCall[] = [];
  const fetch: LauncherFetch = (url, init) =>
    new Promise<LauncherResponse>((resolve, reject) => {
      calls.push({ url, init, respond: resolve, reject });
    });
  return { calls, fetch };
}

function json(value: unknown, status = 200): LauncherResponse {
  const body = typeof value === 'string' ? value : JSON.stringify(value);
  return { ok: status >= 200 && status < 300, status, text: () => Promise.resolve(body) };
}

function bodyOf(call: PendingCall | undefined): Record<string, unknown> {
  if (!call) throw new Error('no request was made');
  const body: unknown = typeof call.init?.body === 'string' ? JSON.parse(call.init.body) : null;
  if (typeof body !== 'object' || body === null || Array.isArray(body)) {
    throw new Error(`the request had no JSON body: ${String(call.init?.body)}`);
  }
  return body as Record<string, unknown>;
}

function fakeAudio(overrides: Partial<LauncherAudio> = {}): LauncherAudio {
  const audio: LauncherAudio = {
    playing: false,
    blocked: false,
    volume: 1,
    play() {
      audio.playing = true;
      return Promise.resolve();
    },
    pause() {
      audio.playing = false;
    },
    ...overrides,
  };
  return audio;
}

/* ------------------------------------------------------------------------- harness ---- */

interface Harness {
  readonly win: FakeWindow;
  readonly network: ReturnType<typeof fakeNetwork>;
  readonly clock: Clock;
  readonly bridge: LauncherBridge;
  readonly audio: LauncherAudio;
  /** The App's `WindowProxy`, one per harness: where replies have to arrive. */
  readonly app: FakeWindow;
}

function harness(options: { audio?: Partial<LauncherAudio>; appOrigins?: string[] } = {}): Harness {
  const win = new FakeWindow();
  const network = fakeNetwork();
  const clock = fakeClock();
  const audio = fakeAudio(options.audio);
  const bridge = createLauncherBridge({
    win,
    fetch: network.fetch,
    origin: DEVICE,
    appOrigins: options.appOrigins ?? [APP],
    now: clock.now,
    timers: clock.timers,
    audio,
  });
  bridge.start();
  return { win, network, clock, bridge, audio, app: new FakeWindow() };
}

const tick = (): Promise<void> =>
  new Promise((resolve) => {
    setImmediate(resolve);
  });

function announce(h: Harness, source: object = h.app, origin = APP): void {
  h.win.deliver(origin, source, hello('app'));
}

function send(h: Harness, id: string, op: OpName, args: RequestArgs = null, source: object = h.app): void {
  h.win.deliver(APP, source, req(id, op, args));
}

function responses(h: Harness): ResponseMessage[] {
  const out: ResponseMessage[] = [];
  for (const post of h.app.posts) {
    const message = parseMessage(post.message);
    if (message !== null && message.t === 'res') out.push(message);
  }
  return out;
}

function onlyResponse(h: Harness): ResponseMessage {
  const list = responses(h);
  if (list.length !== 1) throw new Error(`expected one response, got ${list.length}`);
  const [first] = list;
  if (!first) throw new Error('expected one response, got none');
  return first;
}

function codeOf(message: ResponseMessage): string {
  return message.ok ? 'ok' : message.error.code;
}

/* --------------------------------------------------------------------------- tests ---- */

describe('the handshake', () => {
  it('answers a trusted hello with welcome, stamps its role and version, and opens a session', () => {
    const h = harness();
    announce(h);

    expect(h.app.posts).toHaveLength(1);
    const [post] = h.app.posts;
    expect(post?.targetOrigin).toBe(APP);
    expect(parseMessage(post?.message)).toEqual({
      t: 'welcome',
      v: 1,
      role: 'launcher',
      audio: { playing: false, volume: 1 },
    });
    const sessions = h.bridge.sessions();
    expect(sessions).toHaveLength(1);
    expect(sessions[0]?.origin).toBe(APP);
    expect(sessions[0]?.version).toBe(1);
    expect(sessions[0]?.source).toBe(h.app);
  });

  it('drops a hello from an origin that is not on the baked list', () => {
    const h = harness();
    h.win.deliver('https://evil.example', h.app, hello('app'));
    h.win.deliver('*', h.app, hello('app'));
    h.win.deliver('http://k7.local', h.app, hello('app'));
    expect(h.app.posts).toEqual([]);
    expect(h.bridge.sessions()).toEqual([]);
  });

  it('drops a hello with the wrong role instead of answering it (W7)', () => {
    const h = harness();
    h.win.deliver(APP, h.app, hello('launcher'));
    expect(h.app.posts).toEqual([]);
    expect(h.bridge.sessions()).toEqual([]);
  });

  it('reads a hello with unknown fields as a hello', () => {
    const h = harness();
    h.win.deliver(APP, h.app, { t: 'hello', v: 1, role: 'app', app: APP, extra: { a: 1 } });
    expect(parseMessage(h.app.posts[0]?.message)?.t).toBe('welcome');
    expect(h.bridge.sessions()).toHaveLength(1);
  });

  it('drops a malformed req rather than answering a message it cannot read', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    h.win.deliver(APP, h.app, { t: 'req', id: 7, op: 'status', args: null });
    h.win.deliver(APP, h.app, { t: 'req', id: '', op: 'status', args: null });
    h.win.deliver(APP, h.app, { t: 'req', id: '1', op: 'status', args: [] });
    await tick();
    expect(h.app.posts).toEqual([]);
    expect(h.network.calls).toHaveLength(0);
  });

  it('welcomes a peer on another version but refuses its requests with `version` (W8)', async () => {
    const h = harness();
    h.win.deliver(APP, h.app, { t: 'hello', v: 2, role: 'app' });
    expect(parseMessage(h.app.posts[0]?.message)).toEqual({
      t: 'welcome',
      v: 1,
      role: 'launcher',
      audio: { playing: false, volume: 1 },
    });
    h.app.clear();

    send(h, '1', 'status');
    await tick();
    expect(codeOf(onlyResponse(h))).toBe('version');
    expect(h.network.calls).toHaveLength(0);
  });
});

describe('requests', () => {
  it('drops a req from a source with no session, and nothing reaches the device', async () => {
    const h = harness();
    h.win.deliver(APP, {}, req('1', 'status'));
    await tick();
    expect(h.app.posts).toEqual([]);
    expect(h.network.calls).toHaveLength(0);
  });

  it('round-trips status against GET /api/status', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '1', 'status');
    await tick();
    expect(h.network.calls).toHaveLength(1);
    expect(h.network.calls[0]?.url).toBe('http://k7.local/api/status');
    expect(h.network.calls[0]?.init?.method).toBe('GET');

    h.network.calls[0]?.respond(json({ card: true, tracks: 3, order: 'tree' }));
    await tick();
    expect(onlyResponse(h)).toEqual(ok('1', { card: true, tracks: 3, order: 'tree' }));
  });

  it('answers an op outside the table with `unknown-op` and calls nothing', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    h.win.deliver(APP, h.app, { t: 'req', id: '9', op: 'library.delete', args: null });
    await tick();
    const response = onlyResponse(h);
    expect(codeOf(response)).toBe('unknown-op');
    expect(h.network.calls).toHaveLength(0);
  });

  it('refuses a request over 256 KiB before it is sent', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '5', 'snapshot', { bytes: new Uint8Array(256 * 1024 + 1) });
    await tick();
    expect(codeOf(onlyResponse(h))).toBe('too-large');
    expect(h.network.calls).toHaveLength(0);
  });

  it('refuses args that fail the op schema', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '6', 'play', { path: 42 });
    await tick();
    expect(codeOf(onlyResponse(h))).toBe('bad-args');
    expect(h.network.calls).toHaveLength(0);
  });

  it('answers `timeout` at the op budget while the request is still outstanding', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '1', 'status');
    await tick();
    expect(h.network.calls).toHaveLength(1);

    h.clock.advance(4_000);
    await tick();
    expect(codeOf(onlyResponse(h))).toBe('timeout');
  });

  it('runs one request at a time: the second fetch starts only after the first settles', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '1', 'status');
    send(h, '2', 'status');
    await tick();
    expect(h.network.calls).toHaveLength(1);

    h.network.calls[0]?.respond(json({ card: true }));
    await tick();
    expect(h.network.calls).toHaveLength(2);
    expect(responses(h).map((message) => message.id)).toEqual(['1']);

    h.network.calls[1]?.respond(json({ card: false }));
    await tick();
    expect(responses(h).map((message) => message.id)).toEqual(['1', '2']);
  });

  it('maps an unanswered request to `unreachable` and a refusal to `refused` with its status', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '1', 'status');
    await tick();
    h.network.calls[0]?.reject(new Error('no route to host'));
    await tick();
    expect(codeOf(onlyResponse(h))).toBe('unreachable');

    h.app.clear();
    send(h, '2', 'rescan');
    await tick();
    h.network.calls[1]?.respond(json({ error: 'A scan is already running' }, 409));
    await tick();
    const refused = onlyResponse(h);
    expect(refused).toEqual({
      t: 'res',
      id: '2',
      ok: false,
      error: { code: 'refused', message: 'A scan is already running', status: 409 },
    });
  });

  it('refuses a response over 1 MiB', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '1', 'status');
    await tick();
    h.network.calls[0]?.respond(json({ blob: 'x'.repeat(1024 * 1024) }));
    await tick();
    expect(codeOf(onlyResponse(h))).toBe('too-large');
  });

  it('answers a second request with the same in-flight id with `bad-message` (X6)', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '1', 'status');
    await tick();
    send(h, '1', 'status');
    await tick();

    const response = onlyResponse(h);
    expect(codeOf(response)).toBe('bad-message');
    expect(response.id).toBe('1');
    expect(h.network.calls).toHaveLength(1);
  });

  it('never posts without an explicit App targetOrigin', async () => {
    const h = harness();
    announce(h);
    send(h, '1', 'status');
    await tick();
    h.network.calls[0]?.respond(json({ card: true }));
    await tick();
    expect(h.app.posts.length).toBeGreaterThan(1);
    for (const post of h.app.posts) expect(post.targetOrigin).toBe(APP);
  });
});

describe('library over MCP', () => {
  it('initializes once and calls library_search with the device argument names', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '1', 'library.search', { query: 'aphex', bpmMin: 120, key: ['8A'], sort: 'bpm', limit: 10 });
    await tick();
    expect(h.network.calls).toHaveLength(1);
    expect(h.network.calls[0]?.url).toBe('http://k7.local/mcp');
    expect(bodyOf(h.network.calls[0]).method).toBe('initialize');

    h.network.calls[0]?.respond(json({ jsonrpc: '2.0', id: 1, result: { protocolVersion: '2025-11-25' } }));
    await tick();
    expect(h.network.calls).toHaveLength(2);
    const call = bodyOf(h.network.calls[1]);
    expect(call.method).toBe('tools/call');
    expect(call.params).toEqual({
      name: 'library_search',
      arguments: { query: 'aphex', bpm_min: 120, key: ['8A'], sort: 'bpm', limit: 10 },
    });
    expect(h.network.calls[1]?.init?.headers?.['MCP-Protocol-Version']).toBe('2025-11-25');

    h.network.calls[1]?.respond(
      json({
        jsonrpc: '2.0',
        id: 2,
        result: { content: [{ type: 'text', text: JSON.stringify({ rows: [], total: 0 }) }] },
      }),
    );
    await tick();
    expect(onlyResponse(h)).toEqual(ok('1', { rows: [], total: 0 }));
  });

  it('reports a tool that answered isError as `refused`', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '1', 'library.stats');
    await tick();
    h.network.calls[0]?.respond(json({ jsonrpc: '2.0', id: 1, result: { protocolVersion: '2025-11-25' } }));
    await tick();
    h.network.calls[1]?.respond(
      json({ jsonrpc: '2.0', id: 2, result: { isError: true, content: [{ type: 'text', text: 'no snapshot' }] } }),
    );
    await tick();
    const response = onlyResponse(h);
    expect(codeOf(response)).toBe('refused');
    if (response.ok) throw new Error('expected a failure');
    expect(response.error.message).toBe('no snapshot');
  });

  it('refuses library_track without a path and does not call the device', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '1', 'library.track', {});
    await tick();
    expect(codeOf(onlyResponse(h))).toBe('bad-args');
    expect(h.network.calls).toHaveLength(0);
  });
});

describe('the stream', () => {
  it('drives the injected audio without touching the network', async () => {
    const h = harness();
    announce(h);
    h.app.clear();

    send(h, '1', 'stream.state');
    await tick();
    expect(onlyResponse(h)).toEqual(ok('1', { playing: false, volume: 1, blocked: false }));

    h.app.clear();
    send(h, '2', 'stream.volume', { level: 0.25 });
    await tick();
    expect(h.audio.volume).toBe(0.25);
    expect(onlyResponse(h)).toEqual(ok('2', { volume: 0.25 }));

    h.app.clear();
    send(h, '3', 'stream.play');
    await tick();
    expect(h.audio.playing).toBe(true);
    expect(onlyResponse(h)).toEqual(ok('3', { playing: true }));

    h.app.clear();
    send(h, '4', 'stream.pause');
    await tick();
    expect(h.audio.playing).toBe(false);
    expect(onlyResponse(h)).toEqual(ok('4', { playing: false }));
    expect(h.network.calls).toHaveLength(0);
  });

  it('answers `blocked` when the browser refuses play()', async () => {
    const h = harness({
      audio: {
        play: () => Promise.reject(Object.assign(new Error('not allowed'), { name: 'NotAllowedError' })),
      },
    });
    announce(h);
    h.app.clear();

    send(h, '1', 'stream.play');
    await tick();
    const response = onlyResponse(h);
    expect(codeOf(response)).toBe('blocked');
    expect(h.audio.blocked).toBe(true);
    expect(h.network.calls).toHaveLength(0);
  });

  it('also answers `blocked` when play() resolves false', async () => {
    const h = harness({ audio: { play: () => false } });
    announce(h);
    h.app.clear();

    send(h, '1', 'stream.play');
    await tick();
    expect(codeOf(onlyResponse(h))).toBe('blocked');
  });
});

describe('sessions', () => {
  it('clears the session on bye: the next req is dropped', async () => {
    const h = harness();
    announce(h);
    send(h, '1', 'status');
    await tick();
    h.network.calls[0]?.respond(json({ card: true }));
    await tick();
    expect(h.bridge.sessions()).toHaveLength(1);

    h.win.deliver(APP, h.app, bye());
    expect(h.bridge.sessions()).toEqual([]);
    h.app.clear();

    send(h, '2', 'status');
    await tick();
    expect(h.app.posts).toEqual([]);
    expect(h.network.calls).toHaveLength(1);
  });

  it('expires a session after 60 s of silence, refreshed by every request', async () => {
    const h = harness();
    announce(h);

    h.clock.advance(59_000);
    send(h, '1', 'status');
    await tick();
    expect(h.network.calls).toHaveLength(1);
    h.network.calls[0]?.respond(json({ card: true }));
    await tick();
    expect(h.bridge.sessions()).toHaveLength(1);

    h.clock.advance(59_000);
    expect(h.bridge.sessions()).toHaveLength(1);

    h.clock.advance(2_000);
    expect(h.bridge.sessions()).toEqual([]);

    h.app.clear();
    send(h, '2', 'status');
    await tick();
    expect(h.app.posts).toEqual([]);
    expect(h.network.calls).toHaveLength(1);
  });

  it('forgets every session when stopped, and says bye to each origin', () => {
    const h = harness();
    announce(h);
    h.app.clear();

    h.bridge.stop();
    expect(h.win.listening).toBe(0);
    expect(h.bridge.sessions()).toEqual([]);
    expect(h.app.posts).toHaveLength(1);
    expect(parseMessage(h.app.posts[0]?.message)).toEqual(bye());
    expect(h.app.posts[0]?.targetOrigin).toBe(APP);
  });

  it('hands the page the same audio object it was given', () => {
    const h = harness();
    expect(h.bridge.audio()).toBe(h.audio);
  });
});

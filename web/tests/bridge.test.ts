import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  OP_SPECS,
  bye,
  fail,
  hello,
  ok,
  parseMessage,
  req,
  welcome,
  type Message,
  type RequestMessage,
  type ResponseMessage,
} from '../src/bridge/protocol';
import { DEVICE_ORIGIN } from '../src/config';
import { TICK_MS, createBridgeDevice, type BridgeDevice, type BridgeWindow } from '../src/device/bridge';

/**
 * The App's half of the bridge, adversarially: only `http://k7.local`, only the window the
 * App holds, never a `req` before a `welcome`, the op's own budget as the deadline, and the
 * wire's `code`/`status` carried through to the caller.
 *
 * The messages are built and read with the real `protocol.ts`, and the deadlines are the
 * real `OP_SPECS`, so a table that moved would move these tests with it.
 */

const APP = 'https://k7.xperiments.dev';

const statusBody = {
  card: true,
  tracks: 4,
  generation: 2,
  library: 'sha256:abc',
  order: 'tree',
  playlist: null,
  playing: { title: 'A track', elapsed_ms: 12.5 },
};

const tick = (): Promise<void> =>
  new Promise((resolve) => {
    setImmediate(resolve);
  });

interface Post {
  readonly message: unknown;
  readonly targetOrigin: string;
  readonly transfer?: Transferable[];
}

/** A `WindowProxy`: the only thing the App can post to, and the only thing it may hear. */
interface Endpoint {
  readonly posts: Post[];
  postMessage(message: unknown, targetOrigin: string, transfer?: Transferable[]): void;
  readonly closed: boolean;
  close(): void;
}

function fakeEndpoint(): Endpoint {
  const posts: Post[] = [];
  let closed = false;
  return {
    posts,
    postMessage(message, targetOrigin, transfer) {
      posts.push({ message, targetOrigin, transfer });
    },
    get closed() {
      return closed;
    },
    close() {
      closed = true;
    },
  };
}

class FakeWindow implements BridgeWindow {
  readonly opener: unknown;
  readonly opened: { readonly url: string; readonly target: string }[] = [];
  openResult: unknown | null = null;
  private listeners = new Map<string, Set<(event: unknown) => void>>();

  constructor(opener: unknown = null) {
    this.opener = opener;
  }

  addEventListener(type: 'message' | 'focus' | 'visibilitychange', handler: (event: unknown) => void): void {
    const set = this.listeners.get(type) ?? new Set<(event: unknown) => void>();
    set.add(handler);
    this.listeners.set(type, set);
  }

  removeEventListener(type: 'message' | 'focus' | 'visibilitychange', handler: (event: unknown) => void): void {
    this.listeners.get(type)?.delete(handler);
  }

  open(url: string, target: string): unknown | null {
    this.opened.push({ url, target });
    return this.openResult;
  }

  /** Deliver a `message` event, as a browser would. */
  deliver(origin: string, source: unknown, data: unknown): void {
    for (const handler of [...(this.listeners.get('message') ?? [])]) {
      handler({ origin, source, data });
    }
  }

  /** Both of the two events the App re-checks a handle on. */
  foreground(): void {
    for (const type of ['focus', 'visibilitychange']) {
      for (const handler of [...(this.listeners.get(type) ?? [])]) handler(undefined);
    }
  }
}

const devices: BridgeDevice[] = [];

function harness(options: { opener?: unknown } = {}): {
  launcher: Endpoint;
  win: FakeWindow;
  device: BridgeDevice;
} {
  const launcher = fakeEndpoint();
  const win = new FakeWindow(options.opener === undefined ? launcher : options.opener);
  const device = createBridgeDevice({ window: win, appOrigin: APP });
  devices.push(device);
  return { launcher, win, device };
}

/** A harness whose Launcher has already welcomed the App: a live session. */
function paired(): { launcher: Endpoint; win: FakeWindow; device: BridgeDevice } {
  const h = harness();
  h.launcher.posts.length = 0;
  h.win.deliver(DEVICE_ORIGIN, h.launcher, welcome({ playing: false, volume: 1 }));
  return h;
}

function lastRequest(endpoint: Endpoint): RequestMessage {
  for (let at = endpoint.posts.length - 1; at >= 0; at -= 1) {
    const message = parseMessage(endpoint.posts[at]?.message);
    if (message !== null && message.t === 'req') return message;
  }
  throw new Error('no request was posted');
}

function postsOf(endpoint: Endpoint): (Message | null)[] {
  return endpoint.posts.map((post) => parseMessage(post.message));
}

afterEach(() => {
  for (const device of devices.splice(0)) device.dispose();
  vi.useRealTimers();
});

describe('the handshake', () => {
  it('says hello to the Launcher that opened it, on start', () => {
    const { launcher } = harness();

    expect(launcher.posts).toHaveLength(1);
    const [post] = launcher.posts;
    expect(post?.targetOrigin).toBe(DEVICE_ORIGIN);
    expect(parseMessage(post?.message)).toEqual(hello('app', APP));
  });

  it('answers a Launcher hello with its own welcome', () => {
    const { launcher, win, device } = harness();
    launcher.posts.length = 0;

    win.deliver(DEVICE_ORIGIN, launcher, hello('launcher'));
    const welcomes = postsOf(launcher).filter((message) => message !== null && message.t === 'welcome');
    expect(welcomes).toHaveLength(1);
    expect(welcomes[0]).toMatchObject({ t: 'welcome', v: 1, role: 'launcher' });
    expect(device.link()).toBe('none');
  });

  it('refuses to send a request before a welcome (W6)', async () => {
    const { launcher, device } = harness();

    await expect(device.request('status')).rejects.toMatchObject({
      name: 'DeviceError',
      code: 'unreachable',
      message: 'no player is connected',
    });
    expect(launcher.posts.filter((post) => parseMessage(post.message)?.t === 'req')).toEqual([]);
  });

  it('drops a welcome whose origin is not exactly http://k7.local, or whose source is not ours', () => {
    const { launcher, win, device } = harness();
    const audio = { playing: true, volume: 1 };

    win.deliver('https://k7.local.evil.example', launcher, welcome(audio));
    win.deliver('http://k7.local:8080', launcher, welcome(audio));
    win.deliver('https://k7.xperiments.dev', launcher, welcome(audio));
    win.deliver(DEVICE_ORIGIN, {}, welcome(audio));
    win.deliver(DEVICE_ORIGIN, null, welcome(audio));

    expect(device.link()).toBe('none');
  });
});

describe('requests', () => {
  it('round-trips a request and its answer by id, parsing the answer', async () => {
    const { launcher, win, device } = paired();

    const pending = device.request('status');
    await tick();
    const sent = lastRequest(launcher);
    expect(sent.op).toBe('status');
    expect(sent.args).toBeNull();

    win.deliver(DEVICE_ORIGIN, launcher, ok(sent.id, statusBody));
    await expect(pending).resolves.toMatchObject({
      card: true,
      tracks: 4,
      playing: { title: 'A track', elapsed: 12.5 },
    });
  });

  it('gives every request of one device its own id', async () => {
    const { launcher, win, device } = paired();

    const first = device.request('status');
    const firstId = lastRequest(launcher).id;
    const second = device.request('playlists');
    const secondId = lastRequest(launcher).id;
    expect(secondId).not.toBe(firstId);

    win.deliver(DEVICE_ORIGIN, launcher, ok(firstId, statusBody));
    win.deliver(
      DEVICE_ORIGIN,
      launcher,
      ok(secondId, { selected: null, walking: false, missing: 0, lists: [] }),
    );
    await expect(first).resolves.toMatchObject({ card: true });
    await expect(second).resolves.toEqual({ selected: null, walking: false, missing: 0, lists: [] });
  });

  it('rejects at the op budget with `timeout`', async () => {
    vi.useFakeTimers();
    const { device } = paired();

    const pending = device.request('status');
    const assertion = expect(pending).rejects.toMatchObject({
      name: 'DeviceError',
      code: 'timeout',
    });
    await vi.advanceTimersByTimeAsync(OP_SPECS.status.timeout - 1);
    // Still waiting a millisecond before the budget: nothing has been decided yet.
    await vi.advanceTimersByTimeAsync(1);
    await assertion;
  });

  it('propagates `unknown-op` as the wire named it', async () => {
    const { launcher, win, device } = paired();

    const pending = device.request('status');
    const sent = lastRequest(launcher);
    win.deliver(
      DEVICE_ORIGIN,
      launcher,
      fail(sent.id, 'unknown-op', `the bridge has no operation named ${sent.op}`),
    );

    await expect(pending).rejects.toMatchObject({
      name: 'DeviceError',
      code: 'unknown-op',
      message: 'the bridge has no operation named status',
    });
  });

  it('propagates `refused` with the device status the Launcher reported', async () => {
    const { launcher, win, device } = paired();

    const pending = device.request('rescan');
    const sent = lastRequest(launcher);
    win.deliver(DEVICE_ORIGIN, launcher, fail(sent.id, 'refused', 'a scan is already running', 409));

    await expect(pending).rejects.toMatchObject({
      code: 'refused',
      status: 409,
      message: 'a scan is already running',
    });
  });

  it('ignores an answer from a wrong origin or a wrong source', async () => {
    const { launcher, win, device } = paired();

    const pending = device.request('status');
    const sent = lastRequest(launcher);
    win.deliver('https://evil.example', launcher, ok(sent.id, { ...statusBody, card: false }));
    win.deliver(DEVICE_ORIGIN, {}, ok(sent.id, { ...statusBody, card: false }));

    // Neither was heard, so the request is still waiting; the right answer settles it.
    win.deliver(DEVICE_ORIGIN, launcher, ok(sent.id, statusBody));
    await expect(pending).resolves.toMatchObject({ card: true });
  });

  it('ignores an answer with no matching request', async () => {
    const { launcher, win, device } = paired();

    win.deliver(DEVICE_ORIGIN, launcher, ok('no-such-id', statusBody));
    expect(device.link()).toBe('bridge');
  });

  it('treats an answer it cannot parse as a failure, not as data', async () => {
    const { launcher, win, device } = paired();

    const pending = device.request('status');
    const sent = lastRequest(launcher);
    win.deliver(DEVICE_ORIGIN, launcher, ok(sent.id, { nothing: 'like a status' }));

    await expect(pending).rejects.toMatchObject({ code: 'unreachable' });
  });

  it('carries the stream facts the welcome established, with `blocked` false by default', async () => {
    const { launcher, win, device } = harness();
    launcher.posts.length = 0;
    win.deliver(DEVICE_ORIGIN, launcher, welcome({ playing: true, volume: 0.4 }));

    const pending = device.request('stream.state');
    const sent = lastRequest(launcher);
    // The Launcher's stream answers carry what changed, not the whole picture.
    win.deliver(DEVICE_ORIGIN, launcher, ok(sent.id, { playing: false }));
    await expect(pending).resolves.toEqual({ playing: false, volume: 0.4, blocked: false });
  });

  it('never posts with a wildcard target origin', () => {
    const { launcher, device } = harness();
    void device;
    for (const post of launcher.posts) expect(post.targetOrigin).toBe(DEVICE_ORIGIN);
  });
});

describe('transfers', () => {
  it('sends the snapshot buffer in the transfer list, never a copy (C3)', async () => {
    const { launcher, win, device } = paired();

    const bytes = new Uint8Array([1, 2, 3]);
    const pending = device.request('snapshot', { bytes });
    const post = launcher.posts[launcher.posts.length - 1];
    const sent = lastRequest(launcher);

    expect(sent.op).toBe('snapshot');
    expect(sent.args?.['bytes']).toBe(bytes);
    expect(post?.transfer).toEqual([bytes.buffer]);

    // The Launcher's receipt need not repeat the length: the App knew it before posting.
    win.deliver(DEVICE_ORIGIN, launcher, ok(sent.id, { applied: true }));
    await expect(pending).resolves.toEqual({ bytes: 3 });
  });
});

describe('sessions', () => {
  it('goes none on bye, rejects what was in flight, and tells the page', async () => {
    const { launcher, win, device } = paired();
    const changes: string[] = [];
    device.onLinkChange(() => changes.push(device.link()));

    const pending = device.request('status');
    win.deliver(DEVICE_ORIGIN, launcher, bye());

    expect(device.link()).toBe('none');
    expect(changes).toEqual(['none']);
    await expect(pending).rejects.toMatchObject({ code: 'unreachable' });
  });

  it('goes none when the Launcher window closes, on the 5 s tick', async () => {
    vi.useFakeTimers();
    const { launcher, device } = paired();
    expect(device.link()).toBe('bridge');

    launcher.close();
    await vi.advanceTimersByTimeAsync(TICK_MS);
    expect(device.link()).toBe('none');
  });

  it('goes none when the Launcher window closes and the window is focused', () => {
    const { launcher, win, device } = paired();
    expect(device.link()).toBe('bridge');

    launcher.close();
    win.foreground();
    expect(device.link()).toBe('none');
  });

  it('re-hellos when a request times out, because a reloaded Launcher forgot the session', async () => {
    vi.useFakeTimers();
    const { launcher, device } = paired();
    const pending = device.request('status');
    const assertion = expect(pending).rejects.toMatchObject({ code: 'timeout' });
    await vi.advanceTimersByTimeAsync(OP_SPECS.status.timeout);
    await assertion;

    expect(parseMessage(launcher.posts[launcher.posts.length - 1]?.message)).toEqual(hello('app', APP));
  });

  it('re-hellos after a reconnect, and is bridged again on welcome', () => {
    const { launcher, win, device } = paired();
    win.deliver(DEVICE_ORIGIN, launcher, bye());
    expect(device.link()).toBe('none');

    // A Launcher reloaded keeps the window and loses the session (S1): bring it back.
    launcher.posts.length = 0;
    win.foreground();
    expect(parseMessage(launcher.posts[0]?.message)).toEqual(hello('app', APP));
    expect(device.link()).toBe('none');

    win.deliver(DEVICE_ORIGIN, launcher, welcome({ playing: false, volume: 1 }));
    expect(device.link()).toBe('bridge');
  });
});

describe('connect', () => {
  it('opens the Launcher in the k7-bridge window and says hello to it', async () => {
    const { win, device } = harness({ opener: null });
    const target = fakeEndpoint();
    win.openResult = target;

    await device.connect();

    expect(win.opened).toEqual([{ url: `${DEVICE_ORIGIN}/bridge`, target: 'k7-bridge' }]);
    expect(parseMessage(target.posts[0]?.message)).toEqual(hello('app', APP));
    expect(target.posts[0]?.targetOrigin).toBe(DEVICE_ORIGIN);

    win.deliver(DEVICE_ORIGIN, target, welcome({ playing: true, volume: 0.4 }));
    expect(device.link()).toBe('bridge');

    const pending = device.request('stream.state');
    win.deliver(
      DEVICE_ORIGIN,
      target,
      ok(lastRequest(target).id, { playing: true, volume: 0.4, blocked: false }),
    );
    await expect(pending).resolves.toEqual({ playing: true, volume: 0.4, blocked: false });
  });

  it('reports a blocked popup, and names the permission to grant (W4)', async () => {
    const { win, device } = harness({ opener: null });
    win.openResult = null;

    await expect(device.connect()).rejects.toMatchObject({
      name: 'DeviceError',
      code: 'unreachable',
      message: expect.stringContaining('popups'),
    });
    expect(win.opened).toHaveLength(1);
    expect(device.link()).toBe('none');
  });
});

/* The wire's own request builder is used where the App would use it, so a signature change
 * in `protocol.ts` shows up here rather than in a hand-built object. */
describe('the wire', () => {
  it('reads its own messages back', () => {
    expect(parseMessage(req('7', 'status'))).toEqual(req('7', 'status'));
    expect(parseMessage(ok('7', {}))).toEqual({ t: 'res', id: '7', ok: true, data: {} });
    const failed: ResponseMessage = fail('7', 'timeout', 'the player did not answer');
    expect(parseMessage(failed)).toEqual(failed);
  });
});

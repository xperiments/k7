import { afterEach, describe, expect, it, vi } from 'vitest';

import { DEVICE_ORIGIN } from '../src/config';
import { createDirectDevice, type DirectAudio } from '../src/device/direct';
import { OP_SPECS } from '../src/bridge/protocol';
import { DeviceError } from '../src/device/transport';

/**
 * The direct Device: the App *is* the Device copy, so each of the fifteen operations is one
 * `fetch` against the device's own API, and the answer is parsed by the parsers the bridge
 * uses too. The fakes record the request rather than answering it meaningfully, because the
 * question here is what went on the wire and what came back parsed.
 */

const APP = 'https://k7.xperiments.dev';

interface Recorded {
  readonly url: string;
  readonly init: RequestInit | undefined;
}

type Reply = () => Response;

function json(value: unknown, status = 200): Response {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'content-type': 'application/json' },
  });
}

function recorder(): {
  calls: Recorded[];
  replies: Reply[];
  fetch: (url: string, init?: RequestInit) => Promise<Response>;
} {
  const calls: Recorded[] = [];
  const replies: Reply[] = [];
  return {
    calls,
    replies,
    fetch(url, init) {
      calls.push({ url, init });
      const reply = replies.shift();
      if (!reply) return Promise.reject(new Error(`unexpected request: ${url}`));
      return Promise.resolve(reply());
    },
  };
}

function bodyOf(call: Recorded | undefined): Record<string, unknown> {
  if (!call) throw new Error('no request was made');
  const body: unknown = typeof call.init?.body === 'string' ? JSON.parse(call.init.body) : null;
  if (typeof body !== 'object' || body === null || Array.isArray(body)) {
    throw new Error(`the request had no JSON body: ${String(call.init?.body)}`);
  }
  return body as Record<string, unknown>;
}

const statusBody = {
  card: true,
  tracks: 12,
  generation: 3,
  library: 'sha256:abc',
  order: 'tree',
  playlist: null,
  playing: { title: 'A track', elapsed_ms: 12.5, duration_ms: 203 },
};

const mcpHandshake = () =>
  json({ jsonrpc: '2.0', id: 1, result: { protocolVersion: '2025-11-25' } });

const mcpTool = (value: unknown, id = 2) =>
  json({ jsonrpc: '2.0', id, result: { content: [{ type: 'text', text: JSON.stringify(value) }] } });

afterEach(() => {
  vi.unstubAllGlobals();
  vi.useRealTimers();
});

describe('which Device a window gets', () => {
  it('is direct on the device origin, and its first request is GET /api/status', async () => {
    const net = recorder();
    net.replies.push(() => json(statusBody));
    vi.stubGlobal('fetch', vi.fn(net.fetch));
    vi.stubGlobal('location', { origin: DEVICE_ORIGIN, protocol: 'http:' });
    vi.resetModules();

    const { getDevice } = await import('../src/device/device');
    const device = getDevice();
    expect(device.link()).toBe('direct');

    await expect(device.request('status')).resolves.toMatchObject({
      card: true,
      tracks: 12,
      // `elapsed_ms` on the wire, `elapsed` in the view: the parser did its job.
      playing: { title: 'A track', elapsed: 12.5, duration: 203 },
    });
    expect(net.calls[0]?.url).toBe('/api/status');
    expect(net.calls[0]?.init?.method ?? 'GET').toBe('GET');
  });

  it('is none at an App origin with no opener, and every request says so', async () => {
    vi.stubGlobal('location', { origin: APP, protocol: 'https:' });
    vi.resetModules();

    const { getDevice } = await import('../src/device/device');
    const device = getDevice();
    expect(device.link()).toBe('none');
    await expect(device.request('status')).rejects.toMatchObject({
      name: 'DeviceError',
      code: 'unreachable',
      message: 'no player is connected',
    });
  });
});

describe('the requests this Device makes', () => {
  it('reads the Playlist index and one list', async () => {
    const net = recorder();
    net.replies.push(() =>
      json({
        selected: 'aaaa000000000001',
        walking: true,
        missing: 2,
        lists: [{ id: 'aaaa000000000001', name: 'Late night', created: 5, rows: 3 }],
      }),
    );
    net.replies.push(() => json({ id: 'aaaa000000000001', name: 'Late night', rows: ['a.mp3', 7] }));
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('playlists')).resolves.toEqual({
      selected: 'aaaa000000000001',
      walking: true,
      missing: 2,
      lists: [{ id: 'aaaa000000000001', name: 'Late night', created: 5, rows: 3 }],
    });
    expect(net.calls[0]?.url).toBe('/api/playlists');

    // A row that is not a string is not a path this page can use, so it is dropped.
    await expect(device.request('playlist', { id: 'aaaa000000000001' })).resolves.toEqual({
      id: 'aaaa000000000001',
      name: 'Late night',
      rows: ['a.mp3'],
    });
    expect(net.calls[1]?.url).toBe('/api/playlists/aaaa000000000001');
  });

  it('selects a list with POST /api/playlists/select', async () => {
    const net = recorder();
    net.replies.push(() => json({ order: 'list', selected: 'aaaa000000000001' }));
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('select', { id: 'aaaa000000000001' })).resolves.toEqual({
      order: 'list',
      selected: 'aaaa000000000001',
    });
    expect(net.calls[0]?.url).toBe('/api/playlists/select');
    expect(net.calls[0]?.init?.method).toBe('POST');
    expect(bodyOf(net.calls[0])).toEqual({ id: 'aaaa000000000001' });
  });

  it('rescans with POST /api/rescan, and reads 409 as a refusal that names the reason', async () => {
    const net = recorder();
    net.replies.push(() => json({ ok: true }, 202));
    net.replies.push(() => json({ ok: false, error: 'a scan is already running' }, 409));
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('rescan')).resolves.toEqual({ ok: true });
    expect(net.calls[0]?.url).toBe('/api/rescan');
    expect(net.calls[0]?.init?.method).toBe('POST');

    await expect(device.request('rescan')).rejects.toMatchObject({
      code: 'refused',
      status: 409,
      message: 'a scan is already running',
    });
  });

  it('plays one path with POST /api/play', async () => {
    const net = recorder();
    net.replies.push(() => json({}));
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('play', { path: 'Artist/a.mp3' })).resolves.toEqual({});
    expect(net.calls[0]?.url).toBe('/api/play');
    expect(bodyOf(net.calls[0])).toEqual({ path: 'Artist/a.mp3' });
  });
});

describe('the library over MCP', () => {
  it('initializes once and calls the four tools with the device argument names', async () => {
    const net = recorder();
    net.replies.push(mcpHandshake);
    net.replies.push(() =>
      mcpTool({
        query: 'aphex',
        matched: 1,
        tracks: [{ name: 'a.mp3', path: 'Artist/a.mp3', size: 2, format: 'mp3', analysis: { bpm: 120, key: '8A' } }],
        truncated: false,
      }),
    );
    net.replies.push(() =>
      mcpTool({ path: '', entries: [{ name: 'Artist', path: 'Artist', type: 'dir' }] }, 3),
    );
    net.replies.push(() =>
      mcpTool(
        {
          available: true,
          state: 'ok',
          tracks: 2,
          directories: 1,
          generation: 7,
          snapshot: { state: 'ok', coverage: { tracks: 2, analysed: 1, percent: 50 } },
        },
        4,
      ),
    );
    net.replies.push(() =>
      mcpTool({ name: 'a.mp3', path: 'Artist/a.mp3', size: 2, format: 'mp3', analysis: null }, 5),
    );
    const device = createDirectDevice({ fetch: net.fetch });

    const search = await device.request('library.search', { query: 'aphex', bpmMin: 120, sort: 'bpm' });
    expect(search.tracks).toHaveLength(1);
    expect(search.tracks[0]?.analysis?.bpm).toBe(120);
    expect(net.calls[0]?.url).toBe('/mcp');
    expect(bodyOf(net.calls[0]).method).toBe('initialize');

    const call = bodyOf(net.calls[1]);
    expect(call.method).toBe('tools/call');
    expect(call.params).toEqual({
      name: 'library_search',
      arguments: { query: 'aphex', bpm_min: 120, sort: 'bpm' },
    });
    expect((net.calls[1]?.init?.headers as Record<string, string>)['MCP-Protocol-Version']).toBe(
      '2025-11-25',
    );

    await expect(device.request('library.list', { path: '', depth: 8 })).resolves.toMatchObject({
      entries: [{ name: 'Artist', path: 'Artist', type: 'dir' }],
    });
    expect(bodyOf(net.calls[2]).params).toEqual({
      name: 'library_list',
      arguments: { depth: 8 },
    });

    await expect(device.request('library.stats')).resolves.toMatchObject({
      available: true,
      tracks: 2,
      snapshot: { coverage: { analysed: 1, percent: 50 } },
    });

    await expect(device.request('library.track', { path: 'Artist/a.mp3' })).resolves.toMatchObject({
      name: 'a.mp3',
      analysis: null,
    });
    // One handshake for the whole document: five requests became four calls and one initialize.
    expect(net.calls.filter((entry) => entry.url === '/mcp')).toHaveLength(5);
    expect(net.calls.filter((entry) => bodyOf(entry).method === 'initialize')).toHaveLength(1);
  });

  it('reads an MCP tool error as a refusal', async () => {
    const net = recorder();
    net.replies.push(mcpHandshake);
    net.replies.push(() =>
      json({ jsonrpc: '2.0', id: 2, result: { isError: true, content: [{ type: 'text', text: 'no such track' }] } }),
    );
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('library.track', { path: 'gone.mp3' })).rejects.toMatchObject({
      code: 'refused',
      message: 'no such track',
    });
  });
});

describe('the stream on the Device copy', () => {
  it('drives its own audio element and reports the three facts', async () => {
    const audio: DirectAudio = {
      playing: false,
      blocked: false,
      volume: 0.5,
      play() {
        audio.playing = true;
        return true;
      },
      pause() {
        audio.playing = false;
      },
    };
    const device = createDirectDevice({ fetch: () => Promise.reject(new Error('no fetch here')), audio });

    await expect(device.request('stream.state')).resolves.toEqual({
      playing: false,
      volume: 0.5,
      blocked: false,
    });
    await expect(device.request('stream.play')).resolves.toEqual({
      playing: true,
      volume: 0.5,
      blocked: false,
    });
    await expect(device.request('stream.volume', { level: 0.25 })).resolves.toEqual({
      playing: true,
      volume: 0.25,
      blocked: false,
    });
    await expect(device.request('stream.pause')).resolves.toMatchObject({ playing: false });
  });

  it('says the stream is blocked when the browser refuses play()', async () => {
    const audio: DirectAudio = {
      playing: false,
      blocked: false,
      volume: 1,
      play() {
        return Promise.resolve(false);
      },
      pause() {},
    };
    const device = createDirectDevice({ fetch: netless(), audio });

    await expect(device.request('stream.play')).rejects.toMatchObject({
      name: 'DeviceError',
      code: 'blocked',
    });
  });
});

/** A fetch that must not be reached: the stream never leaves the page (O3). */
function netless(): (url: string, init?: RequestInit) => Promise<Response> {
  return (url) => Promise.reject(new Error(`the stream must not fetch: ${url}`));
}

describe('failures', () => {
  it('gives up at the op budget with `timeout`', async () => {
    vi.useFakeTimers();
    const device = createDirectDevice({
      fetch: (_url, init) =>
        new Promise<Response>((_resolve, reject) => {
          init?.signal?.addEventListener('abort', () => reject(new Error('aborted')));
        }),
    });

    const pending = device.request('status');
    const assertion = expect(pending).rejects.toMatchObject({ code: 'timeout' });
    await vi.advanceTimersByTimeAsync(OP_SPECS.status.timeout);
    await assertion;
  });

  it('refuses a document it cannot read rather than handing it to a view', async () => {
    const net = recorder();
    net.replies.push(() => json({ nothing: 'like a status' }));
    net.replies.push(() => json('not a playlist index'));
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('status')).rejects.toMatchObject({
      name: 'DeviceError',
      code: 'unreachable',
      message: 'the player answered a status this page could not read',
    });
    await expect(device.request('playlists')).rejects.toBeInstanceOf(DeviceError);
  });

  it('maps an absent player to `unreachable` with the sentence the page already shows', async () => {
    const device = createDirectDevice({ fetch: () => Promise.reject(new TypeError('failed to fetch')) });

    await expect(device.request('status')).rejects.toMatchObject({
      code: 'unreachable',
      status: 0,
      message: 'the player did not answer; is it on the same network?',
    });
  });
});

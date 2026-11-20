import { describe, expect, it } from 'vitest';

import { parseArtStatus } from '../src/device/art';
import { createDirectDevice } from '../src/device/direct';
import { buildPackBytes } from './fixtures/art-pack';

/**
 * The four art operations on the direct transport (`art-packs.md` §5.1): one route each,
 * the pack's own content type on the upload, and the device's refusal sentence kept.
 */

const STATUS = {
  installed: true,
  valid: true,
  name: 'Neon 2026',
  author: 'Mara Vidal',
  design_count: 2,
  sheet_crc: 4_294_967_000,
  factory_count: 5,
  why: '',
};

const PARSED = {
  installed: true,
  valid: true,
  name: 'Neon 2026',
  author: 'Mara Vidal',
  designCount: 2,
  sheetCrc: 4_294_967_000,
  factoryCount: 5,
  why: '',
};

interface Recorded {
  readonly url: string;
  readonly init: RequestInit | undefined;
}

function json(value: unknown, status = 200): Response {
  return new Response(JSON.stringify(value), {
    status,
    headers: { 'content-type': 'application/json' },
  });
}

function recorder(): {
  calls: Recorded[];
  replies: Array<() => Response>;
  fetch: (url: string, init?: RequestInit) => Promise<Response>;
} {
  const calls: Recorded[] = [];
  const replies: Array<() => Response> = [];
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

function headersOf(call: Recorded | undefined): Record<string, string> {
  return (call?.init?.headers as Record<string, string>) ?? {};
}

describe('the art status', () => {
  it('reads GET /api/art and parses the device’s counts', async () => {
    const net = recorder();
    net.replies.push(() => json(STATUS));
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('art.status')).resolves.toEqual(PARSED);
    expect(net.calls[0]?.url).toBe('/api/art');
    expect(net.calls[0]?.init?.method).toBeUndefined();
    expect(headersOf(net.calls[0]).accept).toBe('application/json');
  });

  it('refuses a status with a missing or malformed field', async () => {
    expect(parseArtStatus({ installed: true })).toBeNull();
    expect(parseArtStatus({ ...STATUS, design_count: 1.5 })).toBeNull();
    expect(parseArtStatus({ ...STATUS, sheet_crc: '42' })).toBeNull();
    expect(parseArtStatus({ ...STATUS, why: undefined })).toBeNull();

    const net = recorder();
    net.replies.push(() => json({ installed: true }));
    const device = createDirectDevice({ fetch: net.fetch });
    await expect(device.request('art.status')).rejects.toMatchObject({
      name: 'DeviceError',
      code: 'unreachable',
      message: 'the player answered a status document this page could not read',
    });
  });
});

describe('the upload', () => {
  it('sends the whole pack with its own content type', async () => {
    const pack = buildPackBytes();
    const net = recorder();
    net.replies.push(() => json(STATUS));
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('art.upload', { pack })).resolves.toEqual(PARSED);
    expect(net.calls[0]?.url).toBe('/api/art');
    expect(net.calls[0]?.init?.method).toBe('POST');
    expect(headersOf(net.calls[0])['content-type']).toBe('application/x-k7artpack');
    expect(net.calls[0]?.init?.body).toBe(pack);
  });

  it('keeps the device’s refusal sentence and HTTP status', async () => {
    const net = recorder();
    net.replies.push(() =>
      json(
        { ok: false, error: 'the pack was built against 3 fonts; this firmware has 2' },
        422,
      ),
    );
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('art.upload', { pack: buildPackBytes() })).rejects.toMatchObject({
      name: 'DeviceError',
      code: 'refused',
      status: 422,
      message: 'the pack was built against 3 fonts; this firmware has 2',
    });
  });
});

describe('the active pack', () => {
  it('reads GET /api/art/pack as bytes', async () => {
    const pack = buildPackBytes();
    const net = recorder();
    net.replies.push(
      () =>
        new Response(new Uint8Array(pack), {
          status: 200,
          headers: { 'content-type': 'application/octet-stream' },
        }),
    );
    const device = createDirectDevice({ fetch: net.fetch });

    const answer = await device.request('art.pack');
    expect(answer.bytes).toEqual(pack);
    expect(net.calls[0]?.url).toBe('/api/art/pack');
    expect(headersOf(net.calls[0]).accept).toBe('application/octet-stream');
  });

  it('reads a 404 as the device’s own refusal', async () => {
    const net = recorder();
    net.replies.push(() => json({ ok: false, error: 'there is no art pack' }, 404));
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('art.pack')).rejects.toMatchObject({
      code: 'refused',
      status: 404,
      message: 'there is no art pack',
    });
  });

  it('refuses an empty pack answer rather than caching nothing', async () => {
    const net = recorder();
    net.replies.push(() => new Response(new Uint8Array(0), { status: 200 }));
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('art.pack')).rejects.toMatchObject({
      code: 'unreachable',
      message: 'the player answered an empty pack',
    });
  });
});

describe('the reset', () => {
  it('erases with DELETE /api/art and reads the factory status', async () => {
    const net = recorder();
    net.replies.push(() => json({ ...STATUS, installed: false, valid: true, design_count: 0 }));
    const device = createDirectDevice({ fetch: net.fetch });

    await expect(device.request('art.reset')).resolves.toMatchObject({
      installed: false,
      valid: true,
      designCount: 0,
    });
    expect(net.calls[0]?.url).toBe('/api/art');
    expect(net.calls[0]?.init?.method).toBe('DELETE');
  });
});

import { describe, expect, it } from 'vitest';
import { createLocalDevice } from '../src/device/local';
import { DeviceError } from '../src/device/transport';

const answer = (body: unknown, status = 200) =>
  Promise.resolve(new Response(JSON.stringify(body), { status }));

const STATUS = {
  card: true,
  tracks: 93,
  generation: 1,
  library: 'ok',
  order: 'tree',
  playlist: null,
  playing: null,
};

describe('the local transport', () => {
  it('asks the device by its own name, and says the target is local', async () => {
    const calls: { url: string; init?: RequestInit & { targetAddressSpace?: string } }[] = [];
    const device = createLocalDevice({
      audio: null,
      fetch: (input, init) => {
        calls.push({ url: String(input), init: init as RequestInit & { targetAddressSpace?: string } });
        return answer(STATUS);
      },
    });

    const status = await device.request('status');

    expect(status.tracks).toBe(93);
    expect(calls[0].url).toBe('http://k7.local/api/status');
    // LNA §2.5: this is what asks for the permission rather than the mixed-content block.
    expect(calls[0].init?.targetAddressSpace).toBe('local');
    expect(calls[0].init?.mode).toBe('cors');
  });

  it('reports itself as a local link', () => {
    const device = createLocalDevice({ audio: null, fetch: () => answer(STATUS) });
    expect(device.link()).toBe('local');
  });

  it('is the Unreachable the caller falls through on, when the browser refuses', async () => {
    const device = createLocalDevice({
      audio: null,
      fetch: () => Promise.reject(new TypeError('Failed to fetch')),
    });

    await expect(device.request('status')).rejects.toBeInstanceOf(DeviceError);
    await expect(device.request('status')).rejects.toMatchObject({ code: 'unreachable' });
  });
});

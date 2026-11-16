import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

/**
 * A load where the first probe fails is not a window with no player: the device was there,
 * the request was early. The status poll is the page's heartbeat, so the next beat has to
 * try the local path again rather than asking a `none` device forever.
 */

const STATUS = {
  card: true,
  mark: 'abcdef0123456789',
  tracks: 2,
  generation: 1,
  library: 'k7',
  order: 'tree',
  playlist: null,
  playing: null,
};

function answer(): Response {
  return new Response(JSON.stringify(STATUS), {
    status: 200,
    headers: { 'content-type': 'application/json' },
  });
}

beforeEach(() => {
  vi.resetModules();
});

afterEach(() => {
  vi.unstubAllGlobals();
  vi.resetModules();
});

describe('the device link', () => {
  it('tries the local path again on the next poll after an early probe failed', async () => {
    const fetchMock = vi
      .fn()
      .mockRejectedValueOnce(new TypeError('Failed to fetch'))
      .mockImplementation(() => Promise.resolve(answer()));
    vi.stubGlobal('fetch', fetchMock);

    const { deviceSnapshot, refreshDevice } = await import('../src/device/useDeviceStatus');

    await refreshDevice();
    expect(deviceSnapshot().reachable).toBe(false);

    await refreshDevice();
    expect(deviceSnapshot().reachable).toBe(true);
    expect(deviceSnapshot().status?.tracks).toBe(2);
  });
});

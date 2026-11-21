import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

/**
 * The device's address, resolved once over `k7.local`.
 *
 * Every request to the name pays an mDNS lookup, and the status poll asks every five
 * seconds, so the App asks the device where it is once and then sends its requests to the
 * address it named. The address is a lease: a failure forgets it.
 */

beforeEach(() => {
  vi.resetModules();
});

afterEach(() => {
  vi.unstubAllGlobals();
  vi.resetModules();
});

function answered(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'content-type': 'application/json' },
  });
}

describe('the device origin', () => {
  it('starts at the name and keeps the address the device names', async () => {
    const fetchMock = vi.fn().mockResolvedValue(answered({ ip: '192.168.1.42', port: 80 }));
    vi.stubGlobal('fetch', fetchMock);

    const { deviceOrigin, resolveDeviceOrigin } = await import('../src/device/origin');

    expect(deviceOrigin()).toBe('http://k7.local');
    expect(await resolveDeviceOrigin()).toBe('http://192.168.1.42:80');
    expect(deviceOrigin()).toBe('http://192.168.1.42:80');
    expect(String(fetchMock.mock.calls[0]?.[0])).toBe('http://k7.local/api/net');
  });

  it('keeps the name when the device has no address to give', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(answered({ error: 'no address' }, 503)));
    const { deviceOrigin, resolveDeviceOrigin } = await import('../src/device/origin');

    expect(await resolveDeviceOrigin()).toBe('http://k7.local');
    expect(deviceOrigin()).toBe('http://k7.local');
  });

  it('refuses an answer that is not an address and a port', async () => {
    const { deviceOrigin, resolveDeviceOrigin } = await import('../src/device/origin');

    for (const body of [{ ip: '192.168.1.42' }, { ip: '', port: 80 }, { ip: 'x', port: 0 }, {}]) {
      vi.stubGlobal('fetch', vi.fn().mockResolvedValue(answered(body)));
      expect(await resolveDeviceOrigin()).toBe('http://k7.local');
      expect(deviceOrigin()).toBe('http://k7.local');
    }
  });

  it('forgets a lease that stopped answering', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(answered({ ip: '192.168.1.42', port: 80 })));
    const { deviceOrigin, forgetDeviceOrigin, resolveDeviceOrigin } = await import(
      '../src/device/origin'
    );

    await resolveDeviceOrigin();
    expect(deviceOrigin()).toBe('http://192.168.1.42:80');

    forgetDeviceOrigin();
    expect(deviceOrigin()).toBe('http://k7.local');
  });
});

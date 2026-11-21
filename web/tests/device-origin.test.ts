import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

/**
 * The device's address, read off its replies.
 *
 * Every request to `k7.local` pays an mDNS lookup, and the status poll asks every five
 * seconds. The device names its own address on every reply (`X-K7-Net`), so the App learns
 * it from a request it was making anyway and sends the next one there. The address is a
 * lease: a failure forgets it.
 */

beforeEach(() => {
  vi.resetModules();
});

afterEach(() => {
  vi.unstubAllGlobals();
  vi.resetModules();
});

describe('the device origin', () => {
  it('starts at the name and takes the address a reply names', async () => {
    const { deviceOrigin, noteDeviceOrigin } = await import('../src/device/origin');

    expect(deviceOrigin()).toBe('http://k7.local');
    noteDeviceOrigin('192.168.0.13:80');
    expect(deviceOrigin()).toBe('http://192.168.0.13:80');
  });

  it('says nothing when a reply names nothing usable', async () => {
    const { deviceOrigin, noteDeviceOrigin } = await import('../src/device/origin');

    for (const value of [null, undefined, '', '  ', '192.168.0.13', 'no-port:', ':80', 'ip:0', 'ip:x']) {
      noteDeviceOrigin(value);
      expect(deviceOrigin()).toBe('http://k7.local');
    }
  });

  it('follows a new lease, and forgets a dead one', async () => {
    const { deviceOrigin, forgetDeviceOrigin, noteDeviceOrigin } = await import(
      '../src/device/origin'
    );

    noteDeviceOrigin('192.168.0.13:80');
    noteDeviceOrigin('192.168.0.99:8080');
    expect(deviceOrigin()).toBe('http://192.168.0.99:8080');

    forgetDeviceOrigin();
    expect(deviceOrigin()).toBe('http://k7.local');
  });

  it('reads the header off a device reply, and sends the next request there', async () => {
    const replies = [
      new Response('{}', { headers: { 'x-k7-net': '192.168.0.13:80' } }),
      new Response('{}'),
    ];
    const fetchMock = vi.fn().mockImplementation(() => Promise.resolve(replies.shift()));
    vi.stubGlobal('fetch', fetchMock);

    const { deviceOrigin } = await import('../src/device/origin');
    const { localRequest } = await import('../src/device/local');

    await localRequest('/api/status');
    expect(deviceOrigin()).toBe('http://192.168.0.13:80');

    await localRequest('/api/status');
    expect(String(fetchMock.mock.calls[1]?.[0])).toBe('http://192.168.0.13:80/api/status');
  });
});

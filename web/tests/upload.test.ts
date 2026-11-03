import { afterEach, describe, expect, it, vi } from 'vitest';

import { DEVICE_ORIGIN } from '../src/config';

/**
 * Sending the image, through the `Device` seam. These are the Device copy's numbers — the
 * App is at `http://k7.local`, so the request is the same-origin `POST /api/snapshot` — and
 * the two refusal shapes the player answers with. The bridge's own path is
 * `tests/bridge.test.ts` (the transfer list) and `tests/direct.test.ts` (the request).
 */

const reply = (status: number, body: string) =>
  new Response(body, { status, headers: { 'Content-Type': 'application/json' } });

/** The Device copy's module graph: the seam picks `direct` because of the origin. */
async function deviceCopy(): Promise<typeof import('../src/device/upload')> {
  vi.stubGlobal('location', { origin: DEVICE_ORIGIN, protocol: 'http:' });
  vi.resetModules();
  return import('../src/device/upload');
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('sending the snapshot to the player', () => {
  it('posts the image as bytes the player can tell apart from a form', async () => {
    const fetchMock = vi.fn().mockResolvedValue(reply(200, '{"applied":true,"bytes":12}'));
    vi.stubGlobal('fetch', fetchMock);
    const { sendSnapshot } = await deviceCopy();

    const result = await sendSnapshot(new Uint8Array([1, 2, 3]));

    expect(result).toEqual({ bytes: 12 });
    const [url, init] = fetchMock.mock.calls[0];
    expect(url).toBe('/api/snapshot');
    expect(init.method).toBe('POST');
    expect(init.headers['Content-Type']).toBe('application/octet-stream');
  });

  it('says why the player refused the image', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue(
        reply(400, '{"applied":false,"error":"image is not its stated length"}'),
      ),
    );
    const { sendSnapshot } = await deviceCopy();

    await expect(sendSnapshot(new Uint8Array([1]))).rejects.toMatchObject({
      name: 'UploadError',
      status: 400,
      message: 'image is not its stated length',
    });
  });

  it('is an UploadError, not a network exception, when the player is absent', async () => {
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new TypeError('failed to fetch')));
    const { sendSnapshot, UploadError } = await deviceCopy();

    await expect(sendSnapshot(new Uint8Array([1]))).rejects.toBeInstanceOf(UploadError);
    await expect(sendSnapshot(new Uint8Array([1]))).rejects.toMatchObject({
      name: 'UploadError',
      message: 'the player did not answer; is it on the same network?',
      status: 0,
    });
  });

  it('treats a 200 without applied:true as a refusal', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(reply(200, '{"applied":false}')));
    const { sendSnapshot } = await deviceCopy();

    await expect(sendSnapshot(new Uint8Array([1]))).rejects.toThrow(/refused it \(200\)/);
  });
});

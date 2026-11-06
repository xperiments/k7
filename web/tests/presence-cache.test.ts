import { describe, expect, it, vi } from 'vitest';

const { localRequest, presenceFromLibraryBlob } = vi.hoisted(() => ({
  localRequest: vi.fn(async () => new Response(new Uint8Array([1, 2, 3]), { status: 200 })),
  presenceFromLibraryBlob: vi.fn(() => ({
    paths: new Set<string>(),
    tracks: [],
    unmeasurable: [],
    complete: true,
  })),
}));

vi.mock('../src/device/local', () => ({ localRequest }));
vi.mock('../src/device/libraryBlob', () => ({ presenceFromLibraryBlob }));

import { fetchPresence } from '../src/media/albums';

describe('the device library presence', () => {
  it('shares one request across the views for the same image', async () => {
    localRequest.mockClear();
    const first = fetchPresence(1500, 71);
    const second = fetchPresence(1500, 71);

    expect(second).toBe(first);
    await Promise.all([first, second]);
    expect(localRequest).toHaveBeenCalledTimes(1);

    await fetchPresence(1500, 71);
    expect(localRequest).toHaveBeenCalledTimes(1);

    await fetchPresence(1500, 72);
    expect(localRequest).toHaveBeenCalledTimes(2);
  });
});

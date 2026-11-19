import { describe, expect, it } from 'vitest';

import { FlashError } from './errors';
import { boardById, type FirmwarePart } from './manifest';
import { downloadPart, sha256Hex } from './parts';

const K7 = boardById('k7');
const bytes = new TextEncoder().encode('bootloader bytes');
const base: FirmwarePart = { file: 'bootloader.bin', offset: 0, size: bytes.length, sha256: '' };

async function part(overrides: Partial<FirmwarePart> = {}): Promise<FirmwarePart> {
  return { ...base, sha256: await sha256Hex(bytes), ...overrides };
}

function respond(body: Uint8Array, status = 200): typeof fetch {
  return (async () =>
    new Response(body as unknown as BodyInit, { status })) as unknown as typeof fetch;
}

async function causeOf(promise: Promise<unknown>): Promise<string | undefined> {
  try {
    await promise;
    return undefined;
  } catch (error) {
    return error instanceof FlashError ? error.cause : 'not-a-flash-error';
  }
}

describe('a part', () => {
  it('is returned when its bytes match the manifest', async () => {
    const data = await downloadPart(K7, await part(), respond(bytes));
    expect(Array.from(data)).toEqual(Array.from(bytes));
  });

  it('is refused when the sha256 does not match', async () => {
    const wrong = await part({ sha256: '0'.repeat(64) });
    expect(await causeOf(downloadPart(K7, wrong, respond(bytes)))).toBe('hash');
  });

  it('is refused when the size does not match', async () => {
    const wrong = await part({ size: bytes.length + 1 });
    expect(await causeOf(downloadPart(K7, wrong, respond(bytes)))).toBe('download');
  });

  it('is refused when the origin answers with an error', async () => {
    expect(await causeOf(downloadPart(K7, await part(), respond(bytes, 404)))).toBe('download');
  });

  it('is refused when the fetch itself fails', async () => {
    const down = (async () => {
      throw new Error('offline');
    }) as unknown as typeof fetch;
    expect(await causeOf(downloadPart(K7, await part(), down))).toBe('download');
  });
});

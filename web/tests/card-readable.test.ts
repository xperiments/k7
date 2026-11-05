import { describe, expect, it, vi } from 'vitest';
import { isNotReadable, openReadable, withReadable } from '../src/card/readable';

const MESSAGE =
  'The requested file could not be read, typically due to permission problems that have occurred after a reference to a file was acquired.';

function staleFile(): File {
  return {
    name: 'stale.mp3',
    async arrayBuffer(): Promise<ArrayBuffer> {
      throw new DOMException(MESSAGE, 'NotReadableError');
    },
  } as unknown as File;
}

function freshFile(bytes: number[] = [7, 7]): File {
  return new File([new Uint8Array(bytes)], 'fresh.mp3');
}

describe('a dead File snapshot', () => {
  it('is recognised by its name, and by the browser sentence when a worker lost the name', () => {
    expect(isNotReadable(new DOMException(MESSAGE, 'NotReadableError'))).toBe(true);
    expect(isNotReadable(new Error(MESSAGE))).toBe(true);
    expect(isNotReadable(new DOMException('gone', 'NotFoundError'))).toBe(false);
    expect(isNotReadable(null)).toBe(false);
  });

  it('is reopened once and the work is run on the fresh file', async () => {
    const reopen = vi.fn(async () => freshFile([9, 9, 9]));
    const use = vi.fn(async (file: File) => new Uint8Array(await file.arrayBuffer()));

    const bytes = await withReadable(staleFile(), reopen, use);

    expect(reopen).toHaveBeenCalledTimes(1);
    expect(use).toHaveBeenCalledTimes(2);
    expect([...bytes]).toEqual([9, 9, 9]);
  });

  it('is opened as bytes together with the file those bytes came from', async () => {
    const fresh = freshFile([4, 5]);
    const opened = await openReadable(staleFile(), async () => fresh);

    expect(opened.file).toBe(fresh);
    expect([...opened.bytes]).toEqual([4, 5]);
  });

  it('keeps the original error when the card cannot hand the file back', async () => {
    const reopen = vi.fn(async () => null);
    await expect(withReadable(staleFile(), reopen, (file) => file.arrayBuffer())).rejects.toMatchObject(
      { name: 'NotReadableError' },
    );
    expect(reopen).toHaveBeenCalledTimes(1);
  });

  it('does not retry an error the card did not cause', async () => {
    const reopen = vi.fn(async () => freshFile());
    const use = vi.fn(async () => {
      throw new Error('the analyser refused this file');
    });

    await expect(withReadable(freshFile(), reopen, use)).rejects.toThrow(
      'the analyser refused this file',
    );
    expect(reopen).not.toHaveBeenCalled();
    expect(use).toHaveBeenCalledTimes(1);
  });
});

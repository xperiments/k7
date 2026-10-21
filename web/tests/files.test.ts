import { describe, expect, it } from 'vitest';
import { scanDataTransfer } from '../src/ui/files';

interface FakeEntry {
  name: string;
  isFile: boolean;
  isDirectory: boolean;
  file?: (ok: (file: File) => void, fail: (error: Error) => void) => void;
  createReader?: () => {
    readEntries: (ok: (entries: FakeEntry[]) => void, fail: (error: Error) => void) => void;
  };
}

const fileEntry = (name: string): FakeEntry => ({
  name,
  isFile: true,
  isDirectory: false,
  file: (ok) => ok(new File([name], name)),
});

const failingEntry = (name: string): FakeEntry => ({
  name,
  isFile: true,
  isDirectory: false,
  file: (_ok, fail) => fail(new Error('unreadable')),
});

const dirEntry = (name: string, children: FakeEntry[]): FakeEntry => ({
  name,
  isFile: false,
  isDirectory: true,
  createReader: () => {
    let sent = false;
    return {
      readEntries: (ok) => {
        const batch = sent ? [] : children;
        sent = true;
        ok(batch);
      },
    };
  },
});

const drop = (entries: FakeEntry[], files: File[] = []): DataTransfer =>
  ({
    items: entries.map((entry) => ({
      kind: 'file',
      webkitGetAsEntry: () => entry,
    })),
    files,
  }) as unknown as DataTransfer;

describe('folder scanning', () => {
  it('walks nested directories in name order and keeps paths', async () => {
    const root = dirEntry('Music', [
      dirEntry('Album', [fileEntry('a.mp3')]),
      fileEntry('.DS_Store'),
      fileEntry('b.flac'),
    ]);

    const result = await scanDataTransfer(drop([root]));

    expect(result.skipped).toBe(0);
    expect(result.files.map((found) => found.path)).toEqual([
      'Music/Album/a.mp3',
      'Music/b.flac',
    ]);
    expect(result.files[0].file.name).toBe('a.mp3');
  });

  it('reads every batch from readEntries', async () => {
    const children = [fileEntry('a.mp3'), fileEntry('b.mp3'), fileEntry('c.mp3')];
    let call = 0;
    const directory: FakeEntry = {
      name: 'Batch',
      isFile: false,
      isDirectory: true,
      createReader: () => ({
        readEntries: (ok) => {
          const batches = [[children[0]], [children[1], children[2]], []];
          ok(batches[call++] ?? []);
        },
      }),
    };

    const result = await scanDataTransfer(drop([directory]));

    expect(result.files.map((found) => found.path)).toEqual([
      'Batch/a.mp3',
      'Batch/b.mp3',
      'Batch/c.mp3',
    ]);
  });

  it('counts unreadable entries instead of failing the scan', async () => {
    const root = dirEntry('Broken', [failingEntry('bad.mp3'), fileEntry('good.mp3')]);

    const result = await scanDataTransfer(drop([root]));

    expect(result.skipped).toBe(1);
    expect(result.files.map((found) => found.path)).toEqual(['Broken/good.mp3']);
  });

  it('falls back to dataTransfer.files when entries are unavailable', async () => {
    const plain = new File(['x'], 'loose.mp3');
    const result = await scanDataTransfer(drop([], [plain]));

    expect(result.files).toEqual([{ file: plain, path: 'loose.mp3' }]);
  });
});

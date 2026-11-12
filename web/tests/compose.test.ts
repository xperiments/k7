import { describe, expect, it } from 'vitest';
import {
  composePath,
  copyIntoMusic,
  diffMusic,
  walkMusic,
  type ComposeDirectory,
  type DroppedFile,
  type WalkDirectory,
} from '../src/card/compose';

/**
 * The compose act and the reconciler's pure half.
 *
 * A fake directory stands in for the card's write side: it records the folders it makes and
 * the files it writes, and a stream stand-in feeds the copy in chunks so the "never one giant
 * buffer" rule is what the test pins, not an accident of `File`'s single-chunk stream.
 */

class FakeFile {
  readonly kind = 'file' as const;
  bytes: Uint8Array = new Uint8Array();
  constructor(
    readonly name: string,
    readonly mtime = 1_700_000_000_000,
  ) {}
  async createWritable() {
    const chunks: Uint8Array[] = [];
    return {
      write: async (data: Uint8Array) => {
        chunks.push(data.slice());
      },
      close: async () => {
        const total = chunks.reduce((n, chunk) => n + chunk.length, 0);
        const all = new Uint8Array(total);
        let at = 0;
        for (const chunk of chunks) {
          all.set(chunk, at);
          at += chunk.length;
        }
        this.bytes = all;
      },
    };
  }
  async getFile(): Promise<File> {
    const copy = new Uint8Array(this.bytes.byteLength);
    copy.set(this.bytes);
    return new File([copy], this.name, { type: 'audio/mpeg', lastModified: this.mtime });
  }
}

class FakeDirectory {
  readonly kind = 'directory' as const;
  readonly children = new Map<string, FakeDirectory | FakeFile>();
  constructor(readonly name: string) {}

  async getDirectoryHandle(name: string, options?: { create?: boolean }): Promise<FakeDirectory> {
    let child = this.children.get(name);
    if (child === undefined) {
      if (options?.create !== true) throw new DOMException(`no ${name}`, 'NotFoundError');
      child = new FakeDirectory(name);
      this.children.set(name, child);
    }
    if (child.kind !== 'directory') throw new Error(`${name} is a file, not a directory`);
    return child;
  }

  async getFileHandle(name: string, options?: { create?: boolean }): Promise<FakeFile> {
    let child = this.children.get(name);
    if (child === undefined) {
      if (options?.create !== true) throw new DOMException(`no ${name}`, 'NotFoundError');
      child = new FakeFile(name);
      this.children.set(name, child);
    }
    if (child.kind !== 'file') throw new Error(`${name} is a directory, not a file`);
    return child;
  }

  async *values(): AsyncIterable<FakeFile | FakeDirectory> {
    for (const child of this.children.values()) yield child;
  }

  file(name: string, bytes: Uint8Array, mtime?: number): FakeFile {
    const child = new FakeFile(name, mtime);
    child.bytes = bytes;
    this.children.set(name, child);
    return child;
  }

  directory(name: string): FakeDirectory {
    const child = new FakeDirectory(name);
    this.children.set(name, child);
    return child;
  }
}

/** A dropped file whose stream yields `chunks` one at a time, so the copy writes several times. */
function dropped(path: string, chunks: Uint8Array[]): DroppedFile {
  const stream = new ReadableStream<Uint8Array>({
    start(controller) {
      for (const chunk of chunks) controller.enqueue(chunk);
      controller.close();
    },
  });
  return { file: { stream: () => stream }, path };
}

const text = (s: string) => new TextEncoder().encode(s);

describe('the destination path', () => {
  it('drops the dragged folder’s own name and keeps the structure below it', () => {
    expect(composePath('Music/Artist/Album/01.mp3')).toBe('Artist/Album/01.mp3');
    expect(composePath('music/Artist/Album/01.mp3')).toBe('Artist/Album/01.mp3');
  });

  it('keeps a loose file’s name when it was dropped on its own', () => {
    expect(composePath('01.mp3')).toBe('01.mp3');
    expect(composePath('Album/01.mp3')).toBe('01.mp3');
  });
});

describe('copying into the card', () => {
  it('streams each file into music/, preserving structure, and reports it as it lands', async () => {
    const card = new FakeDirectory('road-trip-11111111');
    const results: Array<{ path: string; file: File }> = [];

    const result = await copyIntoMusic(
      card as unknown as ComposeDirectory,
      [
        dropped('Music/Artist/Album/01 - Xtal.mp3', [text('XTAL-'), text('BYTES')]),
        dropped('Music/Artist/Album/02 - Tha.mp3', [text('THA')]),
      ],
      (copied) => results.push({ path: copied.path, file: copied.file }),
    );

    expect(result).toEqual({ copied: 2, skipped: 0 });
    const reported = await Promise.all(
      results.map(async ({ path, file }) => ({
        path,
        bytes: new TextDecoder().decode(await file.arrayBuffer()),
      })),
    );
    expect(reported).toEqual([
      { path: 'Artist/Album/01 - Xtal.mp3', bytes: 'XTAL-BYTES' },
      { path: 'Artist/Album/02 - Tha.mp3', bytes: 'THA' },
    ]);

    const music = card.children.get('music') as FakeDirectory;
    const album = (music.children.get('Artist') as FakeDirectory).children.get('Album') as FakeDirectory;
    expect([...album.children.keys()].sort()).toEqual(['01 - Xtal.mp3', '02 - Tha.mp3']);
  });

  it('counts a file whose copy fails without stopping the rest', async () => {
    const card = new FakeDirectory('card');
    const music = card.directory('music');
    // A file already in the card whose write refuses stands in for a quota or permission
    // failure: `getFileHandle` hands back the existing handle, and its write throws.
    const bad = new FakeFile('bad.mp3');
    bad.createWritable = () =>
      Promise.resolve({
        write: async () => {
          throw new Error('full');
        },
        close: async () => {},
      });
    music.children.set('bad.mp3', bad);

    const result = await copyIntoMusic(
      card as unknown as ComposeDirectory,
      [dropped('bad.mp3', [text('BAD')]), dropped('good.mp3', [text('GOOD')])],
      () => {},
    );

    expect(result).toEqual({ copied: 1, skipped: 1 });
  });
});

describe('walking the card', () => {
  it('lists every file below music/, in name order, skipping hidden files', async () => {
    const card = new FakeDirectory('card');
    const music = card.directory('music');
    music.directory('Artist').directory('Album').file('01.mp3', new Uint8Array([1]), 111);
    music.file('root.flac', new Uint8Array([1, 2]), 222);
    music.file('.DS_Store', new Uint8Array([9]), 333);

    const entries = await walkMusic(card as unknown as WalkDirectory);

    expect(entries).toEqual([
      { path: 'Artist/Album/01.mp3', size: 1, mtime: 111 },
      { path: 'root.flac', size: 2, mtime: 222 },
    ]);
  });

  it('treats a card with no music/ as empty', async () => {
    const card = new FakeDirectory('card');
    card.file('notes.txt', new Uint8Array([1]));
    expect(await walkMusic(card as unknown as WalkDirectory)).toEqual([]);
  });
});

describe('the walk diff', () => {
  const entry = (path: string, size: number, mtime: number) => ({ path, size, mtime });

  it('splits two walks into added, removed and changed', () => {
    const previous = [entry('a.mp3', 1, 100), entry('b.mp3', 2, 200), entry('gone.mp3', 3, 300)];
    const current = [entry('a.mp3', 1, 100), entry('b.mp3', 2, 250), entry('c.mp3', 4, 400)];

    expect(diffMusic(previous, current)).toEqual({
      added: [entry('c.mp3', 4, 400)],
      removed: [entry('gone.mp3', 3, 300)],
      changed: [entry('b.mp3', 2, 250)],
    });
  });

  it('leaves a file a copy is still writing out of added and changed', () => {
    const previous: Array<ReturnType<typeof entry>> = [];
    const current = [entry('writing.mp3', 5, 500), entry('done.mp3', 6, 600)];

    expect(diffMusic(previous, current, new Set(['writing.mp3']))).toEqual({
      added: [entry('done.mp3', 6, 600)],
      removed: [],
      changed: [],
    });
  });
});

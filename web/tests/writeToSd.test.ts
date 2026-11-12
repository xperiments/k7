import { describe, expect, it } from 'vitest';
import { sha256Hex } from '../src/lib/sha256';
import type { CachedMedia } from '../src/storage/media';
import {
  planMusic,
  writeCardToSd,
  type WriteDirectory,
  type WriteProgress,
} from '../src/card/writeToSd';

/**
 * The finish act's pure half, against directory stand-ins.
 *
 * One fake stands for both the virtual card (source) and the SD (target): it records the
 * directories it makes and the bytes each file ends with, and a `File`'s own `stream()` is
 * what the copy reads — so "streamed chunk-by-chunk" is pinned against the real thing, not a
 * hand-rolled stream.
 */

class FakeFile {
  readonly kind = 'file' as const;
  bytes: Uint8Array;
  constructor(
    readonly name: string,
    bytes: Uint8Array = new Uint8Array(),
    readonly mtime = 1_700_000_000_000,
  ) {
    this.bytes = bytes;
  }
  async getFile(): Promise<File> {
    const copy = new Uint8Array(this.bytes.byteLength);
    copy.set(this.bytes);
    return new File([copy], this.name, { lastModified: this.mtime });
  }
  async createWritable() {
    const chunks: Uint8Array[] = [];
    return {
      write: async (data: Uint8Array) => {
        chunks.push(data.slice());
      },
      close: async () => {
        const total = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
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
    let child = this.children.get(name);
    if (!(child instanceof FakeFile)) {
      child = new FakeFile(name, bytes, mtime);
      this.children.set(name, child);
    }
    child.bytes = bytes;
    return child;
  }

  directory(name: string): FakeDirectory {
    let child = this.children.get(name);
    if (!(child instanceof FakeDirectory)) {
      child = new FakeDirectory(name);
      this.children.set(name, child);
    }
    return child;
  }

  /** The file at a `/`-separated path below this directory, reached by its segments. */
  fileAt(path: string, bytes: Uint8Array, mtime?: number): FakeFile {
    const segments = path.split('/').filter((segment) => segment.length > 0);
    let directory: FakeDirectory = this;
    for (const parent of segments.slice(0, -1)) directory = directory.directory(parent);
    return directory.file(segments[segments.length - 1], bytes, mtime);
  }
}

const text = (s: string): Uint8Array => new TextEncoder().encode(s);
const MTIME = 1_700_000_000_000;

const mediaRow = async (path: string, bytes: Uint8Array, mtime = MTIME): Promise<CachedMedia> => ({
  path,
  title: '',
  artist: '',
  album: '',
  genre: '',
  at: '2026-11-12T00:00:00.000Z',
  size: bytes.length,
  mtime,
  hash: await sha256Hex(bytes),
});

describe('planning the delta', () => {
  const entry = (path: string, size: number, mtime = MTIME) => ({ path, size, mtime });

  it('copies a file with no record entry (new), and a file with no current hash', () => {
    const plan = planMusic(
      [entry('a.mp3', 1), entry('b.mp3', 2)],
      [],
      new Map(),
    );
    expect(plan.copy.map((file) => file.path)).toEqual(['a.mp3', 'b.mp3']);
    expect(plan.skip).toEqual([]);
  });

  it('skips a file whose recorded hash equals the current one', async () => {
    const bytes = text('SAME');
    const row = await mediaRow('a.mp3', bytes);
    const plan = planMusic([entry('a.mp3', bytes.length)], [row], new Map([['a.mp3', row.hash!]]));
    expect(plan.copy).toEqual([]);
    expect(plan.skip).toEqual([{ path: 'a.mp3', hash: row.hash }]);
  });

  it('copies a file whose recorded hash differs from the current one', async () => {
    const bytes = text('NEW');
    const row = await mediaRow('a.mp3', bytes);
    const plan = planMusic([entry('a.mp3', bytes.length)], [row], new Map([['a.mp3', 'old-hash']]));
    expect(plan.copy.map((file) => file.path)).toEqual(['a.mp3']);
    expect(plan.skip).toEqual([]);
  });

  it('copies a file whose media row is stale (size or mtime moved), so its hash is unknown', async () => {
    const bytes = text('MOVED');
    const row = await mediaRow('a.mp3', bytes, 1_700_000_000_001); // a different mtime
    const plan = planMusic(
      [entry('a.mp3', bytes.length, 1_700_000_000_002)],
      [row],
      new Map([['a.mp3', row.hash!]]),
    );
    expect(plan.copy.map((file) => [file.path, file.hash])).toEqual([['a.mp3', null]]);
    expect(plan.skip).toEqual([]);
  });
});

describe('writing a card to SD', () => {
  it('streams changed music into the SD root, skips unchanged, and writes the root files', async () => {
    const source = new FakeDirectory('card');
    source.directory('music').fileAt('Artist/Album/01.mp3', text('ONE'), MTIME);
    source.directory('music').fileAt('Artist/Album/02.mp3', text('TWO'), MTIME);
    const mark = text('{"v":1,"id":"f0b1c2d3-0000-4000-8000-000000000000","created":"2026-11-12T00:00:00.000Z"}');
    source.file('k7.card', mark);
    source.file('k7.db', text('DB'));

    const one = await sha256Hex(text('ONE'));
    const two = await sha256Hex(text('TWO'));
    const media = [
      await mediaRow('Artist/Album/01.mp3', text('ONE')),
      await mediaRow('Artist/Album/02.mp3', text('TWO')),
    ];
    // The second file was already on the SD; the first was not.
    const prior = new Map([['Artist/Album/02.mp3', two]]);

    const target = new FakeDirectory('SD');
    const progress: WriteProgress[] = [];
    const outcome = await writeCardToSd(
      source as unknown as WriteDirectory,
      target as unknown as WriteDirectory,
      {
        media,
        prior,
        onProgress: (step) => progress.push(step),
      },
    );

    expect(outcome.failed).toEqual([]);
    expect(outcome.written.map((file) => file.path).sort()).toEqual(['Artist/Album/01.mp3']);
    expect(outcome.written[0].hash).toBe(one);
    expect(outcome.skipped).toEqual([{ path: 'Artist/Album/02.mp3', hash: two }]);

    // The copied track landed byte-for-byte under SD/music/.
    const music = target.children.get('music') as FakeDirectory;
    const artist = music.children.get('Artist') as FakeDirectory;
    const album = artist.children.get('Album') as FakeDirectory;
    expect(new TextDecoder().decode((album.children.get('01.mp3') as FakeFile).bytes)).toBe('ONE');

    // The mark and the image carry over to the SD root.
    expect(new TextDecoder().decode((target.children.get('k7.card') as FakeFile).bytes)).toBe(
      new TextDecoder().decode(mark),
    );
    expect(new TextDecoder().decode((target.children.get('k7.db') as FakeFile).bytes)).toBe('DB');
    expect(target.children.get('k7.covers')).toBeUndefined();

    // Progress counts the copied music (1) plus the two root files, and ends at the total.
    expect(progress[0]).toEqual({ done: 0, total: 3 });
    expect(progress[progress.length - 1]).toEqual({ done: 3, total: 3 });
  });

  it('leaves a file whose copy fails out of the written set, so the next write retries it', async () => {
    const source = new FakeDirectory('card');
    source.directory('music').fileAt('bad.mp3', text('BAD'), MTIME);
    source.directory('music').fileAt('good.mp3', text('GOOD'), MTIME);
    source.file('k7.card', text('MARK'));

    const target = new FakeDirectory('SD');
    // The target's `bad.mp3` refuses its write: a quota or permission stand-in.
    const bad = new FakeFile('bad.mp3');
    bad.createWritable = () =>
      Promise.resolve({
        write: async () => {
          throw new Error('full');
        },
        close: async () => {},
      });
    const targetMusic = target.directory('music');
    targetMusic.children.set('bad.mp3', bad);

    const good = await sha256Hex(text('GOOD'));
    const outcome = await writeCardToSd(
      source as unknown as WriteDirectory,
      target as unknown as WriteDirectory,
      {
        media: [await mediaRow('good.mp3', text('GOOD')), await mediaRow('bad.mp3', text('BAD'))],
        prior: new Map(),
      },
    );

    expect(outcome.failed).toEqual(['bad.mp3']);
    expect(outcome.written.map((file) => file.path)).toEqual(['good.mp3']);
    expect(outcome.written[0].hash).toBe(good);
  });

  it('fails a file whose verification finds different bytes (same size, wrong hash)', async () => {
    const source = new FakeDirectory('card');
    source.directory('music').fileAt('track.mp3', text('GOOD'), MTIME);
    source.file('k7.card', text('MARK'));

    const target = new FakeDirectory('SD');
    const corrupt = new FakeFile('track.mp3', new Uint8Array(4));
    corrupt.createWritable = () =>
      Promise.resolve({
        write: async () => {},
        close: async () => {
          corrupt.bytes = text('BADS'); // same length, different bytes
        },
      });
    target.directory('music').children.set('track.mp3', corrupt);

    const outcome = await writeCardToSd(
      source as unknown as WriteDirectory,
      target as unknown as WriteDirectory,
      {
        media: [await mediaRow('track.mp3', text('GOOD'))],
        prior: new Map(),
      },
    );

    expect(outcome.failed).toEqual(['track.mp3']);
    expect(outcome.written).toEqual([]);
  });

  it('treats a root file the source does not have as nothing to copy, not a failure', async () => {
    const source = new FakeDirectory('card');
    source.file('k7.card', text('MARK'));
    // No k7.db and no k7.covers: a card composed but not yet snapshotted.

    const target = new FakeDirectory('SD');
    const outcome = await writeCardToSd(
      source as unknown as WriteDirectory,
      target as unknown as WriteDirectory,
      { media: [], prior: new Map() },
    );

    expect(outcome.failed).toEqual([]);
    expect(target.children.get('k7.card')).toBeDefined();
    expect(target.children.get('k7.db')).toBeUndefined();
    expect(target.children.get('k7.covers')).toBeUndefined();
    expect(target.children.get('music')).toBeUndefined();
  });
});

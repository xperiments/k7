import { describe, expect, it } from 'vitest';
import { writableCard, writeCardFile, type CardWritableDirectory } from '../src/card/write';

/**
 * A card the test can hold: directories and files in memory, with the one property the walk
 * depends on — a name is looked up the way the card's filesystem does, folded.
 */
class FakeFile {
  readonly kind = 'file' as const;
  bytes: Uint8Array;
  constructor(readonly name: string, bytes: Uint8Array) {
    this.bytes = bytes;
  }
  async getFile(): Promise<File> {
    const copy = new Uint8Array(this.bytes.byteLength);
    copy.set(this.bytes);
    return new File([copy], this.name);
  }
  async createWritable() {
    return {
      write: async (data: Uint8Array) => {
        this.bytes = new Uint8Array(data.byteLength);
        this.bytes.set(data);
      },
      close: async () => {},
    };
  }
}

class FakeDirectory {
  readonly kind = 'directory' as const;
  readonly entries = new Map<string, FakeDirectory | FakeFile>();
  constructor(readonly name: string) {}

  directory(name: string): FakeDirectory {
    const entry = new FakeDirectory(name);
    this.entries.set(name, entry);
    return entry;
  }

  file(name: string, bytes: Uint8Array): FakeFile {
    const entry = new FakeFile(name, bytes);
    this.entries.set(name, entry);
    return entry;
  }

  private find(name: string): FakeDirectory | FakeFile | undefined {
    const wanted = name.toLowerCase();
    for (const [key, entry] of this.entries) {
      if (key.toLowerCase() === wanted) return entry;
    }
    return undefined;
  }

  async getFileHandle(name: string): Promise<FakeFile> {
    const entry = this.find(name);
    if (entry === undefined || entry.kind !== 'file') throw new Error(`no file ${name}`);
    return entry;
  }

  async getDirectoryHandle(name: string): Promise<FakeDirectory> {
    const entry = this.find(name);
    if (entry === undefined || entry.kind !== 'directory') throw new Error(`no directory ${name}`);
    return entry;
  }

  async *values(): AsyncIterable<FakeDirectory | FakeFile> {
    for (const entry of this.entries.values()) yield entry;
  }
}

function card(): { root: FakeDirectory; album: FakeDirectory } {
  const root = new FakeDirectory('xtape');
  const album = root.directory('music').directory('Artist').directory('Album');
  album.file('01 - Track.mp3', new Uint8Array([1, 2, 3]));
  return { root, album };
}

const asCard = (root: FakeDirectory): CardWritableDirectory =>
  root as unknown as CardWritableDirectory;

describe('writing a file back to the card', () => {
  it('writes over the Track and hands back the file as the card now holds it', async () => {
    const { root, album } = card();
    const fresh = await writeCardFile(
      asCard(root),
      'xtape/music/Artist/Album/01 - Track.mp3',
      new Uint8Array([9, 9, 9, 9]),
    );
    expect(fresh).not.toBeNull();
    expect(fresh?.size).toBe(4);
    const bytes = new Uint8Array(await fresh!.arrayBuffer());
    expect([...bytes]).toEqual([9, 9, 9, 9]);
    expect([...album.entries].map(([name]) => name)).toEqual(['01 - Track.mp3']);
  });

  it('finds the path through the card’s own spelling, which FAT32 folds', async () => {
    const { root } = card();
    const fresh = await writeCardFile(
      asCard(root),
      'Xtape/MUSIC/artist/album/01 - track.MP3',
      new Uint8Array([7]),
    );
    expect(fresh?.size).toBe(1);
  });

  it('refuses a path that is not below the handle it was given', async () => {
    const { root } = card();
    expect(
      await writeCardFile(
        asCard(root),
        'another-card/music/Artist/Album/01 - Track.mp3',
        new Uint8Array([1]),
      ),
    ).toBeNull();
  });

  it('is null for a file the card no longer holds, rather than creating one', async () => {
    const { root } = card();
    expect(
      await writeCardFile(
        asCard(root),
        'xtape/music/Artist/Album/gone.mp3',
        new Uint8Array([1]),
      ),
    ).toBeNull();
  });
});

describe('what can be written through', () => {
  it('is a picked directory handle, and not a stand-in without the write side', () => {
    expect(writableCard(card().root)).not.toBeNull();
    expect(writableCard({ getFileHandle: async () => ({}) })).toBeNull();
    expect(writableCard(null)).toBeNull();
    expect(writableCard({})).toBeNull();
  });
});

import { beforeEach, describe, expect, it, vi } from 'vitest';

/**
 * A card's playlists, read and written through its directory.
 *
 * The browser's directory API cannot run here, so the card's handle is replaced by a stand-in
 * with the four methods the module uses. What is exercised is the contract the editor and the
 * page depend on: a write lands as the format's file, a read turns files back into lists with
 * their fingerprints, a file that is not a list is skipped rather than hidden, and a card with
 * no handle answers "not open" instead of an empty list.
 */

interface Entry {
  name: string;
  bytes: string;
}

/** One directory: files by name, subdirectories by name. */
function directory(entries = new Map<string, Entry>(), dirs = new Map<string, unknown>()) {
  return {
    kind: 'directory' as const,
    async getDirectoryHandle(name: string, options?: { create?: boolean }) {
      if (!dirs.has(name)) {
        if (!options?.create) throw new Error('NotFoundError');
        dirs.set(name, directory());
      }
      return dirs.get(name) as ReturnType<typeof directory>;
    },
    async getFileHandle(name: string, options?: { create?: boolean }) {
      if (!entries.has(name)) {
        if (!options?.create) throw new Error('NotFoundError');
        entries.set(name, { name, bytes: '' });
      }
      return {
        async getFile() {
          const entry = entries.get(name);
          if (!entry) throw new Error('NotFoundError');
          return { text: async () => entry.bytes };
        },
        async createWritable() {
          let buffer = '';
          return {
            async write(data: string) {
              buffer += data;
            },
            async close() {
              entries.set(name, { name, bytes: buffer });
            },
          };
        },
      };
    },
    async removeEntry(name: string) {
      if (!entries.delete(name)) throw new Error('NotFoundError');
    },
    async *values() {
      for (const entry of entries.values()) {
        yield { kind: 'file' as const, name: entry.name, text: async () => entry.bytes };
      }
      for (const name of dirs.keys()) yield { kind: 'directory' as const, name };
    },
    entriesRef: entries,
    dirsRef: dirs,
  };
}

const CARD = 'mark:0000000000004000800000000000e001';

/** The card this browser holds, as `card/handles` would answer it. */
const held = vi.hoisted(() => ({ root: null as unknown }));

vi.mock('../src/card/handles', () => ({
  cardHandleFor: async () => held.root,
  queryAccess: async () => held.root !== null,
  ensureRead: async () => held.root !== null,
}));

const LATE = {
  id: '00000000000000a1',
  name: 'Late night 8A',
  createdAt: 1_700_000_000,
  rows: ['A/B/one.mp3', 'A/B/two.mp3'],
};

beforeEach(() => {
  held.root = null;
});

describe('a card’s playlists', () => {
  it('answers "not open" when this browser holds no handle', async () => {
    const { readCardLists } = await import('../src/playlists/cardLists');
    expect(await readCardLists(CARD)).toBeNull();
  });

  it('writes a list as the format’s file, and reads it back', async () => {
    const root = directory();
    held.root = root;
    const { readCardLists, writeListFile } = await import('../src/playlists/cardLists');

    expect(await writeListFile(CARD, LATE)).toBe(true);
    const playlists = root.dirsRef.get('k7.playlists') as ReturnType<typeof directory>;
    expect(playlists).toBeDefined();
    expect(playlists.entriesRef.get('00000000000000a1.k7list')?.bytes).toBe(
      'K7LIST 1\nid 00000000000000a1\nname Late night 8A\ncreated 1700000000\nrows 2\n' +
        'A/B/one.mp3\nA/B/two.mp3\n',
    );

    const answer = await readCardLists(CARD);
    expect(answer).toEqual({
      kind: 'card',
      lists: [{ ...LATE, fingerprint: '6142dbb394219d00' }],
      unreadable: 0,
    });
  });

  it('skips a file that is not a list, and counts it', async () => {
    const root = directory();
    held.root = root;
    const { readCardLists } = await import('../src/playlists/cardLists');
    root.dirsRef.set(
      'k7.playlists',
      directory(
        new Map([
          ['00000000000000a1.k7list', { name: '00000000000000a1.k7list', bytes: 'K7LIST 9\n' }],
          ['notes.txt', { name: 'notes.txt', bytes: 'not a list' }],
        ]),
      ),
    );

    const answer = await readCardLists(CARD);
    expect(answer?.lists).toEqual([]);
    expect(answer?.unreadable).toBe(1);
  });

  it('removes one file, and says when there is nothing to remove', async () => {
    const root = directory();
    held.root = root;
    const { removeListFile, writeListFile } = await import('../src/playlists/cardLists');

    await writeListFile(CARD, LATE);
    expect(await removeListFile(CARD, LATE.id)).toBe(true);
    const dir = root.dirsRef.get('k7.playlists') as ReturnType<typeof directory>;
    expect(dir.entriesRef.has('00000000000000a1.k7list')).toBe(false);
    expect(await removeListFile(CARD, LATE.id)).toBe(false);
  });

  it('refuses to write a list the device would refuse, and leaves the card alone', async () => {
    const root = directory();
    held.root = root;
    const { writeListFile } = await import('../src/playlists/cardLists');

    expect(await writeListFile(CARD, { ...LATE, name: 'x'.repeat(64) })).toBe(false);
    expect(await writeListFile(CARD, { ...LATE, rows: ['/absolute.mp3'] })).toBe(false);
    expect(root.dirsRef.get('k7.playlists')).toBeUndefined();
  });
});

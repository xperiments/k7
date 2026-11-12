import 'fake-indexeddb/auto';
import { beforeEach, describe, expect, it } from 'vitest';
import { parseCardMark } from '../src/card/mark';
import { currentCardScope, setCardScope } from '../src/card/session';
import { forgetWorkingDirectory, rememberWorkingDirectory } from '../src/card/vdir';
import {
  createVirtualCard,
  folderName,
  openVirtualCard,
  reconnectWorkingDirectory,
  sizeBytes,
  sizeLabel,
  type VdirDirectory,
} from '../src/card/virtual';
import { listVirtualCards, removeVirtualCard } from '../src/storage/vcards';

/** A random source that fills the buffer, as `crypto.getRandomValues` does. */
const filled = (byte: number) => (bytes: Uint8Array) => {
  bytes.fill(byte);
  return bytes;
};

/** The id `filled(0x11)` produces: a UUID v4 built from sixteen 0x11 bytes. */
const ID = '11111111-1111-4111-9111-111111111111';
const NOW = '2026-11-12T00:00:00.000Z';

/** A directory stand-in: records the names it makes and the files it writes. */
function fakeDirectory() {
  const created: string[] = [];
  const writes = new Map<string, string>();

  const make = (): VdirDirectory => {
    const children = new Map<string, VdirDirectory>();
    return {
      getDirectoryHandle(name, options) {
        const existing = children.get(name);
        if (existing) return Promise.resolve(existing);
        if (options?.create !== true) {
          return Promise.reject(new DOMException(`no ${name}`, 'NotFoundError'));
        }
        const child = make();
        children.set(name, child);
        created.push(name);
        return Promise.resolve(child);
      },
      getFileHandle(name, options) {
        if (options?.create) created.push(name);
        return Promise.resolve({
          createWritable() {
            let bytes: Uint8Array = new Uint8Array();
            return Promise.resolve({
              write(data: Uint8Array) {
                bytes = data;
                return Promise.resolve();
              },
              close() {
                writes.set(name, new TextDecoder().decode(bytes));
                return Promise.resolve();
              },
            });
          },
        });
      },
    };
  };

  return { root: make(), created, writes };
}

beforeEach(async () => {
  setCardScope('local');
  await forgetWorkingDirectory();
  for (const card of await listVirtualCards()) await removeVirtualCard(card.id);
  delete (globalThis as { showDirectoryPicker?: unknown }).showDirectoryPicker;
});

describe('names and sizes', () => {
  it('names a folder from the label, with a short id to keep two same-named cards apart', () => {
    expect(folderName('Road trip', ID)).toBe('road-trip-11111111');
    expect(folderName('   ', ID)).toBe('card-11111111');
    expect(folderName('A/B?! c', ID)).toBe('a-b-c-11111111');
  });

  it('converts gigabytes to bytes and back to a label', () => {
    expect(sizeBytes(32)).toBe(32_000_000_000);
    expect(sizeLabel(sizeBytes(32))).toBe('32 GB');
    expect(sizeLabel(sizeBytes(40))).toBe('40 GB');
  });
});

describe('creating a virtual card', () => {
  it('makes a card-shaped folder — music/ and a mark — and records it', async () => {
    const fake = fakeDirectory();
    const created = await createVirtualCard(fake.root, 'Road trip', sizeBytes(32), {
      now: NOW,
      random: filled(0x11),
    });

    expect(created.card).toEqual({
      id: ID,
      label: 'Road trip',
      folder: 'road-trip-11111111',
      sizeBytes: sizeBytes(32),
      scope: `mark:${ID}`,
      created: NOW,
    });
    // The folder, its music dir, then the mark — a card from the first moment.
    expect(fake.created).toEqual(['road-trip-11111111', 'music', 'k7.card']);

    const mark = parseCardMark(fake.writes.get('k7.card') ?? '');
    expect(mark).toEqual({
      v: 1,
      id: ID,
      created: NOW,
      label: 'Road trip',
      capacity: sizeBytes(32),
    });

    expect(await listVirtualCards()).toEqual([created.card]);
  });

  it('omits an empty name from the mark but still makes a folder', async () => {
    const fake = fakeDirectory();
    const created = await createVirtualCard(fake.root, '   ', sizeBytes(16), {
      now: NOW,
      random: filled(0x11),
    });
    const mark = parseCardMark(fake.writes.get('k7.card') ?? '');
    expect(created.card.label).toBe('');
    expect(typeof mark === 'object' && mark !== null && 'label' in mark).toBe(false);
    expect(created.card.folder).toBe('card-11111111');
  });

  it('refuses a capacity that is not positive', async () => {
    const fake = fakeDirectory();
    await expect(createVirtualCard(fake.root, 'Road trip', 0, { random: filled(0x11) })).rejects.toThrow(
      'positive capacity',
    );
  });
});

describe('opening a virtual card', () => {
  it('makes it the card this session works on', async () => {
    const fake = fakeDirectory();
    const created = await createVirtualCard(fake.root, 'Road trip', sizeBytes(32), {
      now: NOW,
      random: filled(0x11),
    });

    await openVirtualCard(created.card, created.directory);

    expect(currentCardScope()).toBe(`mark:${ID}`);
  });
});

describe('reconnecting the working directory', () => {
  it('says so when the browser has no picker and nothing remembered', async () => {
    expect(await reconnectWorkingDirectory()).toEqual({ kind: 'unsupported' });
  });

  it('picks the directory read-write, under a fixed id', async () => {
    const calls: Array<{ mode?: string; id?: string }> = [];
    (globalThis as { showDirectoryPicker?: unknown }).showDirectoryPicker = async (
      options: { mode?: string; id?: string },
    ) => {
      calls.push(options);
      return fakeDirectory().root;
    };

    const result = await reconnectWorkingDirectory();
    expect(result.kind).toBe('ready');
    expect(calls[0]).toMatchObject({ mode: 'readwrite', id: 'k7-vdir' });
  });

  it('reuses a remembered directory without the picker', async () => {
    await rememberWorkingDirectory({ name: 'working' } as unknown as FileSystemDirectoryHandle);
    const result = await reconnectWorkingDirectory();
    expect(result.kind).toBe('ready');
  });
});

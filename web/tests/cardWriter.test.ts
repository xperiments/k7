import { describe, expect, it } from 'vitest';
import {
  cardPicker,
  identifyCard,
  pickCardDirectory,
  writeSnapshotToCard,
  type CardDirectory,
  type SnapshotDirectory,
} from '../src/ui/cardWriter';
import { MARK_NAME, parseCardMark } from '../src/card/mark';
import { UNSCOPED } from '../src/card/session';

function directory(record: { names: string[]; writes: Uint8Array[]; closed: boolean }) {
  const handle: SnapshotDirectory = {
    async getFileHandle(name, options) {
      record.names.push(name);
      expect(options?.create).toBe(true);
      return {
        async createWritable() {
          return {
            async write(data) {
              record.writes.push(data);
            },
            async close() {
              record.closed = true;
            },
          };
        },
      };
    },
  };
  return handle;
}

describe('writing to the card', () => {
  it('says so when the browser has no picker', async () => {
    expect(await pickCardDirectory(undefined)).toEqual({ kind: 'unsupported' });
  });

  it('treats a cancelled pick as an answer, not an error', async () => {
    const picker = async () => {
      throw new DOMException('cancelled', 'AbortError');
    };
    expect(await pickCardDirectory(picker)).toEqual({ kind: 'cancelled' });
  });

  it('writes k7.db into the chosen directory and closes it', async () => {
    const record = { names: [] as string[], writes: [] as Uint8Array[], closed: false };
    const picked = await pickCardDirectory(async () => directory(record));
    if (picked.kind !== 'picked') throw new Error(`expected a directory, got ${picked.kind}`);

    const bytes = new Uint8Array([1, 2, 3]);
    await writeSnapshotToCard(picked.directory, bytes);

    expect(record.names).toEqual(['k7.db']);
    expect(record.writes).toEqual([bytes]);
    expect(record.closed).toBe(true);
  });
});

describe('the picker', () => {
  it('asks for read-write, so the card can be written back', async () => {
    const calls: Array<{ mode?: string; id?: string }> = [];
    (globalThis as { showDirectoryPicker?: unknown }).showDirectoryPicker = async (options: {
      mode?: string;
      id?: string;
    }) => {
      calls.push(options);
      return {};
    };
    try {
      const picker = cardPicker();
      expect(picker).toBeDefined();
      await picker!();
      expect(calls[0]).toMatchObject({ mode: 'readwrite', id: 'k7-card' });
    } finally {
      delete (globalThis as { showDirectoryPicker?: unknown }).showDirectoryPicker;
    }
  });
});

/** A picked directory that can be listed and read, which the mark flow needs. */
function cardDirectory(contents: Record<string, string>, entries: string[]) {
  const writes: Array<{ name: string; bytes: Uint8Array }> = [];
  const handle: CardDirectory = {
    async *values() {
      for (const name of entries) {
        yield { kind: name === 'music' ? 'directory' : 'file', name } as const;
      }
    },
    async getFileHandle(name, options) {
      const text = contents[name];
      if (text === undefined && options?.create !== true) {
        throw new DOMException(`no ${name}`, 'NotFoundError');
      }
      return {
        async getFile() {
          return new File([text ?? ''], name);
        },
        async createWritable() {
          return {
            async write(data: Uint8Array) {
              writes.push({ name, bytes: data });
            },
            async close() {
              /* nothing to flush in the fake */
            },
          };
        },
      };
    },
  };
  return { handle, writes };
}

const CARD_ROOT = ['music', 'k7.db'];

describe('identifying the picked directory', () => {
  it('marks a card with no mark yet, and returns the scope that mark names', async () => {
    const { handle, writes } = cardDirectory({}, CARD_ROOT);

    const scope = await identifyCard(handle);

    expect(scope.startsWith('mark:')).toBe(true);
    expect(writes.map((write) => write.name)).toEqual([MARK_NAME]);
    const written = parseCardMark(new TextDecoder().decode(writes[0].bytes));
    expect(typeof written).toBe('object');
    if (typeof written !== 'object') throw new Error('expected a mark');
    expect(written.v).toBe(1);
    expect(scope).toBe(`mark:${written.id}`);
  });

  it('adopts an existing mark without writing again', async () => {
    const existing = JSON.stringify({ v: 1, id: 'f0b1c2d3-0000-4000-8000-000000000000', created: '2026-11-03T00:00:00.000Z' });
    const { handle, writes } = cardDirectory({ [MARK_NAME]: existing }, CARD_ROOT);

    expect(await identifyCard(handle)).toBe('mark:f0b1c2d3-0000-4000-8000-000000000000');
    expect(writes).toEqual([]);
  });

  it('never writes to a directory that is not a card root', async () => {
    const { handle, writes } = cardDirectory({}, ['Artist/', 'track.mp3']);

    expect(await identifyCard(handle)).toBe(UNSCOPED);
    expect(writes).toEqual([]);
  });

  it('never overwrites a mark this app did not write', async () => {
    const { handle, writes } = cardDirectory({ [MARK_NAME]: '{"v":1,"id":"nope"}' }, CARD_ROOT);

    expect(await identifyCard(handle)).toBe(UNSCOPED);
    expect(writes).toEqual([]);
  });
});

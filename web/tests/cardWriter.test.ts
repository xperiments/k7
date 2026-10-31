import { describe, expect, it } from 'vitest';
import {
  cardPicker,
  pickCardDirectory,
  writeSnapshotToCard,
  type SnapshotDirectory,
} from '../src/ui/cardWriter';

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

  it('writes xtape.db into the chosen directory and closes it', async () => {
    const record = { names: [] as string[], writes: [] as Uint8Array[], closed: false };
    const picked = await pickCardDirectory(async () => directory(record));
    if (picked.kind !== 'picked') throw new Error(`expected a directory, got ${picked.kind}`);

    const bytes = new Uint8Array([1, 2, 3]);
    await writeSnapshotToCard(picked.directory, bytes);

    expect(record.names).toEqual(['xtape.db']);
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
      expect(calls[0]).toMatchObject({ mode: 'readwrite', id: 'xtape-card' });
    } finally {
      delete (globalThis as { showDirectoryPicker?: unknown }).showDirectoryPicker;
    }
  });
});

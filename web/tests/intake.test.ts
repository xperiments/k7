import { describe, expect, it } from 'vitest';
import { currentCardScope } from '../src/card/session';
import { readCardWithPicker } from '../src/ui/intake';
import type { DirectoryPicker, SnapshotDirectory } from '../src/ui/cardWriter';

/**
 * The one way in.
 *
 * Both "Choose card" and "Change card" call this, so its four steps are the contract: pick
 * read-write, walk, refuse a choice that is not the card root, identify the card. The walk
 * and the mark are faked here; the shape rule is the real one.
 */

interface FakeFile {
  kind: 'file';
  name: string;
  getFile(): Promise<File>;
}

interface FakeFolder {
  kind: 'directory';
  name: string;
  values(): AsyncIterable<FakeFile | FakeFolder>;
  getFileHandle(name: string): Promise<FakeFile>;
}

function file(name: string, bytes: Uint8Array | string = 'x'): FakeFile {
  return {
    kind: 'file',
    name,
    async getFile() {
      const body: BlobPart =
        typeof bytes === 'string' ? new TextEncoder().encode(bytes) : new Uint8Array(bytes);
      return new File([body], name, { type: 'audio/mpeg' });
    },
  };
}

function folder(name: string, children: (FakeFile | FakeFolder)[]): FakeFolder {
  return {
    kind: 'directory',
    name,
    async *values() {
      for (const child of children) yield child;
    },
    async getFileHandle(childName: string) {
      const found = children.find((child) => child.name === childName);
      if (!found || found.kind !== 'file') throw new Error(`no ${childName}`);
      return found;
    },
  };
}

const picker = (directory: FakeFolder): DirectoryPicker => async () =>
  directory as unknown as SnapshotDirectory;

const track = () => file('01 - Xtal.mp3');
const mark = (id: string) =>
  file('k7.card', JSON.stringify({ v: 1, id, created: '2026-11-03T01:20:00Z' }));

const CARD_ID = 'f0b1c2d3-0000-4000-8000-000000000001';

describe('reading a card the user picks', () => {
  it('takes the card root and scopes the work to its mark', async () => {
    const card = folder('SDCARD', [
      mark(CARD_ID),
      folder('music', [folder('Aphex Twin', [track()])]),
    ]);

    const picked = await readCardWithPicker(picker(card));

    expect(picked.kind).toBe('read');
    if (picked.kind !== 'read') return;
    expect(picked.files.map((found) => found.path)).toContain(
      'SDCARD/music/Aphex Twin/01 - Xtal.mp3',
    );
    expect(currentCardScope()).toBe(`mark:${CARD_ID}`);
  });

  it('refuses the music directory by name, and leaves the queue alone', async () => {
    const music = folder('music', [folder('Aphex Twin', [track()])]);

    const picked = await readCardWithPicker(picker(music));

    expect(picked).toEqual({
      kind: 'wrong',
      problem: expect.stringMatching(/music folder, not the card/),
    });
  });

  it('refuses a folder with no music in it', async () => {
    const rips = folder('RIPS', [folder('Aphex Twin', [track()])]);
    const picked = await readCardWithPicker(picker(rips));
    expect(picked.kind).toBe('wrong');
  });

  it('says the browser cannot open a folder instead of failing silently', async () => {
    expect(await readCardWithPicker(undefined)).toEqual({ kind: 'unsupported' });
  });

  it('treats a cancelled picker as the user’s answer', async () => {
    const cancelled: DirectoryPicker = async () => {
      throw new DOMException('cancelled', 'AbortError');
    };
    expect(await readCardWithPicker(cancelled)).toEqual({ kind: 'cancelled' });
  });

  it('ignores a second pick while the first is still open', async () => {
    const card = folder('SDCARD', [
      mark(CARD_ID),
      folder('music', [folder('Aphex Twin', [track()])]),
    ]);
    let release!: (directory: SnapshotDirectory) => void;
    let calls = 0;
    const oneAtATime: DirectoryPicker = () => {
      calls += 1;
      return new Promise((resolve) => {
        release = resolve;
      });
    };

    const first = readCardWithPicker(oneAtATime);
    expect(await readCardWithPicker(oneAtATime)).toEqual({ kind: 'cancelled' });
    expect(calls).toBe(1);

    release(card as unknown as SnapshotDirectory);
    expect((await first).kind).toBe('read');
  });

  it('names a picker that is already active elsewhere', async () => {
    const active: DirectoryPicker = async () => {
      throw new Error(
        "Failed to execute 'showDirectoryPicker' on 'Window': File picker already active.",
      );
    };
    expect(await readCardWithPicker(active)).toEqual({ kind: 'busy' });
  });
});

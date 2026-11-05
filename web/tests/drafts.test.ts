import 'fake-indexeddb/auto';
import { describe, expect, it, vi } from 'vitest';

/**
 * The one thing the two-column editor adds to the drafts store: a block. A folder must
 * stay one block when it is moved or removed, while `rows` — the flat list the Snapshot
 * carries — always follows it.
 */

async function load() {
  vi.resetModules();
  return import('../src/playlists/drafts');
}

const album = {
  kind: 'folder' as const,
  path: 'Artist/Album',
  name: 'Album',
  rows: ['Artist/Album/01.mp3', 'Artist/Album/02.mp3'],
};

describe('a draft made of blocks', () => {
  it('starts a fresh draft as one track block per row', async () => {
    const drafts = await load();
    const draft = drafts.createDraft('Late night', ['a.mp3', 'b.mp3']);

    expect(draft.entries.map((entry) => entry.kind)).toEqual(['track', 'track']);
    expect(draft.rows).toEqual(['a.mp3', 'b.mp3']);
  });

  it('keeps a dropped folder as one block and flattens its files after it', async () => {
    const drafts = await load();
    const draft = drafts.createDraft('Albums', ['intro.mp3']);
    const inserted = drafts.insertEntries(draft.id, [album], 1);

    expect(inserted?.entries.map((entry) => entry.kind)).toEqual(['track', 'folder']);
    expect(inserted?.rows).toEqual(['intro.mp3', ...album.rows]);
  });

  it('moves and removes a folder block with all of its files', async () => {
    const drafts = await load();
    const draft = drafts.createDraft('Albums', ['a.mp3', 'b.mp3']);
    drafts.insertEntries(draft.id, [album], 1);

    const moved = drafts.moveEntry(draft.id, 1, 0);
    expect(moved?.entries.map((entry) => entry.kind)).toEqual(['folder', 'track', 'track']);
    expect(moved?.rows).toEqual([...album.rows, 'a.mp3', 'b.mp3']);

    const removed = drafts.removeEntry(draft.id, 0);
    expect(removed?.rows).toEqual(['a.mp3', 'b.mp3']);
  });

  it('appends a track without disturbing a folder block, and clears the whole list', async () => {
    const drafts = await load();
    const draft = drafts.createDraft('Albums');
    drafts.insertEntries(draft.id, [album], 0);

    const appended = drafts.addRows(draft.id, ['z.mp3']);
    expect(appended?.entries.map((entry) => entry.kind)).toEqual(['folder', 'track']);
    expect(appended?.rows).toEqual([...album.rows, 'z.mp3']);

    const cleared = drafts.clearDraft(draft.id);
    expect(cleared?.entries).toEqual([]);
    expect(cleared?.rows).toEqual([]);
  });
});

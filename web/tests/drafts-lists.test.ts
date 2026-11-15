import 'fake-indexeddb/auto';
import { describe, expect, it, vi } from 'vitest';

/**
 * The two things a card's lists made possible and the draft store did not have: reading a card's
 * list into a draft without forking its id, and deleting a list in a way the card will honour.
 *
 * The modules are re-imported with `vi.resetModules()` so module-level state is rebuilt the way a
 * page load rebuilds it; only IndexedDB persists, as it really does (ADR 0092).
 */

async function load() {
  vi.resetModules();
  const session = await import('../src/card/session');
  const drafts = await import('../src/playlists/drafts');
  return { session, drafts };
}

/** Let the store's fire-and-forget writes reach IndexedDB before the next read. */
async function flush() {
  await new Promise<void>((resolve) => setTimeout(resolve, 0));
  await new Promise<void>((resolve) => setTimeout(resolve, 0));
}

const CARD_A = 'mark:00000000-0000-4000-8000-00000000d0a1';
const CARD_B = 'mark:00000000-0000-4000-8000-00000000d0a2';

const LATE = {
  id: '00000000000000b1',
  name: 'Late night 8A',
  createdAt: 1_700_000_000,
  rows: ['A/B/one.mp3', 'A/B/two.mp3'],
};

describe('adopting a card’s list', () => {
  it('keeps the list’s id, name, date and rows', async () => {
    const { session, drafts } = await load();
    session.setCardScope(CARD_A);
    await drafts.loadDrafts();

    const adopted = drafts.adoptDraft(LATE);
    expect(adopted.id).toBe(LATE.id);
    expect(adopted.name).toBe('Late night 8A');
    expect(adopted.createdAt).toBe(1_700_000_000);
    expect(adopted.rows).toEqual(['A/B/one.mp3', 'A/B/two.mp3']);
    expect(adopted.entries.map((entry) => entry.kind)).toEqual(['track', 'track']);

    await flush();
    await drafts.loadDrafts();
    expect(drafts.listDrafts().map((draft) => draft.id)).toEqual([LATE.id]);
  });

  it('is the same draft when the list is opened twice', async () => {
    const { session, drafts } = await load();
    session.setCardScope(CARD_B);
    await drafts.loadDrafts();

    const first = drafts.adoptDraft(LATE);
    const again = drafts.adoptDraft(LATE);
    expect(again.id).toBe(first.id);
    expect(drafts.listDrafts()).toHaveLength(1);
  });
});

describe('deleting a list', () => {
  it('records a pending removal the card has not answered yet', async () => {
    const { session, drafts } = await load();
    session.setCardScope(CARD_A);
    await drafts.loadDrafts();
    drafts.adoptDraft(LATE);
    await flush();

    drafts.removeList(LATE.id);
    expect(drafts.listDrafts()).toEqual([]);
    expect(drafts.listRemovals()).toEqual([LATE.id]);

    await flush();
    await drafts.loadDrafts();
    expect(drafts.listRemovals()).toEqual([LATE.id]);
  });

  it('is cleared by the write that reaches the card', async () => {
    const { session, drafts } = await load();
    session.setCardScope(CARD_A);
    await drafts.loadDrafts();
    drafts.adoptDraft(LATE);
    drafts.removeList(LATE.id);
    await flush();

    drafts.clearRemovals();
    expect(drafts.listRemovals()).toEqual([]);

    await flush();
    await drafts.loadDrafts();
    expect(drafts.listRemovals()).toEqual([]);
  });

  it('is cleared when the list is adopted again', async () => {
    const { session, drafts } = await load();
    session.setCardScope(CARD_A);
    await drafts.loadDrafts();
    drafts.adoptDraft(LATE);
    drafts.removeList(LATE.id);
    await flush();

    drafts.adoptDraft(LATE);
    expect(drafts.listRemovals()).toEqual([]);
  });

  it('belongs to the card it was deleted for', async () => {
    const { session, drafts } = await load();
    session.setCardScope(CARD_A);
    await drafts.loadDrafts();
    drafts.adoptDraft(LATE);
    drafts.removeList(LATE.id);
    await flush();

    session.setCardScope(CARD_B);
    await drafts.loadDrafts();
    expect(drafts.listRemovals()).toEqual([]);

    session.setCardScope(CARD_A);
    await drafts.loadDrafts();
    expect(drafts.listRemovals()).toEqual([LATE.id]);
  });
});

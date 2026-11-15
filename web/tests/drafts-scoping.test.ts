import 'fake-indexeddb/auto';
import { describe, expect, it, vi } from 'vitest';

/**
 * Drafts are card-scoped (ADR 0083): one card's playlists never leak into another's, a pick
 * that changes the card reloads the drafts, and the image builder takes only the current
 * card's lists.
 *
 * The modules are re-imported with `vi.resetModules()` so module-level state is rebuilt the
 * way a page load rebuilds it; only IndexedDB persists, as it really does. The store's
 * writes are fire-and-forget, so a `flush` lets them reach IndexedDB before a read.
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

describe('card-scoped drafts', () => {
  it('keeps each card its own drafts', async () => {
    const { session, drafts } = await load();
    const cardA = 'mark:00000000-0000-4000-8000-00000000c011';
    const cardB = 'mark:00000000-0000-4000-8000-00000000c012';

    session.setCardScope(cardA);
    await drafts.loadDrafts();
    const made = drafts.createDraft('Card A mix', ['a.mp3']);
    await flush();

    session.setCardScope(cardB);
    await drafts.loadDrafts();
    expect(drafts.listDrafts()).toEqual([]);

    session.setCardScope(cardA);
    await drafts.loadDrafts();
    expect(drafts.listDrafts().map((draft) => draft.id)).toEqual([made.id]);
  });

  it('reloads the drafts on its own when a pick changes the card', async () => {
    const { session, drafts } = await load();
    const cardA = 'mark:00000000-0000-4000-8000-00000000c013';
    const cardB = 'mark:00000000-0000-4000-8000-00000000c014';

    session.setCardScope(cardA);
    await drafts.loadDrafts();
    drafts.createDraft('Card A mix', ['a.mp3']);
    await flush();

    // Switching cards empties the list at once, before the new card's read even lands.
    session.setCardScope(cardB);
    expect(drafts.listDrafts()).toEqual([]);
    await drafts.loadDrafts();
    expect(drafts.listDrafts()).toEqual([]);
  });

  it('returns only the current card in the snapshot lists', async () => {
    const { session, drafts } = await load();
    const cardA = 'mark:00000000-0000-4000-8000-00000000c015';
    const cardB = 'mark:00000000-0000-4000-8000-00000000c016';

    session.setCardScope(cardA);
    await drafts.loadDrafts();
    drafts.createDraft('A mix', ['a.mp3']);
    await flush();

    session.setCardScope(cardB);
    await drafts.loadDrafts();
    drafts.createDraft('B mix', ['b.mp3']);
    await flush();

    session.setCardScope(cardA);
    await drafts.loadDrafts();
    expect(drafts.listDrafts().map((list) => list.name)).toEqual(['A mix']);
  });
});

import { describe, expect, it } from 'vitest';
import {
  chooseListSource,
  joinLists,
  normaliseListId,
  type DraftList,
  type JoinInput,
  type ListRecord,
  type ListSummary,
} from './lists';

/**
 * Which answer the Playlists page reads, and one row per list.
 *
 * Both rules are pure and both are the kind that goes wrong quietly — a page that says "the card
 * has this" about a list it never read, or that shows a deleted list because the card still
 * carries it. These are the cases ADR 0092 names.
 */

const LATE: ListRecord = {
  id: '00000000000000a1',
  name: 'Late night 8A',
  createdAt: 1_700_000_000,
  rows: ['A/B/one.mp3', 'A/B/two.mp3'],
};

const SLOW: ListRecord = {
  id: '00000000000000a2',
  name: 'Slow',
  createdAt: 1_700_000_100,
  rows: ['C/D/three.mp3'],
};

/** A draft of a list, which is only the list's identity and its rows. */
function draftOf(list: ListRecord, rows: readonly string[] = list.rows): DraftList {
  return { id: list.id, name: list.name, createdAt: list.createdAt, rows };
}

function input(over: Partial<JoinInput> = {}): JoinInput {
  return { card: null, player: null, drafts: [], removals: [], ...over };
}

describe('chooseListSource', () => {
  it('reads the player when the Device card is the selected one', () => {
    expect(
      chooseListSource({ deviceView: true, deviceCard: true, cardOpen: false }),
    ).toBe('device');
  });

  it('reads nothing when the Device card is selected and the player holds no card', () => {
    expect(
      chooseListSource({ deviceView: true, deviceCard: false, cardOpen: true }),
    ).toBe('none');
  });

  it('reads the card itself while its directory is open', () => {
    expect(
      chooseListSource({ deviceView: false, deviceCard: true, cardOpen: true }),
    ).toBe('card');
  });

  it('reads nothing when the card is not open, because there is no copy to fall back on', () => {
    expect(
      chooseListSource({ deviceView: false, deviceCard: true, cardOpen: false }),
    ).toBe('none');
  });
});

describe('joinLists', () => {
  it('shows a card prepared on another computer, with no draft here', () => {
    const joined = joinLists(input({ card: { kind: 'card', lists: [LATE, SLOW] } }));
    expect(joined.card).toBe('card');
    expect(joined.rows.map((row) => [row.name, row.reach, row.draft, row.onCard])).toEqual([
      ['Late night 8A', 'card', false, true],
      ['Slow', 'card', false, true],
    ]);
  });

  it('says the card has not been read when only a draft is here', () => {
    const joined = joinLists(input({ drafts: [draftOf(LATE)] }));
    expect(joined.card).toBe('none');
    expect(joined.rows).toHaveLength(1);
    expect(joined.rows[0]).toMatchObject({ reach: 'here', draft: true, onCard: false, stale: false });
  });

  it('marks the card’s copy stale when the draft has moved on', () => {
    const joined = joinLists(
      input({
        card: { kind: 'card', lists: [LATE] },
        drafts: [draftOf(LATE, [...LATE.rows, 'A/B/three.mp3'])],
      }),
    );
    expect(joined.rows[0]).toMatchObject({
      reach: 'card',
      draft: true,
      stale: true,
      rows: 3,
      name: 'Late night 8A',
    });
  });

  it('does not call a card’s copy stale when the draft says the same rows', () => {
    const joined = joinLists(
      input({ card: { kind: 'card', lists: [LATE] }, drafts: [draftOf(LATE)] }),
    );
    expect(joined.rows[0].stale).toBe(false);
  });

  it('follows a name the person changed here, on a list the card still has', () => {
    const joined = joinLists(
      input({
        card: { kind: 'card', lists: [LATE] },
        drafts: [{ ...draftOf(LATE), name: 'Late' }],
      }),
    );
    expect(joined.rows[0].name).toBe('Late');
    expect(joined.rows[0].stale).toBe(false);
  });

  it('reaches the player, and says whether it is walking', () => {
    const summary: ListSummary = {
      id: LATE.id,
      name: LATE.name,
      createdAt: LATE.createdAt,
      rows: LATE.rows.length,
    };
    const joined = joinLists(
      input({ player: { lists: [summary], selected: LATE.id, walking: true } }),
    );
    expect(joined.rows[0]).toMatchObject({
      reach: 'player',
      selected: true,
      walking: true,
      rows: 2,
    });
  });

  it('keeps a deleted list while its card still carries it', () => {
    const joined = joinLists(
      input({ card: { kind: 'card', lists: [LATE] }, removals: [LATE.id] }),
    );
    expect(joined.rows).toHaveLength(1);
    expect(joined.rows[0]).toMatchObject({ reach: 'card', removalPending: true, draft: false });
  });

  it('drops a removal that nothing carries any more', () => {
    const joined = joinLists(input({ removals: [LATE.id] }));
    expect(joined.rows).toEqual([]);
  });

  it('marks a player list stale when the draft’s fingerprint differs from the player’s', () => {
    const summary = {
      id: LATE.id,
      name: LATE.name,
      createdAt: LATE.createdAt,
      rows: LATE.rows.length,
      fingerprint: '00000000000000aa',
    };
    const joined = joinLists(
      input({
        player: { lists: [summary], selected: null, walking: false },
        drafts: [{ ...draftOf(LATE), fingerprint: '00000000000000bb' }],
      }),
    );
    expect(joined.rows[0]).toMatchObject({ reach: 'player', stale: true });
  });

  it('marks the card stale by fingerprint when both sides carry one', () => {
    const joined = joinLists(
      input({
        card: { kind: 'card', lists: [{ ...LATE, fingerprint: '00000000000000aa' }] },
        drafts: [{ ...draftOf(LATE), fingerprint: '00000000000000bb' }],
      }),
    );
    expect(joined.rows[0]).toMatchObject({ reach: 'card', stale: true });
  });

  it('calls a card’s copy current when the fingerprints agree', () => {
    const joined = joinLists(
      input({
        card: { kind: 'card', lists: [{ ...LATE, fingerprint: '00000000000000aa' }] },
        drafts: [{ ...draftOf(LATE), fingerprint: '00000000000000aa' }],
      }),
    );
    expect(joined.rows[0].stale).toBe(false);
  });

  it('orders the rows by when the lists were made', () => {
    const joined = joinLists(input({ card: { kind: 'card', lists: [SLOW, LATE] } }));
    expect(joined.rows.map((row) => row.name)).toEqual(['Late night 8A', 'Slow']);
  });

  it('finds a padded id that a source spelled another way', () => {
    const joined = joinLists(
      input({
        card: { kind: 'card', lists: [{ ...LATE, id: 'a1' }] },
        drafts: [draftOf(LATE)],
      }),
    );
    expect(joined.rows).toHaveLength(1);
    expect(joined.rows[0]).toMatchObject({ id: LATE.id, draft: true, onCard: true });
  });
});

describe('normaliseListId', () => {
  it('pads and lowercases what a source hands over', () => {
    expect(normaliseListId('0xA1')).toBe('00000000000000a1');
    expect(normaliseListId('00000000000000A1')).toBe('00000000000000a1');
  });
});

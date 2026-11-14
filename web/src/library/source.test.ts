import 'fake-indexeddb/auto';
import { describe, expect, it } from 'vitest';
import type { DeviceStatus } from '../device/status';
import type { StoredSnapshot } from '../storage/snapshots';
import { chooseSource, draftScopeFor } from './source';

const stored: StoredSnapshot = {
  scope: 'mark:vcard',
  card: {},
  bytes: new Uint8Array(),
  tracks: 81,
  skipped: 0,
  at: '2026-11-13T12:00:00Z',
};

function status(mark: string | null, card = true): DeviceStatus {
  return {
    card,
    mark,
    tracks: 4120,
    generation: 1,
    library: 'ok',
    order: 'tree',
    playlist: null,
    playing: null,
  };
}

describe('chooseSource', () => {
  it('uses the player only when it reports this card', () => {
    expect(chooseSource('mark:player', stored, status('player'))).toBe('device');
    expect(chooseSource('mark:vcard', stored, status('player'))).toBe('image');
  });

  it('uses the image for a virtual card, whether or not the player is linked', () => {
    expect(chooseSource('mark:vcard', stored, status(null))).toBe('image');
    expect(chooseSource('mark:vcard', stored, null)).toBe('image');
  });

  it('uses the player when the device tile is the selected card', () => {
    expect(chooseSource('mark:vcard', stored, status('player'), true)).toBe('device');
    expect(chooseSource('mark:vcard', stored, status(null, false), true)).toBe('none');
  });

  it('falls back to a linked player, then to nothing', () => {
    expect(chooseSource('mark:other', undefined, status('player'))).toBe('device');
    expect(chooseSource('mark:other', undefined, status(null, false))).toBe('none');
    expect(chooseSource('mark:other', undefined, null)).toBe('none');
  });
});

describe('draftScopeFor', () => {
  it('authors for the card in the computer', () => {
    expect(draftScopeFor('image', 'mark:vcard', status('player'))).toBe('mark:vcard');
  });

  it('authors for the player only when its card is the scope the browser holds', () => {
    expect(draftScopeFor('device', 'mark:player', status('player'))).toBe('mark:player');
    expect(draftScopeFor('device', 'mark:vcard', status('player'))).toBeNull();
    expect(draftScopeFor('device', 'mark:vcard', status(null))).toBeNull();
    expect(draftScopeFor('device', 'mark:vcard', null)).toBeNull();
  });

  it('has nowhere to author when no source answered', () => {
    expect(draftScopeFor('none', 'mark:vcard', status('vcard'))).toBeNull();
  });
});

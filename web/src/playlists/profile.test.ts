import { describe, expect, it } from 'vitest';
import type { LibraryProfile } from '../domain/snapshot';
import type { McpStats } from '../device/mcp';
import type { StoredSnapshot } from '../storage/snapshots';
import { profileFor } from './profile';

const profile: LibraryProfile = {
  tracks: 1,
  genres: [],
  keys: [],
  moods: [],
  duration: null,
  bpm: null,
  energy: null,
  danceability: null,
  valence: null,
  arousal: null,
};

const stored: StoredSnapshot = {
  scope: 'mark:x',
  card: {},
  bytes: new Uint8Array(),
  tracks: 1,
  skipped: 0,
  generation: 7,
  profile,
  at: '2026-01-01T00:00:00Z',
};

function stats(generation: number): McpStats {
  return {
    available: true,
    state: 'ok',
    tracks: 1,
    directories: 1,
    generation: 1,
    snapshot: { state: 'ok', generation },
  };
}

describe('profileFor', () => {
  it('returns the profile only for the image the player is running', () => {
    expect(profileFor(stored, stats(7))).toBe(profile);
    expect(profileFor(stored, stats(8))).toBeNull();
  });

  it('returns nothing when the record has no profile or there is no player', () => {
    expect(profileFor(undefined, stats(7))).toBeNull();
    expect(profileFor({ ...stored, profile: undefined }, stats(7))).toBeNull();
    expect(profileFor(stored, null)).toBeNull();
  });
});

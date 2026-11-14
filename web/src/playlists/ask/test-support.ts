/** Shared fixtures for the assistant's tests: the two ways to open an Intent result, and a profile. */

import type { SearchArgs } from '../../device/mcp';
import type { LibraryProfile } from '../../domain/snapshot';

export function argsOf(result: SearchArgs | { error: string }): SearchArgs {
  if ('error' in result) throw new Error(`expected filters, got: ${result.error}`);
  return result;
}

export function errorOf(result: SearchArgs | { error: string }): string {
  if (!('error' in result)) throw new Error(`expected a failure, got: ${JSON.stringify(result)}`);
  return result.error;
}

export const TEST_PROFILE: LibraryProfile = {
  tracks: 4120,
  genres: ['techno', 'ambient'],
  keys: ['8A', '9B'],
  moods: ['happy', 'relaxed'],
  duration: { min: 45, p25: 180, p50: 300, p75: 420, max: 900 },
  bpm: { min: 85, p25: 98, p50: 120, p75: 131, max: 172 },
  energy: { min: 12, p25: 40, p50: 60, p75: 74, max: 98 },
  danceability: { min: 10, p25: 35, p50: 55, p75: 72, max: 95 },
  valence: { min: 5, p25: 30, p50: 50, p75: 70, max: 99 },
  arousal: { min: 8, p25: 32, p50: 55, p75: 73, max: 97 },
};

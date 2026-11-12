import type { CachedMedia } from '../storage/media';

/**
 * What a card's measured tracks add up to, as pure reads over the media rows.
 *
 * The shelf shows a virtual card's usage against its soft capacity, and warns when the
 * same bytes were added twice. Both facts come from the rows `storage/media.ts` already
 * keeps under the card's scope, so neither needs the card open — only the rows' `size`
 * and `hash`.
 */

/**
 * The measured bytes in a card's media rows: the card's usage against its capacity.
 *
 * A row from before measurement has no `size`, so it contributes nothing. The card reads
 * the bytes that were actually measured, never a guess.
 */
export function usageOf(rows: readonly CachedMedia[]): number {
  return rows.reduce((total, row) => total + (row.size ?? 0), 0);
}

/**
 * How many rows are redundant copies of another row with the same content hash.
 *
 * A row with no `hash` was never measured, so it can never be a duplicate. A hash on one
 * path is the only copy; a hash on N distinct paths leaves N - 1 redundant copies. The
 * paths are distinct because the store is keyed by `[scope, path]`, but the count does not
 * rely on that, so a caller can hand in any rows.
 */
export function duplicatesOf(rows: readonly CachedMedia[]): number {
  const paths = new Map<string, Set<string>>();
  for (const row of rows) {
    if (!row.hash) continue;
    const set = paths.get(row.hash);
    if (set) set.add(row.path);
    else paths.set(row.hash, new Set([row.path]));
  }

  let duplicates = 0;
  for (const set of paths.values()) {
    if (set.size > 1) duplicates += set.size - 1;
  }
  return duplicates;
}

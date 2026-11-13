import type { LibraryProfile } from '../domain/snapshot';
import type { McpStats } from '../device/mcp';
import type { StoredSnapshot } from '../storage/snapshots';

/**
 * The stored profile, only when the player is running the exact image it was built with: the
 * image's generation is in the header and `library_stats` reports the Snapshot's. A stale
 * image, another browser's image, or no image at all leaves the prompt static.
 */
export function profileFor(
  stored: StoredSnapshot | undefined,
  stats: McpStats | null,
): LibraryProfile | null {
  if (!stored?.profile || stored.generation === undefined) return null;
  return stats?.snapshot?.generation === stored.generation ? stored.profile : null;
}

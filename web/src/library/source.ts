import type { LibraryProfile } from '../domain/snapshot';
import { libraryStats, searchTracks, type McpSearch, type SearchArgs } from '../device/mcp';
import type { DeviceStatus } from '../device/status';
import { fetchStatus } from '../device/useDeviceStatus';
import { profileFor } from '../playlists/profile';
import { listMedia } from '../storage/media';
import { loadSnapshot, type StoredSnapshot } from '../storage/snapshots';
import { profileFromRows, readImageRows, searchImage } from './imageSource';

/**
 * Which source answers for the selected card. The player only when it is holding *this*
 * card — a virtual card, or another card in the player, is the Last image. A linked player
 * whose card does not match, with no image, is still the only source there is; the surface
 * says so.
 */
export type SourceKind = 'device' | 'image' | 'none';

export interface LibrarySource {
  kind: 'device' | 'image';
  /** The profile that belongs to the source, or null when there is none. */
  profile: LibraryProfile | null;
  /** What the surface says it is showing (S3). */
  label: string;
  /** Where a playlist built from this answer belongs, or null when it cannot be authored. */
  draftScope: string | null;
  search(args: SearchArgs): Promise<McpSearch>;
}

/** The pure source rule, so the card/player decision is testable without a browser. */
export function chooseSource(
  scope: string,
  stored: StoredSnapshot | undefined,
  status: DeviceStatus | null,
  deviceView = false,
): SourceKind {
  /* The device tile is a view of the player's card, not a scope the stores move to: when it
   * is the selected card, the player answers even though the browser's scope still names the
   * card it was working on. */
  if (deviceView) return status?.card ? 'device' : 'none';
  if (status?.mark && scope === `mark:${status.mark}`) return 'device';
  if (stored) return 'image';
  if (status?.card) return 'device';
  return 'none';
}

/**
 * The card scope a playlist from this source may be stored under. An image is this browser's
 * card: its scope is the answer's. The player is the same card only when its mark is the
 * scope the browser already holds — otherwise the card is not a directory here, and a draft
 * under another scope would name rows that card does not have.
 */
export function draftScopeFor(
  kind: 'device' | 'image' | 'none',
  scope: string,
  status: DeviceStatus | null,
): string | null {
  if (kind === 'image') return scope;
  if (kind === 'device' && status?.mark != null) {
    return scope === `mark:${status.mark}` ? scope : null;
  }
  return null;
}

function imageLabel(stored: StoredSnapshot): string {
  const at = Date.parse(stored.at);
  const day = Number.isFinite(at)
    ? new Date(at).toLocaleDateString('en-GB', { day: 'numeric', month: 'long' })
    : '';
  return `Last image${day ? ` · ${day}` : ''} · ${stored.tracks} tracks`;
}

/**
 * Resolve the selected card's source, its profile, the call that searches it and the scope a
 * playlist it finds belongs to. The image is read once per resolution: `k7.db` is small and the
 * join needs the media index anyway.
 */
export async function resolveSource(
  scope: string,
  deviceView = false,
): Promise<LibrarySource | null> {
  const status = await fetchStatus();
  /* The player's card has its own scope: the profile for the player source is the stored
   * image of the card it reports, not the browser's current scope. */
  const storedScope = deviceView
    ? status?.mark
      ? `mark:${status.mark}`
      : null
    : scope;
  const stored = storedScope ? await loadSnapshot(storedScope) : undefined;
  const kind = chooseSource(scope, stored, status, deviceView);
  if (kind === 'none') return null;

  if (kind === 'device') {
    const stats = await libraryStats().catch(() => null);
    return {
      kind,
      profile: profileFor(stored, stats),
      label: `The player · ${status?.tracks ?? 0} tracks`,
      draftScope: draftScopeFor(kind, scope, status),
      search: (args) => searchTracks(args),
    };
  }

  if (!stored) return null;
  const media = await listMedia(scope);
  const rows = readImageRows(stored.bytes);
  return {
    kind,
    profile: stored.profile ?? profileFromRows(rows),
    label: imageLabel(stored),
    draftScope: draftScopeFor(kind, scope, status),
    search: (args) => Promise.resolve(searchImage(rows, media, args)),
  };
}

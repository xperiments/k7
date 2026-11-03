import type { Analysis, TrackFormat } from '../domain/analysis';
import type { Stage } from '../audio/analyzer';
import type { TrackTags } from '../audio/metadata';

export type ItemStatus =
  | 'queued'
  | 'hashing'
  | 'decoding'
  | 'analysing'
  | 'done'
  | 'tagged'
  | 'error';

export interface QueueItem {
  id: string;
  file: File;
  name: string;
  path: string;
  format: TrackFormat;
  status: ItemStatus;
  stage?: Stage;
  /** What the file's own tags say; read once per Track, before the cache is consulted. */
  tags?: TrackTags;
  analysis?: Analysis;
  cached?: boolean;
  error?: string;
  saving?: boolean;
}

/** The fallbacks that also serve as identity, in the words the Media area uses. */
export const UNKNOWN_ARTIST = 'Unknown artist';
export const UNKNOWN_ALBUM = 'Unknown album';

export interface AlbumGroup {
  artist: string;
  album: string;
  items: QueueItem[];
}

/** An album is a title *by* someone: two artists' "Greatest Hits" are two albums. */
function keyOf(artist: string, album: string): string {
  return `${artist}\u0000${album}`;
}

/**
 * The queue gathered by the album its files name — artist and title, because an album is
 * both — in the order each album first appears, which is the order the card was walked in.
 * Nothing is sorted, so a run never reshuffles itself beyond the row that just learned its
 * tags; a row whose tags have not been read yet has none and sits under the unknown pair
 * until they arrive.
 */
export function groupByAlbum(items: readonly QueueItem[]): AlbumGroup[] {
  const groups = new Map<string, AlbumGroup>();
  for (const item of items) {
    const artist = (item.tags?.artist ?? '').trim() || UNKNOWN_ARTIST;
    const album = (item.tags?.album ?? '').trim() || UNKNOWN_ALBUM;
    const key = keyOf(artist, album);
    const group = groups.get(key);
    if (group) group.items.push(item);
    else groups.set(key, { artist, album, items: [item] });
  }
  return [...groups.values()];
}

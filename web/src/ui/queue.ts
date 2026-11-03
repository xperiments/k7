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

/** What a Track with no album tag is filed under, in the words the Media area uses. */
export const UNKNOWN_ALBUM = 'Unknown album';

export interface AlbumGroup {
  album: string;
  items: QueueItem[];
}

/**
 * The queue gathered by the album its files name — in the order each album first appears,
 * which is the order the card was walked in. Nothing is sorted, so a run never reshuffles
 * itself beyond the row that just learned its album; a row whose tags have not been read yet
 * has none and sits in *Unknown album* until they arrive.
 */
export function groupByAlbum(items: readonly QueueItem[]): AlbumGroup[] {
  const groups = new Map<string, QueueItem[]>();
  for (const item of items) {
    const album = (item.tags?.album ?? '').trim() || UNKNOWN_ALBUM;
    const group = groups.get(album);
    if (group) group.push(item);
    else groups.set(album, [item]);
  }
  return [...groups].map(([album, rows]) => ({ album, items: rows }));
}

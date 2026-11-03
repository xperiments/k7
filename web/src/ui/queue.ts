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

/** A number a ripper put in front of a title: `01 - `, `02. `, `03 – `. */
const TRACK_NUMBER = /^\s*\d{1,3}\s*[-–—.]\s*/;

/**
 * A file name as a reader wants it: the extension goes (the row has a format column), and so
 * does the track number a ripper prefixed. Only a number followed by a separator is a number —
 * `1999 - Something` is a title, and four digits are not three.
 */
export function displayName(name: string): string {
  const withoutExtension = name.replace(/\.(mp3|flac)$/i, '');
  const withoutNumber = withoutExtension.replace(TRACK_NUMBER, '');
  const trimmed = withoutNumber.trim();
  // A name that was *only* its number and extension keeps something the reader can click.
  return trimmed === '' ? name : trimmed;
}

/** The artist a row names, or the unknown one when its tags are blank or unread. */
export function artistOf(item: QueueItem): string {
  return (item.tags?.artist ?? '').trim() || UNKNOWN_ARTIST;
}

export interface AlbumGroup {
  album: string;
  /** The album's one artist, or `null` when it has none yet or several. */
  artist: string | null;
  /** The rows name more than one artist, so each of them says whose track it is. */
  mixed: boolean;
  items: QueueItem[];
}

/**
 * The queue gathered by the album title its files name, in the order each album first appears
 * — the order the card was walked in. Nothing is sorted, so a run never reshuffles itself
 * beyond the row that just learned its tags.
 *
 * The group **is** the title, not the pair: two artists sharing a title are one album on one
 * shelf, and the difference is told row by row (`artistOf`), which is what a compilation looks
 * like. A row whose tags have not been read yet has no artist and does not make the group
 * mixed on its own.
 */
export function groupByAlbum(items: readonly QueueItem[]): AlbumGroup[] {
  const groups = new Map<string, { group: AlbumGroup; artists: Set<string> }>();
  for (const item of items) {
    const album = (item.tags?.album ?? '').trim() || UNKNOWN_ALBUM;
    const entry = groups.get(album);
    if (entry) {
      entry.group.items.push(item);
      if (item.tags?.artist?.trim()) entry.artists.add(item.tags.artist.trim());
      continue;
    }
    const artists = new Set<string>();
    if (item.tags?.artist?.trim()) artists.add(item.tags.artist.trim());
    groups.set(album, { group: { album, artist: null, mixed: false, items: [item] }, artists });
  }

  return [...groups.values()].map(({ group, artists }) => ({
    ...group,
    // One artist is named once in the heading; several make the rows do the naming.
    artist: artists.size === 1 ? [...artists][0] : null,
    mixed: artists.size > 1,
  }));
}

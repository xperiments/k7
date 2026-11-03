import { describe, expect, it } from 'vitest';
import { displayName, groupByAlbum, type QueueItem } from '../src/ui/queue';

/**
 * The queue gathered by album, which is how a run is read: a card is a shelf of albums, not a
 * flat list of files. Nothing is sorted — the order is the card's own walk order — so the
 * header of an album appears where its first Track did.
 */
function item(
  path: string,
  album: string | undefined,
  artist = 'Aphex Twin',
  id = path,
): QueueItem {
  return {
    id,
    file: new File([new Uint8Array([0])], path.split('/').pop() ?? 'x.mp3'),
    name: path.split('/').pop() ?? 'x.mp3',
    path,
    format: 'mp3',
    status: 'done',
    tags: album === undefined ? undefined : { title: '', artist, album, genre: '' },
  };
}

describe('grouping the queue by album', () => {
  it('keeps the walk order and gathers an album’s tracks under its first appearance', () => {
    const groups = groupByAlbum([
      item('A/one.mp3', 'First'),
      item('B/two.mp3', 'Second'),
      item('A/three.mp3', 'First'),
    ]);

    expect(groups.map((group) => group.album)).toEqual(['First', 'Second']);
    expect(groups.map((group) => group.artist)).toEqual(['Aphex Twin', 'Aphex Twin']);
    expect(groups.map((group) => group.mixed)).toEqual([false, false]);
    expect(groups[0].items.map((row) => row.name)).toEqual(['one.mp3', 'three.mp3']);
    expect(groups[1].items.map((row) => row.name)).toEqual(['two.mp3']);
  });

  it('files a track with no tags under the unknown album, with no artist to name', () => {
    const groups = groupByAlbum([item('loose.mp3', undefined), item('loose2.mp3', '  ', '  ')]);
    expect(groups).toHaveLength(1);
    // Nothing known to put in the heading; the row itself would say "Unknown artist" if the
    // album turned out to mix artists.
    expect(groups[0]).toMatchObject({ album: 'Unknown album', artist: null, mixed: false });
    expect(groups[0].items).toHaveLength(2);
  });

  it('makes one album of a title several artists share, and says so', () => {
    const groups = groupByAlbum([
      item('a/1.mp3', 'Greatest Hits', 'Blur'),
      item('b/1.mp3', 'Greatest Hits', 'Queen'),
      item('a/2.mp3', 'Greatest Hits', 'Blur'),
    ]);
    expect(groups).toHaveLength(1);
    // No single artist to put in the heading, so every row names its own.
    expect(groups[0]).toMatchObject({ album: 'Greatest Hits', artist: null, mixed: true });
    expect(groups[0].items.map((row) => row.name)).toEqual(['1.mp3', '1.mp3', '2.mp3']);
  });

  it('is empty for an empty queue', () => {
    expect(groupByAlbum([])).toEqual([]);
  });
});

describe('a file name as a row shows it', () => {
  it('drops the extension, which the format column carries', () => {
    expect(displayName('Xtal.mp3')).toBe('Xtal');
    expect(displayName('Xtal.FLAC')).toBe('Xtal');
    expect(displayName('Xtal.flac')).toBe('Xtal');
  });

  it('drops a track number when a ripper prefixed one', () => {
    expect(displayName('01 - Xtal.mp3')).toBe('Xtal');
    expect(displayName('02. Xtal.mp3')).toBe('Xtal');
    expect(displayName('3 – Xtal.mp3')).toBe('Xtal');
    expect(displayName('07-Tha.flac')).toBe('Tha');
  });

  it('keeps a title that starts with a number but is not numbered', () => {
    expect(displayName('1999 - Something.mp3')).toBe('1999 - Something');
    expect(displayName('7 Rings.mp3')).toBe('7 Rings');
  });
});

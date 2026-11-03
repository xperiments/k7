import { describe, expect, it } from 'vitest';
import { groupByAlbum, type QueueItem } from '../src/ui/queue';

/**
 * The queue gathered by album, which is how a run is read: a card is a shelf of albums, not a
 * flat list of files. Nothing is sorted — the order is the card's own walk order — so the
 * header of an album appears where its first Track did.
 */
function item(path: string, album: string | undefined, id = path): QueueItem {
  return {
    id,
    file: new File([new Uint8Array([0])], path.split('/').pop() ?? 'x.mp3'),
    name: path.split('/').pop() ?? 'x.mp3',
    path,
    format: 'mp3',
    status: 'done',
    tags: album === undefined ? undefined : { title: '', artist: '', album, genre: '' },
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
    expect(groups[0].items.map((row) => row.name)).toEqual(['one.mp3', 'three.mp3']);
    expect(groups[1].items.map((row) => row.name)).toEqual(['two.mp3']);
  });

  it('files a track with no album tag under a name the reader recognises', () => {
    const groups = groupByAlbum([item('loose.mp3', undefined), item('loose2.mp3', '  ')]);
    expect(groups).toHaveLength(1);
    expect(groups[0].album).toBe('Unknown album');
    expect(groups[0].items).toHaveLength(2);
  });

  it('is empty for an empty queue', () => {
    expect(groupByAlbum([])).toEqual([]);
  });
});

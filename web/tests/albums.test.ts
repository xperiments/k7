import 'fake-indexeddb/auto';
import { describe, expect, it } from 'vitest';
import {
  albumsOf,
  buildMediaIndex,
  findAlbum,
  UNKNOWN_ALBUM,
  UNKNOWN_ARTIST,
  type Presence,
} from '../src/media/albums';
import type { CachedMedia } from '../src/storage/media';

/**
 * What makes two cache rows one album. The title alone does: a compilation names a different
 * artist on every track and is still one shelf, which is the rule Process already groups its
 * queue by. These tests read as that sentence.
 */

function row(path: string, artist: string, album: string, genre = 'IDM'): CachedMedia {
  return {
    path,
    title: path.split('/').pop() ?? path,
    artist,
    album,
    genre,
    at: new Date().toISOString(),
  };
}

function indexOf(rows: CachedMedia[], presence: Presence | null = null) {
  return buildMediaIndex(
    new Map(rows.map((entry) => [entry.path, entry] as const)),
    presence,
  );
}

const listing = (paths: string[]): Presence => ({
  paths: new Set(paths),
  tracks: paths.map((path) => ({ name: path.split('/').pop() ?? path, path })),
  complete: true,
});

describe('the album index', () => {
  it('lists one album per title, not one per artist', () => {
    const index = indexOf([
      row('comp/01.mp3', 'Aphex Twin', 'Ambient Works'),
      row('comp/02.mp3', 'Brian Eno', 'Ambient Works'),
      row('comp/03.mp3', 'Aphex Twin', 'Ambient Works'),
    ]);

    expect(index.albums).toHaveLength(1);
    expect(index.albums[0]).toMatchObject({
      album: 'Ambient Works',
      artists: ['Aphex Twin', 'Brian Eno'],
      tracks: 3,
    });
  });

  it('keeps two titles apart even under one artist', () => {
    const index = indexOf([
      row('a/01.mp3', 'Aphex Twin', 'Selected Ambient Works'),
      row('b/01.mp3', 'Aphex Twin', 'Richard D. James'),
    ]);

    expect(index.albums.map((album) => album.album)).toEqual([
      'Richard D. James',
      'Selected Ambient Works',
    ]);
    expect(index.albums.every((album) => album.artists.length === 1)).toBe(true);
  });

  it('groups blank tags under the unknown names instead of dropping the rows', () => {
    const index = indexOf([row('x/01.mp3', '', '')]);

    expect(index.albums).toHaveLength(1);
    expect(index.albums[0]).toMatchObject({ album: UNKNOWN_ALBUM, artists: [UNKNOWN_ARTIST] });
  });

  it('opens an album by its title', () => {
    const index = indexOf([row('comp/01.mp3', 'Aphex Twin', 'Ambient Works')]);

    expect(findAlbum(index, 'Ambient Works')?.tracks).toBe(1);
    expect(findAlbum(index, 'ambient works')).toBeNull();
  });

  it('gives an artist every shelf their name is on, compilations included', () => {
    const index = indexOf([
      row('comp/02.mp3', 'Brian Eno', 'Ambient Works'),
      row('solo/01.mp3', 'Brian Eno', 'Another Green World'),
    ]);

    expect(albumsOf(index, 'Brian Eno').map((album) => album.album)).toEqual([
      'Ambient Works',
      'Another Green World',
    ]);
    expect(albumsOf(index, 'Aphex Twin')).toEqual([]);
  });

  it('leaves a gone track out when the listing covered the whole library', () => {
    const index = indexOf(
      [row('a/01.mp3', 'Aphex Twin', 'Ambient Works'), row('a/02.mp3', 'Aphex Twin', 'Gone')],
      listing(['a/01.mp3']),
    );

    expect(index.albums.map((album) => album.album)).toEqual(['Ambient Works']);
  });

  it('keeps the cache whole when the listing was partial', () => {
    const index = indexOf(
      [row('a/01.mp3', 'Aphex Twin', 'Ambient Works'), row('a/02.mp3', 'Aphex Twin', 'Gone')],
      { ...listing(['a/01.mp3']), complete: false },
    );

    expect(index.albums).toHaveLength(2);
  });
});

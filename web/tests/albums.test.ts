import 'fake-indexeddb/auto';
import { describe, expect, it } from 'vitest';
import {
  albumsOf,
  buildMediaIndex,
  findAlbum,
  presenceOf,
  type Presence,
  UNKNOWN_ALBUM,
  UNKNOWN_ARTIST,
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

/** The device's answer to one walk: every path is a track it listed. */
function deviceLists(paths: string[]) {
  return presenceOf(
    {
      path: '',
      entries: paths.map((path) => ({
        name: path.split('/').pop() ?? path,
        path,
        type: 'track' as const,
      })),
    },
    paths.length,
  );
}

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
      deviceLists(['a/01.mp3']),
    );

    expect(index.albums.map((album) => album.album)).toEqual(['Ambient Works']);
  });

  it('joins the cache and the listing without regard to case', () => {
    const index = indexOf(
      [row('Artist/01.mp3', 'Aphex Twin', 'Ambient Works')],
      // The device's spelling differs, as a FAT32 card may hand it back; one file either way.
      deviceLists(['artist/01.MP3']),
    );

    expect(index.albums).toHaveLength(1);
    expect(index.unmeasured).toEqual([]);
  });

  it('keeps a format the App cannot open out of "not measured"', () => {
    const index = indexOf(
      [row('a/01.mp3', 'Aphex Twin', 'Ambient Works')],
      deviceLists(['a/01.mp3', 'a/02.wav']),
    );

    expect(index.unmeasured).toEqual([]);
    expect(index.unmeasurable.map((track) => track.path)).toEqual(['a/02.wav']);
    expect(index.presence?.complete).toBe(true);
  });

  it('shows only the albums the device still lists, even from a partial listing', () => {
    const index = indexOf(
      [row('a/01.mp3', 'Aphex Twin', 'Ambient Works'), row('a/02.mp3', 'Aphex Twin', 'Gone')],
      { ...deviceLists(['a/01.mp3']), complete: false },
    );

    expect(index.albums.map((album) => album.album)).toEqual(['Ambient Works']);
  });
});

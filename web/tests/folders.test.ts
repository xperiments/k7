import { describe, expect, it } from 'vitest';
import type { DeviceTrack } from '../src/media/albums';
import {
  buildFolderTree,
  filterFolderTree,
  folderCounts,
  folderTrackPaths,
  locateFolder,
  MUSIC_ROOT,
} from '../src/media/folders';
import type { CachedCover, CachedMedia } from '../src/storage/media';

/**
 * The Drive view's tree. A cache row's path is the folder it is in, so these tests are mostly
 * the card's own shape read back: folders of folders, files under them, and the counts and
 * faces a folder row needs to describe itself.
 */

const cover: CachedCover = { bytes: new Uint8Array([1, 2, 3]), format: 'image/jpeg' };

function row(
  path: string,
  tags: { title?: string; artist?: string; album?: string; cover?: CachedCover } = {},
): CachedMedia {
  return {
    path,
    title: tags.title ?? '',
    artist: tags.artist ?? '',
    album: tags.album ?? '',
    genre: '',
    ...(tags.cover !== undefined ? { cover: tags.cover } : {}),
    at: new Date().toISOString(),
  };
}

function listed(path: string): DeviceTrack {
  return { name: path.split('/').pop() ?? path, path };
}

describe('the folder tree', () => {
  it('turns paths into folders of folders, with files where they live', () => {
    const tree = buildFolderTree([
      row('Artist/Album/02.mp3'),
      row('Artist/Album/01.mp3'),
      row('Other/Only/03.mp3'),
    ]);

    expect(tree.name).toBe(MUSIC_ROOT);
    expect(tree.path).toBe('');
    expect(tree.folders.map((folder) => folder.name)).toEqual(['Artist', 'Other']);
    expect(tree.folders[0].folders.map((folder) => folder.name)).toEqual(['Album']);
    expect(tree.folders[0].folders[0].tracks.map((track) => track.name)).toEqual([
      '01.mp3',
      '02.mp3',
    ]);
  });

  it('counts everything below a folder, not only its own files', () => {
    const tree = buildFolderTree([
      row('Artist/00.mp3'),
      row('Artist/Album/01.mp3'),
      row('Artist/Album/Bonus/02.mp3'),
      row('Other/03.mp3'),
    ]);

    expect(tree.tracksBelow).toBe(4);
    expect(tree.folders[0].tracksBelow).toBe(3);
    expect(tree.folders[0].folders[0].tracksBelow).toBe(2);
    expect(tree.folders[0].tracks).toHaveLength(1);
  });

  it('keeps the cache spelling when the device names a folder differently by case', () => {
    const tree = buildFolderTree(
      [row('Artist/Album/01.mp3')],
      [listed('artist/album/02.mp3')],
    );

    expect(tree.folders.map((folder) => folder.name)).toEqual(['Artist']);
    expect(tree.folders[0].folders.map((folder) => folder.name)).toEqual(['Album']);
    expect(tree.folders[0].folders[0].tracks).toHaveLength(2);
  });

  it('adds a file the browser never measured, without inventing its tags', () => {
    const tree = buildFolderTree(
      [row('Artist/Album/01.mp3', { title: 'Measured' })],
      [listed('Artist/Album/02 - Unmeasured.mp3')],
    );
    const folder = tree.folders[0].folders[0];

    expect(folder.tracks).toHaveLength(2);
    expect(folder.tracks[1]).toMatchObject({
      name: '02 - Unmeasured.mp3',
      kind: 'unmeasured',
      title: '02 - Unmeasured',
      row: null,
    });
  });

  it('keeps a file the App never opens apart from one it merely has not read', () => {
    const tree = buildFolderTree(
      [],
      [listed('Card/03.mp3')],
      [listed('Card/02.wav')],
    );
    const tracks = tree.folders[0].tracks;

    expect(tracks.map((track) => track.kind)).toEqual(['unmeasurable', 'unmeasured']);
  });

  it('counts one file once when both sources name it', () => {
    const tree = buildFolderTree(
      [row('Artist/01.mp3', { title: 'Tagged' })],
      [listed('artist/01.MP3')],
    );

    expect(tree.folders[0].tracks).toHaveLength(1);
    expect(tree.folders[0].tracks[0]).toMatchObject({ kind: 'measured', title: 'Tagged' });
  });

  it('gives a folder the first cover it or its children carry', () => {
    const tree = buildFolderTree([
      row('Artist/Album/01.mp3'),
      row('Artist/Album/02.mp3', { cover }),
    ]);
    const album = tree.folders[0].folders[0];

    expect(album.cover).toBe(cover);
    expect(tree.folders[0].cover).toBe(cover);
    expect(tree.cover).toBe(cover);
  });

  it('falls back to the file name for a title, without its extension', () => {
    const tree = buildFolderTree([row('Artist/Album/01 - Untagged.flac')]);

    expect(tree.folders[0].folders[0].tracks[0].title).toBe('01 - Untagged');
  });
});

describe('finding a folder', () => {
  const tree = buildFolderTree([
    row('Artist/Album/01.mp3'),
    row('Other/02.mp3'),
  ]);

  it('opens a folder by its path, whatever case the address used', () => {
    const found = locateFolder(tree, 'artist/ALBUM');

    expect(found?.folder.name).toBe('Album');
    expect(found?.trail.map((folder) => folder.name)).toEqual([MUSIC_ROOT, 'Artist', 'Album']);
  });

  it('treats the empty path as the root', () => {
    expect(locateFolder(tree, '')?.folder).toBe(tree);
  });

  it('has no answer for a folder the tree does not hold', () => {
    expect(locateFolder(tree, 'Artist/Not There')).toBeNull();
    expect(locateFolder(tree, 'Nobody')).toBeNull();
  });
});

describe('searching the drive', () => {
  const tree = buildFolderTree([
    row('Artist/Album/01.mp3', { title: 'Deep Cut', artist: 'Brian Eno' }),
    row('Artist/02.mp3', { title: 'Here', artist: 'Brian Eno', album: 'Another Green World' }),
    row('Other/03.mp3', { title: 'Elsewhere' }),
  ]);

  it('searches every level, keeping the folders that lead to a match', () => {
    const found = filterFolderTree(tree, 'deep');

    expect(found?.folders.map((folder) => folder.name)).toEqual(['Artist']);
    expect(found?.folders[0].folders.map((folder) => folder.name)).toEqual(['Album']);
    expect(found?.folders[0].folders[0].tracks.map((track) => track.title)).toEqual([
      'Deep Cut',
    ]);
    // The sibling file and the whole Other branch are not part of the answer.
    expect(found?.folders[0].tracks).toEqual([]);
  });

  it('keeps a folder’s whole subtree when its own name matches', () => {
    const found = filterFolderTree(tree, 'album');

    expect(found?.folders[0].folders[0].tracks.map((track) => track.title)).toEqual([
      'Deep Cut',
    ]);
    expect(found?.folders[0].tracks).toEqual([]);
  });

  it('matches a file by title, name, artist or album', () => {
    expect(filterFolderTree(tree, 'green')?.folders[0].tracks.map((track) => track.title)).toEqual([
      'Here',
    ]);
    expect(filterFolderTree(tree, 'eno')?.folders[0].tracks.map((track) => track.title)).toEqual([
      'Here',
    ]);
    expect(filterFolderTree(tree, '02.MP3')?.folders[0].tracks.map((track) => track.title)).toEqual([
      'Here',
    ]);
  });

  it('has no answer when nothing matches', () => {
    expect(filterFolderTree(tree, 'nobody')).toBeNull();
  });

  it('returns the whole tree for an empty query', () => {
    expect(filterFolderTree(tree, '  ')).toBe(tree);
  });
});

describe('counting the tree', () => {
  it('counts every folder and file below the root, not the root itself', () => {
    const tree = buildFolderTree([
      row('Artist/Album/01.mp3'),
      row('Artist/Album/Bonus/02.mp3'),
      row('Other/03.mp3'),
    ]);

    expect(folderCounts(tree)).toEqual({ folders: 4, tracks: 3 });
  });
});

describe('dragging a folder', () => {
  it('lists every file below it in the order the tree draws them', () => {
    const tree = buildFolderTree([
      row('Artist/Album/01.mp3'),
      row('Artist/Album/Bonus/02.mp3'),
      row('Artist/03.mp3'),
    ]);

    // The tree draws Bonus before Album's own file, and Album before Artist's own file.
    expect(folderTrackPaths(tree.folders[0])).toEqual([
      'Artist/Album/Bonus/02.mp3',
      'Artist/Album/01.mp3',
      'Artist/03.mp3',
    ]);
  });

  it('has nothing to add for a folder with no files at any level', () => {
    const tree = buildFolderTree([row('Artist/Album/01.mp3')]);
    const empty = { ...tree.folders[0], folders: [], tracks: [], tracksBelow: 0 };

    expect(folderTrackPaths(empty)).toEqual([]);
  });
});

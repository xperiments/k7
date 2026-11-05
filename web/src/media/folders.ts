/**
 * The folder tree: the shape Media's Drive view browses.
 *
 * The album index answers "what albums are here", and an album is a title its cache rows
 * share. Drive asks a different question — "what folders does the card actually hold" — and
 * the cache's paths already are that answer, read as a tree instead of grouped by name. Two
 * sources feed it, exactly as they feed the album index:
 *
 * 1. The rows this browser measured (`k7-media`), which carry the titles, artists and covers
 *    that make a folder browsable without opening a file again.
 * 2. The listing the device walked, which adds the files this browser has not measured and
 *    the ones it never opens (a `.wav`, say). That listing is capped at 200 entries, so it
 *    fills a folder with what it saw; it does not claim to be the whole card.
 *
 * The rules the album index states still hold here: a path is compared folded (FAT32 does not
 * keep case), and a track the device listed without a cache row is shown as **not measured**
 * rather than invented. Rows the card no longer holds were already dropped by
 * `buildMediaIndex` when its listing was complete, so this module never has to decide that
 * again.
 *
 * What this module does not do is fetch. It is pure, so the tree's shape is testable without
 * a card, a device, or a browser.
 */

import type { CachedCover, CachedMedia } from '../storage/media';
import type { DeviceTrack } from './albums';
import { fileTitle, foldPath } from './paths';

/** What the root of the tree is called: the card's `music` directory, below which paths live. */
export const MUSIC_ROOT = 'Music';

/** How a file reached this tree, which is also what a Drive row can say about it. */
export type FolderTrackKind = 'measured' | 'unmeasured' | 'unmeasurable';

/** One file in a folder. A measured file has a row; a listed one may not. */
export interface FolderTrack {
  /** The file's name: the last segment of its path. */
  name: string;
  /** The path below the card's `music` directory, as its source spelled it. */
  path: string;
  /** The cache row this browser wrote, or null when only the device listed the file. */
  row: CachedMedia | null;
  kind: FolderTrackKind;
  /** The row's tag, or the file name without its extension. */
  title: string;
}

/** One directory, with what it holds directly and how much lies below it. */
export interface Folder {
  /** The path below `music`; `''` for the root. */
  path: string;
  /** The last segment, or `Music` at the root. */
  name: string;
  /** Directories inside, by name. */
  folders: Folder[];
  /** Files inside, by name. */
  tracks: FolderTrack[];
  /** Files at or below this folder: what a row's "12 tracks" counts. */
  tracksBelow: number;
  /** The first picture here or below, for the folder's face. */
  cover: CachedCover | null;
}

/** A folder and how the route reached it, for a breadcrumb that can link each level. */
export interface FolderLocation {
  folder: Folder;
  /** The root through the folder, in order: the last entry is the folder itself. */
  trail: Folder[];
}

function blank(path: string, name: string): Folder {
  return { path, name, folders: [], tracks: [], tracksBelow: 0, cover: null };
}

function byText(left: string, right: string): number {
  return left.localeCompare(right, undefined, { sensitivity: 'base' });
}

/** Every segment of a path, with the empty ones a stray slash makes dropped. */
function segmentsOf(path: string): string[] {
  return path.split('/').filter((segment) => segment.length > 0);
}

/**
 * Build the tree from the rows this browser measured and the files the device listed. The
 * cache goes in first, so a file both sides name keeps the browser's spelling and its tags;
 * a path already seen is not added twice, whichever source repeats it.
 */
export function buildFolderTree(
  rows: readonly CachedMedia[],
  unmeasured: readonly DeviceTrack[] = [],
  unmeasurable: readonly DeviceTrack[] = [],
): Folder {
  const root = blank('', MUSIC_ROOT);
  /* Keyed folded: two spellings of one directory are one directory, as on the card. */
  const nodes = new Map<string, Folder>([[foldPath(''), root]]);
  const seenTracks = new Set<string>();

  const folderAt = (path: string[]): Folder => {
    let node = root;
    let prefix = '';
    for (const segment of path) {
      prefix = prefix.length === 0 ? segment : `${prefix}/${segment}`;
      const key = foldPath(prefix);
      let child = nodes.get(key);
      if (child === undefined) {
        child = blank(prefix, segment);
        nodes.set(key, child);
        node.folders.push(child);
      }
      node = child;
    }
    return node;
  };

  const add = (path: string, row: CachedMedia | null, kind: FolderTrackKind): void => {
    const folded = foldPath(path);
    if (seenTracks.has(folded)) return;
    const segments = segmentsOf(path);
    const name = segments.pop();
    if (name === undefined) return;
    seenTracks.add(folded);

    folderAt(segments).tracks.push({
      name,
      path,
      row,
      kind,
      title: (row?.title ?? '').trim() || fileTitle(path),
    });
  };

  for (const row of rows) add(row.path, row, 'measured');
  for (const track of unmeasured) add(track.path, null, 'unmeasured');
  for (const track of unmeasurable) add(track.path, null, 'unmeasurable');

  settle(root);
  return root;
}

/** Sort a folder and everything below it, and roll up the counts and the first cover. */
function settle(node: Folder): void {
  node.folders.sort((left, right) => byText(left.name, right.name));
  node.tracks.sort((left, right) => byText(left.name, right.name));

  let tracks = node.tracks.length;
  let cover = node.tracks.find((track) => track.row?.cover)?.row?.cover ?? null;
  for (const child of node.folders) {
    settle(child);
    tracks += child.tracksBelow;
    if (cover === null) cover = child.cover;
  }
  node.tracksBelow = tracks;
  node.cover = cover;
}

/**
 * The folder a route names, and the trail that links back up it. Comparison is per segment
 * and folded, so an address typed with different case than the card still lands; an address
 * naming a folder the tree does not have is `null`, never the nearest match.
 */
export function locateFolder(tree: Folder, path: string): FolderLocation | null {
  const trail: Folder[] = [tree];
  let node = tree;
  for (const segment of segmentsOf(path)) {
    const wanted = foldPath(segment);
    const next = node.folders.find((folder) => foldPath(folder.name) === wanted);
    if (next === undefined) return null;
    trail.push(next);
    node = next;
  }
  return { folder: node, trail };
}

/**
 * Whether a file answers a search: its title, its file name, its artist or its album.
 * The album often carries the words the title does not, and the file name is what a person
 * who organised the card by hand will remember.
 */
function matchesTrack(track: FolderTrack, needle: string): boolean {
  return [track.title, track.name, track.row?.artist ?? '', track.row?.album ?? ''].some(
    (field) => field.toLowerCase().includes(needle),
  );
}

/**
 * The tree pruned to what a search matches, with every folder that leads to a match kept.
 * A folder whose own name matches keeps its whole subtree — searching for an album should
 * show its files, not only its own row — while a file match keeps the branch that holds it
 * and drops its siblings.
 *
 * `null` means nothing matched. An empty query returns the tree itself, so a blank search
 * costs no copying.
 */
export function filterFolderTree(tree: Folder, query: string): Folder | null {
  const needle = query.trim().toLowerCase();
  if (needle.length === 0) return tree;
  return prune(tree, needle);
}

function prune(folder: Folder, needle: string): Folder | null {
  /* A matched folder is shown whole: its contents are what the person was looking for. */
  if (folder.path !== '' && folder.name.toLowerCase().includes(needle)) return folder;

  const folders = folder.folders
    .map((child) => prune(child, needle))
    .filter((child): child is Folder => child !== null);
  const tracks = folder.tracks.filter((track) => matchesTrack(track, needle));
  if (folders.length === 0 && tracks.length === 0) return null;

  return {
    ...folder,
    folders,
    tracks,
    tracksBelow: tracks.length + folders.reduce((total, child) => total + child.tracksBelow, 0),
  };
}

/** How many folders and files the tree holds, at every level, never counting the root. */
export function folderCounts(tree: Folder): { folders: number; tracks: number } {
  let folders = 0;
  let tracks = tree.tracks.length;
  for (const child of tree.folders) {
    folders += 1;
    const below = folderCounts(child);
    folders += below.folders;
    tracks += below.tracks;
  }
  return { folders, tracks };
}

/**
 * Every file at or below a folder, in the order the tree draws them: a subfolder's contents
 * first, then the files directly in the folder. This is what dragging a folder into a
 * playlist adds, so the list reads the same way the tree did.
 */
export function folderTrackPaths(folder: Folder): string[] {
  const paths: string[] = [];
  for (const child of folder.folders) paths.push(...folderTrackPaths(child));
  for (const track of folder.tracks) paths.push(track.path);
  return paths;
}

import type { CachedCover, CachedMedia } from '../storage/media';
import type { Folder, FolderTrack } from '../media/folders';

/**
 * File management on the card in the computer: delete, rename and move entries below
 * `music/`. These are the operations the tree's manager offers, and they are the card's own
 * directory — the File System Access API — never the player's listing.
 *
 * The platform has no rename and no move: both are a copy followed by a delete. A copy is
 * streamed, like the compose path, so a large file is never held whole; a folder is copied
 * recursively. The media store is not touched here — the caller keeps that in step, because
 * a path's row is the browser's record, not the card's.
 */

export interface CardFile {
  kind: 'file';
  name: string;
  getFile(): Promise<File>;
  createWritable(): Promise<FileSystemWritableFileStream>;
}

export interface CardDir {
  kind: 'directory';
  name: string;
  getDirectoryHandle(name: string, options?: { create?: boolean }): Promise<CardDir>;
  getFileHandle(name: string, options?: { create?: boolean }): Promise<CardFile>;
  removeEntry(name: string, options?: { recursive?: boolean }): Promise<void>;
  values(): AsyncIterable<CardDir | CardFile>;
}

const segmentsOf = (path: string): string[] => path.split('/').filter((s) => s.length > 0);

/** The card's `music/`, matched folded: a card is FAT32, where `MUSIC` is `music`. */
async function musicOf(card: CardDir): Promise<CardDir> {
  try {
    return await card.getDirectoryHandle('music', { create: true });
  } catch {
    /* The platform's lookup is case-sensitive: fall back to the directory's own names. */
  }
  for await (const child of card.values()) {
    if (child.kind === 'directory' && child.name.toLowerCase() === 'music') return child;
  }
  return await card.getDirectoryHandle('music', { create: true });
}

/** The folder a path's parent is, below `music/`. */
async function parentOf(card: CardDir, path: string): Promise<CardDir> {
  let directory = await musicOf(card);
  for (const segment of segmentsOf(path).slice(0, -1)) {
    directory = await directory.getDirectoryHandle(segment);
  }
  return directory;
}

/** One entry, file or folder, at a path below `music/`. */
async function entryAt(card: CardDir, path: string): Promise<CardDir | CardFile> {
  const parts = segmentsOf(path);
  const parent = await parentOf(card, path);
  const name = parts[parts.length - 1];
  try {
    return await parent.getFileHandle(name);
  } catch {
    return await parent.getDirectoryHandle(name);
  }
}

/** Delete one file or folder (and everything under it) below `music/`. */
export async function removeFromCard(card: CardDir, path: string): Promise<void> {
  const parts = segmentsOf(path);
  if (parts.length === 0) return;
  const parent = await parentOf(card, path);
  await parent.removeEntry(parts[parts.length - 1], { recursive: true });
}

async function copyEntry(source: CardDir | CardFile, target: CardDir, name: string): Promise<void> {
  if (source.kind === 'file') {
    const file = await source.getFile();
    const handle = await target.getFileHandle(name, { create: true });
    const writable = await handle.createWritable();
    const reader = file.stream().getReader();
    try {
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        await writable.write(value);
      }
    } finally {
      await writable.close();
    }
    return;
  }
  const directory = await target.getDirectoryHandle(name, { create: true });
  for await (const child of source.values()) {
    await copyEntry(child, directory, child.name);
  }
}

/**
 * Rename or move an entry: copy it to its new folder under its new name, then delete the
 * original. Returns the new path below `music/`, or null when that is where it already was.
 */
export async function relocateInCard(
  card: CardDir,
  path: string,
  toFolder: string,
  newName?: string,
): Promise<string | null> {
  const parts = segmentsOf(path);
  const oldName = parts[parts.length - 1];
  const name = newName ?? oldName;
  const fromFolder = parts.slice(0, -1).join('/');
  if (fromFolder === toFolder && name === oldName) return null;

  const source = await entryAt(card, path);
  let target = await musicOf(card);
  for (const segment of segmentsOf(toFolder)) {
    target = await target.getDirectoryHandle(segment, { create: true });
  }
  await copyEntry(source, target, name);
  const parent = await parentOf(card, path);
  await parent.removeEntry(oldName, { recursive: true });
  return [toFolder, name].filter((part) => part.length > 0).join('/');
}

/** Rename a file or folder in place. */
export function renameInCard(card: CardDir, path: string, name: string): Promise<string | null> {
  const folder = segmentsOf(path).slice(0, -1).join('/');
  return relocateInCard(card, path, folder, name);
}

/** Move a file or folder into another folder below `music/` (`''` is the root). */
export function moveInCard(card: CardDir, path: string, toFolder: string): Promise<string | null> {
  return relocateInCard(card, path, toFolder);
}

const byName = (left: { name: string }, right: { name: string }): number =>
  left.name.localeCompare(right.name, undefined, { sensitivity: 'base' });

/**
 * The card's own `music/` as a `Folder` tree, walked from the disk, with the browser's rows
 * joined by path for titles and covers. Unlike `buildFolderTree`, this shows folders the
 * card actually has even when they are empty — one the manager just made — and files the
 * browser has not measured, which the manager can still delete, rename or move.
 */
export async function cardFolderTree(card: CardDir, rows: readonly CachedMedia[]): Promise<Folder> {
  const byPath = new Map(rows.map((row) => [row.path, row]));
  const music = await musicOf(card);

  const walk = async (directory: CardDir, path: string, name: string): Promise<Folder> => {
    const folders: Folder[] = [];
    const tracks: FolderTrack[] = [];
    for await (const child of directory.values()) {
      if (child.name.startsWith('.')) continue;
      const childPath = path === '' ? child.name : `${path}/${child.name}`;
      if (child.kind === 'directory') {
        folders.push(await walk(child, childPath, child.name));
      } else {
        const row = byPath.get(childPath) ?? null;
        tracks.push({
          name: child.name,
          path: childPath,
          row,
          kind: row !== null ? 'measured' : 'unmeasured',
          title: row !== null && row.title !== '' ? row.title : child.name.replace(/\.[^.]+$/, ''),
        });
      }
    }
    folders.sort(byName);
    tracks.sort(byName);
    const tracksBelow = tracks.length + folders.reduce((sum, folder) => sum + folder.tracksBelow, 0);
    const cover: CachedCover | null =
      tracks.find((track) => track.row?.cover !== undefined)?.row?.cover ??
      folders.find((folder) => folder.cover !== null)?.cover ??
      null;
    return { path, name, folders, tracks, tracksBelow, cover };
  };

  return walk(music, '', 'Music');
}

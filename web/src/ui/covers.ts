import { zipStore, type ZipEntry } from '../lib/zip';

/**
 * The album art already inside the files, pulled out into one archive.
 *
 * Nothing here writes to the card: a page on plain HTTP has no directory picker, so "to
 * local" means a download, and a folder of covers is more useful than 164 separate saves.
 * The folder layout is the card's own — `Artist/Album/cover.jpg` — so the archive can be
 * dropped back onto the card, or opened as a gallery, without rearranging anything.
 *
 * One image per album, the first one found: an album's tracks almost always carry the same
 * picture, and repeating it per track would be a 30 MB archive of duplicates.
 */

export interface CoverProgress {
  done: number;
  total: number;
  name: string;
}

export interface CoverResult {
  archive: Uint8Array<ArrayBuffer>;
  covers: number;
  albums: number;
  missing: string[];
}

/** `music/Artist/Album/01.flac` → `Artist/Album`; a file with no folder is its own album. */
function albumOf(path: string): string {
  const parts = path.split('/').filter(Boolean);
  const withoutMusic = parts[0] === 'music' ? parts.slice(1) : parts;
  const folders = withoutMusic.slice(0, -1);
  return folders.length ? folders.join('/') : withoutMusic[0] ?? 'album';
}

function extensionOf(format: string): string {
  const mime = format.toLowerCase();
  if (mime.includes('png')) return 'png';
  if (mime.includes('webp')) return 'webp';
  if (mime.includes('gif')) return 'gif';
  return 'jpg';
}

export async function extractCovers(
  files: readonly { file: File; path: string }[],
  onProgress?: (progress: CoverProgress) => void,
): Promise<CoverResult> {
  const { parseBlob } = await import('music-metadata');
  const entries = new Map<string, ZipEntry>();
  const missing: string[] = [];

  for (const [index, item] of files.entries()) {
    onProgress?.({ done: index, total: files.length, name: item.path });
    let picture: { data: Uint8Array; format: string } | undefined;
    try {
      const parsed = await parseBlob(item.file, { duration: false, skipCovers: false });
      const found = parsed.common.picture?.[0];
      if (found) {
        picture = { data: new Uint8Array(found.data), format: found.format };
      }
    } catch {
      /* A file whose tags cannot be read is one cover short, not a failed run. */
    }
    if (!picture) {
      missing.push(item.path);
      continue;
    }

    const album = albumOf(item.path);
    if (entries.has(album)) continue;
    entries.set(album, {
      path: `${album}/cover.${extensionOf(picture.format)}`,
      bytes: picture.data,
    });
  }

  onProgress?.({ done: files.length, total: files.length, name: '' });
  const entriesOut = [...entries.values()];
  return {
    archive: zipStore(entriesOut) as Uint8Array<ArrayBuffer>,
    covers: entriesOut.length,
    albums: entriesOut.length,
    missing,
  };
}

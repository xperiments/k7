/**
 * The small rules about the path strings a card hands back, kept apart from the album index
 * so the folder tree can use them without pulling in the store or the hooks. Nothing here
 * reads, writes, or knows what a card is.
 */

/**
 * A path folded the way the card's filesystem is: case is not information on FAT32, and the
 * device folds it too (`library_find`). Two sides that name one file differently by case are
 * naming one file, so a joined path is compared folded and kept as it arrived.
 */
export function foldPath(path: string): string {
  return path.toLowerCase();
}

/** The last segment of a path: the file's own name, extension included. */
export function fileName(path: string): string {
  return path.split('/').pop() ?? path;
}

/** A track's name without the extension the card gave it: the title a row falls back to. */
export function fileTitle(path: string): string {
  return fileName(path).replace(/\.[^.]+$/, '');
}

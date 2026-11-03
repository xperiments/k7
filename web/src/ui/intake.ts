/**
 * Reading a card the user picks: the one way in, out of the component that draws it.
 *
 * Two places ask for a card — the drop zone in Process, and the "Change card" action in its
 * header — and both need the same four steps in the same order: pick read-write, walk the
 * files, refuse a choice that is not the card root, then identify the card so the scope exists
 * before the first cover is written. Keeping the order here is what makes "Change card" the
 * same act as the first pick rather than a second implementation of it.
 */

import { cardFingerprint } from '../card/fingerprint';
import { rememberCardHandle } from '../card/handles';
import { fingerprintScope } from '../card/mark';
import { cardShapeOf, cardShapeProblem } from '../card/shape';
import { setCardScope } from '../card/session';
import {
  cardPicker,
  identifyCard,
  pickCardDirectory,
  UNMARKED_CARD,
  type CardDirectory,
  type DirectoryPicker,
  type SnapshotDirectory,
} from './cardWriter';
import { scanDirectoryHandle, type CardDirectoryHandle, type FoundFile } from './files';

export type Intake =
  | { kind: 'read'; files: FoundFile[]; skipped: number; handle: SnapshotDirectory }
  | { kind: 'cancelled' }
  | { kind: 'unsupported' }
  | { kind: 'wrong'; problem: string }
  | { kind: 'failed'; message: string };

/** A card's fingerprint scope: a change detector, for a card with no usable mark. */
export function fingerprintOf(files: readonly FoundFile[]): Promise<string> {
  return cardFingerprint(
    files.map((found) => ({
      path: found.path,
      size: found.file.size,
      mtime: found.file.lastModified,
    })),
  );
}

/** The picked directory's own walk, or null when the handle cannot enumerate itself. */
export function walkable(handle: SnapshotDirectory): CardDirectoryHandle | null {
  const values = (handle as { values?: unknown }).values;
  return typeof values === 'function' ? (handle as unknown as CardDirectoryHandle) : null;
}

/** Walk a card the page already holds open — the same files, as they are now. */
export async function readOpenCard(
  handle: SnapshotDirectory,
): Promise<{ files: FoundFile[]; skipped: number } | { problem: string }> {
  const walk = walkable(handle);
  if (walk === null) {
    return { problem: 'This browser will not let the page read the folder it opened.' };
  }
  const result = await scanDirectoryHandle(walk);
  const problem = cardShapeProblem(cardShapeOf(result.files.map((found) => found.path)));
  if (problem !== null) return { problem };
  return { files: result.files, skipped: result.skipped };
}

/** Ask for the card, read it, and leave the Scope set for whatever runs next. */
export async function readCardWithPicker(
  picker: DirectoryPicker | undefined = cardPicker(),
): Promise<Intake> {
  let picked;
  try {
    picked = await pickCardDirectory(picker);
  } catch (failure) {
    return { kind: 'failed', message: failure instanceof Error ? failure.message : String(failure) };
  }
  if (picked.kind === 'unsupported') return { kind: 'unsupported' };
  if (picked.kind === 'cancelled') return { kind: 'cancelled' };

  try {
    const directory = picked.directory;
    const result = await scanDirectoryHandle(directory as unknown as CardDirectoryHandle);
    // The card root is the only choice that can be written back: one level down is the music
    // directory, one up is the card's parent, and neither can hold k7.db (K1).
    const problem = cardShapeProblem(cardShapeOf(result.files.map((found) => found.path)));
    if (problem !== null) return { kind: 'wrong', problem };

    // Identify the card before the run: the scope has to exist before the first cover is
    // written, and a mark this App did not write is never overwritten (card-mark.md §4).
    const identified = await identifyCard(directory as unknown as CardDirectory);
    const scope =
      identified === UNMARKED_CARD
        ? fingerprintScope(await fingerprintOf(result.files))
        : identified;
    setCardScope(scope);
    // Remembered so a reload can still read the card's own files (play, rescan): the handle
    // survives storage, the permission is asked for again on the gesture that needs it.
    await rememberCardHandle(scope, directory as unknown as FileSystemDirectoryHandle);
    return { kind: 'read', files: result.files, skipped: result.skipped, handle: directory };
  } catch (failure) {
    return { kind: 'failed', message: failure instanceof Error ? failure.message : String(failure) };
  }
}

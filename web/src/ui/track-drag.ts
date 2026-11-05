import type { DragEvent } from 'react';

/**
 * The native drag that carries something from the card's tree into a playlist.
 *
 * Two private MIME types, one for a file and one for a folder, because a folder means "add
 * everything under it" rather than "add this one path". They also distinguish a tree drag
 * from a row being reordered inside the playlist: that drag carries only an index in
 * `text/plain`, and reading a bare number as a Snapshot path would insert a row called "3".
 * The custom type is what lets a drop target decide on `dragover` — before the browser lets
 * it read the payload — whether to accept the drop at all.
 */
export const TRACK_DRAG_TYPE = 'application/x-k7-track';
export const FOLDER_DRAG_TYPE = 'application/x-k7-folder';

function begin(event: DragEvent, type: string, path: string): void {
  event.dataTransfer.effectAllowed = 'copy';
  event.dataTransfer.setData(type, path);
  // Firefox refuses to start a drag whose data transfer is empty of text.
  event.dataTransfer.setData('text/plain', path);
}

export function beginTrackDrag(event: DragEvent, path: string): void {
  begin(event, TRACK_DRAG_TYPE, path);
}

export function beginFolderDrag(event: DragEvent, path: string): void {
  begin(event, FOLDER_DRAG_TYPE, path);
}

/** The Snapshot path of the file a drag is carrying, or null when it is not a file drag. */
export function draggedTrack(event: DragEvent): string | null {
  const path = event.dataTransfer.getData(TRACK_DRAG_TYPE);
  return path.length > 0 ? path : null;
}

/** The folder path a drag is carrying, or null when it is not a folder drag. */
export function draggedFolder(event: DragEvent): string | null {
  const path = event.dataTransfer.getData(FOLDER_DRAG_TYPE);
  return path.length > 0 ? path : null;
}

/**
 * Whether a drag over an element comes from the tree at all. `dragover` may only inspect
 * `types`, not the payload, so this is the one question a drop target can ask before the
 * drop is allowed.
 */
export function isLibraryDrag(event: DragEvent): boolean {
  return (
    event.dataTransfer.types.includes(TRACK_DRAG_TYPE) ||
    event.dataTransfer.types.includes(FOLDER_DRAG_TYPE)
  );
}

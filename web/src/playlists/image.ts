import { assembleImage } from '../domain/snapshot';
import type { SnapshotPlaylist } from '../domain/snapshot';

/**
 * Give an existing Snapshot new Playlists without touching its measurements.
 *
 * The lists live at the end of the image (format spec §2), so everything before them — the
 * header's provenance, the rows and the rows' strings — can be carried over byte for byte
 * and only the sizes and the CRC change. That is what lets the Playlists page send from a
 * browser whose card is in the player: the measurements are already in the **last image**
 * this browser built, and re-measuring to send a list would be absurd (ADR 0051).
 *
 * A version 1 image becomes version 2 when there is a list to carry, and a version 2 image
 * whose lists are replaced loses its old list strings rather than accumulating them.
 */

/* The header's magic: eight bytes, so `K7SNAP` is followed by two NULs. The whole field is
 * compared, never a trimmed string, so only a real Snapshot passes. */
const MAGIC = 'K7SNAP\0\0';
const HEADER_V1 = 64;
const HEADER_V2 = 80;
const ROW_SIZE = 32;

export class ImageError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ImageError';
  }
}

function u32(view: DataView, at: number): number {
  return view.getUint32(at, true);
}

function u64(view: DataView, at: number): bigint {
  return view.getBigUint64(at, true);
}

/** The end of the four NUL-terminated strings starting at `offset`, or -1 if they do not fit. */
function stringsEnd(image: Uint8Array, poolStart: number, poolSize: number, offset: number): number {
  if (offset >= poolSize) return -1;
  let at = poolStart + offset;
  for (let string = 0; string < 4; string += 1) {
    while (at < poolStart + poolSize && image[at] !== 0) at += 1;
    if (at >= poolStart + poolSize) return -1;
    at += 1;
  }
  return at - poolStart;
}

/** The image with these Playlists in it. Throws `ImageError` for bytes that are not one. */
export function patchListArea(
  image: Uint8Array,
  lists: readonly SnapshotPlaylist[],
): Uint8Array<ArrayBuffer> {
  if (image.length < HEADER_V1) throw new ImageError('the image is shorter than its header');
  const magic = String.fromCharCode(...image.subarray(0, 8));
  if (magic !== MAGIC) throw new ImageError('this is not a Snapshot');

  const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
  const version = view.getUint16(8, true);
  if (version !== 1 && version !== 2) throw new ImageError(`unknown format version ${version}`);
  const headerSize = view.getUint16(10, true);
  if (headerSize !== (version === 1 ? HEADER_V1 : HEADER_V2)) {
    throw new ImageError(`a version ${version} image has a ${headerSize}-byte header`);
  }
  if (view.getUint16(20, true) !== ROW_SIZE) throw new ImageError('unexpected row size');
  if (u32(view, 12) !== image.length) throw new ImageError('the image is not its stated length');

  const rowCount = u32(view, 16);
  const poolSize = u32(view, 32);
  const poolStart = headerSize + rowCount * ROW_SIZE;
  if (poolStart + poolSize > image.length) throw new ImageError('the string pool is out of range');

  const rowStringOffsets: number[] = [];
  let rowStringsEnd = 0;
  for (let row = 0; row < rowCount; row += 1) {
    const offset = u32(view, headerSize + row * ROW_SIZE + 12);
    const end = stringsEnd(image, poolStart, poolSize, offset);
    if (end < 0) throw new ImageError(`row ${row} has strings outside the pool`);
    rowStringOffsets.push(offset);
    rowStringsEnd = Math.max(rowStringsEnd, end);
  }

  return assembleImage({
    generationSeconds: Number(u64(view, 24)),
    analyserVersion: u32(view, 40),
    appSchema: u32(view, 44),
    modelSet: u64(view, 48),
    rowCount,
    rowBytes: image.slice(headerSize, poolStart),
    rowStringOffsets,
    rowPool: image.slice(poolStart, poolStart + rowStringsEnd),
    lists,
  });
}

import { assembleImage } from '../domain/snapshot';
import type { SnapshotPlaylist } from '../domain/snapshot';

/**
 * Give an existing Snapshot new Playlists without touching its measurements.
 *
 * The lists live at the end of the image (format spec §2), so everything before them — the
 * header's provenance, the rows and the rows' strings — can be carried over byte for byte
 * and only the sizes and the CRC change. `row_pool_size` (header offset 76) says where the
 * rows' strings end, so a patch never has to know the row dictionary and never keeps the
 * list strings it replaces. That is what lets the Playlists page send from a browser whose
 * card is in the player: the measurements are already in the **last image** this browser
 * built, and re-measuring to send a list would be absurd (ADR 0051).
 */

/* The header's magic: eight bytes, so `K7SNAP` is followed by two NULs. The whole field is
 * compared, never a trimmed string, so only a real Snapshot passes. */
const MAGIC = 'K7SNAP\0\0';
const HEADER_SIZE = 80;
const FORMAT_VERSION = 3;
const ROW_SIZE = 32;
const LIST_SIZE = 32;
const LIST_ROW_SIZE = 4;

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

/**
 * The image with these Playlists in it. Throws `ImageError` for bytes that are not a
 * version 3 Snapshot, and rebuilds only the list area: the rows, their strings and the
 * header's provenance are copied verbatim.
 */
export function patchListArea(
  image: Uint8Array,
  lists: readonly SnapshotPlaylist[],
): Uint8Array<ArrayBuffer> {
  if (image.length < HEADER_SIZE) throw new ImageError('the image is shorter than its header');
  const magic = String.fromCharCode(...image.subarray(0, 8));
  if (magic !== MAGIC) throw new ImageError('this is not a Snapshot');

  const view = new DataView(image.buffer, image.byteOffset, image.byteLength);
  const version = view.getUint16(8, true);
  if (version !== FORMAT_VERSION) throw new ImageError(`unknown format version ${version}`);
  const headerSize = view.getUint16(10, true);
  if (headerSize !== HEADER_SIZE) {
    throw new ImageError(`a version ${version} image has a ${headerSize}-byte header`);
  }
  if (view.getUint16(20, true) !== ROW_SIZE) throw new ImageError('unexpected row size');
  if (u32(view, 12) !== image.length) throw new ImageError('the image is not its stated length');

  const rowCount = u32(view, 16);
  const poolSize = u32(view, 32);
  const listCount = view.getUint16(64, true);
  const listRows = u32(view, 72);
  const rowPoolSize = u32(view, 76);
  if (rowPoolSize > poolSize) throw new ImageError('the row strings are outside the pool');

  const expected =
    HEADER_SIZE + rowCount * ROW_SIZE + poolSize + listCount * LIST_SIZE + listRows * LIST_ROW_SIZE;
  if (expected !== image.length) throw new ImageError('the sections are not the stated length');

  const poolStart = HEADER_SIZE + rowCount * ROW_SIZE;

  return assembleImage({
    generationSeconds: Number(u64(view, 24)),
    analyserVersion: u32(view, 40),
    appSchema: u32(view, 44),
    modelSet: u64(view, 48),
    rowCount,
    rowBytes: image.slice(HEADER_SIZE, poolStart),
    // §7: only `[0, row_pool_size)` is the rows' region; the rest is list strings
    // that this patch replaces rather than accumulating.
    rowPool: image.slice(poolStart, poolStart + rowPoolSize),
    lists,
  });
}

/**
 * The K7's one user K7Art pack, over the App's device link (`art-packs.md` §4/§5).
 *
 * Four operations, one per route on the device's front door:
 *
 *   art.status  GET    /api/art        what the partition holds, and why it was refused
 *   art.upload  POST   /api/art        the pack's bytes; the device validates before it writes
 *   art.pack    GET    /api/art/pack   the active pack's bytes, for the App's J-cards
 *   art.reset   DELETE /api/art        erase the user pack; the factory sheet is untouched
 *
 * Nothing here decides what a pack *is*: the bytes are checked by `jcard/pack.ts` on the
 * App's side and by `label_pack_open_why()` on the device's. This module only carries them
 * and reads the status, so the Art packs page is the one place that knows both checks and
 * the order they run in.
 */

import { getDevice } from './device';

/**
 * The device's status, already parsed. An absent pack is `installed: false, valid: true`
 * (nothing is wrong); an invalid one is `installed: true, valid: false` and `why` says what
 * the device refused (`art-packs.md` §5.1).
 */
export interface ArtStatus {
  installed: boolean;
  valid: boolean;
  name: string;
  author: string;
  designCount: number;
  /** The sheet's own CRC (the sheet header's), not the pack's. */
  sheetCrc: number;
  /** How many designs the embedded factory sheet carries. */
  factoryCount: number;
  /** The invalid pack's reason, or the empty string. */
  why: string;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function count(value: unknown): number | null {
  return typeof value === 'number' && Number.isInteger(value) && value >= 0 ? value : null;
}

/**
 * The status document, checked at the boundary. A malformed body returns null, which the
 * direct transport turns into a sentence the page can show rather than a status with made-up
 * counts.
 */
export function parseArtStatus(value: unknown): ArtStatus | null {
  if (!isRecord(value)) return null;
  if (typeof value.installed !== 'boolean' || typeof value.valid !== 'boolean') return null;
  if (typeof value.name !== 'string' || typeof value.author !== 'string') return null;
  if (typeof value.why !== 'string') return null;

  const designCount = count(value.design_count);
  const sheetCrc = count(value.sheet_crc);
  const factoryCount = count(value.factory_count);
  if (designCount === null || sheetCrc === null || factoryCount === null) return null;

  return {
    installed: value.installed,
    valid: value.valid,
    name: value.name,
    author: value.author,
    designCount,
    sheetCrc,
    factoryCount,
    why: value.why,
  };
}

/** `GET /api/art`: the active pack, or the factory's own status. */
export async function fetchArtStatus(): Promise<ArtStatus> {
  return getDevice().request('art.status');
}

/** `GET /api/art/pack`: the active pack's bytes. The device answers 404 when there is none. */
export async function fetchArtPack(): Promise<Uint8Array<ArrayBuffer>> {
  const { bytes } = await getDevice().request('art.pack');
  return bytes;
}

/** `POST /api/art`: one whole pack. The device refuses it before anything is written. */
export async function uploadArt(pack: Uint8Array<ArrayBuffer>): Promise<ArtStatus> {
  return getDevice().request('art.upload', { pack });
}

/** `DELETE /api/art`: the factory's own status. The embedded sheet is never touched. */
export async function resetArt(): Promise<ArtStatus> {
  return getDevice().request('art.reset');
}

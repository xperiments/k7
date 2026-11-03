/**
 * Sending the snapshot to the player.
 *
 * The card stays in the computer: the browser reads it there, the analysis happens there,
 * and the measurements — a few kilobytes — are posted to the player that owns the music.
 * The player validates the image in full before it touches its flash, so this side's job
 * is to say what happened, not to prepare the partition.
 *
 * `application/octet-stream` is deliberate and lives in `direct.ts`, where the Device copy
 * posts: it is not a CORS-safelisted type, so another origin cannot post to the player
 * without a preflight the player never answers. Through the bridge the same bytes cross
 * `postMessage` in the transfer list instead (C3), and the Launcher posts them.
 */

import { getDevice } from './device';
import { DeviceError } from './transport';

export interface UploadResult {
  bytes: number;
}

export class UploadError extends Error {
  constructor(
    message: string,
    readonly status: number,
  ) {
    super(message);
    this.name = 'UploadError';
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/**
 * `POST /api/snapshot`'s receipt: `{applied, bytes}`. `applied` is the fact — a 200 without
 * it is a refusal, which both links have already turned into a failed request.
 */
export function parseUploadResult(value: unknown, sentBytes: number): UploadResult | null {
  if (!isRecord(value) || value.applied !== true) return null;
  return {
    bytes:
      typeof value.bytes === 'number' && Number.isFinite(value.bytes) ? value.bytes : sentBytes,
  };
}

/**
 * Send the image. The `player` argument is kept for the callers that were written against
 * a base URL; the link decides who carries the request, and no caller passes one.
 */
export async function sendSnapshot(bytes: Uint8Array, _player = ''): Promise<UploadResult> {
  /* C3: the buffer is what crosses, so it has to be an `ArrayBuffer` rather than a
   * `SharedArrayBuffer` the transfer list refuses. A view over the same bytes, never a copy. */
  if (!(bytes.buffer instanceof ArrayBuffer)) {
    throw new UploadError('the image is not transferable from this window', 0);
  }
  const transferable = new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  try {
    return await getDevice().request('snapshot', { bytes: transferable });
  } catch (error) {
    if (error instanceof DeviceError) throw new UploadError(error.message, error.status ?? 0);
    throw new UploadError(error instanceof Error ? error.message : String(error), 0);
  }
}

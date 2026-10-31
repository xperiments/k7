/**
 * Sending the snapshot to the player.
 *
 * The card stays in the computer: the browser reads it there, the analysis happens there,
 * and the measurements — a few kilobytes — are posted to the player that owns the music.
 * The player validates the image in full before it touches its flash, so this side's job
 * is to say what happened, not to prepare the partition.
 *
 * `application/octet-stream` is deliberate: it is not a CORS-safelisted type, so another
 * origin cannot post to the player without a preflight the player never answers. That is
 * the whole CSRF story, and it lives at both ends of this call.
 */

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

export async function sendSnapshot(
  bytes: Uint8Array,
  player: string = '',
): Promise<UploadResult> {
  let response: Response;
  try {
    response = await fetch(`${player}/api/snapshot`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: bytes as BodyInit,
    });
  } catch {
    throw new UploadError('the player did not answer; is it on the same network?', 0);
  }

  const text = await response.text();
  let document: { applied?: boolean; bytes?: number; error?: string } = {};
  try {
    document = text ? (JSON.parse(text) as typeof document) : {};
  } catch {
    /* A reply that is not JSON is the player's error page; the status says enough. */
  }

  if (!response.ok || document.applied !== true) {
    throw new UploadError(document.error ?? `the player refused it (${response.status})`, response.status);
  }
  return { bytes: document.bytes ?? bytes.length };
}

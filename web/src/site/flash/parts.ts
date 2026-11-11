/**
 * The parts, from the Site to memory, with the manifest's hash as the gate.
 *
 * A part is fetched from this origin, checked against the size and sha256 in
 * `firmware.json`, and only then handed to the loader (k7-site.md F9). A part that does not
 * match is refused before a byte reaches the board.
 */
import { FlashError } from './errors';
import { partUrl, type FirmwarePart } from './manifest';

export async function sha256Hex(data: Uint8Array): Promise<string> {
  /* A view's `buffer` may be a SharedArrayBuffer in the type system; digests take an
   * ArrayBuffer, so the exact bytes are copied into one. */
  const bytes = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) as ArrayBuffer;
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return Array.from(new Uint8Array(digest))
    .map((byte) => byte.toString(16).padStart(2, '0'))
    .join('');
}

export async function downloadPart(
  part: FirmwarePart,
  fetchImpl: typeof fetch = fetch,
): Promise<Uint8Array> {
  let response: Response;
  try {
    response = await fetchImpl(partUrl(part), { cache: 'no-store' });
  } catch {
    throw new FlashError('download', `${part.file} could not be downloaded.`);
  }

  if (!response.ok) {
    throw new FlashError('download', `${part.file} answered ${response.status}.`);
  }

  const data = new Uint8Array(await response.arrayBuffer());
  if (data.length !== part.size) {
    throw new FlashError(
      'download',
      `${part.file} is ${data.length} B; the manifest says ${part.size} B.`,
    );
  }

  if ((await sha256Hex(data)) !== part.sha256) {
    throw new FlashError('hash', `${part.file} does not match the manifest's sha256.`);
  }

  return data;
}

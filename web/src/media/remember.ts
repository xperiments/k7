/**
 * Remember the player's card from the card itself.
 *
 * A card in the player is out of this computer's reach, but it is not unknowable: the App
 * wrote the card's image (`k7.db`) when it measured it, and the image's rows carry each
 * Track's title, artist, album and genre beside its measurements (snapshot-format.md). The
 * device serves that file verbatim (`/api/k7db`), and its library arena lists the paths, so
 * the two join by path fingerprint into exactly the rows the Albums page reads — the names
 * come back without the card being put in a computer.
 *
 * The pictures are not in the image: they are the card's cover archive, which the App
 * restores separately by hash (`restoreCoversFromDevice`, `/api/covers`).
 */
import { presenceFromLibraryBlob } from '../device/libraryBlob';
import { localRequest } from '../device/local';
import { fnv1a64 } from '../domain/snapshot';
import { readImageRows } from '../library/imageSource';
import { putMedia, type CachedMedia } from '../storage/media';

/**
 * Write the player's card into `scope` and answer how many rows were written. Throws what
 * the player refused: a card with no image is one the App has never measured anywhere, and
 * no listing can invent the names it never wrote.
 */
export async function rememberCardFromPlayer(scope: string): Promise<number> {
  const [image, library] = await Promise.all([
    localRequest('/api/k7db', { cache: 'no-store' }),
    localRequest('/api/library.bin', { cache: 'no-store' }),
  ]);
  if (image.status === 404) throw new Error('the card in the player has no image on it');
  if (!image.ok) throw new Error(`the player refused its image (${image.status})`);
  if (!library.ok) throw new Error(`the player refused its library (${library.status})`);

  const rows = readImageRows(new Uint8Array(await image.arrayBuffer()));
  const presence = presenceFromLibraryBlob(new Uint8Array(await library.arrayBuffer()));
  const byFingerprint = new Map(rows.map((row) => [row.fingerprint.toString(), row] as const));
  const encoder = new TextEncoder();
  const at = new Date().toISOString();
  let written = 0;
  for (const track of presence.tracks) {
    const row = byFingerprint.get(fnv1a64(encoder.encode(track.path)).toString());
    if (!row) continue;
    const media: CachedMedia = {
      path: track.path,
      title: row.title,
      artist: row.artist,
      album: row.album,
      genre: row.genre,
      at,
      ...(row.size > 0 ? { size: row.size } : {}),
    };
    await putMedia(scope, media);
    written += 1;
  }
  return written;
}

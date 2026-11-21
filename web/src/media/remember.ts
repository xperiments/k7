/**
 * Remember the player's card from the device.
 *
 * A card in the player is out of this computer's reach, but the device is answering from an
 * image: the `k7.db` the App wrote when it measured the card, applied to the player over
 * POST or off the card at boot, whose rows carry each Track's title, artist, album and
 * genre beside its measurements (snapshot-format.md). The device serves that image as it
 * stands (`GET /api/snapshot`), and its library arena lists the paths, so the two join by
 * path fingerprint into exactly the rows the Albums page reads - the names come back
 * without the card being put in a computer.
 *
 * The pictures are not in the image: they are the card's cover archive, which the App
 * restores separately by hash (`restoreCoversFromDevice`, `/api/covers`).
 */
import { presenceFromLibraryBlob } from '../device/libraryBlob';
import { localRequest } from '../device/local';
import { fnv1a64 } from '../domain/snapshot';
import { readImageRows } from '../library/imageSource';
import { folderOf } from './paths';
import { restoreCoversFromDevice } from '../ui/cardWriter';
import { countMedia, putMediaMany, type CachedMedia } from '../storage/media';

/**
 * Write the player's card into `scope` and answer how many rows were written. Throws what
 * the player refused: a card with no image is one the App has never measured anywhere, and
 * no listing can invent the names it never wrote.
 */
export async function rememberCardFromPlayer(
  scope: string,
  onProgress: (fraction: number) => void = () => {},
): Promise<number> {
  /* Three steps worth naming: the image and the listing, the pictures, the rows. The writes
   * are each one transaction, so there is no finer grain to report than their edges. */
  onProgress(0.05);
  const [image, library] = await Promise.all([
    localRequest('/api/snapshot', { cache: 'no-store' }),
    localRequest('/api/library.bin', { cache: 'no-store' }),
  ]);
  if (image.status === 404) throw new Error('the card in the player has no image on it');
  if (!image.ok) throw new Error(`the player refused its image (${image.status})`);
  if (!library.ok) throw new Error(`the player refused its library (${library.status})`);

  onProgress(0.25);
  const rows = readImageRows(new Uint8Array(await image.arrayBuffer()));
  const presence = presenceFromLibraryBlob(new Uint8Array(await library.arrayBuffer()));
  /* The pictures are the archive's, and the archive says which album wears which: without
   * the index a cleared browser has the names and nothing to draw (cover-archive.md). */
  let albums = new Map<string, string>();
  try {
    const restored = await restoreCoversFromDevice();
    albums = restored.albums;
    onProgress(0.55);
  } catch {
    /* A card with no archive is a card with no pictures: the names still come back. */
  }
  const byFingerprint = new Map(rows.map((row) => [row.fingerprint.toString(), row] as const));
  const encoder = new TextEncoder();
  const at = new Date().toISOString();
  const fresh: CachedMedia[] = [];
  for (const track of presence.tracks) {
    const row = byFingerprint.get(fnv1a64(encoder.encode(track.path)).toString());
    if (!row) continue;
    const coverHash = albums.get(folderOf(track.path));
    fresh.push({
      path: track.path,
      title: row.title,
      artist: row.artist,
      album: row.album,
      genre: row.genre,
      at,
      ...(coverHash ? { coverHash } : {}),
      ...(row.size > 0 ? { size: row.size } : {}),
    });
  }
  /* One transaction for the card: a remembered library is thousands of rows, and one write
   * each is slow enough that a closed connection could catch it half-written. */
  /* Rows go in batches so the bar can count them: each is one transaction, and the count is
   * the only real number this operation has. */
  const BATCH = 250;
  for (let at = 0; at < fresh.length; at += BATCH) {
    await putMediaMany(scope, fresh.slice(at, at + BATCH));
    onProgress(0.55 + 0.4 * (Math.min(at + BATCH, fresh.length) / Math.max(1, fresh.length)));
  }
  /* Read it back: a transaction that silently did nothing (a closed connection, a store
   * that answered with an abort) must not leave the page waiting on rows that are not
   * there. */
  const landed = await countMedia(scope);
  if (landed === 0) throw new Error('the rows did not land in this browser’s store');
  onProgress(1);
  return fresh.length;
}

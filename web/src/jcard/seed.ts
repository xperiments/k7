/**
 * The stable face seed: a coverless album's J-card is chosen by the same hash that used to
 * choose one of the five drawn CSS faces (art-packs.md §6). It has to be stable across
 * visits, browsers and machines, so it is a plain character fold, not `Math.random` and not
 * `Intl`.
 *
 * The pair, not the title alone, is the seed: two albums may share a title, and a Track's
 * case should not have to equal its album's. The NUL keeps the pair unambiguous - without it
 * ("ab", "c") and ("a", "bc") would hash the same string.
 */

/** A hue per name: the case's tint and the drawn slot's colour, stable everywhere. */
export function hueOf(text: string): number {
  let hue = 0;
  for (let at = 0; at < text.length; at += 1) hue = (hue * 31 + text.charCodeAt(at)) % 360;
  return hue;
}

/** The pair's stable seed: `hueOf` of the two names, kept distinct. */
export function faceSeedOf(title: string, artist: string): number {
  return hueOf(`${title}\u0000${artist}`);
}

/**
 * The design a name gets from a pool of `count`, or -1 when there is nothing to draw.
 * Selection is modulo the pool's size, factory designs first (art-packs.md §6).
 */
export function faceIndexFor(title: string, artist: string, count: number): number {
  if (!Number.isInteger(count) || count <= 0) return -1;
  return faceSeedOf(title, artist) % count;
}

import type { CSSProperties } from 'react';

/**
 * A hue per name: the drawn cover, the spine, the side and the no-cover face need *a*
 * colour, and a hash of the name is stable across visits, machines and the device's own
 * table.
 */
export function hueOf(text: string): number {
  let hue = 0;
  for (let at = 0; at < text.length; at += 1) hue = (hue * 31 + text.charCodeAt(at)) % 360;
  return hue;
}

/** The words a drawn cover carries. The meta is the cassette line the reference uses. */
export interface AlbumArtData {
  title: string;
  artist: string;
  /** The small line under the rule; "Side A · Side B" when it is not given. */
  meta?: string;
}

/**
 * Which of the reference's five faces this title and artist get.
 *
 * The pair, not the title alone, is the seed: two albums may share a title, and a track's
 * face should not have to equal its album's. The same pair always gets the same face, so a
 * coverless album looks the same on the shelf, in its card and in the player.
 */
export function coverTemplateOf(title: string, artist: string): 1 | 2 | 3 | 4 | 5 {
  return ((hueOf(`${title}\u0000${artist}`) % 5) + 1) as 1 | 2 | 3 | 4 | 5;
}

/**
 * The cover art a picture would have been: one of the cassette case reference's five faces,
 * with the title, artist and cassette meta. It fills its container and takes its type sizes
 * from the container's width (`cqw`), so the same face works at the shelf's 13rem case, in
 * an album card and in the player's square. The container query in `index.css` hides the
 * words when the face is too small to carry them.
 *
 * It is decorative: the album or track it belongs to is named beside it, and the case's own
 * label is the accessible name. The hues and the cream are cover art, the coverflow's own
 * exception to the skin (k7-look.md §5.3/K19).
 */
export function AlbumArt({ title, artist, meta = 'Side A · Side B' }: AlbumArtData) {
  const hue = hueOf(title);
  const template = coverTemplateOf(title, artist);
  return (
    <span
      aria-hidden="true"
      className={`k7-album-art k7-album-art-tpl-${template}`}
      style={{ '--hue': `${hue}deg` } as CSSProperties}
    >
      <span className="k7-album-ring" />
      <span className="k7-album-artist">{artist}</span>
      <span className="k7-album-title">{title}</span>
      <span className="k7-album-rule" />
      <span className="k7-album-meta">{meta}</span>
    </span>
  );
}

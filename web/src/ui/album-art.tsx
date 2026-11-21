import type { CSSProperties, ReactNode } from 'react';

import {
  Jcard,
  hueOf,
  useJcardDesign,
  type JcardFit,
  type JcardPart,
} from '../jcard';

export { hueOf } from '../jcard';

/**
 * The words a drawn face carries. `meta` is the cassette line the reference uses; a Track's
 * case passes its album, and an album's its default (k7art.md §4).
 */
export interface AlbumArtData {
  title: string;
  artist: string;
  /** The small line under the rule; "Side A · Side B" when it is not given. */
  meta?: string;
  /** The album's name when the surface knows it apart from the title (a Track's case). */
  album?: string;
  /** The album's genre, when the surface has it. */
  genre?: string;
}

/** The line an album's case prints when the caller has nothing more specific. */
export const DEFAULT_META = 'Side A · Side B';

export interface AlbumArtOptions {
  /** Which J-card panel the surface shows; the cover on a case, the spine on the shelf. */
  part?: JcardPart;
  /**
   * `fill` stretches the panel over a case face (the 0.1% difference between 16:25 and the
   * cover's proportions is the case's own); `cover` scales uniformly and centres, which is
   * how a square card shows the cover's safe square (k7art.md §5).
   */
  fit?: JcardFit;
  /** What the slot shows if the pool has no design at all; the initial, as before. */
  fallback?: ReactNode;
}

/**
 * The face of an album or Track with no cover picture: its pool's J-card, chosen by the
 * name's stable seed (art-packs.md §6). It fills its container and takes its type sizes from
 * the container's width, so the same design works at the shelf's 13rem case, in an album
 * card and in the Player bar's mini case. The stylesheet hides its words when the face is too
 * small to carry them.
 *
 * It is decorative: the album or Track is named beside it. The case's `hueOf` tint stays
 * outside this component; a real cover picture never reaches here, because the cover's own
 * branch wins before the face is chosen.
 */
export function AlbumArt({
  title,
  artist,
  meta = DEFAULT_META,
  album,
  genre,
  part = 'cover',
  fit = 'fill',
  fallback = null,
}: AlbumArtData & AlbumArtOptions) {
  const design = useJcardDesign(title, artist);
  const hue = hueOf(title);
  if (!design) return <>{fallback}</>;
  /* A design may bind a text to the K7's own fields as well as the J-card's three. The
   * surface fills what it knows — an album's card has its name and genre, a Track's case its
   * own title and album — and the rest print nothing. */
  return (
    <Jcard
      design={design}
      fields={{
        title,
        artist,
        meta,
        song_title: title,
        song_artist: artist,
        song_album: album ?? '',
        genre: genre ?? '',
      }}
      part={part}
      fit={fit}
      style={{ '--hue': `${hue}deg` } as CSSProperties}
    />
  );
}

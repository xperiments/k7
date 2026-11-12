import type { CSSProperties } from 'react';

import type { CachedCover } from '../storage/media';
import { AlbumArt, type AlbumArtData } from './album-art';
import { useCoverUrl } from './cover';

/**
 * The picture on a case's face: the art, or the album's drawn cover when it has no picture,
 * or a tinted face with its initial where neither is available (the 30px player case). The
 * moulding is drawn on top either way.
 */
export function CassetteFront({
  cover,
  initial,
  art,
}: {
  cover: CachedCover | null;
  initial: string;
  /** The drawn face for a coverless album; omitted by the tiny cases. */
  art?: AlbumArtData;
}) {
  const url = useCoverUrl(cover);
  return (
    <>
      {url ? (
        <>
          <img src={url} alt="" className="h-full w-full object-cover" draggable={false} />
          {/* The unblended copy: mix-blend-mode cannot be interpolated, so the turned
              case fades this one in over the multiplied face. */}
          <img
            src={url}
            alt=""
            aria-hidden="true"
            className="k7-front-plain absolute inset-0 h-full w-full object-cover"
            draggable={false}
          />
        </>
      ) : art ? (
        <AlbumArt {...art} />
      ) : (
        <span className="k7-cover-fallback" aria-hidden="true">
          {initial}
        </span>
      )}
      <span className="k7-frame" aria-hidden="true" />
      <span className="k7-sheen" aria-hidden="true" />
    </>
  );
}

/**
 * One case, front to the reader: the shelf's case without the shelf. The player bar shows
 * what is playing as the object it is rather than as a thumbnail — a cassette with the cover
 * on its face — and the size is `.k7-mini`'s, because a case is a 3D box and its width is
 * what its height follows from.
 */
export function CassetteCase({
  cover,
  initial,
  hue,
  art,
  className,
}: {
  cover: CachedCover | null;
  initial: string;
  /** Degrees; the tint of the face when there is no picture. */
  hue?: number;
  /** The drawn face for a coverless track; the mini hides its words. */
  art?: AlbumArtData;
  className?: string;
}) {
  return (
    <div className={className ? `k7-mini ${className}` : 'k7-mini'} aria-hidden="true">
      <div className="k7-shelf-item">
        <div
          className="k7-case"
          data-active="true"
          style={hue === undefined ? undefined : ({ '--hue': `${hue}deg` } as CSSProperties)}
        >
          <span className="k7-face k7-front">
            <CassetteFront cover={cover} initial={initial} art={art} />
          </span>
        </div>
      </div>
    </div>
  );
}

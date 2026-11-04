import type { CSSProperties } from 'react';

import type { CachedCover } from '../storage/media';
import { useCoverUrl } from './cover';

/**
 * A hue per name: the spine, the side and the no-cover face need *a* colour, and a hash of
 * the name is stable across visits, machines and the device's own table.
 */
export function hueOf(text: string): number {
  let hue = 0;
  for (let at = 0; at < text.length; at += 1) hue = (hue * 31 + text.charCodeAt(at)) % 360;
  return hue;
}

/** The picture on a case's face: the art, or a tinted face with its initial, and the moulding. */
export function CassetteFront({ cover, initial }: { cover: CachedCover | null; initial: string }) {
  const url = useCoverUrl(cover);
  return (
    <>
      {url ? (
        <img src={url} alt="" className="h-full w-full object-cover" draggable={false} />
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
  className,
}: {
  cover: CachedCover | null;
  initial: string;
  /** Degrees; the tint of the face when there is no picture. */
  hue?: number;
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
            <CassetteFront cover={cover} initial={initial} />
          </span>
        </div>
      </div>
    </div>
  );
}

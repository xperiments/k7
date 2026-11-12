import { useEffect, useState } from 'react';
import { cn } from 'cn';
import type { CachedCover } from '../storage/media';
import { AlbumArt, type AlbumArtData } from './album-art';

/**
 * The cover, as a blob URL made once per image and released when the element goes away.
 *
 * Two callers shape it differently: the grid and the bars want a square that crops, and the
 * album shelf wants a face that fills a cassette case. Both want the same lifetime, so the
 * URL is a hook and the element is a component on top of it.
 */
export function useCoverUrl(cover: CachedCover | null): string | null {
  const [url, setUrl] = useState<string | null>(null);

  useEffect(() => {
    if (!cover) {
      setUrl(null);
      return;
    }
    const objectUrl = URL.createObjectURL(
      new Blob([cover.bytes], { type: cover.format || 'image/jpeg' }),
    );
    setUrl(objectUrl);
    return () => URL.revokeObjectURL(objectUrl);
  }, [cover]);

  return url;
}

export function Cover({
  cover,
  initial,
  alt = '',
  className,
  art,
}: {
  cover: CachedCover | null;
  /** What the placeholder tile shows when there is no picture and no drawn face. */
  initial: string;
  alt?: string;
  className?: string;
  /** The drawn face for a coverless album or track. */
  art?: AlbumArtData;
}) {
  const url = useCoverUrl(cover);

  if (!cover || !url) {
    return (
      <div
        aria-hidden="true"
        className={cn(
          'k7-cover-tile flex aspect-square w-full items-center justify-center overflow-clip bg-muted text-2xl font-light text-muted-foreground',
          className,
        )}
      >
        {art ? <AlbumArt {...art} /> : (initial.trim().slice(0, 1) || '?').toUpperCase()}
      </div>
    );
  }

  return <img src={url} alt={alt} className={cn('aspect-square w-full object-cover', className)} />;
}

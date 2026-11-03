import { useMemo, useState } from 'react';
import { ArrowLeft, ExternalLink, Play } from 'lucide-react';

import { Button } from '@/components/ui/button';
import type { Video } from '../../media/youtube';
import { watchUrl } from '../../media/youtube';
import { routeHref } from '../router';

/**
 * One Video, in place.
 *
 * The player is a **click**: the poster is the thumbnail the search already fetched, and the
 * embed is created only when someone presses play. Nothing third-party is requested while the
 * page is idle, which is the same rule the weights follow — a page that loads does not reach
 * out on its own.
 *
 * The embed is YouTube's no-cookie host rather than the user's Invidious instance on purpose.
 * The policy can name a fixed host (`frame-src`), and an instance chosen in Settings cannot be
 * named in a static header; framing it would mean `frame-src https:`, and an instance that
 * refuses framing would show a blank player with no way to tell why. The instance remains the
 * search — no key, no client id — and "Open on YouTube" is the escape hatch.
 *
 * The title and channel are a memory, not a fetch: the search that found the Video is the only
 * place they were known, so it leaves them behind on the click and this route reads them back.
 */

const REMEMBERED = 'k7.video.';

/** Keep what the search knew about a Video, for the route that plays it. */
export function rememberVideo(video: Video): void {
  try {
    sessionStorage.setItem(
      `${REMEMBERED}${video.id}`,
      JSON.stringify({ title: video.title, channel: video.channel, thumbnail: video.thumbnail }),
    );
  } catch {
    /* A browser that refuses storage still plays the video, without its name. */
  }
}

interface Remembered {
  title: string;
  channel: string;
  thumbnail: string;
}

function rememberedVideo(id: string): Remembered | null {
  try {
    const raw = sessionStorage.getItem(`${REMEMBERED}${id}`);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as { title?: unknown; channel?: unknown; thumbnail?: unknown };
    if (typeof parsed.title !== 'string') return null;
    return {
      title: parsed.title,
      channel: typeof parsed.channel === 'string' ? parsed.channel : '',
      thumbnail: typeof parsed.thumbnail === 'string' ? parsed.thumbnail : '',
    };
  } catch {
    return null;
  }
}

/** The no-cookie embed, asked to start on its own: it is only created by a click. */
export function embedUrl(id: string): string {
  return `https://www.youtube-nocookie.com/embed/${encodeURIComponent(id)}?autoplay=1&rel=0`;
}

export function VideoArea({ id }: { id: string }) {
  const info = useMemo(() => rememberedVideo(id), [id]);
  const [playing, setPlaying] = useState(false);
  const watch = watchUrl(id);
  const poster = info?.thumbnail || `https://i.ytimg.com/vi/${encodeURIComponent(id)}/mqdefault.jpg`;
  const title = info?.title ?? 'Video';

  return (
    <div className="flex h-full min-h-0 flex-col">
      <div className="flex shrink-0 flex-col gap-2 border-b p-4 sm:flex-row sm:items-center sm:justify-between">
        <div className="flex min-w-0 flex-col gap-0.5">
          <h1 className="truncate text-sm font-medium">{title}</h1>
          <p className="truncate text-xs text-muted-foreground">
            {info?.channel ? `${info.channel} · plays here` : 'Plays here, on YouTube’s player'}
          </p>
        </div>
        <div className="flex shrink-0 items-center gap-2">
          <Button
            variant="ghost"
            size="sm"
            nativeButton={false}
            render={<a href={routeHref({ name: 'media' })} />}
          >
            <ArrowLeft aria-hidden="true" />
            Media
          </Button>
          <Button
            variant="outline"
            size="sm"
            nativeButton={false}
            render={<a href={watch} target="_blank" rel="noopener noreferrer" />}
          >
            <ExternalLink aria-hidden="true" />
            Open on YouTube
          </Button>
        </div>
      </div>

      <div className="relative min-h-0 flex-1 bg-black">
        {playing ? (
          <iframe
            src={embedUrl(id)}
            title={title}
            allow="autoplay; encrypted-media; picture-in-picture; fullscreen"
            allowFullScreen
            className="absolute inset-0 h-full w-full border-0"
          />
        ) : (
          <button
            type="button"
            onClick={() => setPlaying(true)}
            className="group absolute inset-0 size-full cursor-pointer"
            aria-label={`Play ${title}`}
          >
            <img src={poster} alt="" className="size-full object-contain" />
            <span className="absolute inset-0 flex items-center justify-center bg-black/25 transition-colors group-hover:bg-black/10">
              <span className="flex size-14 items-center justify-center rounded-full bg-background/90 text-foreground shadow-sm">
                <Play aria-hidden="true" className="size-6" />
              </span>
            </span>
          </button>
        )}
      </div>

      <p className="shrink-0 border-t px-4 py-2 text-xs text-muted-foreground">
        The player is YouTube&apos;s: nothing here is downloaded, and a Video never joins a
        playlist.
      </p>
    </div>
  );
}

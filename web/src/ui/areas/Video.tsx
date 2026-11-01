import { useMemo } from 'react';
import { ArrowLeft, ExternalLink } from 'lucide-react';

import { Button } from '@/components/ui/button';
import type { Video } from '../../media/youtube';
import { routeHref } from '../router';

/**
 * One Video, taking the viewport.
 *
 * A cross-origin embed cannot be made fullscreen by the page that hosts it -- the iframe's
 * own player does that, with its own button -- so "full screen" here is a layout: the route
 * gives the embed the whole content area, and the shell's sidebar stays.
 *
 * The title and channel are a memory, not a fetch: the search that found the Video is the
 * only place they were known, so it leaves them behind on the click and this route reads
 * them back. A reload therefore says "Video" rather than inventing a name.
 */

const REMEMBERED = 'xtape.video.';

/** Keep the name of a Video the user just chose, for the route that shows it. */
export function rememberVideo(video: Video): void {
  try {
    sessionStorage.setItem(
      `${REMEMBERED}${video.id}`,
      JSON.stringify({ title: video.title, channel: video.channel }),
    );
  } catch {
    /* A browser that refuses storage still plays the video, without its name. */
  }
}

function rememberedVideo(id: string): { title: string; channel: string } | null {
  try {
    const raw = sessionStorage.getItem(`${REMEMBERED}${id}`);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as { title?: unknown; channel?: unknown };
    if (typeof parsed.title !== 'string') return null;
    return {
      title: parsed.title,
      channel: typeof parsed.channel === 'string' ? parsed.channel : '',
    };
  } catch {
    return null;
  }
}

export function VideoArea({ id }: { id: string }) {
  const info = useMemo(() => rememberedVideo(id), [id]);
  const watchUrl = `https://www.youtube.com/watch?v=${encodeURIComponent(id)}`;

  return (
    <div className="flex h-full min-h-0 flex-col">
      <div className="flex shrink-0 flex-col gap-2 border-b p-4 sm:flex-row sm:items-center sm:justify-between">
        <div className="flex min-w-0 flex-col gap-0.5">
          <h1 className="truncate text-sm font-medium">{info?.title ?? 'Video'}</h1>
          <p className="truncate text-xs text-muted-foreground">
            {info?.channel
              ? `${info.channel} · plays in the embed below`
              : 'Plays in the embed below · the player has its own fullscreen'}
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
            render={<a href={watchUrl} target="_blank" rel="noopener noreferrer" />}
          >
            <ExternalLink aria-hidden="true" />
            Open on YouTube
          </Button>
        </div>
      </div>

      <div className="relative min-h-0 flex-1 bg-background">
        <iframe
          src={`https://www.youtube.com/embed/${encodeURIComponent(id)}?autoplay=1&rel=0`}
          title={info?.title ?? 'YouTube video'}
          allow="autoplay; encrypted-media; picture-in-picture; fullscreen"
          allowFullScreen
          className="absolute inset-0 h-full w-full border-0"
        />
      </div>

      <p className="shrink-0 border-t px-4 py-2 text-xs text-muted-foreground">
        The player is YouTube&apos;s: nothing here is downloaded, and a Video never joins a
        playlist.
      </p>
    </div>
  );
}

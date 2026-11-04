import { useEffect, useState } from 'react';
import { Disc3, Pause, Play, Radio, SkipBack, SkipForward, Square } from 'lucide-react';

import { Button } from '@/components/ui/button';
import { useCardScope } from '../card/session';
import { useDeviceStatus } from '../device/useDeviceStatus';
import { getMedia, type CachedCover } from '../storage/media';
import { formatDuration } from './format';
import { Cover } from './cover';
import { usePlayback } from './playback';

/**
 * The page's player, at the bottom of every area.
 *
 * It follows the two sources the provider has. A **stream** is the player's broadcast: one
 * live thing, no beginning and no end, so it shows "live" where a file shows its clock and
 * there is nothing to skip to. A **card track** is a track from an album on the card in the
 * computer: it has a length, so the scrub spans the bar, and it has neighbours, so it has
 * previous and next. The bar never invents a state: with nothing chosen it says so and its one
 * button is the way in.
 */

/** The metadata the bar shows for what the device is playing, when the bar is on the stream. */
function useDeviceTrack(path: string | null): { title: string; artist: string; cover: CachedCover | null } {
  const scope = useCardScope();
  const [row, setRow] = useState<{ title: string; artist: string; cover: CachedCover | null } | null>(
    null,
  );

  useEffect(() => {
    if (path === null || path === '') {
      setRow(null);
      return;
    }
    let cancelled = false;
    getMedia(scope, path)
      .then((found) => {
        if (cancelled) return;
        setRow(
          found
            ? { title: found.title, artist: found.artist, cover: found.cover ?? null }
            : null,
        );
      })
      .catch(() => {
        if (!cancelled) setRow(null);
      });
    return () => {
      cancelled = true;
    };
  }, [scope, path]);

  return row ?? { title: '', artist: '', cover: null };
}

export function PlayerBar() {
  const playback = usePlayback();
  const { status } = useDeviceStatus();
  const playing = status?.playing ?? null;
  const onStream = playback.source === null || playback.source.kind === 'stream';
  const cardTrack = playback.source?.kind === 'card' ? playback.source.track : null;
  const deviceTrack = useDeviceTrack(onStream ? (playing?.path ?? null) : null);

  const title = cardTrack
    ? cardTrack.title
    : playing?.title || (playback.source === null ? 'Nothing playing' : 'The player’s stream');
  const artist = cardTrack ? cardTrack.artist : playback.source === null ? '' : 'Live from the player';
  const cover = cardTrack ? cardTrack.cover : deviceTrack.cover;
  const seekable = playback.source?.kind === 'card';
  /* The player is emitting something, and this page is playing a file instead: one icon is
   * the way back to the broadcast. It is the fact that decides, not the link — a player with
   * a track on is a stream, whatever this page can or cannot fetch. */
  const emitting = Boolean(playing?.title);
  const canGoLive = seekable && emitting;
  const safeDuration = Number.isFinite(playback.duration) && playback.duration > 0
    ? playback.duration
    : 0;

  return (
    <div className="flex shrink-0 flex-col border-t bg-background">
      <div className="flex h-14 items-center gap-3 px-3 sm:px-4">
        {cover || title !== 'Nothing playing' ? (
          <div className="w-10 shrink-0 overflow-hidden rounded-md">
            <Cover cover={cover} initial={title || 'K'} className="size-10 text-base" />
          </div>
        ) : (
          <span className="flex size-10 shrink-0 items-center justify-center rounded-md bg-muted text-muted-foreground">
            <Disc3 aria-hidden="true" className="size-5" />
          </span>
        )}

        <div className="flex min-w-0 flex-1 flex-col gap-0.5">
          <span className="truncate text-sm font-medium">{title}</span>
          <span
            className={
              playback.error
                ? 'truncate text-xs text-destructive'
                : 'truncate text-xs text-muted-foreground'
            }
            title={playback.error ?? undefined}
          >
            {playback.error ??
              (artist || (cardTrack ? 'From this card' : 'The player’s broadcast'))}
            {playback.source?.kind === 'card' ? (
              <span className="tabular-nums">
                {' '}
                · {playback.source.index + 1}/{playback.source.length}
              </span>
            ) : playback.source !== null ? (
              <span className="inline-flex items-center gap-1.5">
                {' '}
                · <span className="size-1.5 rounded-full bg-success" aria-hidden="true" /> live
              </span>
            ) : null}
          </span>
        </div>

        {seekable ? (
          <Button
            size="icon"
            variant="ghost"
            aria-label="Previous track"
            onClick={playback.previous}
            disabled={!playback.hasPrevious}
          >
            <SkipBack aria-hidden="true" />
          </Button>
        ) : null}

        <Button
          size="icon"
          variant="default"
          aria-label={playback.playing ? 'Pause' : 'Play'}
          onClick={playback.toggle}
        >
          {playback.playing ? <Pause aria-hidden="true" /> : <Play aria-hidden="true" />}
        </Button>

        {seekable ? (
          <Button
            size="icon"
            variant="ghost"
            aria-label="Next track"
            onClick={playback.next}
            disabled={!playback.hasNext}
          >
            <SkipForward aria-hidden="true" />
          </Button>
        ) : null}

        {playback.source !== null ? (
          <Button size="icon" variant="ghost" aria-label="Stop" onClick={playback.stop}>
            <Square aria-hidden="true" />
          </Button>
        ) : null}

        {canGoLive ? (
          <Button
            size="icon"
            variant="ghost"
            aria-label="Go live"
            title="Go live: what the player is emitting"
            onClick={playback.playStream}
          >
            <Radio aria-hidden="true" />
          </Button>
        ) : null}

        <input
          type="range"
          aria-label="Volume"
          min={0}
          max={1}
          step={0.01}
          value={playback.volume}
          onChange={(event) => playback.setVolume(Number(event.target.value))}
          className="hidden h-1 w-24 accent-primary md:block"
        />
      </div>

      {seekable ? (
        <div className="flex items-center gap-2 px-3 pb-2 sm:px-4">
          <span className="w-9 shrink-0 text-right text-xs text-muted-foreground tabular-nums">
            {formatDuration(playback.time)}
          </span>
          <input
            type="range"
            aria-label="Seek"
            min={0}
            max={safeDuration}
            step={0.5}
            value={Math.min(playback.time, safeDuration)}
            onChange={(event) => playback.seek(Number(event.target.value))}
            className="h-1 w-full flex-1 accent-primary"
          />
          <span className="w-9 shrink-0 text-xs text-muted-foreground tabular-nums">
            {formatDuration(playback.duration)}
          </span>
        </div>
      ) : null}
    </div>
  );
}

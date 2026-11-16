import { useEffect, useState } from 'react';

import { Alert, AlertDescription, AlertTitle } from '@/components/reui/alert';
import { Badge } from '@/components/reui/badge';
import { Card, CardHeader, CardTitle } from '@/components/ui/card';
import { Progress } from '@/components/ui/progress';
import { trackAt, type McpTrack } from '../../device/mcp';
import { useDeviceStatus } from '../../device/useDeviceStatus';
import { currentCardScope } from '../../card/session';
import { getMedia, type CachedMedia } from '../../storage/media';
import { hueOf } from '../album-art';
import { CassetteCase } from '../cassette';
import { CassetteScreen } from '../cassette-screen';
import { formatDuration } from '../format';

/**
 * The Player: what the broadcast is carrying. The device's own facts — the library and the
 * Snapshot — live on Sources/Device.
 *
 * Read-only on purpose. A live stream cannot be paused for one listener, and the only
 * transport verb the page has is `POST /api/play {path}`, which the album rows offer; a
 * next button here would promise a control the device does not have (front door spec §4.4).
 *
 * The joins are the interesting part, and each one degrades to a dash rather than an error:
 *
 *   cover        the `k7-media` cache by the Snapshot path in `/api/status`, printed on the case
 *   case         the same CassetteCase the shelf draws, at the shelf's width, beside the screen
 *   name         `playing.title`, then the cache, then the device's row, then the file name
 *   artist/album the cache first, the device's row when the cache has no names
 */

/** A file name without its extension, for a track whose tags carried no title. */
function fromPath(path: string): string {
  const name = path.split('/').pop() ?? path;
  return name.replace(/\.[^.]+$/, '');
}

export function PlayerArea() {
  const { status, reachable } = useDeviceStatus();
  const playing = status?.playing ?? null;
  const path = playing?.path?.trim() || null;

  const [cached, setCached] = useState<CachedMedia | null>(null);
  const [measured, setMeasured] = useState<McpTrack | null>(null);
  // A failed poll is silent about whether the device is off or merely slow, so the page
  // waits before it says "not answering" rather than flashing it on every cold load.
  const [waited, setWaited] = useState(false);

  useEffect(() => {
    const timer = setTimeout(() => setWaited(true), 2000);
    return () => clearTimeout(timer);
  }, []);

  // The cover and the names, joined by the Snapshot path in `/api/status`.
  useEffect(() => {
    if (!path) {
      setCached(null);
      setMeasured(null);
      return;
    }
    let cancelled = false;
    setCached(null);
    setMeasured(null);
    void getMedia(currentCardScope(), path)
      .then((row) => {
        if (!cancelled) setCached(row ?? null);
      })
      .catch(() => {
        if (!cancelled) setCached(null);
      });
    void trackAt(path)
      .then((track) => {
        if (!cancelled) setMeasured(track);
      })
      .catch(() => {
        if (!cancelled) setMeasured(null);
      });
    return () => {
      cancelled = true;
    };
  }, [path]);

  const analysis = measured?.analysis ?? null;
  const title =
    playing?.title?.trim() ||
    cached?.title?.trim() ||
    analysis?.title?.trim() ||
    (measured?.name ? measured.name.replace(/\.[^.]+$/, '') : '') ||
    (path ? fromPath(path) : '') ||
    'Nothing is playing';

  const artist = cached?.artist?.trim() || analysis?.artist?.trim() || '';
  const album = cached?.album?.trim() || analysis?.album?.trim() || '';
  const subtitle = [artist, album].filter(Boolean).join(' · ');

  const elapsed = playing?.elapsed ?? 0;
  const duration = playing?.duration ?? 0;
  const percent = duration > 0 ? Math.min(100, Math.max(0, (elapsed / duration) * 100)) : 0;

  return (
    <div className="flex flex-col gap-4 p-4 sm:gap-6 sm:p-6">
      {!status && !waited ? (
        <p className="text-sm text-muted-foreground" role="status">
          Waiting for the device to answer…
        </p>
      ) : !reachable && (status || waited) ? (
        <Alert variant="warning">
          <AlertTitle>The device is not answering</AlertTitle>
          <AlertDescription>
            {status
              ? 'What you last saw is below. The live stream keeps playing while the page cannot ask what it is playing.'
              : 'The page has never reached it. The live stream still plays; this page just cannot say what is on it.'}
          </AlertDescription>
        </Alert>
      ) : null}

      {!path ? (
        <div className="flex flex-col gap-4 lg:flex-row lg:items-start lg:gap-6">
          <figure className="m-0 w-full min-w-0 max-w-full shrink-0 overflow-hidden rounded-panel border border-border lg:w-[min(58%,42rem)]">
            <CassetteScreen playing={false} elapsedMs={0} durationMs={0} trackKey="" />
          </figure>
          <Card className="min-w-0 lg:flex-1">
            <CardHeader>
              <CardTitle>Nothing is playing</CardTitle>
              <p className="text-sm text-muted-foreground">
                {status
                  ? 'The broadcast has no track on air — the library may be empty, or the card may have just left.'
                  : 'The broadcast’s state is only known once the device answers.'}
              </p>
            </CardHeader>
          </Card>
        </div>
      ) : (
        <div className="flex flex-col gap-4 lg:flex-row lg:items-start lg:gap-6">
          <CassetteCase
            cover={cached?.cover ?? null}
            initial={title}
            hue={hueOf(title)}
            art={{ title, artist, meta: album || undefined }}
            size="shelf"
            className="self-center lg:self-start"
          />

          <div className="flex min-w-0 flex-1 flex-col gap-4 sm:gap-6 lg:max-w-[42rem]">
            <figure className="m-0 w-full overflow-hidden rounded-panel border border-border">
              <CassetteScreen
                playing
                elapsedMs={elapsed}
                durationMs={duration}
                trackKey={path}
              />
            </figure>

            <div className="flex min-w-0 flex-col gap-2">
              <h1 className="text-2xl font-light tracking-tight break-words">{title}</h1>
              {subtitle ? (
                <p className="text-sm text-muted-foreground break-words">{subtitle}</p>
              ) : (
                <p className="text-sm text-muted-foreground">
                  The tags carried no artist or album, and the Snapshot has no row for this track.
                </p>
              )}

              {analysis ? (
                <div className="flex flex-wrap items-center gap-1.5">
                  <Badge variant="secondary">
                    {analysis.bpm === null ? 'no BPM' : `${analysis.bpm.toFixed(1)} BPM`}
                  </Badge>
                  <Badge variant="secondary">
                    {analysis.key ?? 'no key'}
                  </Badge>
                  {analysis.energy === null ? null : (
                    <Badge variant="secondary">
                      {analysis.energy}% energy
                    </Badge>
                  )}
                  {analysis.mood.length > 0 ? (
                    <Badge variant="secondary">
                      {analysis.mood.join(', ')}
                    </Badge>
                  ) : null}
                </div>
              ) : null}

              <div className="flex flex-col gap-1 pt-2">
                <Progress value={percent} aria-label="Playback position" />
                <div className="flex items-center justify-between text-xs text-muted-foreground tabular-nums">
                  <span>{formatDuration(elapsed / 1000)}</span>
                  <span>{duration > 0 ? formatDuration(duration / 1000) : '—'}</span>
                </div>
              </div>

              <p className="truncate text-xs text-muted-foreground" title={path}>
                {path}
              </p>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

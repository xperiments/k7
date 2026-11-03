import { useEffect, useState } from 'react';

import { Alert, AlertDescription, AlertTitle } from '@/components/reui/alert';
import { Badge } from '@/components/reui/badge';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Progress } from '@/components/ui/progress';
import { Skeleton } from '@/components/ui/skeleton';
import { libraryStats, statsCoverage, trackAt, type McpStats, type McpTrack } from '../../device/mcp';
import type { DeviceStatus } from '../../device/status';
import { useDeviceStatus } from '../../device/useDeviceStatus';
import { currentCardScope } from '../../card/session';
import { getMedia, type CachedMedia } from '../../storage/media';
import { formatDuration, plural } from '../format';
import { Cover } from './Media';

/**
 * The Player: what the broadcast is carrying, and what the device says about itself.
 *
 * Read-only on purpose. A live stream cannot be paused for one listener, and the only
 * transport verb the page has is `POST /api/play {path}`, which the album rows offer; a
 * next button here would promise a control the device does not have (front door spec §4.5).
 *
 * The joins are the interesting part, and each one degrades to a dash rather than an error:
 *
 *   cover        the `k7-media` cache by the Snapshot path in `/api/status`
 *   name         `playing.title`, then the cache, then the device's row, then the file name
 *   artist/album the cache first, the device's row when the cache has no names
 *   order        `/api/status`'s `order` and `playlist`, when the firmware reports them
 *   coverage     `library_stats`, which is also the only place a Snapshot's age is known
 */

/** The order the broadcast walks, as the device reports it; the page never assumes a list. */
function orderLabel(status: DeviceStatus | null): string {
  if (!status) return '—';
  if (status.order === 'list' && status.playlist?.name.trim()) {
    return `the playlist “${status.playlist.name.trim()}”`;
  }
  return 'the library';
}

/** A file name without its extension, for a track whose tags carried no title. */
function fromPath(path: string): string {
  const name = path.split('/').pop() ?? path;
  return name.replace(/\.[^.]+$/, '');
}

function Fact({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="flex flex-col gap-0.5">
      <dt className="text-xs text-muted-foreground">{label}</dt>
      <dd className="break-words text-sm">{children}</dd>
    </div>
  );
}

function snapshotState(state: string): string {
  switch (state) {
    case 'ok':
      return 'valid';
    case 'no_update':
      return 'none flashed yet';
    case 'no_partition':
      return 'no snapshot partition in this build';
    case 'invalid':
      return 'refused (the image did not validate)';
    default:
      return state;
  }
}

export function PlayerArea() {
  const { status, reachable } = useDeviceStatus();
  const playing = status?.playing ?? null;
  const path = playing?.path?.trim() || null;

  const [cached, setCached] = useState<CachedMedia | null>(null);
  const [measured, setMeasured] = useState<McpTrack | null>(null);
  const [stats, setStats] = useState<McpStats | null>(null);
  const [statsFailed, setStatsFailed] = useState(false);
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

  // Coverage and the Snapshot's age: one call, and a failure is a dash rather than a page.
  const hasStatus = status !== null;
  const generation = status?.generation ?? 0;
  useEffect(() => {
    if (!hasStatus) return;
    let cancelled = false;
    setStatsFailed(false);
    void libraryStats()
      .then((answer) => {
        if (!cancelled) setStats(answer);
      })
      .catch(() => {
        if (cancelled) return;
        setStats(null);
        setStatsFailed(true);
      });
    return () => {
      cancelled = true;
    };
  }, [hasStatus, generation]);

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

  const snapshot = stats?.snapshot ?? null;
  const coverage = statsCoverage(stats);
  const builtAt =
    snapshot && typeof snapshot.generation === 'number' && snapshot.generation > 0
      ? new Date(snapshot.generation * 1000).toLocaleString()
      : null;
  const libraryEmpty = Boolean(stats && !stats.available);

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
        <Card>
          <CardHeader>
            <CardTitle>Nothing is playing</CardTitle>
            <p className="text-sm text-muted-foreground">
              {status
                ? 'The broadcast has no track on air — the library may be empty, or the card may have just left.'
                : 'The broadcast’s state is only known once the device answers.'}
            </p>
          </CardHeader>
        </Card>
      ) : (
        <div className="flex flex-col gap-4 sm:flex-row sm:items-start sm:gap-6">
          <div className="w-56 shrink-0 sm:w-64">
            <Cover cover={cached?.cover ?? null} initial={title} alt={`Cover of ${title}`} />
          </div>

          <div className="flex min-w-0 flex-1 flex-col gap-2">
            <h1 className="text-2xl font-semibold tracking-tight break-words">{title}</h1>
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
                <span>{formatDuration(elapsed)}</span>
                <span>{duration > 0 ? formatDuration(duration) : '—'}</span>
              </div>
            </div>

            <p className="truncate text-xs text-muted-foreground" title={path}>
              {path}
            </p>
          </div>
        </div>
      )}

      <Card>
        <CardHeader>
          <CardTitle>The device</CardTitle>
          <p className="text-sm text-muted-foreground">
            What the firmware reports about the card, the library and the Snapshot it holds.
          </p>
        </CardHeader>
        <CardContent>
          <dl className="grid grid-cols-1 gap-x-6 gap-y-4 sm:grid-cols-2 lg:grid-cols-3">
            <Fact label="Card">
              {status ? (status.card ? 'in the player' : 'out of the player') : '—'}
            </Fact>
            <Fact label="Tracks">
              {status ? (
                <span className="tabular-nums">{plural(status.tracks, 'track')}</span>
              ) : (
                '—'
              )}
            </Fact>
            <Fact label="Library generation">
              {status ? <span className="tabular-nums">{status.generation}</span> : '—'}
            </Fact>
            <Fact label="Order">{orderLabel(status)}</Fact>
            <Fact label="Snapshot">
              {statsFailed ? (
                '—'
              ) : stats === null ? (
                <Skeleton className="h-4 w-32" />
              ) : libraryEmpty ? (
                'the device has no library'
              ) : !snapshot || snapshot.state !== 'ok' ? (
                `no Snapshot (${snapshotState(snapshot?.state ?? 'unknown')})`
              ) : coverage ? (
                <span className="tabular-nums">
                  {coverage.analysed} of {coverage.tracks} analysed ({coverage.percent}%)
                </span>
              ) : (
                '—'
              )}
            </Fact>
            <Fact label="Built">
              {statsFailed ? (
                '—'
              ) : stats === null ? (
                <Skeleton className="h-4 w-32" />
              ) : (
                builtAt ?? '—'
              )}
            </Fact>
          </dl>
        </CardContent>
      </Card>

      <p className="text-xs text-muted-foreground">
        The live stream is shared: every listener hears the same track at the same moment, and this
        page cannot pause it.
      </p>
    </div>
  );
}

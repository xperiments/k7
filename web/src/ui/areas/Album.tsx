import { useEffect, useState } from 'react';
import { ArrowLeft, MoreHorizontal, Play as PlayIcon } from 'lucide-react';

import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
import { Empty, EmptyContent, EmptyDescription, EmptyHeader, EmptyTitle } from '@/components/ui/empty';
import { Separator } from '@/components/ui/separator';
import { Skeleton } from '@/components/ui/skeleton';
import { getDevice } from '../../device/device';
import { trackAt, type McpTrack } from '../../device/mcp';
import { refreshDevice } from '../../device/useDeviceStatus';
import { findAlbum, UNKNOWN_ARTIST, useMediaIndex, type Album } from '../../media/albums';
import { isAbort, searchVideos, VideoError, type Video } from '../../media/youtube';
import { addRows, createDraft, useDrafts } from '../../playlists/drafts';
import { getSettings } from '../../settings';
import type { CachedMedia } from '../../storage/media';
import { formatDuration, plural } from '../format';
import { Cover } from './Media';
import { navigate, routeHref } from '../router';
import { rememberVideo } from './Video';

/**
 * One album: the cache rows it is made of, the measurements the device answers for them,
 * and the three things a row can do -- play it, add it to a Playlist, or look it up.
 *
 * Measurements arrive one `library_track` call per row, in parallel, and are kept in state
 * for the visit. They are deliberately not part of the index: an album's names are this
 * browser's, its numbers are the device's, and the page says "not measured" where the
 * device has no row rather than showing a zero.
 */

/** `play` moves the shared broadcast to one Snapshot path, through whichever link exists. */
async function playTrack(path: string): Promise<void> {
  await getDevice().request('play', { path });
}

function titleOf(row: CachedMedia): string {
  const tagged = row.title.trim();
  if (tagged) return tagged;
  const name = row.path.split('/').pop() ?? row.path;
  return name.replace(/\.[^.]+$/, '');
}

/** The sentence to search YouTube with: the artist if it is known, then the title. */
function queryFor(row: CachedMedia, album: Album): string {
  const title = titleOf(row);
  const artist = album.artist === UNKNOWN_ARTIST ? '' : album.artist;
  const singer = row.artist.trim() || artist;
  return [singer, title].filter(Boolean).join(' ');
}

type Notice = { kind: 'ok' | 'error'; text: string };

function TrackMeasurement({ track }: { track: McpTrack | null | undefined }) {
  if (track === undefined) {
    return (
      <span className="flex items-center gap-2">
        <Skeleton className="h-4 w-12" />
        <Skeleton className="h-4 w-8" />
      </span>
    );
  }
  if (track === null || track.analysis === null) {
    return <span className="text-xs text-muted-foreground">not measured</span>;
  }
  return (
    <span className="flex items-center gap-3 text-sm tabular-nums">
      <span>{track.analysis.bpm === null ? '—' : `${track.analysis.bpm.toFixed(1)} BPM`}</span>
      <span className="text-muted-foreground">{track.analysis.key ?? '—'}</span>
    </span>
  );
}

function LookupPanel({ lookup, onClose }: { lookup: { query: string; title: string }; onClose: () => void }) {
  const [videos, setVideos] = useState<Video[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [searching, setSearching] = useState(true);

  useEffect(() => {
    const controller = new AbortController();
    setVideos(null);
    setError(null);
    setSearching(true);
    void searchVideos(lookup.query, getSettings().invidious, controller.signal)
      .then((found) => {
        if (controller.signal.aborted) return;
        setVideos(found);
      })
      .catch((failure: unknown) => {
        if (controller.signal.aborted || isAbort(failure)) return;
        setError(failure instanceof VideoError ? failure.message : String(failure));
      })
      .finally(() => {
        if (!controller.signal.aborted) setSearching(false);
      });
    return () => controller.abort();
  }, [lookup]);

  return (
    <Card size="sm">
      <CardHeader>
        <CardTitle>On YouTube for “{lookup.title}”</CardTitle>
        <p className="text-sm text-muted-foreground">
          Searched on the Invidious instance in Settings. Nothing here is downloaded and nothing
          joins a playlist.
        </p>
      </CardHeader>
      <CardContent className="flex flex-col gap-3">
        {searching ? (
          <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-3">
            {Array.from({ length: 3 }, (_, index) => (
              <Skeleton key={index} className="h-40 w-full" />
            ))}
          </div>
        ) : error ? (
          <div className="flex flex-col gap-2">
            <p className="text-sm text-destructive">{error}</p>
            <div>
              <Button
                size="sm"
                variant="outline"
                onClick={() => navigate(routeHref({ name: 'settings' }))}
              >
                Check the instance in Settings
              </Button>
            </div>
          </div>
        ) : videos && videos.length === 0 ? (
          <p className="text-sm text-muted-foreground">The instance found nothing for that query.</p>
        ) : (
          <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-3">
            {(videos ?? []).map((video) => (
              <a
                key={video.id}
                href={routeHref({ name: 'video', id: video.id })}
                onClick={() => rememberVideo(video)}
                className="flex flex-col gap-1.5 outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-inset"
              >
                <img
                  src={video.thumbnail}
                  alt=""
                  className="aspect-video w-full bg-muted object-cover"
                />
                <span className="line-clamp-2 text-sm font-medium">{video.title}</span>
                <span className="truncate text-xs text-muted-foreground">
                  {video.channel}
                  {video.duration > 0 ? ` · ${formatDuration(video.duration)}` : ''}
                </span>
              </a>
            ))}
          </div>
        )}
        <div>
          <Button size="sm" variant="ghost" onClick={onClose}>
            Close
          </Button>
        </div>
      </CardContent>
    </Card>
  );
}

function TrackRow({
  row,
  track,
  onPlay,
  onLookup,
  onAdd,
  onNew,
}: {
  row: CachedMedia;
  track: McpTrack | null | undefined;
  onPlay: () => void;
  onLookup: () => void;
  onAdd: (draftId: string, draftName: string) => void;
  onNew: () => void;
}) {
  const drafts = useDrafts();
  const title = titleOf(row);

  return (
    <li className="flex flex-col gap-2 border-b py-2 last:border-b-0 sm:flex-row sm:items-center sm:justify-between">
      <div className="flex min-w-0 flex-col gap-0.5">
        <span className="truncate text-sm font-medium">{title}</span>
        <span className="truncate text-xs text-muted-foreground">{row.path}</span>
      </div>
      <div className="flex shrink-0 items-center gap-2">
        <TrackMeasurement track={track} />
        <Button variant="outline" size="sm" onClick={onPlay}>
          <PlayIcon aria-hidden="true" />
          Play
        </Button>
        <DropdownMenu>
          <DropdownMenuTrigger
            render={<Button variant="ghost" size="icon-sm" aria-label={`Actions for ${title}`} />}
          >
            <MoreHorizontal aria-hidden="true" />
          </DropdownMenuTrigger>
          <DropdownMenuContent align="end" className="w-56">
            <DropdownMenuGroup>
              <DropdownMenuItem onClick={onPlay}>Play now</DropdownMenuItem>
              <DropdownMenuItem onClick={onLookup}>Look up on YouTube</DropdownMenuItem>
            </DropdownMenuGroup>
            <DropdownMenuSeparator />
            <DropdownMenuGroup>
              <DropdownMenuLabel>Add to playlist</DropdownMenuLabel>
              {drafts.map((draft) => (
                <DropdownMenuItem key={draft.id} onClick={() => onAdd(draft.id, draft.name)}>
                  <span className="truncate">Add to “{draft.name}”</span>
                </DropdownMenuItem>
              ))}
              <DropdownMenuItem onClick={onNew}>New playlist…</DropdownMenuItem>
            </DropdownMenuGroup>
          </DropdownMenuContent>
        </DropdownMenu>
      </div>
    </li>
  );
}

export function AlbumArea({ artist, album }: { artist: string; album: string }) {
  const state = useMediaIndex();
  const found = findAlbum(state.index, artist, album);
  const [measurements, setMeasurements] = useState<Map<string, McpTrack | null> | null>(null);
  const [lookup, setLookup] = useState<{ query: string; title: string } | null>(null);
  const [notice, setNotice] = useState<Notice | null>(null);

  useEffect(() => {
    if (!found) return;
    let cancelled = false;
    setMeasurements(null);
    void Promise.all(
      found.rows.map(async (row) => {
        try {
          return [row.path, await trackAt(row.path)] as const;
        } catch {
          // A row the device cannot answer for is "not measured", not an error page.
          return [row.path, null] as const;
        }
      }),
    ).then((entries) => {
      if (!cancelled) setMeasurements(new Map(entries));
    });
    return () => {
      cancelled = true;
    };
  }, [found]);

  const play = async (row: CachedMedia) => {
    try {
      await playTrack(row.path);
      await refreshDevice();
      setNotice({ kind: 'ok', text: `Playing “${titleOf(row)}”.` });
    } catch (error) {
      setNotice({
        kind: 'error',
        text: `Could not play it: ${error instanceof Error ? error.message : String(error)}`,
      });
    }
  };

  const addTo = (draftId: string, draftName: string, row: CachedMedia) => {
    const draft = addRows(draftId, [row.path]);
    setNotice(
      draft
        ? { kind: 'ok', text: `Added “${titleOf(row)}” to “${draftName}”.` }
        : { kind: 'error', text: 'That playlist is no longer here.' },
    );
  };

  const addToNew = (row: CachedMedia) => {
    const draft = createDraft(found?.album ?? 'New playlist', [row.path]);
    setNotice({ kind: 'ok', text: `Added “${titleOf(row)}” to a new playlist, “${draft.name}”.` });
  };

  if (state.loading) {
    return (
      <div className="flex flex-col gap-4 p-4 sm:p-6">
        <Skeleton className="h-8 w-40" />
        <Skeleton className="h-48 w-48" />
      </div>
    );
  }

  if (!found) {
    return (
      <div className="p-4 sm:p-6">
        <Empty className="border">
          <EmptyHeader>
            <EmptyTitle>This album is not in this browser</EmptyTitle>
            <EmptyDescription>
              The album index is built from the tags this browser read. If the card was measured
              somewhere else, measure it here once.
            </EmptyDescription>
          </EmptyHeader>
          <EmptyContent>
            <Button size="sm" onClick={() => navigate(routeHref({ name: 'media' }))}>
              Back to Media
            </Button>
          </EmptyContent>
        </Empty>
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-4 p-4 sm:gap-6 sm:p-6">
      <div>
        <Button
          variant="ghost"
          size="sm"
          nativeButton={false}
          render={<a href={routeHref({ name: 'media' })} />}
        >
          <ArrowLeft aria-hidden="true" />
          Media
        </Button>
      </div>

      <header className="flex flex-col gap-4 sm:flex-row sm:items-start">
        <div className="w-40 shrink-0 sm:w-48">
          <Cover cover={found.cover} initial={found.album} />
        </div>
        <div className="flex min-w-0 flex-col gap-1">
          <h1 className="text-xl font-semibold tracking-tight">{found.album}</h1>
          <a
            href={routeHref({ name: 'artist', artist: found.artist })}
            className="w-fit text-sm text-muted-foreground underline-offset-4 transition-colors hover:text-foreground hover:underline focus-visible:ring-2 focus-visible:ring-ring focus-visible:outline-none"
          >
            {found.artist}
          </a>
          <span className="text-xs text-muted-foreground tabular-nums">
            {plural(found.tracks, 'track')} · {found.rows[0]?.genre || 'no genre tag'}
          </span>
        </div>
      </header>

      {notice ? (
        <div role="status" className="flex items-center gap-2 text-sm">
          <Badge variant={notice.kind === 'ok' ? 'success-light' : 'destructive-light'}>
            {notice.kind === 'ok' ? 'done' : 'failed'}
          </Badge>
          <span className="min-w-0 break-words">{notice.text}</span>
        </div>
      ) : null}

      {lookup ? <LookupPanel lookup={lookup} onClose={() => setLookup(null)} /> : null}

      <Card>
        <CardHeader>
          <CardTitle>Tracks</CardTitle>
          <p className="text-sm text-muted-foreground">
            Tempo and key are the device&apos;s measurements; a dash means the Snapshot has no row
            for that track.
          </p>
        </CardHeader>
        <CardContent>
          <ul className="flex flex-col">
            {found.rows.map((row) => (
              <TrackRow
                key={row.path}
                row={row}
                track={measurements === null ? undefined : (measurements.get(row.path) ?? null)}
                onPlay={() => void play(row)}
                onLookup={() => setLookup({ query: queryFor(row, found), title: titleOf(row) })}
                onAdd={(draftId, draftName) => addTo(draftId, draftName, row)}
                onNew={() => addToNew(row)}
              />
            ))}
          </ul>
        </CardContent>
      </Card>

      <Separator />
      <p className="text-xs text-muted-foreground">
        Playing moves the live stream for everyone listening; it is not a private queue.
      </p>
    </div>
  );
}

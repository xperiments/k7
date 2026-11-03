import { useEffect, useState } from 'react';
import { ArrowLeft, MoreHorizontal, Play as PlayIcon } from 'lucide-react';

import { Alert, AlertDescription, AlertTitle } from '@/components/reui/alert';
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
import { findAlbum, useMediaIndex, type Album, type MediaIndexState } from '../../media/albums';
import { useCardScope } from '../../card/session';
import {
  imageRowKey,
  readImageMeasurements,
  type ImageMeasurement,
} from '../../domain/snapshot';
import { loadSnapshot } from '../../storage/snapshots';
import { isAbort, searchVideos, VideoError, type Video } from '../../media/youtube';
import { addRows, createDraft, useDrafts } from '../../playlists/drafts';
import { getSettings } from '../../settings';
import type { CachedMedia } from '../../storage/media';
import { formatDuration, plural } from '../format';
import { Cover } from './Media';
import { navigate, routeHref } from '../router';
import { rememberVideo } from './Video';
import { usePlayback } from '../playback';
import { useAnalysis } from '../state/analysis';

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

/** The sentence to search YouTube with: the row's artist, then the title. */
function queryFor(row: CachedMedia, album: Album): string {
  const title = titleOf(row);
  const singer = row.artist.trim() || (album.artists.length === 1 ? album.artists[0] : '');
  return [singer, title].filter(Boolean).join(' ');
}

type Notice = { kind: 'ok' | 'error'; text: string };

/** The image this browser wrote for the card being browsed, as far as the device is told. */
interface CardImage {
  at: string;
  tracks: number;
}

function when(iso: string): string {
  const date = new Date(iso);
  return Number.isNaN(date.getTime()) ? iso : date.toLocaleString();
}

/**
 * A whole album of "not in the image", explained as one fact.
 *
 * The player keeps the last image it applied, and it applies one when a card carrying `k7.db`
 * arrives -- not when the page writes it. So a card measured since then is answered from an
 * older image, and every track of it looks unmeasured even though the browser measured it.
 * The page used to say "not measured" for all of these, which reads as "you have not done
 * this"; the sentence now says which image answered, and what makes it answer from this one.
 */
function imageProblem(state: MediaIndexState, card: CardImage | null): string {
  const image = state.deviceImage;
  if (image === null) {
    return 'The player did not say which image it is answering from. Its measurements come from the image it last applied, so this album cannot be checked against it.';
  }

  const built = when(image.at);
  const sameImage =
    card !== null &&
    card.tracks === image.rows &&
    Math.abs(Date.parse(card.at) - Date.parse(image.at)) < 120_000;

  if (sameImage) {
    return `The player is using this card's image (${plural(image.rows, 'row')}, built ${built}) and it has no row for any track here. The files may have changed since it was written: measure the card again in Process.`;
  }

  const written =
    card === null
      ? 'This browser has no image for this card. '
      : `This browser wrote this card's image ${when(card.at)}, with ${plural(card.tracks, 'track')}. `;

  return `The player answers from an image built ${built}, with ${plural(image.rows, 'row')} — not this card's. ${written}The player applies a card's image when the card arrives: put the card in and it will answer from its own.`;
}

/** The album's genre when every row agrees, and a word for what it is when they do not. */
function genreText(album: Album): string {
  const genres = new Set(album.rows.map((row) => row.genre.trim()).filter(Boolean));
  if (genres.size === 0) return 'no genre tag';
  return genres.size === 1 ? [...genres][0] : 'mixed genres';
}

/**
 * One track's measurements, and the difference between two silences the page used to blur:
 * `null` is the device not answering for the track at all (the request failed), while a
 * track document whose `analysis` is null is an answer -- this file exists and the image the
 * player is holding has no row for it. They mean different things and lead to different
 * acts, so they are not one word.
 */
function TrackMeasurement({
  track,
  image,
}: {
  track: McpTrack | null | undefined;
  /** The row this browser wrote for the card, used when the player cannot answer. */
  image?: ImageMeasurement | null;
}) {
  if (track === undefined) {
    return (
      <span className="flex items-center gap-2">
        <Skeleton className="h-4 w-12" />
        <Skeleton className="h-4 w-8" />
      </span>
    );
  }

  /* The device first: with the card in it, its answer is what the player will play. */
  if (track !== null && track.analysis !== null) {
    return (
      <span className="flex items-center gap-3 text-sm tabular-nums">
        <span>{track.analysis.bpm === null ? '—' : `${track.analysis.bpm.toFixed(1)} BPM`}</span>
        <span className="text-muted-foreground">{track.analysis.key ?? '—'}</span>
      </span>
    );
  }

  /* The card is in the computer, or its image has not been applied: the browser wrote the
   * image, so it can read the row back rather than leaving the album unmeasured. */
  if (image) {
    return (
      <span
        className="flex items-center gap-3 text-sm tabular-nums"
        title="From the image this browser wrote for the card; the player did not answer for it"
      >
        <span>{image.bpm === null ? '—' : `${image.bpm.toFixed(1)} BPM`}</span>
        <span className="text-muted-foreground">{image.key ?? '—'}</span>
      </span>
    );
  }

  if (track === null) {
    return (
      <span className="text-xs text-muted-foreground" title="The device did not answer for this track">
        no answer
      </span>
    );
  }
  return (
    <span className="text-xs text-muted-foreground" title="The player's image has no row for this track">
      not in the image
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
          Searched on the Invidious instance in Settings. A tile plays here; nothing is
          downloaded and nothing joins a playlist.
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
                  loading="lazy"
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
  image,
  showArtist,
  onPlay,
  onLookup,
  onAdd,
  onNew,
}: {
  row: CachedMedia;
  track: McpTrack | null | undefined;
  /** This browser's row for the track, when the player cannot answer for it. */
  image: ImageMeasurement | null;
  /** True on a compilation, where the album's header cannot say whose track this is. */
  showArtist: boolean;
  onPlay: () => void;
  onLookup: () => void;
  onAdd: (draftId: string, draftName: string) => void;
  onNew: () => void;
}) {
  const drafts = useDrafts();
  const title = titleOf(row);
  const label = showArtist ? `${row.artist} - ${title}` : title;

  return (
    <li className="flex flex-col gap-2 border-b py-2 last:border-b-0 sm:flex-row sm:items-center sm:justify-between">
      <div className="flex min-w-0 flex-col gap-0.5">
        <span className="truncate text-sm font-medium">{label}</span>
        <span className="truncate text-xs text-muted-foreground">{row.path}</span>
      </div>
      <div className="flex shrink-0 items-center gap-2">
        <TrackMeasurement track={track} image={image} />
        <Button variant="outline" size="sm" onClick={onPlay}>
          <PlayIcon aria-hidden="true" />
          Play
        </Button>
        <DropdownMenu>
          <DropdownMenuTrigger
            render={<Button variant="ghost" size="icon-sm" aria-label={`Actions for ${label}`} />}
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

export function AlbumArea({ album }: { album: string }) {
  const state = useMediaIndex();
  const found = findAlbum(state.index, album);
  const playback = usePlayback();
  const { readCardFile } = useAnalysis();
  const scope = useCardScope();
  const [cardImage, setCardImage] = useState<CardImage | null>(null);
  const [imageRows, setImageRows] = useState<Map<string, ImageMeasurement> | null>(null);
  const [measurements, setMeasurements] = useState<Map<string, McpTrack | null> | null>(null);
  const [lookup, setLookup] = useState<{ query: string; title: string } | null>(null);
  const [notice, setNotice] = useState<Notice | null>(null);

  /* The image this browser wrote for this card, so the page can say whether the player is
   * answering from it. Stored per Scope, so it is the right card's even after a swap. */
  useEffect(() => {
    let cancelled = false;
    loadSnapshot(scope)
      .then((stored) => {
        if (cancelled) return;
        setCardImage(stored ? { at: stored.at, tracks: stored.tracks } : null);
        try {
          setImageRows(stored ? readImageMeasurements(stored.bytes) : null);
        } catch {
          // An image this build cannot read is no fallback: the device's answer stands alone.
          setImageRows(null);
        }
      })
      .catch(() => {
        if (cancelled) return;
        setCardImage(null);
        setImageRows(null);
      });
    return () => {
      cancelled = true;
    };
  }, [scope]);

  useEffect(() => {
    if (!found) return;
    // No card in the player, no library: asking for every row would be a dozen refusals for
    // an answer already known. The browser's own image answers instead.
    if (!state.card) {
      setMeasurements(new Map(found.rows.map((row) => [row.path, null] as const)));
      return;
    }
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
  }, [found, state.card]);

  /* The row's numbers, as this browser wrote them, when the player cannot give them back. */
  const imageOf = (row: CachedMedia): ImageMeasurement | null =>
    imageRows?.get(imageRowKey(row.path, row.size ?? -1)) ?? null;

  /* Two silences, counted rather than repeated per row: no answer at all (the device, or the
   * card, is not there) and an answer with no row (the image is not this album's). */
  const answers = measurements === null ? null : [...measurements.values()];
  const answered = (answers ?? []).filter((track) => track !== null);
  const noRow = answered.filter((track) => track.analysis === null);
  const nothingAnswered = answers !== null && answers.length > 0 && answered.length === 0;
  const nothingInTheImage = answered.length > 0 && noRow.length === answered.length;
  // Every row the player could not answer for is one this browser can: the album is not lost.
  const allFromImage =
    found !== null && found.rows.length > 0 && found.rows.every((row) => imageOf(row) !== null);

  /* Two kinds of play. With the card in the player, the player plays it and the bar follows
   * its broadcast. With the card in the computer, the player has nothing to play — so the
   * album is handed to the bar, in its own order, and each track is read off the card as the
   * play reaches it: that is what makes the bar's next and previous mean something. */
  const play = async (row: CachedMedia) => {
    if (!state.card) {
      const tracks = (found?.rows ?? []).map((entry) => ({
        path: entry.path,
        title: titleOf(entry),
        artist: entry.artist,
        cover: found?.cover ?? null,
      }));
      const start = tracks.findIndex((track) => track.path === row.path);
      const played = await playback.playAlbum(tracks, Math.max(0, start), readCardFile);
      if (!played) {
        setNotice({
          kind: 'error',
          text: 'This browser could not read that file from the card. Play again to allow access, or open the card in Process.',
        });
        return;
      }
      setNotice({ kind: 'ok', text: `Playing “${titleOf(row)}” from the card.` });
      return;
    }

    try {
      await playTrack(row.path);
      await refreshDevice();
      playback.playStream();
      setNotice({ kind: 'ok', text: `Playing “${titleOf(row)}” on the player.` });
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
          {found.artists.length === 1 ? (
            <a
              href={routeHref({ name: 'artist', artist: found.artists[0] })}
              className="w-fit text-sm text-muted-foreground underline-offset-4 transition-colors hover:text-foreground hover:underline focus-visible:ring-2 focus-visible:ring-ring focus-visible:outline-none"
            >
              {found.artists[0]}
            </a>
          ) : (
            <span className="w-fit text-sm text-muted-foreground">
              {plural(found.artists.length, 'artist')} · each track names its own
            </span>
          )}
          <span className="text-xs text-muted-foreground tabular-nums">
            {plural(found.tracks, 'track')} · {genreText(found)}
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

      {(nothingAnswered || nothingInTheImage) && allFromImage ? (
        <Alert>
          <AlertTitle>
            {nothingAnswered
              ? 'The player is not answering for this album'
              : 'The player has not applied this card’s image'}
          </AlertTitle>
          <AlertDescription>
            {nothingAnswered
              ? 'The card is not in the player, so it cannot answer for these files. The numbers below are the ones from the image this browser wrote for the card — the same rows the player uses once the card is in it.'
              : `${imageProblem(state, cardImage)} The numbers below come from the image this browser wrote.`}
          </AlertDescription>
        </Alert>
      ) : nothingAnswered ? (
        <Alert variant="warning">
          <AlertTitle>The device did not answer for any track here</AlertTitle>
          <AlertDescription>
            No path on this album answered, so nothing here can be checked against the player.
            The device may have no card in it, or these files may no longer be on it.
          </AlertDescription>
        </Alert>
      ) : nothingInTheImage ? (
        <Alert variant="warning">
          <AlertTitle>No track here is in the player&apos;s image</AlertTitle>
          <AlertDescription>{imageProblem(state, cardImage)}</AlertDescription>
        </Alert>
      ) : null}

      <Card>
        <CardHeader>
          <CardTitle>Tracks</CardTitle>
          <p className="text-sm text-muted-foreground">
            Tempo and key are the device&apos;s measurements. <em>Not in the image</em> means the
            player&apos;s Snapshot has no row for the track; <em>no answer</em> means the device did
            not reply for it at all.
          </p>
        </CardHeader>
        <CardContent>
          <ul className="flex flex-col">
            {found.rows.map((row) => (
              <TrackRow
                key={row.path}
                row={row}
                showArtist={found.artists.length > 1}
                image={imageOf(row)}
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

import { useEffect, useState, useSyncExternalStore } from 'react';
import { ListPlus, MoreHorizontal, Play as PlayIcon } from 'lucide-react';

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
import { Skeleton } from '@/components/ui/skeleton';
import { getDevice } from '../device/device';
import { trackAt, type McpTrack } from '../device/mcp';
import { refreshDevice } from '../device/useDeviceStatus';
import {
  UNKNOWN_ALBUM,
  UNKNOWN_ARTIST,
  type Album,
  type MediaIndexState,
} from '../media/albums';
import { useCardScope } from '../card/session';
import {
  imageRowKey,
  readImageMeasurements,
  type ImageMeasurement,
} from '../domain/snapshot';
import { loadSnapshot } from '../storage/snapshots';
import { isAbort, searchVideos, VideoError, type Video } from '../media/youtube';
import { addRows, createDraft, useDrafts } from '../playlists/drafts';
import { getSettings, subscribeSettings } from '../settings';
import type { CachedMedia } from '../storage/media';
import { formatDuration } from './format';
import { navigate, routeHref } from './router';
import { rememberVideo } from './areas/Video';
import { usePlayback } from './playback';
import { useAnalysis } from './state/analysis';

/**
 * One album: the cache rows it is made of, the measurements the device answers for them,
 * and the two things a row can do -- play it, or add it to a Playlist. The video search is
 * the album's own: when the Invidious instance in Settings answers, its results sit after
 * the tracks, and the row menu no longer offers a second way to the same thing.
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

/**
 * The sentence the album's own search uses: the artist when it has one name, then the title.
 * An album with no tags at all has nothing worth asking an index about, so it asks nothing.
 */
function albumQueryFor(album: Album): string | null {
  if (album.album === UNKNOWN_ALBUM) return null;
  const singer = album.artists.length === 1 && album.artists[0] !== UNKNOWN_ARTIST
    ? album.artists[0]
    : '';
  return [singer, album.album].filter(Boolean).join(' ');
}

type Notice = { kind: 'ok' | 'error'; text: string };

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

/**
 * An instance that has not answered in this long is treated as not answering: the album page
 * then keeps its row menu, and a hand-run lookup says why instead of sitting on skeletons.
 */
const VIDEO_SEARCH_TIMEOUT_MS = 10_000;

/** What one video search is doing, so the panel and the page can ask the same question. */
interface VideoSearch {
  videos: Video[] | null;
  error: string | null;
  searching: boolean;
}

/**
 * One Invidious search, or none when there is no query.
 *
 * The instance is read when the search starts, so a page opened after Settings changed
 * searches where the settings now say; the abort is what keeps a fast album switch from
 * letting an old answer land under a new album's name.
 */
function useVideoSearch(query: string | null): VideoSearch {
  const [videos, setVideos] = useState<Video[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [searching, setSearching] = useState(false);

  useEffect(() => {
    if (query === null) {
      setVideos(null);
      setError(null);
      setSearching(false);
      return;
    }
    const controller = new AbortController();
    let timedOut = false;
    const timer = window.setTimeout(() => {
      timedOut = true;
      controller.abort();
    }, VIDEO_SEARCH_TIMEOUT_MS);
    setVideos(null);
    setError(null);
    setSearching(true);
    void searchVideos(query, getSettings().invidious, controller.signal)
      .then((album) => {
        if (controller.signal.aborted) return;
        setVideos(album);
      })
      .catch((failure: unknown) => {
        // A timeout is an answer of sorts; the page's own abort is not.
        if (timedOut) {
          setError('the instance did not answer in time');
          return;
        }
        if (controller.signal.aborted || isAbort(failure)) return;
        setError(failure instanceof VideoError ? failure.message : String(failure));
      })
      .finally(() => {
        window.clearTimeout(timer);
        if (!controller.signal.aborted || timedOut) setSearching(false);
      });
    return () => {
      window.clearTimeout(timer);
      controller.abort();
    };
  }, [query]);

  return { videos, error, searching };
}

/** The search's answer: skeletons, an error with the way to Settings, the tiles, or nothing album. */
function LookupResults({
  title,
  search,
  onClose,
}: {
  title: string;
  search: VideoSearch;
  /** Present only for a lookup the page opened by hand: an automatic one has nothing to close. */
  onClose?: () => void;
}) {
  const { videos, error, searching } = search;

  return (
    <Card size="sm">
      <CardHeader>
        <CardTitle>On YouTube for “{title}”</CardTitle>
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
          <p className="text-sm text-muted-foreground">The instance album nothing for that query.</p>
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
        {onClose ? (
          <div>
            <Button size="sm" variant="ghost" onClick={onClose}>
              Close
            </Button>
          </div>
        ) : null}
      </CardContent>
    </Card>
  );
}

/** A lookup the reader asked for from a row, when the album's own search is not there. */
function LookupPanel({
  lookup,
  onClose,
}: {
  lookup: { query: string; title: string };
  onClose: () => void;
}) {
  const search = useVideoSearch(lookup.query);
  return <LookupResults title={lookup.title} search={search} onClose={onClose} />;
}

function TrackRow({
  row,
  track,
  image,
  showArtist,
  action,
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
  /**
   * What the row's one button does. With the card in the player there is nothing to read
   * here: the press **queues** the track on the player, which starts it at once — the sound
   * arrives through the stream, a moment later, not in this page's player.
   */
  action: 'play' | 'queue';
  onPlay: () => void;
  /** Absent when the album's own search already fills the page: one door, not two. */
  onLookup?: () => void;
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
        {/* <span className="truncate text-xs text-muted-foreground">{row.path}</span> */}
      </div>
      <div className="flex shrink-0 items-center gap-2">
        {/* One width for the numbers, right-aligned: "128.0 BPM 8A" and "no answer" are not
            the same length, and the buttons after them must not move row to row — nor the
            room a song's name has to be truncated in. */}
        <div className="flex w-24 shrink-0 items-center justify-end">
          <TrackMeasurement track={track} image={image} />
        </div>
        <Button variant="outline" size="sm" onClick={onPlay}>
          {action === 'queue' ? (
            <ListPlus aria-hidden="true" />
          ) : (
            <PlayIcon aria-hidden="true" />
          )}
          {action === 'queue' ? 'Queue' : 'Play'}
        </Button>
        <DropdownMenu>
          <DropdownMenuTrigger
            render={<Button variant="ghost" size="icon-sm" aria-label={`Actions for ${label}`} />}
          >
            <MoreHorizontal aria-hidden="true" />
          </DropdownMenuTrigger>
          <DropdownMenuContent align="end" className="w-56">
            <DropdownMenuGroup>
              <DropdownMenuItem onClick={onPlay}>
                {action === 'queue' ? 'Queue on the player' : 'Play now'}
              </DropdownMenuItem>
              {onLookup ? (
                <DropdownMenuItem onClick={onLookup}>Look up on YouTube</DropdownMenuItem>
              ) : null}
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

export function AlbumDetail({ album, state }: { album: Album; state: MediaIndexState }) {
  const playback = usePlayback();
  const { readCardFile } = useAnalysis();
  const scope = useCardScope();
  const [imageRows, setImageRows] = useState<Map<string, ImageMeasurement> | null>(null);
  const [measurements, setMeasurements] = useState<Map<string, McpTrack | null> | null>(null);
  const [lookup, setLookup] = useState<{ query: string; title: string } | null>(null);
  const [notice, setNotice] = useState<Notice | null>(null);

  /* The album's own place on YouTube, searched only when Settings names an instance. An
   * instance that answers - even with nothing - is what replaces the row menu's lookup: the
   * section is already on the page, so the menu would be a second door to the same room. An
   * instance that does not answer changes nothing, and the row keeps its own way in. */
  const settings = useSyncExternalStore(subscribeSettings, getSettings, getSettings);
  const albumQuery =
    album !== null && settings.invidious.trim() !== '' ? albumQueryFor(album) : null;
  const albumSearch = useVideoSearch(albumQuery);
  /* Only a search with something in it replaces the row menu: an album's query can come back
   * empty while a track's finds the song, so an empty answer keeps both doors. */
  const videosHere = (albumSearch.videos?.length ?? 0) > 0;

  useEffect(() => {
    if (videosHere) setLookup(null);
  }, [videosHere]);

  /* The image this browser wrote for this card, so the page can say whether the player is
   * answering from it. Stored per Scope, so it is the right card's even after a swap. */
  useEffect(() => {
    let cancelled = false;
    loadSnapshot(scope)
      .then((stored) => {
        if (cancelled) return;
        try {
          setImageRows(stored ? readImageMeasurements(stored.bytes) : null);
        } catch {
          // An image this build cannot read is no fallback: the device's answer stands alone.
          setImageRows(null);
        }
      })
      .catch(() => {
        if (cancelled) return;
        setImageRows(null);
      });
    return () => {
      cancelled = true;
    };
  }, [scope]);

  useEffect(() => {
    if (!album) return;
    // No card in the player, no library: asking for every row would be a dozen refusals for
    // an answer already known. The browser's own image answers instead.
    if (!state.card) {
      setMeasurements(new Map(album.rows.map((row) => [row.path, null] as const)));
      return;
    }
    let cancelled = false;
    setMeasurements(null);
    void Promise.all(
      album.rows.map(async (row) => {
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
  }, [album, state.card]);

  /* The row's numbers, as this browser wrote them, when the player cannot give them back. */
  const imageOf = (row: CachedMedia): ImageMeasurement | null =>
    imageRows?.get(imageRowKey(row.path, row.size ?? -1)) ?? null;

  /* Two kinds of play. With the card in the player, the player plays it and the bar follows
   * its broadcast. With the card in the computer, the player has nothing to play — so the
   * album is handed to the bar, in its own order, and each track is read off the card as the
   * play reaches it: that is what makes the bar's next and previous mean something. */
  const play = async (row: CachedMedia) => {
    if (!state.card) {
      const tracks = (album?.rows ?? []).map((entry) => ({
        path: entry.path,
        title: titleOf(entry),
        artist: entry.artist,
        cover: album?.cover ?? null,
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
      setNotice({ kind: 'ok', text: `Queued “${titleOf(row)}” on the player.` });
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
    const draft = createDraft(album.album ?? 'New playlist', [row.path]);
    setNotice({ kind: 'ok', text: `Added “${titleOf(row)}” to a new playlist, “${draft.name}”.` });
  };

  return (
    <div className="flex flex-col gap-4">
        <div className="flex min-w-0 flex-col gap-4">
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
            <CardContent>
              <ul className="flex flex-col">
                {album.rows.map((row) => (
                  <TrackRow
                    key={row.path}
                    row={row}
                    showArtist={album.artists.length > 1}
                    action={state.card ? 'queue' : 'play'}
                    image={imageOf(row)}
                    track={measurements === null ? undefined : (measurements.get(row.path) ?? null)}
                    onPlay={() => void play(row)}
                    onLookup={
                      videosHere
                        ? undefined
                        : () => setLookup({ query: queryFor(row, album), title: titleOf(row) })
                    }
                    onAdd={(draftId, draftName) => addTo(draftId, draftName, row)}
                    onNew={() => addToNew(row)}
                  />
                ))}
              </ul>
            </CardContent>
          </Card>
{/* 
          {videosHere ? (
            <LookupResults title={album.album} search={albumSearch} />
          ) : null} */}

          
      </div>
    </div>
  );
}

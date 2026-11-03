import { useEffect, useId, useMemo, useState } from 'react';
import { cn } from 'cn';
import { Disc3 } from 'lucide-react';

import { Alert, AlertDescription, AlertTitle } from '@/components/reui/alert';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import {
  Empty,
  EmptyContent,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from '@/components/ui/empty';
import { Input } from '@/components/ui/input';
import { Skeleton } from '@/components/ui/skeleton';
import { useMediaIndex, type Album, type MediaIndexState } from '../../media/albums';
import type { CachedCover } from '../../storage/media';
import { plural } from '../format';
import { navigate, routeHref } from '../router';

/**
 * The Media area: the albums this browser read from the card, and the way into one album's
 * tracks. There is no artist list beside it: an artist is the name on an album, and its page
 * is reached from the album it made.
 *
 * The rule the index is built on is in `src/media/albums.ts`: names come from the cache,
 * existence from the device, and the tracks the device lists without a cache row are shown
 * as **not measured** and sent to Process rather than invented. That is why this area can be
 * empty on a browser that never had the card, and why it can still be useful with the device
 * off.
 */

/** The cover, as a blob URL made once per image and released when the tile goes away. */
export function Cover({
  cover,
  initial,
  alt = '',
  className,
}: {
  cover: CachedCover | null;
  /** What the placeholder tile shows when there is no picture. */
  initial: string;
  alt?: string;
  className?: string;
}) {
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

  if (!cover || !url) {
    return (
      <div
        aria-hidden="true"
        className={cn(
          'flex aspect-square w-full items-center justify-center bg-muted text-2xl font-semibold text-muted-foreground',
          className,
        )}
      >
        {(initial.trim().slice(0, 1) || '?').toUpperCase()}
      </div>
    );
  }

  return <img src={url} alt={alt} className={cn('aspect-square w-full object-cover', className)} />;
}

/** One album in a grid: the picture, its name, who made it, and how many tracks. */
export function AlbumCard({ album }: { album: Album }) {
  return (
    <Card size="sm">
      <a
        href={routeHref({ name: 'album', album: album.album })}
        className="flex flex-col gap-2 outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-inset"
      >
        <Cover cover={album.cover} initial={album.album} />
        <span className="flex min-w-0 flex-col gap-0.5">
          <span className="truncate text-sm font-medium">{album.album}</span>
          <span className="truncate text-xs text-muted-foreground">
            {album.artists.length === 1
              ? album.artists[0]
              : plural(album.artists.length, 'artist')}
          </span>
          <span className="text-xs text-muted-foreground tabular-nums">
            {plural(album.tracks, 'track')}
          </span>
        </span>
      </a>
    </Card>
  );
}

/** The grid an album list uses, so Media and an artist's page agree on the shape. */
export function AlbumGrid({ albums }: { albums: Album[] }) {
  return (
    <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-3 2xl:grid-cols-4">
      {albums.map((album) => (
        <AlbumCard key={album.album} album={album} />
      ))}
    </div>
  );
}

function NoAlbums({ reachable }: { reachable: boolean }) {
  return (
    <Empty className="border">
      <EmptyHeader>
        <EmptyMedia variant="icon">
          <Disc3 aria-hidden="true" />
        </EmptyMedia>
        <EmptyTitle>No albums here yet</EmptyTitle>
        <EmptyDescription>
          {reachable
            ? 'Measure a card in this browser and its albums appear here. The device lists the tracks; this browser remembers the names and covers.'
            : 'The device is not answering, and this browser has not measured a card. Put the card in the computer and measure it once.'}
        </EmptyDescription>
      </EmptyHeader>
      <EmptyContent>
        <Button size="sm" onClick={() => navigate(routeHref({ name: 'process' }))}>
          Open Process
        </Button>
      </EmptyContent>
    </Empty>
  );
}

/** The tracks the device lists that no cache row covers: a few names, a count, and Process. */
function NotMeasured({ state }: { state: MediaIndexState }) {
  const index = state.index;
  if (!index || index.unmeasured.length === 0) return null;

  const shown = index.unmeasured.slice(0, 40);
  const listed = index.presence?.tracks.length ?? 0;
  const partial = !(index.presence?.complete ?? false);

  return (
    <Card size="sm">
      <CardHeader>
        <CardTitle>Not measured</CardTitle>
        <p className="text-sm text-muted-foreground">
          {plural(index.unmeasured.length, 'track')} the device lists that this browser has no tags
          for. They have no album here because nothing was read from them.
        </p>
      </CardHeader>
      <CardContent className="flex flex-col gap-3">
        <ul className="flex flex-col gap-1 text-sm">
          {shown.map((track) => (
            <li key={track.path} className="truncate text-muted-foreground" title={track.path}>
              {track.name}
            </li>
          ))}
          {index.unmeasured.length > shown.length ? (
            <li className="text-xs text-muted-foreground">
              and {plural(index.unmeasured.length - shown.length, 'more track')}
            </li>
          ) : null}
        </ul>
        {partial ? (
          <p className="text-xs text-muted-foreground">
            The device lists at most 200 tracks at a time, so this only covers the{' '}
            {plural(listed, 'track')} it named.
          </p>
        ) : null}
        <div>
          <Button
            size="sm"
            variant="outline"
            onClick={() => navigate(routeHref({ name: 'process' }))}
          >
            Measure them in Process
          </Button>
        </div>
      </CardContent>
    </Card>
  );
}

export function MediaArea() {
  const state = useMediaIndex();
  const [query, setQuery] = useState('');
  const searchId = useId();

  const needle = query.trim().toLowerCase();
  const albums = useMemo(
    () =>
      (state.index?.albums ?? []).filter(
        (album) =>
          !needle ||
          album.album.toLowerCase().includes(needle) ||
          album.artists.some((artist) => artist.toLowerCase().includes(needle)),
      ),
    [state.index, needle],
  );

  const hasAlbums = (state.index?.albums.length ?? 0) > 0;
  const nothingMatches = state.index !== null && needle.length > 0 && albums.length === 0;

  return (
    <div className="flex flex-col gap-4 p-4 sm:gap-6 sm:p-6">
      <header className="flex flex-col gap-0.5">
        <h1 className="text-xl font-semibold tracking-tight">Albums</h1>
        <p className="text-sm text-muted-foreground">
          What this browser read from the card.
        </p>
      </header>

      {!state.reachable && !state.loading ? (
        <Alert variant="warning">
          <AlertTitle>The device is not answering</AlertTitle>
          <AlertDescription>
            Showing what this browser measured. Nothing is being marked as gone while the device
            is away.
          </AlertDescription>
        </Alert>
      ) : null}

      <div className="flex flex-col gap-1">
        <label htmlFor={searchId} className="sr-only">
          Search albums
        </label>
        <Input
          id={searchId}
          type="search"
          value={query}
          placeholder="Search albums"
          autoComplete="off"
          onChange={(event) => setQuery(event.target.value)}
        />
      </div>

      {state.loading ? (
        <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-3 2xl:grid-cols-4">
          {Array.from({ length: 8 }, (_, index) => (
            <Skeleton key={index} className="h-48 w-full" />
          ))}
        </div>
      ) : !hasAlbums ? (
        <NoAlbums reachable={state.reachable} />
      ) : nothingMatches ? (
        <Empty className="border">
          <EmptyHeader>
            <EmptyTitle>Nothing matches “{query.trim()}”</EmptyTitle>
            <EmptyDescription>Album names and artists are searched.</EmptyDescription>
          </EmptyHeader>
          <EmptyContent>
            <Button size="sm" variant="outline" onClick={() => setQuery('')}>
              Clear the search
            </Button>
          </EmptyContent>
        </Empty>
      ) : (
        <AlbumGrid albums={albums} />
      )}

      <NotMeasured state={state} />
    </div>
  );
}

import { useEffect, useId, useMemo, useRef, useState } from 'react';
import { Disc3, FolderOpen, LayoutGrid } from 'lucide-react';

import { Button } from '@/components/ui/button';
import { Card } from '@/components/ui/card';
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
import { AlbumDetail } from '../album-detail';
import { AlbumShelf } from '../album-shelf';
import { Cover } from '../cover';
import { DriveView } from '../drive';
import { plural } from '../format';
import { navigate, routeHref } from '../router';

/**
 * The Media area: the albums this browser read from the card, and the way into one album's
 * tracks. There is no artist list beside it: an artist is the name on an album, and its page
 * is reached from the album it made.
 *
 * The area has two views of the same rows, and the switch between them is a pair of routes,
 * not a hidden mode: **Carousel** is the shelf an album's tracks hang under, and **Drive**
 * is the card's folders as a tree. Both are built from the cache in `media/albums.ts` and
 * `media/folders.ts`: names come from this browser, existence from the device, and the tracks
 * the device lists without a cache row are shown as **not measured** and sent to Process
 * rather than invented. That is why this area can be empty on a browser that never had the
 * card, and why it can still be useful with the device off.
 */

/** One album in a grid: the picture, its name, who made it, and how many tracks. */
export function AlbumCard({ album }: { album: Album }) {
  return (
    <Card size="sm">
      <a
        href={routeHref({ name: 'album', album: album.album })}
        className="flex flex-col gap-2 outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-inset"
      >
        <Cover cover={album.cover} initial={album.album} className="aspect-[16/25]" />
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

/**
 * The grid an album list uses, so Media and an artist's page agree on the shape.
 *
 * A cassette stands on its short end: 100 mm of tape, 64 mm tall, so a cover is **16:25** —
 * taller than wide, the way a case sits in a shelf. The grid is denser than a square grid of
 * the same page because each tile is narrower than it is tall.
 */
export const ALBUM_GRID = 'grid grid-cols-2 gap-3 sm:grid-cols-3 lg:grid-cols-4 xl:grid-cols-5 2xl:grid-cols-6';

export function AlbumGrid({ albums }: { albums: Album[] }) {
  return (
    <div className={ALBUM_GRID}>
      {albums.map((album) => (
        <AlbumCard key={album.album} album={album} />
      ))}
    </div>
  );
}

/**
 * The two views, as links: the Carousel is `#/media`, the Drive is `#/media/drive`. Links
 * rather than tabs because each view is a place — the back button walks between them and an
 * address can be shared — and `aria-current` is what tells a reader which one is open.
 */
function ViewSwitch({ view }: { view: 'carousel' | 'drive' }) {
  return (
    <nav aria-label="Media view" className="flex items-center gap-1">
      <Button
        variant={view === 'carousel' ? 'secondary' : 'ghost'}
        size="sm"
        nativeButton={false}
        aria-current={view === 'carousel' ? 'page' : undefined}
        render={<a href={routeHref({ name: 'media' })} />}
      >
        <LayoutGrid aria-hidden="true" />
        Carousel
      </Button>
      <Button
        variant={view === 'drive' ? 'secondary' : 'ghost'}
        size="sm"
        nativeButton={false}
        aria-current={view === 'drive' ? 'page' : undefined}
        render={<a href={routeHref({ name: 'drive', path: '' })} />}
      >
        <FolderOpen aria-hidden="true" />
        Drive
      </Button>
    </nav>
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

/**
 * The shelf and the album under it. It owns which case is turned, because that is the shelf's
 * fact and not the page's; the search is the page's, so both views read the same box.
 */
function ShelfView({
  state,
  album,
  query,
  onClear,
}: {
  state: MediaIndexState;
  /** The album the route names, when the address came from a link to one. */
  album?: string;
  query: string;
  onClear: () => void;
}) {
  const [selected, setSelected] = useState(0);
  /* The route's album is applied once, when it arrives: a search that keeps it visible must
   * not drag the selection back to it after the reader has moved on. */
  const applied = useRef<string | null>(null);

  const needle = query.trim().toLowerCase();
  const albums = useMemo(
    () =>
      (state.index?.albums ?? []).filter(
        (entry) =>
          !needle ||
          entry.album.toLowerCase().includes(needle) ||
          entry.artists.some((artist) => artist.toLowerCase().includes(needle)),
      ),
    [state.index, needle],
  );

  /* A route that names an album turns it; a search that hides the selection falls back to
   * the first case rather than to a case that is no longer on the shelf. Leaving the album
   * route clears the marker, so a later link to the same album turns it again rather than
   * being taken for the turn already done. */
  useEffect(() => {
    if (album === undefined) {
      applied.current = null;
      return;
    }
    if (applied.current === album) return;
    const at = albums.findIndex((entry) => entry.album === album);
    if (at < 0) return;
    applied.current = album;
    setSelected(at);
  }, [album, albums]);

  useEffect(() => {
    setSelected((at) => Math.min(at, Math.max(0, albums.length - 1)));
  }, [albums]);

  const active = albums.length > 0 ? albums[Math.min(selected, albums.length - 1)] : null;

  const hasAlbums = (state.index?.albums.length ?? 0) > 0;
  const nothingMatches = state.index !== null && needle.length > 0 && albums.length === 0;

  return (
    /* Two columns from `lg`: the shelf on the left, the tracks on the right. The shelf
       keeps its own height and never scrolls; the right column is the only scroll region
       on the page, so the case stays in view while its tracks go past. Below `lg` the
       shelf goes back on top and the tracks keep the remaining height. */
    <div className="flex min-h-0 flex-1 flex-col gap-4 lg:grid lg:grid-cols-2 lg:gap-6">
      {!state.loading && hasAlbums && !nothingMatches && active ? (
        <aside className="min-h-0 lg:self-center">
          <AlbumShelf albums={albums} selected={selected} onSelect={setSelected} />
        </aside>
      ) : null}

      {/* The column fills its half and the list floats in the middle of it. The margins —
          not flex centring — are what places it, because auto margins collapse to nothing
          when the content is taller than the column, where centring would clip the first
          row. */}
      <div className="flex min-h-0 flex-1 flex-col overflow-auto">
        <div className="m-auto flex w-full flex-col gap-4">
          {state.loading ? (
            <div className="flex justify-center py-6">
              <Skeleton className="aspect-[16/25] w-40" />
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
                <Button size="sm" variant="outline" onClick={onClear}>
                  Clear the search
                </Button>
              </EmptyContent>
            </Empty>
          ) : active ? (
            <AlbumDetail album={active} state={state} />
          ) : null}
        </div>
      </div>
    </div>
  );
}

export function MediaArea({ album, drive }: { album?: string; drive?: string } = {}) {
  const state = useMediaIndex();
  return <MediaView state={state} album={album} drive={drive} />;
}

/**
 * The area's rendering, given the index. Keeping the fetch above it means a view can be
 * handed a tree without a card, a player or a store: the preview harness does exactly that,
 * and so would a test of the two views.
 */
export function MediaView({
  state,
  album,
  drive,
}: {
  state: MediaIndexState;
  album?: string;
  drive?: string;
}) {
  const readingDrive = drive !== undefined;
  const [query, setQuery] = useState('');
  const searchId = useId();

  /* A search is about what is on screen: another view, or another folder a link names,
   * starts clean rather than carrying a word that matched a case into a folder that has
   * never heard of it. `drive` is `undefined` on the Carousel and a path on the Drive, so
   * this covers both the switch and a deep link. */
  useEffect(() => {
    setQuery('');
  }, [drive]);

  const placeholder = readingDrive ? 'Search the drive' : 'Search albums';

  return (
    /* The shelf keeps its own scroll region so the case stays in view; the Drive is a page
       that scrolls in the shell, because a folder list has no fixed thing above it to keep. */
    <div
      className={
        readingDrive
          ? 'mx-auto flex w-full max-w-5xl flex-col gap-4 px-4 py-6 sm:px-6'
          : 'flex h-full min-h-0 flex-col gap-3 p-4 sm:gap-4 sm:p-6'
      }
    >
      <header className="flex flex-wrap items-center justify-between gap-x-6 gap-y-3">
        <h1 className="text-xl font-semibold tracking-tight">Media</h1>
        {/* The view and the search are the header's two controls: beside each other when
            there is room for them, stacked under the title when there is not. */}
        <div className="flex w-full flex-col gap-2 sm:w-auto sm:flex-row sm:items-center sm:gap-3">
          <ViewSwitch view={readingDrive ? 'drive' : 'carousel'} />
          <div className="flex w-full flex-col gap-1 sm:w-64">
            <label htmlFor={searchId} className="sr-only">
              {placeholder}
            </label>
            <Input
              id={searchId}
              type="search"
              value={query}
              placeholder={placeholder}
              autoComplete="off"
              onChange={(event) => setQuery(event.target.value)}
            />
          </div>
        </div>
      </header>

      {readingDrive ? (
        <DriveView state={state} path={drive} query={query} />
      ) : (
        <ShelfView state={state} album={album} query={query} onClear={() => setQuery('')} />
      )}
    </div>
  );
}

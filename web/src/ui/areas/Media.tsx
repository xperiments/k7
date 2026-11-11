import { useEffect, useId, useMemo, useRef, useState } from 'react';
import { Disc3, FolderOpen, LayoutGrid } from 'lucide-react';
import { cn } from 'cn';

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
import {
  artistOf,
  byArtistThenAlbum,
  letterOf,
  useMediaIndex,
  type Album,
  type MediaIndexState,
} from '../../media/albums';
import { AlbumDetail } from '../album-detail';
import { AlbumShelf, AlphabetIndex } from '../album-shelf';
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
        variant={view === 'carousel' ? 'solid' : 'default'}
        size="sm"
        nativeButton={false}
        aria-current={view === 'carousel' ? 'page' : undefined}
        render={<a href={routeHref({ name: 'media' })} />}
      >
        <LayoutGrid aria-hidden="true" />
        Carousel
      </Button>
      <Button
        variant={view === 'drive' ? 'solid' : 'default'}
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
        <Button variant="solid" size="sm" onClick={() => navigate(routeHref({ name: 'process' }))}>
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
  albums,
  selected,
  onSelect,
  active,
  query,
  onClear,
}: {
  state: MediaIndexState;
  albums: Album[];
  selected: number;
  onSelect: (index: number) => void;
  active: Album | null;
  query: string;
  onClear: () => void;
}) {
  const [open, setOpen] = useState(false);

  /* The tracks are the scroll region and they belong to the turned case, so a jump along the
   * row — a letter, a case, an arrow — shows the new album from its top. */
  const tracks = useRef<HTMLDivElement>(null);
  const shelf = useRef<HTMLElement>(null);
  useEffect(() => {
    tracks.current?.scrollTo({ top: 0 });
  }, [active?.album]);

  /* The tracks are a side panel, not a route: a pointer outside the shelf or the panel
   * closes them, and Escape does the same from the keyboard. */
  useEffect(() => {
    if (!open) return;
    const onPointerDown = (event: PointerEvent) => {
      const target = event.target as Node;
      if (shelf.current?.contains(target) || tracks.current?.contains(target)) return;
      setOpen(false);
    };
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') setOpen(false);
    };
    document.addEventListener('pointerdown', onPointerDown, true);
    document.addEventListener('keydown', onKeyDown);
    return () => {
      document.removeEventListener('pointerdown', onPointerDown, true);
      document.removeEventListener('keydown', onKeyDown);
    };
  }, [open]);

  const hasAlbums = (state.index?.albums.length ?? 0) > 0;
  const nothingMatches = state.index !== null && query.trim().length > 0 && albums.length === 0;
  const shelfVisible = !state.loading && hasAlbums && !nothingMatches && active !== null;

  /* The shelf is the page: full width, floating in the middle of the stage. A case click
     opens its tracks to one side, and the arrows keep walking the row with the panel open.
     The loading, empty and no-match states are the page's, not the panel's, so they show
     whether or not it is. */
  const notice = state.loading ? (
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
  ) : null;

  return (
    <div
      className={cn(
        'flex min-h-0 flex-1 flex-col gap-4',
        open && shelfVisible && 'lg:grid lg:grid-cols-2 lg:gap-6',
      )}
    >
      {shelfVisible ? (
        <aside ref={shelf} className="flex min-h-0 flex-1 flex-col justify-center">
          <AlbumShelf
            albums={albums}
            selected={selected}
            onSelect={onSelect}
            onOpen={() => setOpen(true)}
          />
        </aside>
      ) : null}

      {shelfVisible ? (
        open ? (
          <div ref={tracks} className="flex min-h-0 flex-1 flex-col overflow-auto">
            <div className="m-auto flex w-full flex-col gap-4">
              <AlbumDetail album={active!} state={state} />
            </div>
          </div>
        ) : null
      ) : (
        <div className="flex min-h-0 flex-1 flex-col overflow-auto">
          <div className="m-auto flex w-full flex-col gap-4">{notice}</div>
        </div>
      )}
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
/** The view switch and the search, shared by the two views so they agree on the box. */
function MediaHeader({
  readingDrive,
  query,
  onQuery,
  searchId,
}: {
  readingDrive: boolean;
  query: string;
  onQuery: (value: string) => void;
  searchId: string;
}) {
  const placeholder = readingDrive ? 'Search the drive' : 'Search albums';
  return (
    <header className="flex flex-wrap items-center justify-end gap-x-6 gap-y-3">
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
            onChange={(event) => onQuery(event.target.value)}
          />
        </div>
      </div>
    </header>
  );
}

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

  const needle = query.trim().toLowerCase();
  /* The Carousel walks artists, not album titles: the abecedary is the artists' initials,
   * and a letter only leads somewhere if the rows are grouped by who made them. The title
   * is the tie-break inside one name. */
  const albums = useMemo(
    () =>
      (state.index?.albums ?? [])
        .filter(
          (entry) =>
            !needle ||
            entry.album.toLowerCase().includes(needle) ||
            entry.artists.some((artist) => artist.toLowerCase().includes(needle)),
        )
        .sort(byArtistThenAlbum),
    [state.index, needle],
  );

  const [selected, setSelected] = useState(0);
  /* The route's album is applied once, when it arrives: a search that keeps it visible must
   * not drag the selection back to it after the reader has moved on. */
  const applied = useRef<string | null>(null);
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

  /* A letter does not teleport the row: the selection walks every case to the artist's
   * first album. The walk takes longer the further the letter is — one second for a step
   * within a letter, three for a crossing of the alphabet — so the step is the duration
   * divided by the cases. Any direct choice stops it. */
  const walk = useRef<number | null>(null);
  const stopWalk = () => {
    if (walk.current !== null) {
      window.clearInterval(walk.current);
      walk.current = null;
    }
  };
  useEffect(() => stopWalk, []);
  const selectNow = (index: number) => {
    stopWalk();
    setSelected(index);
  };
  const jumpToLetter = (target: number) => {
    stopWalk();
    let at = selected;
    if (at === target || albums.length === 0) return;
    const direction = target > at ? 1 : -1;
    const from = letterOf(artistOf(albums[at])).charCodeAt(0);
    const to = letterOf(artistOf(albums[target])).charCodeAt(0);
    const letters = Math.min(26, Math.abs(to - from));
    const duration = Math.min(3000, Math.max(1000, 1000 + (letters / 26) * 2000));
    const steps = Math.max(1, Math.abs(target - at));
    const step = Math.max(4, Math.round(duration / steps));
    walk.current = window.setInterval(() => {
      if (at === target) {
        stopWalk();
        return;
      }
      const next = at + direction;
      if (next < 0 || next >= albums.length) {
        stopWalk();
        return;
      }
      at = next;
      setSelected(at);
    }, step);
  };

  if (readingDrive) {
    return (
      <div className="mx-auto flex w-full flex-col gap-4 px-4 py-6 sm:px-6">
        <MediaHeader readingDrive query={query} onQuery={setQuery} searchId={searchId} />
        <DriveView state={state} path={drive} query={query} />
      </div>
    );
  }

  /* The rail is the stage's right edge, with the page's header and shelf to its left. */
  return (
    <div className="flex h-full min-h-0 gap-3 p-4 sm:gap-4 sm:p-6">
      <div className="flex min-h-0 flex-1 flex-col gap-3">
        <MediaHeader readingDrive={false} query={query} onQuery={setQuery} searchId={searchId} />
        <ShelfView
          state={state}
          albums={albums}
          selected={selected}
          onSelect={selectNow}
          active={active}
          query={query}
          onClear={() => setQuery('')}
        />
      </div>
      <AlphabetIndex
        albums={albums}
        selected={selected}
        onSelect={selectNow}
        onJump={jumpToLetter}
      />
    </div>
  );
}

import { ArrowLeft } from 'lucide-react';

import { Button } from '@/components/ui/button';
import { Empty, EmptyContent, EmptyDescription, EmptyHeader, EmptyTitle } from '@/components/ui/empty';
import { Skeleton } from '@/components/ui/skeleton';
import { albumsOf, UNKNOWN_ARTIST, useMediaIndex } from '../../media/albums';
import { plural } from '../format';
import { AlbumGrid } from './Media';
import { navigate, routeHref } from '../router';

/**
 * One artist's albums: the same index the Media grid uses, filtered by the name its route
 * carries. Nothing is fetched here -- an artist exists because cache rows said so.
 */
export function ArtistArea({ artist }: { artist: string }) {
  const state = useMediaIndex();
  const albums = albumsOf(state.index, artist);
  const tracks = albums.reduce((total, album) => total + album.tracks, 0);
  const known = artist !== UNKNOWN_ARTIST;

  if (state.loading) {
    return (
      <div className="flex flex-col gap-4 p-4 sm:p-6">
        <Skeleton className="h-8 w-40" />
        <div className="grid grid-cols-1 gap-3 sm:grid-cols-2 lg:grid-cols-3 2xl:grid-cols-4">
          {Array.from({ length: 4 }, (_, index) => (
            <Skeleton key={index} className="h-48 w-full" />
          ))}
        </div>
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

      <header className="flex flex-col gap-0.5">
        <h1 className="text-xl font-semibold tracking-tight">{artist}</h1>
        <p className="text-sm text-muted-foreground">
          {known
            ? `${plural(albums.length, 'album')} · ${plural(tracks, 'track')}`
            : 'Tracks whose tags carried no artist. The card still holds them; nothing here knows who made them.'}
        </p>
      </header>

      {albums.length === 0 ? (
        <Empty className="border">
          <EmptyHeader>
            <EmptyTitle>No albums under this name</EmptyTitle>
            <EmptyDescription>
              The album index is built from the tags this browser read. A track whose artist tag
              changed since then will not be here.
            </EmptyDescription>
          </EmptyHeader>
          <EmptyContent>
            <Button size="sm" onClick={() => navigate(routeHref({ name: 'media' }))}>
              Back to Media
            </Button>
          </EmptyContent>
        </Empty>
      ) : (
        <AlbumGrid albums={albums} />
      )}
    </div>
  );
}

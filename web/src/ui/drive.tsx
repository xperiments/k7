import { useMemo, useState } from 'react';
import { Disc3 } from 'lucide-react';

import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import {
  Empty,
  EmptyContent,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from '@/components/ui/empty';
import { Skeleton } from '@/components/ui/skeleton';
import { getDevice } from '../device/device';
import { refreshDevice } from '../device/useDeviceStatus';
import type { MediaIndexState } from '../media/albums';
import {
  buildFolderTree,
  type Folder as FolderNode,
  type FolderTrack,
} from '../media/folders';
import { FileTree } from './file-tree';
import { usePlayback } from './playback';
import { navigate, routeHref } from './router';
import { useAnalysis } from './state/analysis';

/**
 * Media's second view: the card's own folders, browsed the way a drive is.
 *
 * The album shelf asks what the tags say; this asks what the card holds. They are different
 * shapes of the same rows — an album can span two folders and a folder can hold two albums —
 * so this view is not a re-sort of the shelf, but the tree `media/folders.ts` builds from the
 * same cache plus the device's listing. `file-tree.tsx` draws it with ReUI's Tree.
 *
 * A folder row opens in place and a file row plays. Play is the only action here on purpose:
 * the album page is where a track is added to a Playlist, and a Drive that repeated every
 * menu would be two ways to the same work. Measured files are played with their folder as
 * the album, so next and previous mean the file's neighbours on the card, measured or not.
 */

/** What one play did, for the line above the tree. */
type Notice = { kind: 'ok' | 'error'; text: string };

/** `play` moves the shared broadcast to one Snapshot path, through whichever link exists. */
async function playOnPlayer(path: string): Promise<void> {
  await getDevice().request('play', { path });
}

/** The cover a file is played with: its own, or the one its folder found. */
function coverFor(track: FolderTrack, folder: FolderNode) {
  return track.row?.cover ?? folder.cover;
}

function DriveSkeleton() {
  const indents = ['ps-0', 'ps-5', 'ps-10'];

  return (
    <Card size="sm">
      <CardContent className="flex flex-col gap-2">
        {Array.from({ length: 6 }, (_, index) => (
          <div
            key={index}
            className={`flex items-center gap-3 py-2 ${indents[index % indents.length]}`}
          >
            <Skeleton className="size-4 shrink-0" />
            <Skeleton className="h-4 w-2/5" />
          </div>
        ))}
      </CardContent>
    </Card>
  );
}

function NoLibrary({ reachable }: { reachable: boolean }) {
  return (
    <Empty className="border">
      <EmptyHeader>
        <EmptyMedia variant="icon">
          <Disc3 aria-hidden="true" />
        </EmptyMedia>
        <EmptyTitle>No folders here yet</EmptyTitle>
        <EmptyDescription>
          {reachable
            ? 'Measure a card in this browser and its folders appear here, in the shape the card holds them.'
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

export function DriveView({
  state,
  path,
  query,
}: {
  state: MediaIndexState;
  /** The folder the route names, opened in the tree. `''` is the whole drive. */
  path: string;
  query: string;
}) {
  const playback = usePlayback();
  const { readCardFile } = useAnalysis();
  const [notice, setNotice] = useState<Notice | null>(null);

  /* The same rows the shelf is made of, read as a tree. The cache holds the names and the
   * covers; the listing holds the files this browser has not read. */
  const tree = useMemo(
    () =>
      state.index
        ? buildFolderTree(
            state.index.albums.flatMap((album) => album.rows),
            state.index.unmeasured,
            state.index.unmeasurable,
          )
        : null,
    [state.index],
  );

  if (state.loading) return <DriveSkeleton />;
  if (!tree) return null;

  /* An empty card is the empty state, not a folder that happens to be empty: the root has
   * nothing and there is nothing below it either. */
  if (tree.folders.length === 0 && tree.tracks.length === 0) {
    return <NoLibrary reachable={state.reachable} />;
  }

  /* Play one file. With a card in the player the player takes it and the bar follows the
   * broadcast; with the card in the computer the folder is handed to the bar, so next means
   * the next file on the card. */
  const play = async (track: FolderTrack, home: FolderNode): Promise<void> => {
    if (!state.card) {
      const context = home.tracks.map((entry) => ({
        path: entry.path,
        title: entry.title,
        artist: entry.row?.artist ?? '',
        cover: coverFor(entry, home),
      }));
      const start = home.tracks.findIndex((entry) => entry.path === track.path);
      const played = await playback.playAlbum(context, Math.max(0, start), readCardFile);
      if (!played) {
        setNotice({
          kind: 'error',
          text: 'This browser could not read that file from the card. Play again to allow access, or open the card in Process.',
        });
        return;
      }
      setNotice({ kind: 'ok', text: `Playing “${track.title}” from the card.` });
      return;
    }

    try {
      await playOnPlayer(track.path);
      await refreshDevice();
      playback.playStream();
      setNotice({ kind: 'ok', text: `Queued “${track.title}” on the player.` });
    } catch (error) {
      setNotice({
        kind: 'error',
        text: `Could not play it: ${error instanceof Error ? error.message : String(error)}`,
      });
    }
  };

  return (
    <div className="flex flex-col gap-4">
      {notice ? (
        <div role="status" className="flex items-center gap-2 text-sm">
          <Badge variant={notice.kind === 'ok' ? 'success-light' : 'destructive-light'}>
            {notice.kind === 'ok' ? 'done' : 'failed'}
          </Badge>
          <span className="min-w-0 break-words">{notice.text}</span>
        </div>
      ) : null}

      <FileTree
        key={`${path}\u0000${query}`}
        tree={tree}
        query={query}
        path={path}
        action={state.card ? 'queue' : 'play'}
        onActivate={(track, home) => void play(track, home)}
      />
    </div>
  );
}

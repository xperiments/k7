import { useEffect, useMemo, useRef } from 'react';
import {
  FileMusic,
  Folder as FolderIcon,
  FolderOpen,
  ListPlus,
  Play as PlayIcon,
  Plus,
} from 'lucide-react';
import {
  expandAllFeature,
  hotkeysCoreFeature,
  syncDataLoaderFeature,
} from '@headless-tree/core';
import { useTree } from '@headless-tree/react';

import { Tree, TreeItem, TreeItemLabel } from '@/components/reui/tree';
import { Button } from '@/components/ui/button';
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyTitle,
} from '@/components/ui/empty';
import {
  filterFolderTree,
  folderCounts,
  locateFolder,
  MUSIC_ROOT,
  type Folder as FolderNode,
  type FolderTrack,
} from '../media/folders';
import { plural } from './format';
import { beginFolderDrag, beginTrackDrag } from './track-drag';

/**
 * The Drive view's tree: the card's folders and the files under them, built on ReUI's Tree
 * and headless-tree. One tree replaces the walk through one folder at a time — a folder row
 * opens and closes in place, and a file row plays when it is pressed.
 *
 * The tree is a *view* of `media/folders.ts`, not a second model: the same nodes the old
 * breadcrumb walked are flattened into the loader's record here, so a folder's count, its
 * files and its identity are the ones the tree builder already settled.
 */

/** The one record the data loader hands out, for a folder or a file. */
interface TreeEntry {
  id: string;
  name: string;
  kind: 'folder' | 'track';
  children?: string[];
  /** The folder this node came from, for a folder row's count and a file row's context. */
  folder?: FolderNode;
  track?: FolderTrack;
  /** The folder that directly holds a track: its album for playback, and the cover fallback. */
  home?: FolderNode;
}

/** The root id is never drawn; its children are the top level of the visible tree. */
const ROOT_ID = 'root';

/** Folders and files share path strings, so the id carries which one a row is. */
const folderId = (path: string): string => `f:${path}`;
const trackId = (path: string): string => `t:${path}`;

const EMPTY_TREE: FolderNode = {
  path: '',
  name: MUSIC_ROOT,
  folders: [],
  tracks: [],
  tracksBelow: 0,
  cover: null,
};

/** Flatten a folder tree into the record the headless loader walks. */
function buildEntries(root: FolderNode): Record<string, TreeEntry> {
  const entries: Record<string, TreeEntry> = {};

  const walk = (folder: FolderNode): string[] => {
    const children: string[] = [];
    // Folders first, then files: the order the old list used, and the order a tree reads in.
    for (const child of folder.folders) {
      const id = folderId(child.path);
      const entry: TreeEntry = {
        id,
        name: child.name,
        kind: 'folder',
        folder: child,
        children: [],
      };
      entries[id] = entry;
      entry.children = walk(child);
      children.push(id);
    }
    for (const track of folder.tracks) {
      const id = trackId(track.path);
      entries[id] = {
        id,
        name: track.title,
        kind: 'track',
        track,
        home: folder,
      };
      children.push(id);
    }
    return children;
  };

  entries[ROOT_ID] = {
    id: ROOT_ID,
    name: root.name,
    kind: 'folder',
    folder: root,
    children: walk(root),
  };
  return entries;
}

/** The folders a deep link opens on the way to the folder it names. */
function expandedForPath(root: FolderNode, path: string): string[] {
  const found = locateFolder(root, path);
  if (found === null) return [];
  return found.trail.slice(1).map((folder) => folderId(folder.path));
}

/** Every folder in a tree, for a search that shows its matches open. */
function allFolderIds(folder: FolderNode): string[] {
  const ids: string[] = [];
  for (const child of folder.folders) {
    ids.push(folderId(child.path), ...allFolderIds(child));
  }
  return ids;
}

/** The line after a file's name: who and what, or that the browser has not measured it. */
function metaOf(track: FolderTrack): string {
  if (track.kind === 'unmeasured') return 'not measured';
  if (track.kind === 'unmeasurable') return 'not measured here';
  const tagged = [track.row?.artist.trim(), track.row?.album.trim()].filter(Boolean).join(' · ');
  return tagged || track.name;
}

/** What pressing a file's row does. A source tree adds; the Drive plays or queues. */
export type FileTreeAction = 'play' | 'queue' | 'add';

export function FileTree({
  tree: root,
  query,
  path,
  action,
  onActivate,
  draggableSources = false,
}: {
  tree: FolderNode;
  /** The header's search term: the whole drive is filtered, folders leading to a file kept. */
  query: string;
  /** The folder a `#/media/drive/…` link names, opened on arrival. */
  path: string;
  /** What a file row does when it is pressed. */
  action: FileTreeAction;
  onActivate: (track: FolderTrack, home: FolderNode) => void;
  /** A source tree's folders and files can be dragged into a playlist. */
  draggableSources?: boolean;
}) {
  /* Searching prunes the tree instead of hiding rows: a visible folder never claims a file
     that is not under it, and a matching album can show its own files. */
  const filtered = useMemo(
    () => (query.trim().length > 0 ? filterFolderTree(root, query) : root),
    [root, query],
  );
  const data = filtered ?? EMPTY_TREE;

  const entries = useMemo(() => buildEntries(data), [data]);
  const counts = useMemo(() => folderCounts(data), [data]);
  /* A drag ends with a click on some browsers; that click must not also add the file. */
  const dragging = useRef(false);

  /* The rows to open on the tree's first render. A search opens the folders it kept so
     matches are visible; otherwise a deep link opens the way down to its folder. The view
     remounts us when either changes (the `key` in `DriveView`), which is what makes this
     initial state the current one rather than only the first. */
  const initialExpanded = useMemo(
    () => (query.trim().length > 0 ? allFolderIds(data) : expandedForPath(root, path)),
    [data, root, path, query],
  );

  const instance = useTree<TreeEntry>({
    rootItemId: ROOT_ID,
    indent: 20,
    initialState: { expandedItems: initialExpanded },
    getItemName: (item) => {
      const entry = item.getItemData();
      // A source row's action is not visible to a screen reader in the row's text, so the
      // accessible name says what pressing it does; the drawn name is still the file's.
      return action === 'add' && entry.kind === 'track'
        ? `${entry.name} — add to playlist`
        : entry.name;
    },
    isItemFolder: (item) => item.getItemData().kind === 'folder',
    onPrimaryAction: (item) => {
      if (dragging.current) return;
      const entry = item.getItemData();
      if (entry.kind === 'track' && entry.track !== undefined && entry.home !== undefined) {
        onActivate(entry.track, entry.home);
      }
    },
    dataLoader: {
      getItem: (itemId) => entries[itemId],
      getChildren: (itemId) => entries[itemId]?.children ?? [],
    },
    features: [syncDataLoaderFeature, hotkeysCoreFeature, expandAllFeature],
  });

  /* The data changes when the card or the drive does; the tree's structure cache has to be
     rebuilt before the new rows can be drawn. */
  useEffect(() => {
    instance.rebuildTree();
  }, [instance, entries]);

  if (filtered === null) {
    return (
      <Empty className="border">
        <EmptyHeader>
          <EmptyTitle>Nothing matches “{query.trim()}”</EmptyTitle>
          <EmptyDescription>
            Folder names, titles, artists and albums in the drive are searched.
          </EmptyDescription>
        </EmptyHeader>
      </Empty>
    );
  }

  if (data.folders.length === 0 && data.tracks.length === 0) return null;

  const summary = [
    counts.folders > 0 ? plural(counts.folders, 'folder') : null,
    plural(counts.tracks, 'track'),
  ]
    .filter((part): part is string => part !== null)
    .join(' · ');

  const verb =
    action === 'add' ? 'Add to playlist' : action === 'queue' ? 'Queue on the player' : 'Play';

  return (
    <div className="flex flex-col gap-3">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <div className="flex items-baseline gap-2">
          <span className="text-sm font-medium">{data.name}</span>
          <span className="text-xs text-muted-foreground tabular-nums">{summary}</span>
        </div>
        <div className="flex items-center gap-1">
          <Button variant="ghost" size="sm" onClick={() => void instance.expandAll()}>
            Expand all
          </Button>
          <Button variant="ghost" size="sm" onClick={() => instance.collapseAll()}>
            Collapse all
          </Button>
        </div>
      </div>

      {/* One gradient paints the indent guides; each label masks it under its own row, so
          the lines show only in the gutter the indentation opens. */}
      <Tree
        indent={20}
        tree={instance}
        className="relative before:absolute before:inset-0 before:-ms-1 before:bg-[repeating-linear-gradient(to_right,transparent_0,transparent_calc(var(--tree-indent)-1px),var(--border)_calc(var(--tree-indent)-1px),var(--border)_calc(var(--tree-indent)))]"
      >
        {instance.getItems().map((item) => {
          const entry = item.getItemData();
          const folder = entry.folder;
          const track = entry.track;

          return (
            <TreeItem
              key={item.getId()}
              type="button"
              item={item}
              title={
                track !== undefined
                  ? `${verb} — ${track.path}`
                  : action === 'add' && folder !== undefined
                    ? `Drag to add its files — ${folder.path}`
                    : folder?.path
              }
              className={
                draggableSources ? 'cursor-grab active:cursor-grabbing' : undefined
              }
              draggable={draggableSources}
              onDragStart={
                draggableSources
                  ? (event) => {
                      dragging.current = true;
                      if (track !== undefined) beginTrackDrag(event, track.path);
                      else if (folder !== undefined) beginFolderDrag(event, folder.path);
                    }
                  : undefined
              }
              onDragEnd={
                draggableSources
                  ? () => {
                      // Clear after the click that some browsers send on drop.
                      window.setTimeout(() => {
                        dragging.current = false;
                      }, 0);
                    }
                  : undefined
              }
            >
              <TreeItemLabel className="relative before:absolute before:inset-x-0 before:-inset-y-0.5 before:-z-10 before:bg-background">
                <span className="flex min-w-0 flex-1 items-center gap-2">
                  {entry.kind === 'folder' ? (
                    item.isExpanded() ? (
                      <FolderOpen aria-hidden="true" className="size-4 text-muted-foreground" />
                    ) : (
                      <FolderIcon aria-hidden="true" className="size-4 text-muted-foreground" />
                    )
                  ) : (
                    <FileMusic aria-hidden="true" className="size-4 text-muted-foreground" />
                  )}

                  <span className="truncate">{entry.name}</span>

                  {entry.kind === 'folder' ? (
                    <span className="ml-auto shrink-0 text-xs text-muted-foreground tabular-nums">
                      {plural(folder?.tracksBelow ?? 0, 'track')}
                    </span>
                  ) : track !== undefined ? (
                    <>
                      {/* A source column is narrow: a measured file's tags would take the
                          room its name needs, and the tree above it already says where it
                          is. A file with no measurement still says so. */}
                      {action !== 'add' || track.kind !== 'measured' ? (
                        <span className="truncate text-xs text-muted-foreground">
                          · {metaOf(track)}
                        </span>
                      ) : null}
                      {action === 'add' ? (
                        <Plus
                          aria-hidden="true"
                          className="ml-auto size-3.5 shrink-0 text-muted-foreground"
                        />
                      ) : action === 'queue' ? (
                        <ListPlus
                          aria-hidden="true"
                          className="ml-auto size-3.5 shrink-0 text-muted-foreground"
                        />
                      ) : (
                        <PlayIcon
                          aria-hidden="true"
                          className="ml-auto size-3.5 shrink-0 text-muted-foreground"
                        />
                      )}
                    </>
                  ) : null}
                </span>
              </TreeItemLabel>
            </TreeItem>
          );
        })}
      </Tree>
    </div>
  );
}

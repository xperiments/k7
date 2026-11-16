import { useEffect, useMemo, useRef, type MutableRefObject } from 'react';
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
  selectionFeature,
  syncDataLoaderFeature,
} from '@headless-tree/core';
import { useTree } from '@headless-tree/react';

import { Tree, TreeItem, TreeItemLabel } from '@/components/reui/tree';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
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

/** The row controls a caller's own toolbar can drive. */
export interface TreeControls {
  expandAll: () => void;
  collapseAll: () => void;
  selectAll: () => void;
  deselectAll: () => void;
}

/** What pressing a file's row does. A source tree adds; the Drive plays or queues. */
export type FileTreeAction = 'play' | 'queue' | 'add' | 'none';

export function FileTree({
  tree: root,
  query,
  path,
  action,
  onActivate,
  draggableSources = false,
  onDropOnFolder,
  onMove,
  selectable = false,
  onSelectionChange,
  clearToken,
  onRename,
  renaming,
  showControls = true,
  expandAll = false,
  controls,
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
  /** Every folder becomes a drop target: files dropped on it land in that folder. */
  onDropOnFolder?: (folderPath: string, data: DataTransfer) => void;
  /** Rows can be picked: click selects, cmd/ctrl toggles, shift extends. */
  selectable?: boolean;
  /** The selected paths below `music/`, whenever the selection changes. */
  onSelectionChange?: (paths: string[]) => void;
  /** Bump to clear the tree's own selection (a click-away or a finished operation). */
  clearToken?: number;
  /** Dragging rows onto a folder moves them there. */
  onMove?: (paths: string[], toFolder: string) => void;
  /** Double-clicking a row starts an inline rename. */
  onRename?: (path: string) => void;
  /** The row being renamed, and the input's state. */
  renaming?: {
    path: string;
    value: string;
    onValue: (value: string) => void;
    onCommit: () => void;
    onCancel: () => void;
  };
  /** Draw the header's expand/collapse buttons; a caller with its own bar says no. */
  showControls?: boolean;
  /** Open every folder on the first render: a short tree is read at once, not walked. */
  expandAll?: boolean;
  /** Filled with the tree's expand/collapse, for a caller's own bar. */
  controls?: MutableRefObject<TreeControls | null>;
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
    () =>
      expandAll || query.trim().length > 0 ? allFolderIds(data) : expandedForPath(root, path),
    [data, root, path, query, expandAll],
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
      if (action === 'none' || dragging.current) return;
      const entry = item.getItemData();
      if (entry.kind === 'track' && entry.track !== undefined && entry.home !== undefined) {
        onActivate(entry.track, entry.home);
      }
    },
    dataLoader: {
      getItem: (itemId) => entries[itemId],
      getChildren: (itemId) => entries[itemId]?.children ?? [],
    },
    features: [
      syncDataLoaderFeature,
      hotkeysCoreFeature,
      expandAllFeature,
      ...(selectable ? [selectionFeature] : []),
    ],
  });

  /* Report the selection so a caller can act on it. The manager's toolbar lives outside the
   * tree: a row is too small to hold a menu, and the actions are not about one row. */
  const selectedPaths = selectable
    ? instance
        .getSelectedItems()
        .map((item) => {
          const data = entries[item.getId()];
          if (data === undefined) return '';
          return data.kind === 'folder' ? data.folder?.path ?? '' : data.track?.path ?? '';
        })
        .filter((path) => path !== '')
    : [];
  const selectedKey = selectedPaths.join('\n');
  useEffect(() => {
    if (!selectable) return;
    onSelectionChange?.(selectedKey === '' ? [] : selectedKey.split('\n'));
    // `selectedKey` is the identity of the selection; the instances behind it are rebuilt.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedKey, selectable]);

  /* The caller can clear the selection without the tree knowing why (a click outside, or an
   * operation that removed the rows). Bumping the token empties the feature's own state, so
   * `data-selected` — and the accent ink that hangs off it — goes too. */
  useEffect(() => {
    if (clearToken === undefined) return;
    instance.setSelectedItems([]);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [clearToken]);

  /* Hand the expand/collapse pair to a caller that has its own toolbar. */
  useEffect(() => {
    if (controls === undefined) return;
    controls.current = {
      expandAll: () => void instance.expandAll(),
      collapseAll: () => instance.collapseAll(),
      selectAll: () =>
        instance.setSelectedItems(Object.keys(entries).filter((id) => id !== ROOT_ID)),
      deselectAll: () => instance.setSelectedItems([]),
    };
    return () => {
      controls.current = null;
    };
  }, [instance, controls, entries]);

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
    action === 'add'
      ? 'Add to playlist'
      : action === 'queue'
        ? 'Queue on the player'
        : action === 'play'
          ? 'Play'
          : '';

  return (
    <div className="flex shrink-0 flex-col gap-3">
      <div className="flex flex-wrap items-center justify-between gap-2">
        <div className="flex items-baseline gap-2">
          <span className="text-sm font-light">{data.name}</span>
          <span className="text-xs text-muted-foreground tabular-nums">{summary}</span>
        </div>
        {showControls ? (
          <div className="flex items-center gap-1">
            <Button variant="ghost" size="sm" onClick={() => void instance.expandAll()}>
              Expand all
            </Button>
            <Button variant="ghost" size="sm" onClick={() => instance.collapseAll()}>
              Collapse all
            </Button>
          </div>
        ) : null}
      </div>

      {/* One gradient paints the indent guides; each label masks it under its own row, so
          the lines show only in the gutter the indentation opens. */}
      <Tree
        indent={20}
        tree={instance}
        className="relative before:absolute before:inset-0 before:-ms-1 before:bg-[repeating-linear-gradient(to_right,transparent_0,transparent_calc(var(--tree-indent)-1px),var(--border)_calc(var(--tree-indent)-1px),var(--border)_calc(var(--tree-indent)))]"
      >
        {instance.getItems().map((item) => {
          // The tree's own state can name an id the current data no longer has — a folder
          // deleted or renamed since the last rebuild. Skip it rather than ask for its data.
          const entry = entries[item.getId()];
          if (entry === undefined) return null;
          const folder = entry.folder;
          const track = entry.track;
          const entryPath = entry.kind === 'folder' ? folder?.path ?? '' : track?.path ?? '';

          return (
            <TreeItem
              key={item.getId()}
              type="button"
              item={item}
              title={
                track !== undefined
                  ? verb === ''
                    ? track.path
                    : `${verb} — ${track.path}`
                  : action === 'add' && folder !== undefined
                    ? `Drag to add its files — ${folder.path}`
                    : folder?.path
              }
              className={
                draggableSources ? 'cursor-grab active:cursor-grabbing' : undefined
              }
              onDoubleClick={onRename !== undefined ? () => onRename(entryPath) : undefined}
            >
              {/* The drag lives on the label, not the row's button: a <span> is a reliable
                  drag source in Chrome, and the label is the whole visible row anyway. */}
              <TreeItemLabel
                className="relative before:absolute before:inset-x-0 before:-inset-y-0.5 before:-z-10 before:bg-background in-data-[selected=true]:bg-transparent in-data-[selected=true]:text-primary"
                draggable={draggableSources || onMove !== undefined}
                onDragStart={
                  onMove !== undefined
                    ? (event) => {
                        dragging.current = true;
                        const paths = selectedPaths.includes(entryPath)
                          ? selectedPaths
                          : [entryPath];
                        event.dataTransfer.setData('application/x-k7-paths', JSON.stringify(paths));
                        event.dataTransfer.effectAllowed = 'move';
                      }
                    : draggableSources
                      ? (event) => {
                          dragging.current = true;
                          if (track !== undefined) beginTrackDrag(event, track.path);
                          else if (folder !== undefined) beginFolderDrag(event, folder.path);
                        }
                      : undefined
                }
                onDragEnd={
                  draggableSources || onMove !== undefined
                    ? () => {
                        // Clear after the click that some browsers send on drop.
                        window.setTimeout(() => {
                          dragging.current = false;
                        }, 0);
                      }
                    : undefined
                }
                onDragOver={
                  (onDropOnFolder !== undefined || onMove !== undefined) && entry.kind === 'folder'
                    ? (event) => {
                        event.preventDefault();
                        event.dataTransfer.dropEffect = event.dataTransfer.types.includes(
                          'application/x-k7-paths',
                        )
                          ? 'move'
                          : 'copy';
                      }
                    : undefined
                }
                onDrop={
                  (onDropOnFolder !== undefined || onMove !== undefined) && entry.kind === 'folder'
                    ? (event) => {
                        event.preventDefault();
                        // The row's own folder wins over the tree's root drop target.
                        event.stopPropagation();
                        const raw = event.dataTransfer.getData('application/x-k7-paths');
                        if (raw !== '' && onMove !== undefined) {
                          // A row dragged from this tree moves; anything else is new music.
                          try {
                            onMove(JSON.parse(raw) as string[], entryPath);
                          } catch {
                            /* A malformed payload is treated as no move. */
                          }
                          return;
                        }
                        onDropOnFolder?.(entry.folder?.path ?? '', event.dataTransfer);
                      }
                    : undefined
                }
              >
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

                  {renaming?.path === entryPath ? (
                    <Input
                      value={renaming.value}
                      onChange={(event) => renaming.onValue(event.target.value)}
                      onKeyDown={(event) => {
                        if (event.key === 'Enter') renaming.onCommit();
                        if (event.key === 'Escape') renaming.onCancel();
                      }}
                      onBlur={() => renaming.onCommit()}
                      onClick={(event) => event.stopPropagation()}
                      onDoubleClick={(event) => event.stopPropagation()}
                      className="h-7 w-48"
                      autoFocus
                    />
                  ) : (
                    <span className="truncate">{entry.name}</span>
                  )}

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
                      ) : action === 'play' ? (
                        <PlayIcon
                          aria-hidden="true"
                          className="ml-auto size-3.5 shrink-0 text-muted-foreground"
                        />
                      ) : null}
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

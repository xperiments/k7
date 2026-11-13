import { useEffect, useRef, useState } from 'react';
import { FolderOpen, FolderPlus, Trash2 } from 'lucide-react';

import { Button } from '@/components/ui/button';
import { Card } from '@/components/ui/card';
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from '@/components/ui/empty';
import { Skeleton } from '@/components/ui/skeleton';
import {
  cardFolderTree,
  relocateInCard,
  removeFromCard,
  renameInCard,
  type CardDir,
} from '../card/files';
import { cardHandleFor } from '../card/handles';
import { useCardScope } from '../card/session';
import { buildFolderTree, type Folder } from '../media/folders';
import { deleteMediaBelow, listMedia, moveMediaBelow } from '../storage/media';
import { scanDataTransfer } from './files';
import { FileTree, type TreeControls } from './file-tree';
import { useAnalysis } from './state/analysis';

/**
 * The open card's own `music/` as a tree, walked from the disk and every folder a drop
 * target. This replaces the generic intake once a card is chosen: the user is already looking
 * at the directory they are adding to, so a drop lands in the folder it is made on.
 *
 * A file manager lives on top of it: a top bar makes a folder or deletes the selection,
 * double-clicking a row renames it in place, and dragging rows onto a folder moves them.
 * Every operation is the card's directory, and each one keeps the browser's rows in step.
 */
export function CardTree() {
  const scope = useCardScope();
  const { compose, mediaCount } = useAnalysis();
  const [tree, setTree] = useState<Folder | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [revision, setRevision] = useState(0);
  const [selected, setSelected] = useState<string[]>([]);
  const [renaming, setRenaming] = useState<{ path: string; value: string } | null>(null);
  /* Bumped whenever the selection must be dropped in the tree too, not just here. */
  const [clearToken, setClearToken] = useState(0);
  /* One button, toggling: what the next press will do is its label. */
  const [allExpanded, setAllExpanded] = useState(false);
  const container = useRef<HTMLDivElement>(null);
  /* The tree's own expand/collapse, driven from this bar. */
  const treeControls = useRef<TreeControls | null>(null);

  /* Re-read when the card, its rows or a disk operation change. The tree is the card's real
   * directory when there is a writable handle, so an empty folder the manager just made shows;
   * a read-only dropped card falls back to what the browser measured. */
  useEffect(() => {
    let cancelled = false;
    void (async () => {
      const rows = await listMedia(scope).catch(() => []);
      const directory = await cardHandleFor(scope).catch(() => null);
      const next =
        directory !== null
          ? await cardFolderTree(directory as unknown as CardDir, rows).catch(() =>
              buildFolderTree(rows),
            )
          : buildFolderTree(rows);
      if (!cancelled) setTree(next);
    })();
    return () => {
      cancelled = true;
    };
  }, [scope, mediaCount, revision]);

  /* Clicking away from the tree drops the selection: nothing else owns it, so leaving it
   * lit is a claim about what the next Delete acts on. */
  useEffect(() => {
    if (selected.length === 0) return;
    const onPointerDown = (event: PointerEvent) => {
      if (container.current !== null && event.target instanceof Node && container.current.contains(event.target)) {
        return;
      }
      setSelected([]);
      setClearToken((value) => value + 1);
      setRenaming(null);
    };
    document.addEventListener('pointerdown', onPointerDown);
    return () => document.removeEventListener('pointerdown', onPointerDown);
  }, [selected.length]);

  const handle = async (): Promise<CardDir | null> => {
    const directory = await cardHandleFor(scope);
    return (directory as unknown as CardDir | null) ?? null;
  };

  const dropInto = async (into: string, data: DataTransfer) => {
    setBusy(true);
    setError(null);
    try {
      const result = await scanDataTransfer(data);
      await compose(result.files, result.skipped, into);
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      setBusy(false);
    }
  };

  const createFolder = async () => {
    const card = await handle();
    if (card === null) return;
    setBusy(true);
    setError(null);
    try {
      const music = await card.getDirectoryHandle('music', { create: true });
      const taken = new Set((tree?.folders ?? []).map((folder) => folder.name));
      let name = 'New folder';
      let suffix = 2;
      while (taken.has(name)) name = `New folder ${suffix++}`;
      await music.getDirectoryHandle(name, { create: true });
      setRenaming({ path: name, value: name });
      setRevision((value) => value + 1);
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      setBusy(false);
    }
  };

  const removeSelected = async () => {
    const card = await handle();
    if (card === null || selected.length === 0) return;
    setBusy(true);
    setError(null);
    try {
      for (const path of topLevel(selected)) {
        await removeFromCard(card, path);
        await deleteMediaBelow(scope, path);
      }
      setSelected([]);
      setClearToken((value) => value + 1);
      setRevision((value) => value + 1);
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      setBusy(false);
    }
  };

  const commitRename = async () => {
    if (renaming === null) return;
    const name = renaming.value.trim();
    if (name === '' || name === renaming.path.split('/').pop()) {
      setRenaming(null);
      return;
    }
    const card = await handle();
    if (card === null) return;
    setBusy(true);
    setError(null);
    try {
      const next = await renameInCard(card, renaming.path, name);
      if (next !== null) await moveMediaBelow(scope, renaming.path, next);
      setRenaming(null);
      setSelected([]);
      setClearToken((value) => value + 1);
      setRevision((value) => value + 1);
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      setBusy(false);
    }
  };

  const moveSelection = async (paths: string[], toFolder: string) => {
    const card = await handle();
    if (card === null) return;
    setBusy(true);
    setError(null);
    try {
      for (const path of topLevel(paths)) {
        // A folder cannot move into itself or below itself.
        if (path === '' || path === toFolder || toFolder.startsWith(`${path}/`)) continue;
        const next = await relocateInCard(card, path, toFolder);
        if (next !== null) await moveMediaBelow(scope, path, next);
      }
      setSelected([]);
      setClearToken((value) => value + 1);
      setRevision((value) => value + 1);
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      setBusy(false);
    }
  };

  if (tree === null) return <Skeleton className="h-40 w-full" />;

  const empty = tree.folders.length === 0 && tree.tracks.length === 0;
  const total = countItems(tree);
  const allSelected = total > 0 && selected.length >= total;

  return (
    <Card
      size="sm"
      className="relative z-0 flex h-[calc(100dvh-19rem)] flex-col gap-0 overflow-hidden py-0"
    >
      <div ref={container} className="flex min-h-0 flex-1 flex-col">
        <div
          className="min-h-0 flex-1 overflow-y-auto"
          onDragOver={(event) => {
            event.preventDefault();
            event.dataTransfer.dropEffect = event.dataTransfer.types.includes(
              'application/x-k7-paths',
            )
              ? 'move'
              : 'copy';
          }}
          onDrop={(event) => {
            event.preventDefault();
            const raw = event.dataTransfer.getData('application/x-k7-paths');
            if (raw !== '') {
              // A row dragged within the tree landing on the body means the music root.
              try {
                void moveSelection(JSON.parse(raw) as string[], '');
              } catch {
                /* A malformed payload is treated as no move. */
              }
              return;
            }
            void dropInto('', event.dataTransfer);
          }}
        >
          {/* The bar is the first row of the scroll and sticks: the actions stay put while
              the tree below them moves. */}
          <div className="sticky top-0 z-10 flex flex-wrap items-center gap-1 border-b border-foreground/10 bg-background px-4 py-2">
            <Button size="sm" variant="outline" onClick={() => void createFolder()} disabled={busy}>
              <FolderPlus aria-hidden="true" /> New folder
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={() => void removeSelected()}
              disabled={busy || selected.length === 0}
            >
              <Trash2 aria-hidden="true" /> Delete
            </Button>
            <div className="ml-auto flex items-center gap-1">
              <Button
                size="sm"
                variant="ghost"
                onClick={() => {
                  if (allSelected) treeControls.current?.deselectAll();
                  else treeControls.current?.selectAll();
                }}
              >
                {allSelected ? 'Deselect all' : 'Select all'}
              </Button>
              <Button
                size="sm"
                variant="ghost"
                onClick={() => {
                  if (allExpanded) treeControls.current?.collapseAll();
                  else treeControls.current?.expandAll();
                  setAllExpanded((value) => !value);
                }}
              >
                {allExpanded ? 'Collapse all' : 'Expand all'}
              </Button>
            </div>
          </div>

          <div className="px-4 py-2">
            {error !== null ? <p className="text-xs text-destructive">{error}</p> : null}
            {empty ? (
              <Empty className="border py-6">
                <EmptyHeader>
                  <EmptyMedia variant="icon">
                    <FolderOpen aria-hidden="true" />
                  </EmptyMedia>
                  <EmptyTitle>This card has no music yet</EmptyTitle>
                  <EmptyDescription>Drop a folder of music here.</EmptyDescription>
                </EmptyHeader>
              </Empty>
            ) : (
              <FileTree
                tree={tree}
                query=""
                path=""
                action="none"
                onActivate={() => undefined}
                selectable
                clearToken={clearToken}
                onSelectionChange={setSelected}
                onRename={(path) => setRenaming({ path, value: path.split('/').pop() ?? '' })}
                renaming={
                  renaming === null
                    ? undefined
                    : {
                        path: renaming.path,
                        value: renaming.value,
                        onValue: (value) => setRenaming({ ...renaming, value }),
                        onCommit: () => void commitRename(),
                        onCancel: () => setRenaming(null),
                      }
                }
                showControls={false}
                controls={treeControls}
                onMove={(paths, toFolder) => void moveSelection(paths, toFolder)}
                onDropOnFolder={(path, data) => void dropInto(path, data)}
              />
            )}
          </div>
        </div>
      </div>
    </Card>
  );
}

/** Every selectable row: each folder and each file below the root. */
function countItems(folder: Folder): number {
  let count = folder.tracks.length;
  for (const child of folder.folders) count += 1 + countItems(child);
  return count;
}

/**
 * Drop the paths a selected ancestor already covers. Deleting a folder removes its children
 * on disk, so visiting a child afterwards asks for a file that is already gone; moving a
 * folder takes them with it for the same reason.
 */
function topLevel(paths: readonly string[]): string[] {
  const chosen = new Set(paths);
  const parentOf = (path: string): string =>
    path.includes('/') ? path.slice(0, path.lastIndexOf('/')) : '';
  return paths.filter((path) => {
    for (let parent = parentOf(path); parent !== ''; parent = parentOf(parent)) {
      if (chosen.has(parent)) return false;
    }
    return true;
  });
}

import { useEffect, useId, useMemo, useRef, useState, type DragEvent } from 'react';
import {
  ArrowDown,
  ArrowUp,
  ChevronDown,
  ChevronRight,
  Folder,
  GripVertical,
  ListMusic,
  Plus,
  Trash2,
} from 'lucide-react';
import { cn } from 'cn';

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
import { Input } from '@/components/ui/input';
import { Skeleton } from '@/components/ui/skeleton';
import { McpError, trackAt, type McpTrack } from '../../device/mcp';
import { useMediaIndex } from '../../media/albums';
import { buildFolderTree, folderTrackPaths, locateFolder } from '../../media/folders';
import { fileName } from '../../media/paths';
import {
  clearDraft,
  draftsLoaded,
  insertEntries,
  moveEntry,
  removeEntry,
  updateDraft,
  useDrafts,
  type Draft,
  type NewEntry,
} from '../../playlists/drafts';
import { FileTree } from '../file-tree';
import { plural } from '../format';
import { navigate, routeHref } from '../router';
import { draggedFolder, draggedTrack, isLibraryDrag } from '../track-drag';

/**
 * One draft as a workspace: the card's folders on the left, the list on the right.
 *
 * A source row can be **pressed** to add a track at the end, or **dragged** into the list to
 * place it exactly. A folder is a block of its own — dropping it adds one row that carries
 * every file under it, and moving that row moves the whole album — while a file is a block
 * of one. The Device never sees the blocks: `drafts.ts` flattens them to the one Snapshot
 * path list the player walks.
 *
 * The press is the keyboard-reachable path to the same act the drag performs, and the arrows
 * beside each block are the keyboard-reachable path to the reorder — native drag and drop is
 * not reachable with a keyboard, and a feature that only works with a mouse is half a feature.
 * The order is the author's, always: nothing re-sorts it behind their back.
 *
 * The measurements shown beside a row come from the device (`library_track`), eight at a
 * time, and are cached for this visit. A row the Library does not hold is `null` and shown
 * as missing; a transport failure is not a missing row and says so instead.
 */

interface RowFacts {
  bpm: number | null;
  key: string | null;
  energy: number | null;
  artist: string | null;
  album: string | null;
  title: string | null;
}

type FactMap = Map<string, RowFacts | null>;

const BATCH = 8;

function factsOf(track: McpTrack): RowFacts {
  const analysis = track.analysis;
  return {
    bpm: analysis?.bpm ?? null,
    key: analysis?.key ?? null,
    energy: analysis?.energy ?? null,
    artist: analysis?.artist ?? null,
    album: analysis?.album ?? null,
    title: analysis?.title ?? null,
  };
}

interface FactsState {
  /** Keyed by Snapshot path; `null` means the Library does not have that path. */
  map: FactMap;
  loading: boolean;
  error: string | null;
}

/**
 * Resolve every distinct row the draft holds. The effect re-runs when the *set* of paths
 * changes (a reorder does not), and a path is asked for once:
 * `settled` keeps the answers and `inFlight` keeps two overlapping runs from duplicating.
 */
function useRowFacts(paths: readonly string[]): FactsState {
  const key = useMemo(() => Array.from(new Set(paths)).sort().join('\n'), [paths]);
  const [state, setState] = useState<FactsState>({ map: new Map(), loading: false, error: null });
  const settled = useRef(new Set<string>());
  const inFlight = useRef(new Set<string>());

  useEffect(() => {
    let cancelled = false;
    const targets = (key ? key.split('\n') : []).filter(
      (path) => path && !settled.current.has(path) && !inFlight.current.has(path),
    );
    if (targets.length === 0) return;

    const run = async () => {
      setState((current) => ({ ...current, loading: true, error: null }));

      for (let at = 0; at < targets.length; at += BATCH) {
        if (cancelled) return;
        const batch = targets.slice(at, at + BATCH);
        for (const path of batch) inFlight.current.add(path);

        let results: [string, RowFacts | null][];
        try {
          results = await Promise.all(
            batch.map(async (path): Promise<[string, RowFacts | null]> => {
              try {
                return [path, factsOf(await trackAt(path))];
              } catch (error) {
                // `library_track` answers "no such track" for a path the Library does not
                // hold; every other refusal — no card, a broken endpoint — is a failure to
                // ask, not a missing row, and must not be shown as one.
                if (error instanceof McpError && /no such track/i.test(error.message)) {
                  return [path, null];
                }
                throw error;
              }
            }),
          );
        } catch {
          for (const path of batch) inFlight.current.delete(path);
          if (!cancelled) {
            setState((current) => ({
              ...current,
              loading: false,
              error: 'The device did not answer while checking the rows.',
            }));
          }
          return;
        }

        for (const path of batch) inFlight.current.delete(path);
        if (cancelled) return;
        for (const [path] of results) settled.current.add(path);
        setState((current) => {
          const map = new Map(current.map);
          for (const [path, facts] of results) map.set(path, facts);
          return { ...current, map };
        });
      }

      if (!cancelled) setState((current) => ({ ...current, loading: false }));
    };

    void run();
    return () => {
      cancelled = true;
    };
  }, [key]);

  return state;
}

function normaliseId(id: string): string {
  return id.trim().toLowerCase().replace(/^0x/, '');
}

function findDraft(drafts: readonly Draft[], id: string): Draft | undefined {
  const wanted = normaliseId(id);
  if (!wanted) return undefined;
  return drafts.find((draft) => draft.id === wanted || draft.id === wanted.padStart(16, '0'));
}

function EditorMissing() {
  return (
    <div className="mx-auto flex w-full max-w-2xl flex-col gap-4 p-4 sm:p-6">
      <Empty>
        <EmptyHeader>
          <EmptyMedia variant="icon">
            <ListMusic aria-hidden="true" />
          </EmptyMedia>
          <EmptyTitle>That playlist is not here</EmptyTitle>
          <EmptyDescription>
            Drafts live in this browser. A link from another browser, or a list deleted since the
            link was made, lands here.
          </EmptyDescription>
        </EmptyHeader>
        <EmptyContent>
          <Button size="sm" onClick={() => navigate(routeHref({ name: 'playlists' }))}>
            All playlists
          </Button>
        </EmptyContent>
      </Empty>
    </div>
  );
}

function EditorLoading() {
  return (
    <div className="mx-auto flex w-full max-w-7xl flex-col gap-4 p-4 sm:p-6">
      <Skeleton className="h-8 w-64" />
      <div className="grid gap-6 lg:grid-cols-2">
        <Skeleton className="h-64 w-full" />
        <Skeleton className="h-64 w-full" />
      </div>
    </div>
  );
}

function NoSources({ reachable }: { reachable: boolean }) {
  return (
    <Empty className="border">
      <EmptyHeader>
        <EmptyMedia variant="icon">
          <ListMusic aria-hidden="true" />
        </EmptyMedia>
        <EmptyTitle>No card to browse</EmptyTitle>
        <EmptyDescription>
          {reachable
            ? 'Measure a card in this browser and its folders appear here, ready to drag into the list.'
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

function SourceLoading() {
  return (
    <div className="flex flex-col gap-2">
      {Array.from({ length: 6 }, (_, index) => (
        <Skeleton key={index} className="h-7 w-full" />
      ))}
    </div>
  );
}

/** One file's name, path and state, without any of the buttons around it. */
function TrackInfo({
  path,
  facts,
}: {
  path: string;
  facts: RowFacts | null | undefined;
}) {
  const missing = facts === null;
  return (
    <div className="flex min-w-0 flex-1 flex-col gap-0.5">
      <div className="flex min-w-0 items-center gap-2">
        <span className="truncate text-sm" title={path}>
          {facts?.title || fileName(path)}
        </span>
        {missing ? <Badge variant="destructive-light">Not on the card</Badge> : null}
      </div>
      <span className="truncate text-xs text-muted-foreground">{path}</span>
    </div>
  );
}

function FactsLine({ facts }: { facts: RowFacts | null | undefined }) {
  if (!facts) return null;
  return (
    <div className="hidden shrink-0 items-center gap-2 text-xs text-muted-foreground tabular-nums xl:flex">
      {facts.bpm != null ? <span>{facts.bpm.toFixed(1)} BPM</span> : null}
      {facts.key ? <span>{facts.key}</span> : null}
      {facts.energy != null ? <span>{Math.round(facts.energy)} energy</span> : null}
    </div>
  );
}

/** The up/down/remove controls every block has; a folder's label is the folder's name. */
function BlockActions({
  index,
  count,
  label,
  onUp,
  onDown,
  onRemove,
}: {
  index: number;
  count: number;
  label: string;
  onUp: () => void;
  onDown: () => void;
  onRemove: () => void;
}) {
  return (
    <div className="flex shrink-0 items-center gap-0.5">
      <Button
        variant="ghost"
        size="icon-sm"
        disabled={index === 0}
        onClick={onUp}
        aria-label={`Move ${label} up`}
      >
        <ArrowUp aria-hidden="true" />
      </Button>
      <Button
        variant="ghost"
        size="icon-sm"
        disabled={index === count - 1}
        onClick={onDown}
        aria-label={`Move ${label} down`}
      >
        <ArrowDown aria-hidden="true" />
      </Button>
      <Button
        variant="ghost"
        size="icon-sm"
        onClick={onRemove}
        aria-label={`Remove ${label}`}
      >
        <Trash2 aria-hidden="true" />
      </Button>
    </div>
  );
}

function Editor({ draft }: { draft: Draft }) {
  const facts = useRowFacts(draft.rows);
  const media = useMediaIndex();
  const [name, setName] = useState(draft.name);
  const [entryFrom, setEntryFrom] = useState<number | null>(null);
  const [entryDropAt, setEntryDropAt] = useState<number | null>(null);
  const [overEmpty, setOverEmpty] = useState(false);
  const [overEnd, setOverEnd] = useState(false);
  const [confirmingClear, setConfirmingClear] = useState(false);
  const [sourceQuery, setSourceQuery] = useState('');
  const [expanded, setExpanded] = useState<ReadonlySet<string>>(new Set());
  const nameField = useId();
  const sourceSearch = useId();

  /* The same tree Media's Drive shows, read from the same cache and listing. The editor
   * starts it at the root: the point here is to pick a file, not to walk a history. */
  const sourceTree = useMemo(
    () =>
      media.index
        ? buildFolderTree(
            media.index.albums.flatMap((album) => album.rows),
            media.index.unmeasured,
            media.index.unmeasurable,
          )
        : null,
    [media.index],
  );
  const hasSources =
    sourceTree !== null && (sourceTree.folders.length > 0 || sourceTree.tracks.length > 0);
  const folderCount = draft.entries.filter((entry) => entry.kind === 'folder').length;

  const checked = draft.rows.every((path) => facts.map.has(path));
  const missing = checked ? draft.rows.filter((path) => facts.map.get(path) === null).length : null;

  const commitName = () => {
    const trimmed = name.trim();
    if (!trimmed) {
      setName(draft.name);
      return;
    }
    if (trimmed !== draft.name) updateDraft(draft.id, { name: trimmed });
  };

  const toggleExpanded = (id: string) => {
    setExpanded((current) => {
      const next = new Set(current);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  /* A folder dropped for the first time opens, so the person sees what arrived; every other
     block arrives closed or is a single track. */
  const insertAt = (inputs: readonly NewEntry[], at: number) => {
    if (inputs.length === 0) return;
    const updated = insertEntries(draft.id, inputs, at);
    const added = updated?.entries[at];
    if (added?.kind === 'folder') {
      setExpanded((current) => new Set(current).add(added.id));
    }
  };

  const addToEnd = (inputs: readonly NewEntry[]) => {
    insertAt(inputs, draft.entries.length);
  };

  /* A file drag carries one path; a folder drag becomes one block holding every file the
     folder had, in the order the source tree drew them. */
  const entriesFromDrag = (event: DragEvent): NewEntry[] => {
    const track = draggedTrack(event);
    if (track !== null) return [{ kind: 'track', path: track }];
    const folderPath = draggedFolder(event);
    if (folderPath === null || sourceTree === null) return [];
    const found = locateFolder(sourceTree, folderPath);
    if (found === null) return [];
    const rows = folderTrackPaths(found.folder);
    return rows.length > 0
      ? [{ kind: 'folder', path: found.folder.path, name: found.folder.name, rows }]
      : [];
  };

  const clearAll = () => {
    clearDraft(draft.id);
    setConfirmingClear(false);
    setExpanded(new Set());
  };

  const dragProps = (index: number) => ({
    draggable: true,
    onDragStart: (event: DragEvent<HTMLLIElement>) => {
      setEntryFrom(index);
      event.dataTransfer.effectAllowed = 'move';
      // Firefox refuses a drag with no data on it.
      event.dataTransfer.setData('text/plain', String(index));
    },
    onDragOver: (event: DragEvent<HTMLLIElement>) => {
      // Always own the drop: an unhandled drop over a block would let the browser open
      // the dragged file as a page.
      event.preventDefault();
      event.stopPropagation();
      const fromTree = isLibraryDrag(event);
      const fromList = entryFrom !== null;
      event.dataTransfer.dropEffect = fromTree ? 'copy' : fromList ? 'move' : 'none';
      if ((fromTree || fromList) && entryDropAt !== index) setEntryDropAt(index);
    },
    onDragLeave: () => {
      if (entryDropAt === index) setEntryDropAt(null);
    },
    onDrop: (event: DragEvent<HTMLLIElement>) => {
      event.preventDefault();
      event.stopPropagation();
      const inputs = entriesFromDrag(event);
      if (inputs.length > 0) {
        insertAt(inputs, index);
        setEntryDropAt(null);
        return;
      }
      if (entryFrom !== null) {
        moveEntry(draft.id, entryFrom, index);
        setEntryFrom(null);
        setEntryDropAt(null);
      }
    },
    onDragEnd: () => {
      setEntryFrom(null);
      setEntryDropAt(null);
    },
  });

  return (
    <div className="mx-auto flex w-full max-w-7xl flex-col gap-4 p-4 sm:p-6 lg:h-full lg:min-h-0">
      <h1 className="sr-only">Edit playlist</h1>
      <header className="flex flex-wrap items-end justify-between gap-3">
        <div className="flex w-full max-w-xl flex-col gap-1">
          <label htmlFor={nameField} className="text-xs text-muted-foreground">
            Name
          </label>
          <Input
            id={nameField}
            value={name}
            onChange={(event) => setName(event.target.value)}
            onBlur={commitName}
            onKeyDown={(event) => {
              if (event.key === 'Enter') {
                event.preventDefault();
                event.currentTarget.blur();
              }
              if (event.key === 'Escape') {
                // Reverting must not blur: onBlur reads the name from this render, and
                // that is still the text being abandoned.
                setName(draft.name);
              }
            }}
            maxLength={60}
            autoComplete="off"
          />
        </div>
        <Button
          variant="ghost"
          size="sm"
          onClick={() => navigate(routeHref({ name: 'playlists' }))}
        >
          All playlists
        </Button>
      </header>

      <div className="grid min-w-0 gap-6 lg:min-h-0 lg:flex-1 lg:grid-cols-[minmax(0,0.95fr)_minmax(0,1.05fr)]">
        {/* Sources: the card, as a tree, with every file and folder a drag or a press away. */}
        <section aria-labelledby="sources-heading" className="flex min-w-0 flex-col gap-3 lg:min-h-0">
          <div className="flex flex-col gap-2">
            <div className="flex flex-col gap-0.5">
              <h2 id="sources-heading" className="text-sm font-medium">
                Sources
              </h2>
              <p className="text-xs text-muted-foreground">
                The card&apos;s folders. Drag a file or folder into the list, or press a file to
                add it at the end.
              </p>
            </div>
            {!media.loading && hasSources ? (
              <div className="flex flex-col gap-1">
                <label htmlFor={sourceSearch} className="sr-only">
                  Search sources
                </label>
                <Input
                  id={sourceSearch}
                  type="search"
                  value={sourceQuery}
                  onChange={(event) => setSourceQuery(event.target.value)}
                  placeholder="Search sources"
                  autoComplete="off"
                />
              </div>
            ) : null}
          </div>

          <div className="min-h-0 lg:flex-1 lg:overflow-auto">
            {media.loading ? (
              <SourceLoading />
            ) : !hasSources ? (
              <NoSources reachable={media.reachable} />
            ) : (
              <FileTree
                key={`sources\u0000${sourceQuery}`}
                tree={sourceTree}
                query={sourceQuery}
                path=""
                action="add"
                draggableSources
                onActivate={(track) => addToEnd([{ kind: 'track', path: track.path }])}
              />
            )}
          </div>
        </section>

        {/* The draft: blocks in order, each one a drop target and a drag handle. */}
        <section
          aria-labelledby="playlist-heading"
          className="flex min-w-0 flex-col gap-3 lg:min-h-0"
        >
          <div className="flex flex-wrap items-end justify-between gap-2">
            <div className="flex flex-col gap-0.5">
              <h2 id="playlist-heading" className="text-sm font-medium">
                Playlist
              </h2>
              <p className="text-xs text-muted-foreground tabular-nums" role="status">
                {plural(draft.rows.length, 'track')}
                {folderCount > 0 ? ` · ${plural(folderCount, 'folder')}` : ''} · not on the card:{' '}
                {facts.error ? facts.error : missing === null ? 'checking…' : missing}
              </p>
            </div>

            <div className="flex flex-wrap items-center gap-1">
              {confirmingClear ? (
                <>
                  <Button variant="destructive" size="sm" onClick={clearAll}>
                    Clear {plural(draft.rows.length, 'track')}
                  </Button>
                  <Button variant="ghost" size="sm" onClick={() => setConfirmingClear(false)}>
                    Keep
                  </Button>
                </>
              ) : (
                <Button
                  variant="ghost"
                  size="sm"
                  disabled={draft.entries.length === 0}
                  onClick={() => setConfirmingClear(true)}
                >
                  <Trash2 aria-hidden="true" />
                  Clear
                </Button>
              )}
            </div>
          </div>

          <div className="flex min-h-0 flex-col gap-3 lg:flex-1 lg:overflow-auto">
            <Card size="sm">
              <CardContent className="p-0">
                {draft.entries.length === 0 ? (
                  <div
                    className={cn('p-4', overEmpty && 'bg-primary/5')}
                    onDragOver={(event) => {
                      // Prevent the browser's own "open the dropped file" for anything that
                      // is not a library drag; only a library drag gets a copy cursor.
                      event.preventDefault();
                      const fromTree = isLibraryDrag(event);
                      event.dataTransfer.dropEffect = fromTree ? 'copy' : 'none';
                      if (fromTree) setOverEmpty(true);
                    }}
                    onDragLeave={() => setOverEmpty(false)}
                    onDrop={(event) => {
                      event.preventDefault();
                      addToEnd(entriesFromDrag(event));
                      setOverEmpty(false);
                    }}
                  >
                    <Empty>
                      <EmptyHeader>
                        <EmptyMedia variant="icon">
                          <ListMusic aria-hidden="true" />
                        </EmptyMedia>
                        <EmptyTitle>No rows yet</EmptyTitle>
                        <EmptyDescription>
                          Drag a file or folder from Sources into this card, or press a file to
                          add it at the end.
                        </EmptyDescription>
                      </EmptyHeader>
                    </Empty>
                  </div>
                ) : (
                  <>
                    <ol className="flex flex-col">
                      {draft.entries.map((entry, index) => {
                        const expandedHere = expanded.has(entry.id);
                        const label = entry.kind === 'folder' ? entry.name : fileName(entry.path);

                        return (
                          <li
                            key={entry.id}
                            {...dragProps(index)}
                            className={cn(
                              'border-b last:border-b-0',
                              entryDropAt === index && 'bg-primary/10',
                              entryFrom === index && 'opacity-60',
                            )}
                          >
                            {entry.kind === 'track' ? (
                              <div className="flex items-center gap-2 px-3 py-2">
                                <GripVertical
                                  className="size-4 shrink-0 text-muted-foreground"
                                  aria-hidden="true"
                                />
                                <span className="w-6 shrink-0 text-right text-xs text-muted-foreground tabular-nums">
                                  {index + 1}
                                </span>
                                <TrackInfo path={entry.path} facts={facts.map.get(entry.path)} />
                                <FactsLine facts={facts.map.get(entry.path)} />
                                <BlockActions
                                  index={index}
                                  count={draft.entries.length}
                                  label={label}
                                  onUp={() => moveEntry(draft.id, index, index - 1)}
                                  onDown={() => moveEntry(draft.id, index, index + 1)}
                                  onRemove={() => removeEntry(draft.id, index)}
                                />
                              </div>
                            ) : (
                              <div className="flex flex-col">
                                <div className="flex items-center gap-2 px-3 py-2">
                                  <GripVertical
                                    className="size-4 shrink-0 text-muted-foreground"
                                    aria-hidden="true"
                                  />
                                  <span className="w-6 shrink-0 text-right text-xs text-muted-foreground tabular-nums">
                                    {index + 1}
                                  </span>
                                  <Button
                                    variant="ghost"
                                    size="icon-sm"
                                    onClick={() => toggleExpanded(entry.id)}
                                    aria-expanded={expandedHere}
                                    aria-label={`${expandedHere ? 'Collapse' : 'Expand'} ${entry.name}`}
                                  >
                                    {expandedHere ? (
                                      <ChevronDown aria-hidden="true" />
                                    ) : (
                                      <ChevronRight aria-hidden="true" />
                                    )}
                                  </Button>
                                  <Folder
                                    aria-hidden="true"
                                    className="size-4 shrink-0 text-muted-foreground"
                                  />
                                  <div className="flex min-w-0 flex-1 flex-col gap-0.5">
                                    <span className="truncate text-sm font-medium" title={entry.path}>
                                      {entry.name}
                                    </span>
                                    <div className="flex min-w-0 items-center gap-2">
                                      <span className="truncate text-xs text-muted-foreground">
                                        {entry.path}
                                      </span>
                                      <span className="shrink-0 text-xs text-muted-foreground tabular-nums">
                                        {plural(entry.rows.length, 'track')}
                                      </span>
                                    </div>
                                  </div>
                                  <BlockActions
                                    index={index}
                                    count={draft.entries.length}
                                    label={label}
                                    onUp={() => moveEntry(draft.id, index, index - 1)}
                                    onDown={() => moveEntry(draft.id, index, index + 1)}
                                    onRemove={() => removeEntry(draft.id, index)}
                                  />
                                </div>
                                {expandedHere ? (
                                  <ul className="flex flex-col border-t bg-muted/20">
                                    {entry.rows.map((path) => (
                                      <li
                                        key={`${entry.id}:${path}`}
                                        className="flex items-center gap-2 border-b px-3 py-1.5 ps-14 last:border-b-0"
                                      >
                                        <TrackInfo path={path} facts={facts.map.get(path)} />
                                        <FactsLine facts={facts.map.get(path)} />
                                      </li>
                                    ))}
                                  </ul>
                                ) : null}
                              </div>
                            )}
                          </li>
                        );
                      })}
                    </ol>

                    {/* The end is a drop target of its own: a block inserts before itself, so
                        appending needs somewhere after the last block to land. */}
                    <div
                      className={cn(
                        'flex items-center gap-2 border-t px-3 py-2.5 text-xs text-muted-foreground',
                        overEnd && 'bg-primary/5 text-foreground',
                      )}
                      onDragOver={(event) => {
                        event.preventDefault();
                        event.stopPropagation();
                        const fromTree = isLibraryDrag(event);
                        event.dataTransfer.dropEffect = fromTree ? 'copy' : 'none';
                        if (fromTree) setOverEnd(true);
                      }}
                      onDragLeave={() => setOverEnd(false)}
                      onDrop={(event) => {
                        event.preventDefault();
                        event.stopPropagation();
                        addToEnd(entriesFromDrag(event));
                        setOverEnd(false);
                      }}
                    >
                      <Plus className="size-4 shrink-0" aria-hidden="true" />
                      Drop a file or folder here to add it at the end
                    </div>
                  </>
                )}
              </CardContent>
            </Card>

            <p className="text-xs text-muted-foreground">
              Drag a row or a folder to move it; a folder moves as one block. Use the arrows
              with a keyboard. This draft reaches the player when Process sends an image.
            </p>
          </div>
        </section>
      </div>
    </div>
  );
}

export function PlaylistEditor({ id }: { id: string }) {
  const drafts = useDrafts();
  const draft = findDraft(drafts, id);

  // The drafts are read once at boot; until then an unknown id is "loading", not "missing".
  if (!draft && !draftsLoaded()) return <EditorLoading />;
  if (!draft) return <EditorMissing />;
  // Keyed by id: opening another list resets the name field and the drag state.
  return <Editor key={draft.id} draft={draft} />;
}

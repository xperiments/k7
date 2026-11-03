import { useEffect, useId, useMemo, useRef, useState } from 'react';
import { ArrowDown, ArrowUp, ArrowUpDown, GripVertical, ListMusic, Trash2 } from 'lucide-react';
import { cn } from 'cn';

import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
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
import {
  draftsLoaded,
  moveRow,
  removeRow,
  updateDraft,
  useDrafts,
  type Draft,
} from '../../playlists/drafts';
import {
  baseName,
  needsFacts,
  SORT_LABELS,
  SORT_RULES,
  sortRows,
  type SortFacts,
  type SortRule,
} from '../../playlists/sort';
import { navigate, routeHref } from '../router';

/**
 * One draft's rows: the order, the sorts, and the move/remove controls (front door §4.3).
 *
 * Reordering is native HTML5 drag and drop — no dependency — and the same move is reachable
 * from the keyboard through the up/down buttons, which is why those buttons exist rather
 * than being a fallback nobody uses. A sort is applied **once**: it rewrites the rows and
 * the rule is forgotten, so a row the user moves afterwards stays where they put it.
 *
 * The measurements a sort needs come from the device (`library_track`), eight at a time,
 * and are cached for this visit. A row the Library does not hold is `null` and shown as
 * missing; a transport failure is not a missing row and says so instead.
 */

interface RowFacts extends SortFacts {
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
 * changes (a reorder does not, and a sort only reorders), and a path is asked for once:
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

function SortMenu({
  blocked,
  onSort,
}: {
  /** The fact-dependent rules are unavailable while the device is being asked. */
  blocked: boolean;
  onSort: (rule: SortRule) => void;
}) {
  return (
    <DropdownMenu>
      <DropdownMenuTrigger render={<Button variant="outline" size="sm" />}>
        <ArrowUpDown aria-hidden="true" />
        Sort by
      </DropdownMenuTrigger>
      <DropdownMenuContent align="end">
        {/* Base UI's label needs a group; outside one it throws (error #31). */}
        <DropdownMenuGroup>
          <DropdownMenuLabel>Applied once</DropdownMenuLabel>
          {SORT_RULES.map((rule) => (
            <DropdownMenuItem
              key={rule}
              disabled={blocked && needsFacts(rule)}
              onClick={() => onSort(rule)}
            >
              {SORT_LABELS[rule]}
            </DropdownMenuItem>
          ))}
        </DropdownMenuGroup>
      </DropdownMenuContent>
    </DropdownMenu>
  );
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
    <div className="mx-auto flex w-full max-w-2xl flex-col gap-3 p-4 sm:p-6">
      <Skeleton className="h-8 w-64" />
      <Skeleton className="h-24 w-full" />
      <Skeleton className="h-24 w-full" />
    </div>
  );
}

function Editor({ draft }: { draft: Draft }) {
  const facts = useRowFacts(draft.rows);
  const [name, setName] = useState(draft.name);
  const [dragFrom, setDragFrom] = useState<number | null>(null);
  const [dropAt, setDropAt] = useState<number | null>(null);
  const nameField = useId();

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

  const applySort = (rule: SortRule) => {
    updateDraft(draft.id, { rows: sortRows(draft.rows, rule, facts.map) });
  };

  const drop = (to: number, from: number | null) => {
    setDragFrom(null);
    setDropAt(null);
    if (from === null || from === to) return;
    moveRow(draft.id, from, to);
  };

  return (
    <div className="mx-auto flex w-full max-w-4xl flex-col gap-4 p-4 sm:p-6">
      <header className="flex flex-col gap-2">
        <div className="flex flex-wrap items-end justify-between gap-3">
          <div className="flex min-w-0 flex-1 flex-col gap-1">
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
          <div className="flex flex-wrap items-center gap-1.5">
            <SortMenu blocked={facts.loading || facts.error !== null} onSort={applySort} />
            <Button
              variant="ghost"
              size="sm"
              onClick={() => navigate(routeHref({ name: 'playlists' }))}
            >
              All playlists
            </Button>
          </div>
        </div>

        <p className="text-xs text-muted-foreground tabular-nums" role="status">
          rows: {draft.rows.length} · not on the card:{' '}
          {facts.error ? facts.error : missing === null ? 'checking…' : missing}
        </p>
      </header>

      <Card size="sm">
        <CardContent className="p-0">
          {draft.rows.length === 0 ? (
            <div className="p-4">
              <Empty>
                <EmptyHeader>
                  <EmptyMedia variant="icon">
                    <ListMusic aria-hidden="true" />
                  </EmptyMedia>
                  <EmptyTitle>No rows yet</EmptyTitle>
                  <EmptyDescription>
                    Add tracks from Media, or ask the model for a set and turn its results into a
                    draft.
                  </EmptyDescription>
                </EmptyHeader>
                <EmptyContent>
                  <Button size="sm" onClick={() => navigate(routeHref({ name: 'media' }))}>
                    Browse Media
                  </Button>
                </EmptyContent>
              </Empty>
            </div>
          ) : (
            <ol className="flex flex-col">
              {draft.rows.map((path, index) => {
                const row = facts.map.get(path);
                const missingRow = row === null;
                return (
                  <li
                    key={`${index}:${path}`}
                    draggable
                    onDragStart={(event) => {
                      setDragFrom(index);
                      event.dataTransfer.effectAllowed = 'move';
                      // Firefox refuses a drag with no data on it.
                      event.dataTransfer.setData('text/plain', String(index));
                    }}
                    onDragOver={(event) => {
                      if (dragFrom === null) return;
                      event.preventDefault();
                      event.dataTransfer.dropEffect = 'move';
                      if (dropAt !== index) setDropAt(index);
                    }}
                    onDragLeave={() => {
                      if (dropAt === index) setDropAt(null);
                    }}
                    onDrop={(event) => {
                      event.preventDefault();
                      const carried = Number(event.dataTransfer.getData('text/plain'));
                      drop(index, dragFrom ?? (Number.isInteger(carried) ? carried : null));
                    }}
                    onDragEnd={() => {
                      setDragFrom(null);
                      setDropAt(null);
                    }}
                    className={cn(
                      'flex items-center gap-2 border-b px-3 py-2 last:border-b-0',
                      dropAt === index && 'bg-primary/10',
                      dragFrom === index && 'opacity-60',
                    )}
                  >
                    <GripVertical
                      className="size-4 shrink-0 text-muted-foreground"
                      aria-hidden="true"
                    />
                    <span className="w-6 shrink-0 text-right text-xs text-muted-foreground tabular-nums">
                      {index + 1}
                    </span>

                    <div className="flex min-w-0 flex-1 flex-col gap-0.5">
                      <div className="flex min-w-0 items-center gap-2">
                        <span className="truncate text-sm" title={path}>
                          {row?.title || baseName(path)}
                        </span>
                        {missingRow ? (
                          <Badge variant="destructive-light">
                            Not on the card
                          </Badge>
                        ) : null}
                      </div>
                      <span className="truncate text-xs text-muted-foreground">{path}</span>
                    </div>

                    <div className="flex shrink-0 items-center gap-2 text-xs text-muted-foreground tabular-nums">
                      {row?.bpm != null ? <span>{row.bpm.toFixed(1)} BPM</span> : null}
                      {row?.key ? <span>{row.key}</span> : null}
                      {row?.energy != null ? <span>{Math.round(row.energy)} energy</span> : null}
                    </div>

                    <div className="flex shrink-0 items-center gap-0.5">
                      <Button
                        variant="ghost"
                        size="icon-sm"
                        disabled={index === 0}
                        onClick={() => moveRow(draft.id, index, index - 1)}
                        aria-label={`Move ${baseName(path)} up`}
                      >
                        <ArrowUp aria-hidden="true" />
                      </Button>
                      <Button
                        variant="ghost"
                        size="icon-sm"
                        disabled={index === draft.rows.length - 1}
                        onClick={() => moveRow(draft.id, index, index + 1)}
                        aria-label={`Move ${baseName(path)} down`}
                      >
                        <ArrowDown aria-hidden="true" />
                      </Button>
                      <Button
                        variant="ghost"
                        size="icon-sm"
                        onClick={() => removeRow(draft.id, index)}
                        aria-label={`Remove ${baseName(path)}`}
                      >
                        <Trash2 aria-hidden="true" />
                      </Button>
                    </div>
                  </li>
                );
              })}
            </ol>
          )}
        </CardContent>
      </Card>

      <p className="text-xs text-muted-foreground">
        Drag a row to move it, or use the arrows. Sorting applies once: the order is yours
        afterwards. This draft reaches the player when Process sends an image.
      </p>
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

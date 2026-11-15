import { useEffect, useId, useMemo, useState, type FormEvent } from 'react';
import { ListMusic, Plus, RefreshCw } from 'lucide-react';

import { Alert, AlertAction, AlertDescription } from '@/components/reui/alert';
import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog';
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
import { scopeFor } from '../../card/mark';
import { useDeviceView } from '../../card/session';
import { useListScope } from '../lists-scope';
import {
  deletePlaylist,
  fetchPlaylistRows,
  fetchPlaylists,
  PlaylistsError,
  selectPlaylist,
  type DevicePlaylists,
} from '../../device/playlists';
import { refreshDevice, useDeviceStatus } from '../../device/useDeviceStatus';
import {
  adoptDraft,
  clearRemovals,
  createDraft,
  draftsLoaded,
  removeList,
  useDrafts,
  useRemovals,
} from '../../playlists/drafts';
import {
  readCardLists,
  removeListFile,
  type CardLists,
} from '../../playlists/cardLists';
import {
  chooseListSource,
  joinLists,
  normaliseListId,
  type ListRecord,
  type ListRow,
  type ListSourceKind,
} from '../../playlists/lists';
import { plural } from '../format';
import { navigate, routeHref } from '../router';

/**
 * Playlists: the selected card's lists, once each.
 *
 * The area follows the header's card switcher, like Media. With the **Device card** selected the
 * player answers; with an SD or virtual card selected its own `k7.db` does, or this browser's copy
 * of the image it wrote there when the card is not open. A list appears once, badged by how far it
 * has **reached** — only here, on this card, on the player — and marked when the card's copy is
 * older or a deletion is waiting for the card (ADR 0092).
 *
 * The card is the store, so the two commands that change it are the same two the store has:
 * **Open** (adopting a card's list this browser has never seen) and **Delete** (which leaves a
 * pending removal until the card is written). Nothing here sends an image: Prepare is still the
 * only delivery (ADR 0051, ADR 0065), and the page says where each list has got to instead.
 */

function describeError(error: unknown): string {
  if (error instanceof PlaylistsError) {
    switch (error.status) {
      case 0:
        return 'The device did not answer. Check that the player is on.';
      case 404:
        return 'The player does not hold that list any more.';
      case 409:
        return 'That list has no track this library can find.';
      case 503:
        return 'The player has no Snapshot yet — send one from Prepare.';
      default:
        return error.message || `The player refused it (${error.status}).`;
    }
  }
  return error instanceof Error ? error.message : String(error);
}

function made(seconds: number): string {
  const at = new Date(seconds * 1000);
  return Number.isNaN(at.getTime()) ? 'an unknown date' : at.toLocaleDateString();
}

/** How far a list has got, in the row's own words. */
function reachLabel(row: ListRow): string {
  if (row.reach === 'player') {
    if (row.walking) return 'Walking';
    return row.selected ? 'Waiting for the card' : 'On the player';
  }
  return row.reach === 'card' ? 'On this card' : 'Only here';
}

/** The one condition worth a second badge, and only when there is one. */
function conditionLabel(row: ListRow): string | null {
  if (row.removalPending) return 'Removed, waiting for the card';
  if (row.stale) return 'Edited, the card has the older copy';
  return null;
}

/** One list: its name, how far it got, what it needs, and the one or two things to do with it. */
function ListEntry({
  row,
  record,
  busy,
  onOpen,
  onWalk,
  onRemove,
  onUndo,
}: {
  row: ListRow;
  /** The card's own copy of the list, when the card answered with its rows. */
  record: ListRecord | null;
  /** A command is in flight. */
  busy: boolean;
  onOpen: () => void;
  onWalk: () => void;
  onRemove: () => void;
  onUndo: () => void;
}) {
  const [confirming, setConfirming] = useState(false);
  const condition = conditionLabel(row);
  /* A list is openable when this browser holds it, or when the card does and it can be adopted;
   * a removal is undone by adopting the card's copy again. */
  const openable = row.draft || record !== null;
  const removable = openable && !row.removalPending;
  const label = `${row.name} (${reachLabel(row)})`;

  return (
    <li className="flex flex-wrap items-center gap-2 border-b px-3 py-2.5 last:border-b-0">
      <div className="flex min-w-0 flex-1 flex-col gap-0.5">
        <div className="flex min-w-0 flex-wrap items-center gap-1.5">
          <span className="truncate text-sm font-light">{row.name}</span>
          <Badge variant="outline" size="sm">
            {reachLabel(row)}
          </Badge>
          {condition ? (
            <Badge variant="outline" size="sm">
              {condition}
            </Badge>
          ) : null}
        </div>
        <span className="text-xs text-muted-foreground tabular-nums">
          {plural(row.rows, 'track')} · made {made(row.createdAt)}
        </span>
      </div>
      <div className="flex flex-wrap items-center gap-1.5">
        {row.removalPending ? (
          record !== null ? (
            <Button variant="outline" size="sm" disabled={busy} onClick={onUndo}>
              Undo removal
            </Button>
          ) : null
        ) : openable ? (
          <Button variant="outline" size="sm" disabled={busy} onClick={onOpen} aria-label={`Open ${label}`}>
            Open
          </Button>
        ) : null}
        {row.reach === 'player' && !row.removalPending ? (
          <Button
            variant="outline"
            size="sm"
            disabled={busy || row.walking}
            onClick={onWalk}
            aria-label={`Walk ${row.name}`}
          >
            {row.walking ? 'Walking' : 'Walk this'}
          </Button>
        ) : null}
        {removable ? (
          confirming ? (
            <>
              <Button variant="solid" size="sm" onClick={onRemove}>
                Delete list
              </Button>
              <Button variant="ghost" size="sm" onClick={() => setConfirming(false)}>
                Keep
              </Button>
            </>
          ) : (
            <Button
              variant="ghost"
              size="sm"
              onClick={() => setConfirming(true)}
              aria-label={`Delete ${row.name}`}
            >
              Delete
            </Button>
          )
        ) : null}
      </div>
    </li>
  );
}

/** The one way a list starts: a name, then the editor. */
function NewPlaylistDialog({
  open,
  onOpenChange,
}: {
  open: boolean;
  onOpenChange: (next: boolean) => void;
}) {
  const [name, setName] = useState('');
  const field = useId();

  const submit = (event: FormEvent) => {
    event.preventDefault();
    const trimmed = name.trim();
    if (!trimmed) return;
    const draft = createDraft(trimmed);
    setName('');
    onOpenChange(false);
    navigate(routeHref({ name: 'playlist', id: draft.id }));
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent>
        <form onSubmit={submit} className="flex flex-col gap-4">
          <DialogHeader>
            <DialogTitle>New playlist</DialogTitle>
            <DialogDescription>
              Name it, then choose its tracks from the card&apos;s files.
            </DialogDescription>
          </DialogHeader>
          <div className="flex flex-col gap-1">
            <label htmlFor={field} className="text-xs text-muted-foreground">
              Name
            </label>
            <Input
              id={field}
              value={name}
              onChange={(event) => setName(event.target.value)}
              placeholder="Late night 8A"
              maxLength={60}
              autoComplete="off"
            />
          </div>
          <DialogFooter>
            <Button type="button" variant="outline" size="sm" onClick={() => onOpenChange(false)}>
              Cancel
            </Button>
            <Button type="submit" variant="solid" size="sm" disabled={name.trim().length === 0}>
              Create playlist
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  );
}

/** What the page says it is showing, in one line, per source. */
function sourceLine(
  source: ListSourceKind | null,
  deviceView: boolean,
  reachable: boolean,
): string {
  if (source === null) return 'Reading this card…';
  if (source === 'device') return 'The player’s lists, and the one it is walking.';
  if (source === 'card') return 'The lists this card carries.';
  if (deviceView) {
    return reachable
      ? 'The player holds no card it can name, so there are no lists to read.'
      : 'The player is not answering, so its lists cannot be read.';
  }
  return 'This card is not open, so its lists cannot be read here. A list named now waits in this browser until the card is opened.';
}

export function PlaylistsArea() {
  const deviceView = useDeviceView();
  const scope = useListScope();
  const { status, reachable } = useDeviceStatus();
  const drafts = useDrafts();
  const removals = useRemovals();

  const [answer, setAnswer] = useState<CardLists | null>(null);
  /** The card's directory was asked and answered; false means it is not open. */
  const [read, setRead] = useState(false);
  const [player, setPlayer] = useState<DevicePlaylists | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<{ kind: 'error' | 'success'; text: string } | null>(null);
  const [busy, setBusy] = useState(false);
  const [reading, setReading] = useState(false);
  const [naming, setNaming] = useState(false);
  const [reload, setReload] = useState(0);

  const deviceCard = status?.card === true;
  /* The card's directory is the source when it is this computer's card and it answers. The
   * Device card is the player and nothing else (ADR 0095). */
  const source: ListSourceKind | null = deviceView
    ? chooseListSource({ deviceView, deviceCard, cardOpen: false })
    : read
      ? chooseListSource({ deviceView, deviceCard, cardOpen: answer !== null })
      : null;

  /* The answer for the selected source, read once per card, source or explicit reload. */
  const holdsThisCard = status?.mark != null && scope === scopeFor({ id: status.mark });
  useEffect(() => {
    if (deviceView) return;
    let live = true;
    setLoading(true);
    setError(null);
    setRead(false);
    void (async () => {
      try {
        const lists = await readCardLists(scope);
        if (!live) return;
        setAnswer(lists);
        setRead(true);
        /* The player's index only settles how far a list has got when it holds this card. */
        setPlayer(holdsThisCard && reachable ? await fetchPlaylists().catch(() => null) : null);
      } catch (failure) {
        if (live) setError(describeError(failure));
      } finally {
        if (live) setLoading(false);
      }
    })();
    return () => {
      live = false;
    };
  }, [deviceView, scope, reload, holdsThisCard, reachable]);

  /* The Device card's answer comes from the player. */
  useEffect(() => {
    if (!deviceView) return;
    let live = true;
    setLoading(true);
    setError(null);
    void (async () => {
      try {
        const lists = await fetchPlaylists();
        if (!live) return;
        setAnswer(null);
        setPlayer(lists);
      } catch (failure) {
        if (live) setError(describeError(failure));
      } finally {
        if (live) setLoading(false);
      }
    })();
    return () => {
      live = false;
    };
  }, [deviceView, deviceCard, reload]);

  const joined = useMemo(
    () =>
      joinLists({
        card: answer === null ? null : { kind: 'card', lists: answer.lists },
        player:
          player === null
            ? null
            : {
                lists: player.lists.map((list) => ({
                  id: list.id,
                  name: list.name,
                  createdAt: list.created,
                  rows: list.rows,
                  fingerprint: list.fingerprint,
                })),
                selected: player.selected,
                walking: player.walking,
              },
        drafts,
        removals,
      }),
    [answer, player, drafts, removals],
  );

  /** The card's own records, by id: what adopting one, and undoing a removal, needs. */
  const records = useMemo(() => {
    const map = new Map<string, ListRecord>();
    for (const list of answer?.lists ?? []) map.set(normaliseListId(list.id), list);
    return map;
  }, [answer]);

  /* A pending removal whose list the source no longer carries is done — the card was written,
   * or never had the list. It is dropped here rather than left as a marker on nothing. Only an
   * answer that was actually read can say that; an unread card answers nothing. */
  useEffect(() => {
    if (loading || error !== null || source === null || source === 'none') return;
    /* Only an answer that arrived can say a list is gone; an error or an empty read says
     * nothing about the card. */
    const answered = source === 'device' ? player !== null : answer !== null;
    if (!answered) return;
    const draftsHere = new Set(drafts.map((draft) => draft.id));
    const onPlayer = new Set((player?.lists ?? []).map((list) => normaliseListId(list.id)));
    const done = removals.filter(
      (id) => !records.has(id) && !onPlayer.has(id) && !draftsHere.has(id),
    );
    if (done.length > 0) clearRemovals(done);
  }, [loading, error, source, answer, records, player, drafts, removals]);

  /**
   * Delete a list from the source that carries it: the player's file when the player holds
   * it, else the card's file here. The marker stays until one of them takes the removal.
   */
  const remove = (row: ListRow) => {
    removeList(row.id);
    const onPlayer = player?.lists.some((list) => list.id === row.id) === true;
    const done = onPlayer
      ? deletePlaylist(row.id).then(() => true)
      : removeListFile(scope, row.id);
    void done
      .then((ok) => {
        if (ok) clearRemovals([row.id]);
        else
          setNotice({
            kind: 'error',
            text: 'The source would not take the removal yet. It is kept here.',
          });
      })
      .catch((failure: unknown) => {
        setNotice({ kind: 'error', text: describeError(failure) });
      });
  };

  const walk = async (id: string) => {
    setBusy(true);
    setNotice(null);
    try {
      await selectPlaylist(id);
      // The command changes what the broadcast walks, so the shell's own device tile and this
      // page's "Walking" badge both need the new answer, not the one from five seconds ago.
      await refreshDevice();
      setReload((value) => value + 1);
      setNotice({ kind: 'success', text: 'The player is walking that list now.' });
    } catch (failure) {
      setNotice({ kind: 'error', text: describeError(failure) });
    } finally {
      setBusy(false);
    }
  };

  /* A gesture, so the grant may be asked for: this is the one action that reads the card's own
   * bytes rather than this browser's copy of them. */
  const readTheCard = async () => {
    setReading(true);
    setNotice(null);
    try {
      const next = await readCardLists(scope, { ask: true });
      if (next !== null) {
        setAnswer(next);
        setRead(true);
      } else {
        setNotice({ kind: 'error', text: 'This browser has no handle for that card. Open it in Sources first.' });
      }
    } catch (failure) {
      setNotice({ kind: 'error', text: describeError(failure) });
    } finally {
      setReading(false);
    }
  };

  /**
   * Open a list, wherever it lives. A card in the computer hands over its file; a list the
   * player holds hands over its rows, which is what makes the Device card editable without
   * this browser ever having opened that card (ADR 0095).
   */
  const open = (row: ListRow) => {
    if (row.draft) {
      navigate(routeHref({ name: 'playlist', id: row.id }));
      return;
    }
    const record = records.get(row.id);
    if (record !== undefined) {
      const draft = adoptDraft(record);
      navigate(routeHref({ name: 'playlist', id: draft.id }));
      return;
    }
    const held = player?.lists.find((list) => list.id === row.id);
    if (held === undefined) return;
    setBusy(true);
    void fetchPlaylistRows(row.id)
      .then((answer) => {
        const draft = adoptDraft({
          id: row.id,
          name: answer.name,
          createdAt: held.created,
          rows: answer.rows,
        });
        setBusy(false);
        navigate(routeHref({ name: 'playlist', id: draft.id }));
      })
      .catch((failure: unknown) => {
        setBusy(false);
        setNotice({ kind: 'error', text: describeError(failure) });
      });
  };

  const undoRemoval = (row: ListRow) => {
    const record = records.get(row.id);
    if (record !== undefined) adoptDraft(record);
  };

  const emptyFor = (kind: ListSourceKind) => {
    if (kind === 'device') {
      return (
        <Empty>
          <EmptyHeader>
            <EmptyMedia variant="icon">
              <ListMusic aria-hidden="true" />
            </EmptyMedia>
            <EmptyTitle>The player has no lists</EmptyTitle>
            <EmptyDescription>
              A list becomes the player’s when the editor writes its file to the card, not from
              here.
            </EmptyDescription>
          </EmptyHeader>
          <EmptyContent>
            <Button variant="solid" size="sm" onClick={() => navigate(routeHref({ name: 'prepare' }))}>
              Open Prepare
            </Button>
          </EmptyContent>
        </Empty>
      );
    }
    if (kind === 'none') {
      return (
        <Empty>
          <EmptyHeader>
            <EmptyMedia variant="icon">
              <ListMusic aria-hidden="true" />
            </EmptyMedia>
            <EmptyTitle>
              {deviceView ? 'The player has no lists to read' : 'This card is not open'}
            </EmptyTitle>
            <EmptyDescription>
              {deviceView
                ? 'It is not answering, or it holds no card it can name.'
                : 'Its lists are files on the card. Open it to read them — or name one here, and it waits in this browser until the card is opened.'}
            </EmptyDescription>
          </EmptyHeader>
          <EmptyContent>
            <Button
              variant="solid"
              size="sm"
              onClick={() =>
                deviceView ? navigate(routeHref({ name: 'prepare' })) : void readTheCard()
              }
              disabled={!deviceView && reading}
            >
              {deviceView ? 'Open Sources' : reading ? 'Opening…' : 'Open the card'}
            </Button>
          </EmptyContent>
        </Empty>
      );
    }
    return (
      <Empty>
        <EmptyHeader>
          <EmptyMedia variant="icon">
            <ListMusic aria-hidden="true" />
          </EmptyMedia>
          <EmptyTitle>No lists on this card yet</EmptyTitle>
          <EmptyDescription>
            Name one, and it is written to the card while the card is open.
          </EmptyDescription>
        </EmptyHeader>
        <EmptyContent>
          <Button variant="solid" size="sm" onClick={() => setNaming(true)}>
            <Plus aria-hidden="true" />
            New playlist
          </Button>
        </EmptyContent>
      </Empty>
    );
  };

  const waiting = loading || !draftsLoaded() || source === null;

  return (
    <div className="mx-auto flex w-full max-w-4xl flex-col gap-6 p-4 sm:p-6">
      <header className="flex flex-wrap items-start justify-between gap-3">
        <div className="flex min-w-0 flex-col gap-0.5">
          <h1 className="text-xl font-light tracking-tight">Playlists</h1>
          <p className="text-xs text-muted-foreground">
            {sourceLine(source, deviceView, reachable)}
          </p>
        </div>
        <div className="flex flex-wrap items-center gap-1.5">
          {source === 'none' && !deviceView ? (
            <Button variant="outline" size="sm" onClick={() => void readTheCard()} disabled={reading}>
              <RefreshCw aria-hidden="true" />
              {reading ? 'Opening…' : 'Open the card'}
            </Button>
          ) : null}
          <Button variant="solid" size="sm" onClick={() => setNaming(true)}>
            <Plus aria-hidden="true" />
            New playlist
          </Button>
        </div>
      </header>

      {notice ? (
        <Alert
          variant={notice.kind === 'error' ? 'destructive' : 'success'}
          role={notice.kind === 'error' ? 'alert' : 'status'}
        >
          <AlertDescription>{notice.text}</AlertDescription>
          <AlertAction>
            <Button variant="outline" size="sm" onClick={() => setNotice(null)}>
              Dismiss
            </Button>
          </AlertAction>
        </Alert>
      ) : null}

      <Card size="sm">
        <CardContent className="p-0">
          {waiting ? (
            <div className="flex flex-col gap-2 p-3">
              <Skeleton className="h-9 w-full" />
              <Skeleton className="h-9 w-full" />
            </div>
          ) : error ? (
            <div className="flex flex-col gap-2 p-3">
              <p className="text-sm text-destructive" role="status">
                {error}
              </p>
              <div>
                <Button variant="solid" size="sm" onClick={() => setReload((value) => value + 1)}>
                  Try again
                </Button>
              </div>
            </div>
          ) : joined.rows.length === 0 ? (
            <div className="p-3">{emptyFor(source)}</div>
          ) : (
            <>
              {player !== null && player.selected !== null && player.missing > 0 ? (
                <p className="px-3 pt-3 text-xs text-muted-foreground tabular-nums">
                  The walking list skips {plural(player.missing, 'row')} the card does not have.
                </p>
              ) : null}
              <ul className="flex flex-col">
                {joined.rows.map((row) => (
                  <ListEntry
                    key={row.id}
                    row={row}
                    record={records.get(row.id) ?? null}
                    busy={busy}
                    onOpen={() => open(row)}
                    onWalk={() => void walk(row.id)}
                    onRemove={() => remove(row)}
                    onUndo={() => undoRemoval(row)}
                  />
                ))}
              </ul>
            </>
          )}
        </CardContent>
      </Card>

      <NewPlaylistDialog open={naming} onOpenChange={setNaming} />
    </div>
  );
}

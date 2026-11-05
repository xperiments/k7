import { useCallback, useEffect, useId, useState, type FormEvent } from 'react';
import { ListMusic, Plus, Sparkles } from 'lucide-react';

import { Alert, AlertAction, AlertDescription } from '@/components/reui/alert';
import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from '@/components/ui/card';
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
  fetchPlaylists,
  PlaylistsError,
  selectPlaylist,
  type DevicePlaylists,
} from '../../device/playlists';
import { refreshDevice } from '../../device/useDeviceStatus';
import {
  createDraft,
  deleteDraft,
  draftsLoaded,
  useDrafts,
  type Draft,
} from '../../playlists/drafts';
import { plural } from '../format';
import { navigate, routeHref } from '../router';

/**
 * Playlists, in two halves that must not be confused (front door §4.2).
 *
 * **On the device** is what the last accepted image said, and which list the broadcast is
 * walking: reading it is a GET, choosing one is the `select` command. **Drafts** are this
 * browser's copies; sending them rewrites the lists inside the last image and posts it, so
 * a draft reaches the player only when
 * Process builds an image and posts it, which is what keeps ADR 0051's one-write rule true.
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
        return 'The player has no Snapshot yet — send one from Process.';
      default:
        return error.message || `The player refused it (${error.status}).`;
    }
  }
  return error instanceof Error ? error.message : String(error);
}

function created(seconds: number): string {
  const at = new Date(seconds * 1000);
  return Number.isNaN(at.getTime()) ? 'an unknown date' : at.toLocaleDateString();
}

/** One list the device holds. Walking it is the only write this page can do. */
function DeviceRow({
  list,
  chosen,
  walking,
  disabled,
  onWalk,
}: {
  list: DevicePlaylists['lists'][number];
  /** This is the list the device was told to walk. */
  chosen: boolean;
  /** …and it is walking now. A chosen list is not walking when the card is out. */
  walking: boolean;
  /** This row's own command is in flight. */
  disabled: boolean;
  onWalk: (id: string) => void;
}) {
  return (
    <li className="flex flex-wrap items-center gap-2 border-b py-2.5 last:border-b-0">
      <div className="flex min-w-0 flex-1 flex-col gap-0.5">
        <div className="flex min-w-0 items-center gap-2">
          <span className="truncate text-sm font-medium">{list.name}</span>
          {walking ? (
            <Badge variant="success-light">Walking</Badge>
          ) : chosen ? (
            <Badge variant="warning-light">Waiting for the card</Badge>
          ) : null}
        </div>
        <span className="text-xs text-muted-foreground tabular-nums">
          {plural(list.rows, 'track')} · made {created(list.created)}
        </span>
      </div>
      <Button
        variant={chosen ? 'outline' : 'secondary'}
        size="sm"
        disabled={walking || disabled}
        onClick={() => onWalk(list.id)}
      >
        {walking ? 'Walking' : disabled ? 'Starting…' : 'Walk this'}
      </Button>
    </li>
  );
}

/** One draft. Deleting one loses work, so it is confirmed in place rather than in a dialog. */
function DraftRow({ draft }: { draft: Draft }) {
  const [confirming, setConfirming] = useState(false);

  return (
    <li className="flex flex-wrap items-center gap-2 border-b py-2.5 last:border-b-0">
      <div className="flex min-w-0 flex-1 flex-col gap-0.5">
        <div className="flex min-w-0 items-center gap-2">
          <span className="truncate text-sm font-medium">{draft.name}</span>
          <Badge variant="info-light">
            Not sent yet
          </Badge>
        </div>
        <span className="text-xs text-muted-foreground tabular-nums">
          {plural(draft.rows.length, 'track')} · made {created(draft.createdAt)}
        </span>
      </div>
      <div className="flex flex-wrap items-center gap-1.5">
        <Button
          variant="outline"
          size="sm"
          onClick={() => navigate(routeHref({ name: 'playlist', id: draft.id }))}
          aria-label={`Open ${draft.name}`}
        >
          Open
        </Button>
        {confirming ? (
          <>
            <Button variant="destructive" size="sm" onClick={() => deleteDraft(draft.id)}>
              Delete draft
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
            aria-label={`Delete ${draft.name}`}
          >
            Delete
          </Button>
        )}
      </div>
    </li>
  );
}

/** The one way a list starts: a name, then the editor. */
function NewPlaylist() {
  const [name, setName] = useState('');
  const field = useId();

  const submit = (event: FormEvent) => {
    event.preventDefault();
    const trimmed = name.trim();
    if (!trimmed) return;
    const draft = createDraft(trimmed);
    setName('');
    navigate(routeHref({ name: 'playlist', id: draft.id }));
  };

  return (
    <form
      onSubmit={submit}
      className="flex flex-col gap-2 border-t pt-3 sm:flex-row sm:items-end"
      aria-label="New playlist"
    >
      <div className="flex min-w-0 flex-1 flex-col gap-1">
        <label htmlFor={field} className="text-xs text-muted-foreground">
          New playlist
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
      <Button type="submit" size="sm" disabled={name.trim().length === 0}>
        <Plus aria-hidden="true" />
        Create playlist
      </Button>
    </form>
  );
}

export function PlaylistsArea() {
  const drafts = useDrafts();
  const [device, setDevice] = useState<DevicePlaylists | null>(null);
  const [loading, setLoading] = useState(true);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [walking, setWalking] = useState<string | null>(null);
  const [notice, setNotice] = useState<{ kind: 'error' | 'success'; text: string } | null>(null);

  const load = useCallback(async (quiet = false) => {
    if (!quiet) setLoading(true);
    try {
      setDevice(await fetchPlaylists());
      setLoadError(null);
    } catch (error) {
      setLoadError(describeError(error));
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const walk = async (id: string) => {
    setWalking(id);
    setNotice(null);
    try {
      await selectPlaylist(id);
      // The command changes what the broadcast walks, so the shell's own device tile and
      // this page's "Walking" badge both need the new answer, not the one from five
      // seconds ago.
      await refreshDevice();
      await load(true);
      setNotice({ kind: 'success', text: 'The player is walking that list now.' });
    } catch (error) {
      setNotice({ kind: 'error', text: describeError(error) });
    } finally {
      setWalking(null);
    }
  };

  const selected = device?.selected ?? null;
  /* The choice and the walk are different facts: a card that is out keeps the choice. */
  const isWalking = Boolean(device?.walking && selected);
  const hasLists = Boolean(device && device.lists.length > 0);

  return (
    <div className="mx-auto flex w-full max-w-4xl flex-col gap-6 p-4 sm:p-6">
      <header className="flex flex-wrap items-start justify-between gap-3">
        <div className="flex min-w-0 flex-col gap-0.5">
          <h1 className="text-xl font-semibold tracking-tight">Playlists</h1>
        </div>
        <Button
          variant="outline"
          size="sm"
          nativeButton={false}
          render={<a href={routeHref({ name: 'ask' })} />}
        >
          <Sparkles aria-hidden="true" />
          Ask for a set
        </Button>
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

      <Card>
        <CardHeader>
          <CardTitle>On the device</CardTitle>
          <CardDescription>
            The lists the last image carried, and the one the broadcast is walking.
          </CardDescription>
        </CardHeader>
        <CardContent>
          {loading ? (
            <div className="flex flex-col gap-2">
              <Skeleton className="h-10 w-full" />
              <Skeleton className="h-10 w-full" />
            </div>
          ) : loadError ? (
            <div className="flex flex-col gap-2">
              <p className="text-sm text-destructive" role="status">
                {loadError}
              </p>
              <div>
                <Button variant="outline" size="sm" onClick={() => void load()}>
                  Try again
                </Button>
              </div>
            </div>
          ) : hasLists && device ? (
            <>
              {selected !== null && device.missing > 0 ? (
                <p className="pb-2 text-xs text-muted-foreground tabular-nums">
                  The walking list skips {plural(device.missing, 'row')} the card does not have.
                </p>
              ) : null}
              <ul className="flex flex-col">
                {device.lists.map((list) => (
                  <DeviceRow
                    key={list.id}
                    list={list}
                    chosen={list.id === selected}
                    walking={isWalking && list.id === selected}
                    disabled={walking !== null}
                    onWalk={(id) => void walk(id)}
                  />
                ))}
              </ul>
            </>
          ) : (
            <Empty>
              <EmptyHeader>
                <EmptyMedia variant="icon">
                  <ListMusic aria-hidden="true" />
                </EmptyMedia>
                <EmptyTitle>The player has no lists</EmptyTitle>
                <EmptyDescription>
                  A list reaches the player inside the image Process sends, not from this page.
                </EmptyDescription>
              </EmptyHeader>
              <EmptyContent>
                <Button size="sm" onClick={() => navigate(routeHref({ name: 'process' }))}>
                  Open Process
                </Button>
              </EmptyContent>
            </Empty>
          )}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Drafts</CardTitle>
          <CardDescription>
            A draft becomes the player’s copy when the next run writes the card: the lists
            travel inside the image, and editing one after a run rewrites the card’s image
            while it is still open.
          </CardDescription>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          {!draftsLoaded() ? (
            <div className="flex flex-col gap-2">
              <Skeleton className="h-10 w-full" />
              <Skeleton className="h-10 w-full" />
            </div>
          ) : drafts.length > 0 ? (
            <ul className="flex flex-col">
              {drafts.map((draft) => (
                <DraftRow key={draft.id} draft={draft} />
              ))}
            </ul>
          ) : (
            <Empty>
              <EmptyHeader>
                <EmptyMedia variant="icon">
                  <ListMusic aria-hidden="true" />
                </EmptyMedia>
                <EmptyTitle>No drafts yet</EmptyTitle>
                <EmptyDescription>
                  Name one below, then add rows from the card or from a search.
                </EmptyDescription>
              </EmptyHeader>
            </Empty>
          )}
          <NewPlaylist />
        </CardContent>
      </Card>
    </div>
  );
}

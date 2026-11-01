import { useEffect, useState } from 'react';
import { Alert, AlertAction, AlertDescription, AlertTitle } from '@/components/reui/alert';
import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import { libraryStats, statsCoverage, type McpStats } from '../../device/mcp';
import { refreshDevice, useDeviceStatus } from '../../device/useDeviceStatus';
import { DropZone } from '../DropZone';
import { plural } from '../format';
import { routeHref } from '../router';
import { useAnalysis } from '../state/analysis';

/**
 * Card → Scan: two acts, named apart because they happen on different machines.
 *
 * "Read a card here" is the **Scan** — this browser walking a card that is in this
 * computer. "The player's library" is the **Rescan** — the device rebuilding its own
 * Library from the card it is holding. They share a page because they share a card and
 * nothing else, and calling both "scan" would be the one thing that makes the page
 * confusing.
 */

type Rescan =
  | { state: 'idle' }
  | { state: 'sending' }
  | { state: 'accepted' }
  | { state: 'busy'; text: string }
  | { state: 'failed'; text: string };

export function ScanArea() {
  const { modelSet, setupError, items, notice, dismissNotice, addFound, addCard } = useAnalysis();
  const { status, reachable } = useDeviceStatus();
  const [rescan, setRescan] = useState<Rescan>({ state: 'idle' });
  const [stats, setStats] = useState<McpStats | null>(null);
  const coverage = statsCoverage(stats);
  const [statsError, setStatsError] = useState<string | null>(null);

  /* Coverage is the device's own answer about its Snapshot (library_stats), so it is read
   * when the device is reachable and again whenever its Library generation changes. */
  useEffect(() => {
    if (!reachable) {
      setStats(null);
      setStatsError(null);
      return;
    }
    let cancelled = false;
    libraryStats()
      .then((next) => {
        if (cancelled) return;
        setStats(next);
        setStatsError(null);
      })
      .catch((error: unknown) => {
        if (cancelled) return;
        setStats(null);
        setStatsError(error instanceof Error ? error.message : String(error));
      });
    return () => {
      cancelled = true;
    };
  }, [reachable, status?.generation]);

  const rescanLibrary = async () => {
    setRescan({ state: 'sending' });
    try {
      const response = await fetch('/api/rescan', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: '{}',
      });
      const text = await response.text();
      let body: { ok?: boolean; error?: string } = {};
      try {
        body = text ? (JSON.parse(text) as typeof body) : {};
      } catch {
        /* the body is the device's error page; the status says enough */
      }

      if (response.status === 202) {
        setRescan({ state: 'accepted' });
      } else if (response.status === 409) {
        setRescan({
          state: 'busy',
          text: body.error ?? 'A scan is already running.',
        });
      } else {
        setRescan({
          state: 'failed',
          text: body.error ?? `The player answered ${response.status}.`,
        });
      }
    } catch {
      setRescan({ state: 'failed', text: 'The player did not answer; is it on the same network?' });
    } finally {
      void refreshDevice();
    }
  };

  /* The one reason a rescan cannot be asked for; `sending` disables the button itself, so
   * it does not read as a fault in the device. */
  const unavailable = !reachable
    ? 'the device is not answering'
    : !status?.card
      ? 'there is no card in the player'
      : null;

  return (
    <div className="mx-auto flex w-full max-w-5xl flex-col gap-8 px-4 py-6 sm:px-6">
      <section className="flex flex-col gap-3">
        <header className="flex flex-col gap-0.5">
          <h1 className="text-xl font-semibold tracking-tight">Scan</h1>
          <p className="text-sm text-muted-foreground">
            Read a card that is in this computer, or ask the player to rebuild the Library it is
            holding.
          </p>
        </header>

        {notice && (
          <Alert
            variant={notice.kind === 'error' ? 'destructive' : 'success'}
            role={notice.kind === 'error' ? 'alert' : 'status'}
          >
            <AlertDescription>{notice.text}</AlertDescription>
            <AlertAction>
              <Button variant="outline" size="sm" onClick={dismissNotice}>
                Dismiss
              </Button>
            </AlertAction>
          </Alert>
        )}

        <h2 className="text-base font-semibold tracking-tight">Read a card here</h2>
        <p className="text-sm text-muted-foreground">
          The page measures in this browser and never uploads a file. Choosing the card opens it
          read-write, so the image can be written back to it; a dropped folder is read-only.
        </p>
        <DropZone
          disabled={!modelSet}
          disabledReason={
            setupError ? 'The models are missing — open Process for what to do.' : undefined
          }
          onFiles={(found, skipped) => addFound(found, skipped, true)}
          onCard={addCard}
        />
        {items.length > 0 && (
          <p className="text-xs text-muted-foreground">
            {plural(items.length, 'track')} in the queue ·{' '}
            <a
              href={routeHref({ name: 'process' })}
              className="font-medium text-primary underline-offset-4 transition-colors hover:underline"
            >
              Open Process
            </a>
          </p>
        )}
      </section>

      <section className="flex flex-col gap-3">
        <h2 className="text-base font-semibold tracking-tight">The player&apos;s library</h2>
        <p className="text-sm text-muted-foreground">
          The device rebuilds its Library from the card it is holding. Nothing here touches the
          card, and a Snapshot that described the old tracks stays where it is.
        </p>

        <Card size="sm">
          <CardContent className="flex flex-col gap-3">
            <div className="flex flex-wrap items-center gap-x-3 gap-y-1.5">
              <Badge
                variant={
                  !reachable ? 'warning-light' : status?.card ? 'success-light' : 'secondary'
                }
              >
                {!reachable
                  ? 'device unreachable'
                  : status?.card
                    ? 'card in the player'
                    : 'no card in the player'}
              </Badge>
              {status && (
                <span className="text-xs text-muted-foreground tabular-nums">
                  {plural(status.tracks, 'track')} · Library generation {status.generation}
                </span>
              )}
              {stats && (
                <span className="text-xs text-muted-foreground tabular-nums">
                  {!stats.available
                    ? 'the player has no library yet'
                    : coverage
                      ? `Snapshot covers ${coverage.analysed} of ${stats.tracks} (${coverage.percent}%)`
                      : stats.snapshot?.state === 'no_update'
                        ? 'no Snapshot on the player yet'
                        : `Snapshot ${stats.snapshot?.state ?? 'unavailable'}`}
                </span>
              )}
              {!stats && reachable && !statsError && (
                <span className="text-xs text-muted-foreground">reading the Snapshot…</span>
              )}
              {statsError && (
                <span className="text-xs text-muted-foreground">
                  Snapshot unreadable: {statsError}
                </span>
              )}
            </div>

            <div className="flex flex-wrap items-center gap-2">
              <Button
                variant="outline"
                size="sm"
                onClick={() => void rescanLibrary()}
                disabled={unavailable !== null || rescan.state === 'sending'}
              >
                {rescan.state === 'sending' ? 'Asking…' : 'Rescan'}
              </Button>
              {unavailable && (
                <span className="text-xs text-muted-foreground">
                  Rescan is unavailable: {unavailable}
                </span>
              )}
            </div>

            {rescan.state === 'accepted' && (
              <Alert variant="success" role="status">
                <AlertTitle>The player accepted the rescan</AlertTitle>
                <AlertDescription>
                  It is rebuilding its Library now; the count and generation in this card follow
                  when it answers the next poll.
                </AlertDescription>
              </Alert>
            )}
            {rescan.state === 'busy' && (
              <Alert variant="warning">
                <AlertTitle>A scan is already running</AlertTitle>
                <AlertDescription>{rescan.text}</AlertDescription>
              </Alert>
            )}
            {rescan.state === 'failed' && (
              <Alert variant="destructive">
                <AlertTitle>The scan did not start</AlertTitle>
                <AlertDescription>{rescan.text}</AlertDescription>
              </Alert>
            )}
          </CardContent>
        </Card>
      </section>
    </div>
  );
}

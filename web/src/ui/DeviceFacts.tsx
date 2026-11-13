import { useEffect, useState } from 'react';

import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Skeleton } from '@/components/ui/skeleton';
import { getDevice } from '../device/device';
import { libraryStats, statsCoverage, type McpStats } from '../device/mcp';
import { DeviceError } from '../device/transport';
import { refreshDevice, useDeviceStatus } from '../device/useDeviceStatus';
import { plural } from './format';

/**
 * The device's own facts: the library it reports, the Snapshot it carries, and the one
 * write it accepts. The drawing beside them is the K7 cassette, painted in the ink.
 *
 * This is the block that used to close the Player page. It belongs with the Device card
 * selection on Sources: the facts are about the card in the player, and that is the card
 * the switcher names when this shows. The Player page keeps the broadcast.
 */

function Fact({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="flex flex-col gap-0.5">
      <dt className="text-xs text-muted-foreground">{label}</dt>
      <dd className="break-words text-sm">{children}</dd>
    </div>
  );
}

type Rescan =
  | { state: 'idle' }
  | { state: 'sending' }
  | { state: 'accepted' }
  | { state: 'busy'; text: string }
  | { state: 'failed'; text: string };

function snapshotState(state: string): string {
  switch (state) {
    case 'ok':
      return 'valid';
    case 'no_update':
      return 'none flashed yet';
    case 'no_partition':
      return 'no snapshot partition in this build';
    case 'invalid':
      return 'refused (the image did not validate)';
    default:
      return state;
  }
}

export function DeviceFacts() {
  const { status, reachable } = useDeviceStatus();
  const [stats, setStats] = useState<McpStats | null>(null);
  const [statsFailed, setStatsFailed] = useState(false);

  // Coverage and the Snapshot's age: one call, and a failure is a dash rather than a page.
  const hasStatus = status !== null;
  const generation = status?.generation ?? 0;
  useEffect(() => {
    if (!hasStatus) return;
    let cancelled = false;
    setStatsFailed(false);
    void libraryStats()
      .then((answer) => {
        if (!cancelled) setStats(answer);
      })
      .catch(() => {
        if (cancelled) return;
        setStats(null);
        setStatsFailed(true);
      });
    return () => {
      cancelled = true;
    };
  }, [hasStatus, generation]);

  const snapshot = stats?.snapshot ?? null;
  const coverage = statsCoverage(stats);
  const builtAt =
    snapshot && typeof snapshot.generation === 'number' && snapshot.generation > 0
      ? new Date(snapshot.generation * 1000).toLocaleString()
      : null;
  const libraryEmpty = Boolean(stats && !stats.available);

  /* The Rescan: the device rebuilding its Library from the card it is holding. It is the one
   * write this block can ask for, so it sits with the facts it changes. */
  const [rescan, setRescan] = useState<Rescan>({ state: 'idle' });
  const rescanUnavailable = !reachable
    ? 'the device is not answering'
    : !status?.card
      ? 'there is no card in the player'
      : null;

  const rescanLibrary = async () => {
    setRescan({ state: 'sending' });
    try {
      const result = await getDevice().request('rescan');
      setRescan(
        result.ok
          ? { state: 'accepted' }
          : { state: 'failed', text: 'The player did not start the scan.' },
      );
    } catch (error) {
      /* A refusal keeps the device's own sentence: "a scan is already running" is its
       * fact, and 409 is how it says so (E1). */
      if (error instanceof DeviceError && error.status === 409) {
        setRescan({ state: 'busy', text: error.message });
      } else if (error instanceof DeviceError && error.code === 'refused') {
        setRescan({
          state: 'failed',
          text: error.message || `The player answered ${error.status}.`,
        });
      } else if (error instanceof DeviceError) {
        setRescan({ state: 'failed', text: 'The player did not answer; is it on the same network?' });
      } else {
        setRescan({ state: 'failed', text: error instanceof Error ? error.message : String(error) });
      }
    } finally {
      void refreshDevice();
    }
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>The device</CardTitle>
      </CardHeader>
      <CardContent className="flex flex-col gap-5 md:flex-row md:items-center md:gap-6">
        <div
          aria-hidden="true"
          className="k7-cassette w-full max-w-72 shrink-0 self-center text-primary/70 md:w-64 lg:w-72"
        />
        <div className="flex min-w-0 flex-1 flex-col gap-4">
          <dl className="grid grid-cols-2 gap-x-6 gap-y-3 sm:grid-cols-3">
            <Fact label="Tracks">
              {status ? (
                <span className="tabular-nums">{plural(status.tracks, 'track')}</span>
              ) : (
                '—'
              )}
            </Fact>
            <Fact label="Snapshot">
              {statsFailed ? (
                '—'
              ) : stats === null ? (
                <Skeleton className="h-4 w-32" />
              ) : libraryEmpty ? (
                'the device has no library'
              ) : !snapshot || snapshot.state !== 'ok' ? (
                `no Snapshot (${snapshotState(snapshot?.state ?? 'unknown')})`
              ) : coverage ? (
                <span className="tabular-nums">
                  {coverage.analysed} of {coverage.tracks} analysed ({coverage.percent}%)
                </span>
              ) : (
                '—'
              )}
            </Fact>
            <Fact label="Built">
              {statsFailed ? (
                '—'
              ) : stats === null ? (
                <Skeleton className="h-4 w-32" />
              ) : (
                builtAt ?? '—'
              )}
            </Fact>
          </dl>

          <div className="flex flex-wrap items-center gap-2 border-t pt-3">
            <Button
              variant="outline"
              size="sm"
              onClick={() => void rescanLibrary()}
              disabled={rescanUnavailable !== null || rescan.state === 'sending'}
            >
              {rescan.state === 'sending' ? 'Asking…' : 'Rescan the card'}
            </Button>
            <span className="text-xs text-muted-foreground">
              {rescanUnavailable
                ? `Unavailable: ${rescanUnavailable}.`
                : rescan.state === 'accepted'
                  ? 'Accepted — the library is rebuilding.'
                  : rescan.state === 'busy'
                    ? rescan.text
                    : rescan.state === 'failed'
                      ? rescan.text
                      : 'Rebuilds the library from the card; nothing is measured.'}
            </span>
          </div>
        </div>
      </CardContent>
    </Card>
  );
}

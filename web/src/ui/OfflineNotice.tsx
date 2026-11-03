import { useSyncExternalStore } from 'react';

import { Button } from '@/components/ui/button';
import { offlineSnapshot, prepareOffline, subscribeOffline } from './prefetchStore';

const MIB = 1024 * 1024;

/**
 * What the offline prep is doing, when it is doing anything (O3–O5).
 *
 * Nothing renders while it is idle or finished: a completed download is not news, and the
 * quota is a Settings fact rather than a permanent fixture of every screen.
 */
export function OfflineNotice() {
  const state = useSyncExternalStore(subscribeOffline, offlineSnapshot, offlineSnapshot);

  if (state.status === 'running') {
    return (
      <div
        role="status"
        className="fixed bottom-4 right-4 z-40 flex max-w-sm flex-col gap-1 border border-border bg-card px-3 py-2 text-xs text-card-foreground"
      >
        <span className="font-medium">Preparing offline analysis</span>
        <span className="tabular-nums text-muted-foreground">
          {state.done} of {state.total} weights ·{' '}
          {Math.round((state.bytesTotal / MIB) * 10) / 10} MB once, then it is local
        </span>
      </div>
    );
  }

  if (state.status === 'error') {
    return (
      <div
        role="alert"
        className="fixed bottom-4 right-4 z-40 flex max-w-sm flex-col gap-2 border border-border bg-card px-3 py-2 text-xs text-card-foreground"
      >
        <span className="font-medium">The offline prep stopped</span>
        <span className="text-muted-foreground">{state.message}</span>
        <Button size="sm" variant="outline" onClick={() => void prepareOffline(true)}>
          Try again
        </Button>
      </div>
    );
  }

  return null;
}

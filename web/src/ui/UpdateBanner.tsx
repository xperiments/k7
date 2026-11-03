import { useSyncExternalStore } from 'react';

import { Button } from '@/components/ui/button';
import { applyUpdateNow, subscribeUpdate, updateReady } from './updateStore';

/**
 * A new version is ready (O6): a line at the bottom of the window, not a takeover. The
 * worker deliberately does not `skipWaiting`, so a reload is the user's decision and a run
 * in flight is never replaced underneath itself.
 */
export function UpdateBanner() {
  const ready = useSyncExternalStore(subscribeUpdate, updateReady, updateReady);
  if (!ready) return null;

  return (
    <div
      role="status"
      className="fixed inset-x-0 bottom-0 z-50 flex items-center justify-between gap-4 border-t border-border bg-card px-4 py-2 text-xs text-card-foreground"
    >
      <span>A new version of K7 is ready.</span>
      <Button size="sm" variant="outline" onClick={() => applyUpdateNow()}>
        Reload
      </Button>
    </div>
  );
}

import { MonitorDown } from 'lucide-react';
import { useSyncExternalStore } from 'react';

import { Button } from '@/components/ui/button';
import { installSnapshot, onInsecureOrigin, promptInstallNow, subscribeInstall } from './installStore';

/**
 * Installing K7 as an app (A10): one explicit control, and only where it can work.
 *
 * - an **insecure origin** never sees it: Chrome cannot install there, and A10 says a dead
 *   button is worse than no button;
 * - **installed** renders nothing, because the app is already the app;
 * - **unsupported** says so in a sentence rather than offering a dialog that will not open
 *   (Safari and Firefox), and names what does work;
 * - **unknown** renders nothing: a Chromium that has not fired `beforeinstallprompt` yet
 *   would answer `unavailable` if pressed.
 */
export function InstallButton() {
  const state = useSyncExternalStore(subscribeInstall, installSnapshot, installSnapshot);

  if (onInsecureOrigin() || state === 'installed' || state === 'unknown') return null;

  if (state === 'installable') {
    return (
      <Button
        variant="outline"
        size="sm"
        className="w-full justify-start gap-2"
        onClick={() => void promptInstallNow()}
      >
        <MonitorDown aria-hidden="true" />
        Install K7
      </Button>
    );
  }

  return (
    <p className="px-2 text-xs text-sidebar-foreground/70">
      This browser does not install apps. Chrome and Edge do.
    </p>
  );
}

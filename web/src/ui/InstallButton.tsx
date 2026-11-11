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
        className="h-10 w-fit min-w-24 justify-start gap-2.5 rounded-[6px] border border-primary p-1 pr-3 font-light"
        onClick={() => void promptInstallNow()}
      >
        <span
          aria-hidden="true"
          className="grid size-8 shrink-0 place-items-center rounded-full bg-primary text-primary-foreground [&_svg]:size-4 [&_svg]:stroke-[1.25]"
        >
          <MonitorDown aria-hidden="true" />
        </span>
        Install K7
      </Button>
    );
  }

  return (
    <p className="px-2 text-xs text-muted-foreground">
      This browser does not install apps. Chrome and Edge do.
    </p>
  );
}

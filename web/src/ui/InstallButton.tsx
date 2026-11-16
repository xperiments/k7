import { MonitorDown } from 'lucide-react';
import { useSyncExternalStore } from 'react';

import { Button } from '@/components/ui/button';
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip';
import { cn } from '@/lib/utils';
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
export function InstallButton({ compact = false }: { compact?: boolean }) {
  const state = useSyncExternalStore(subscribeInstall, installSnapshot, installSnapshot);

  if (onInsecureOrigin() || state === 'installed' || state === 'unknown') return null;

  if (state === 'installable') {
    return (
      <Tooltip disabled={!compact}>
        <TooltipTrigger
          render={
            <Button
              className={cn(
                'h-12 w-full justify-start gap-2.5 rounded-[6px] border border-foreground/25 p-2 font-light',
                compact && 'justify-center px-2',
              )}
              onClick={() => void promptInstallNow()}
            >
              <span
                aria-hidden="true"
                className="grid size-8 shrink-0 place-items-center text-foreground [&_svg]:size-4 [&_svg]:stroke-[1.25]"
              >
                <MonitorDown aria-hidden="true" />
              </span>
              <span className={cn(compact && 'sr-only')}>Install K7</span>
            </Button>
          }
        />
        <TooltipContent side="right" hidden={!compact}>
          Install K7
        </TooltipContent>
      </Tooltip>
    );
  }

  if (compact) return null;

  return (
    <p className="px-2 text-xs text-muted-foreground">
      This browser does not install apps. Chrome and Edge do.
    </p>
  );
}

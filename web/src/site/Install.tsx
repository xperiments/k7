import { useEffect, useState, useSyncExternalStore } from 'react';
import { MonitorDown, MonitorX } from 'lucide-react';

import { Alert, AlertDescription, AlertTitle } from '@/components/reui/alert';
import { Button } from '@/components/ui/button';
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from '@/components/ui/card';
import { Spinner } from '@/components/ui/spinner';
import {
  queryLocalNetwork,
  watchLocalNetwork,
  type LocalNetworkState,
} from '../device/localNetwork';
import { installSnapshot, promptInstallNow, subscribeInstall } from '../ui/installStore';
import { installOffer } from './install-offer';

/**
 * The App's install, offered only where the App can run (k7-site.md S13).
 *
 * The decision is `install-offer.ts`; this is the surface. The install state comes from the
 * App's own store — the same `beforeinstallprompt` capture the App uses, started at the
 * Site's boot — and the permission is read and watched the same way the App's gate reads it.
 */
export function Install() {
  const install = useSyncExternalStore(subscribeInstall, installSnapshot, installSnapshot);
  const [network, setNetwork] = useState<LocalNetworkState | null>(null);

  useEffect(() => {
    let live = true;
    void queryLocalNetwork().then((state) => {
      if (live) setNetwork(state);
    });
    const stop = watchLocalNetwork((state) => {
      if (live) setNetwork(state);
    });
    return () => {
      live = false;
      stop();
    };
  }, []);

  const offer = installOffer(install, network);

  return (
    <Card>
      <CardHeader>
        <CardTitle>Install K7</CardTitle>
        <CardDescription>The App, in a window of its own on this computer.</CardDescription>
      </CardHeader>

      <CardContent className="grid gap-4">
        {offer === 'checking' ? (
          <p className="flex items-center gap-2 text-sm text-muted-foreground">
            <Spinner className="size-4" /> Checking this browser…
          </p>
        ) : null}

        {offer === 'unavailable' ? (
          <Alert variant="warning">
            <MonitorX aria-hidden="true" />
            <AlertTitle>K7 needs Chrome on a computer</AlertTitle>
            <AlertDescription>
              K7 reaches the player over your local network. Chrome is the only browser that
              asks for that permission, so the App cannot run here — and an installed icon
              would open a closed door.
            </AlertDescription>
          </Alert>
        ) : null}

        {offer === 'install' ? (
          <>
            <Button variant="solid" onClick={() => void promptInstallNow()}>
              <MonitorDown aria-hidden="true" />
              Install K7
            </Button>
            <p className="text-xs text-muted-foreground">
              Chrome opens it without an address bar. Installing is the recommended way to
              use K7.
            </p>
          </>
        ) : null}

        {offer === 'open' ? (
          <>
            <p className="text-sm">K7 is installed on this computer.</p>
            <Button render={<a href="/app" />}>Open K7</Button>
          </>
        ) : null}

        {offer === 'waiting' ? (
          <>
            <p className="text-sm text-muted-foreground">
              Chrome offers the install once it has seen this page — or use the install icon
              in the address bar.
            </p>
            <Button variant="outline" render={<a href="/app" />}>
              Open K7
            </Button>
          </>
        ) : null}
      </CardContent>
    </Card>
  );
}

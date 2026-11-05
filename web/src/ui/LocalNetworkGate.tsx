import { useCallback, useEffect, useState } from 'react';
import { MonitorX, Plug, RefreshCw, ShieldAlert, ShieldOff } from 'lucide-react';

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
  requestLocalNetwork,
  watchLocalNetwork,
  type LocalNetworkState,
} from '../device/localNetwork';

/** The gate's own first state: the permission has not been read yet. */
type Phase = 'checking' | LocalNetworkState;

/**
 * Local Network Access, in front of the whole App.
 *
 * The player is `http://k7.local`, and an HTTPS page may only reach it while Chrome's
 * `local-network` permission is granted (LNA §2.5). Every page here reads the player, so
 * there is no useful half of the App to show before the answer: the gate renders the areas
 * on `granted` and a blocking screen otherwise, and it follows the permission afterwards so
 * a grant or a revocation in Chrome's site settings takes effect without a reload.
 *
 * A flow screen, one centered card: the state, the sentence that explains it, and the one
 * action that can change it. Chrome is the only browser with the API, which is a product
 * decision stated plainly rather than hidden behind a browser-shaped fallback.
 */
export function LocalNetworkGate({ children }: { children: React.ReactNode }) {
  const [state, setState] = useState<Phase>('checking');
  const [busy, setBusy] = useState(false);
  const [note, setNote] = useState<string | null>(null);

  useEffect(() => {
    let live = true;
    void queryLocalNetwork().then((next) => {
      if (live) setState(next);
    });
    const stop = watchLocalNetwork((next) => {
      if (live) setState(next);
    });
    return () => {
      live = false;
      stop();
    };
  }, []);

  /* The fetch is the prompt, so this is only called from the button: Chrome refuses to ask
   * for a permission that no person asked for. A refusal is the denied screen, not an
   * error banner over an App that cannot load anything anyway. */
  const allow = useCallback(async () => {
    setBusy(true);
    setNote(null);
    const granted = await requestLocalNetwork();
    setState(granted ? 'granted' : 'denied');
    if (!granted) {
      setNote(
        'Chrome did not grant access. If no prompt appeared, allow “Local network” in this site’s settings, then retry.',
      );
    }
    setBusy(false);
  }, []);

  const recheck = useCallback(async () => {
    setBusy(true);
    setState(await queryLocalNetwork());
    setBusy(false);
  }, []);

  if (state === 'granted') return <>{children}</>;

  return (
    <div className="flex min-h-dvh w-full items-center justify-center bg-background p-6 text-foreground">
      <Card className="w-full max-w-md">
        <CardHeader>
          <img
            src="/icons/k7logo.svg"
            alt="K7"
            draggable={false}
            className="mb-1 size-16 object-contain dark:invert"
          />
          <CardTitle className="text-xl">Local network access</CardTitle>
          <CardDescription>
            K7 talks to your player directly, at k7.local on this network. Chrome has to
            allow this page to reach it.
          </CardDescription>
        </CardHeader>
        <CardContent className="grid gap-4">
          {state === 'checking' ? (
            <p className="flex items-center gap-2 text-sm text-muted-foreground">
              <Spinner className="size-4" />
              Checking this browser’s local network access…
            </p>
          ) : null}

          {state === 'prompt' ? (
            <>
              <Alert variant="info">
                <ShieldAlert aria-hidden="true" />
                <AlertTitle>Chrome needs your permission</AlertTitle>
                <AlertDescription>
                  Allow local network access to load your card, playlists and library from
                  the player. Nothing leaves your network.
                </AlertDescription>
              </Alert>
              <Button onClick={() => void allow()} disabled={busy}>
                {busy ? (
                  <Spinner data-icon="inline-start" />
                ) : (
                  <Plug data-icon="inline-start" aria-hidden="true" />
                )}
                {busy ? 'Waiting for Chrome…' : 'Allow local network'}
              </Button>
              <p className="text-xs text-muted-foreground">
                Chrome asks once per site. If no prompt appears, the permission is already
                blocked — check this site’s settings.
              </p>
            </>
          ) : null}

          {state === 'denied' ? (
            <>
              <Alert variant="destructive">
                <ShieldOff aria-hidden="true" />
                <AlertTitle>Local network access is blocked</AlertTitle>
                <AlertDescription>
                  This site may not reach devices on your network, so the player cannot
                  answer. In Chrome, open this site’s settings → Local network → Allow, then
                  retry.
                </AlertDescription>
              </Alert>
              {note ? <p className="text-xs text-muted-foreground">{note}</p> : null}
              <Button variant="outline" onClick={() => void allow()} disabled={busy}>
                {busy ? <Spinner /> : <RefreshCw aria-hidden="true" />}
                Retry
              </Button>
            </>
          ) : null}

          {state === 'unsupported' ? (
            <>
              <Alert variant="warning">
                <MonitorX aria-hidden="true" />
                <AlertTitle>Chrome is required</AlertTitle>
                <AlertDescription>
                  K7 needs Chrome with Local Network Access to reach the player at k7.local.
                  This browser cannot ask for that permission, so the App stays closed here.
                </AlertDescription>
              </Alert>
              <Button variant="outline" onClick={() => void recheck()} disabled={busy}>
                {busy ? <Spinner /> : <RefreshCw aria-hidden="true" />}
                Check again
              </Button>
            </>
          ) : null}
        </CardContent>
      </Card>
    </div>
  );
}

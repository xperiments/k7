import { Alert, AlertAction, AlertDescription, AlertTitle } from '@/components/reui/alert';
import { Button } from '@/components/ui/button';
import { WEIGHTS_MODE } from '../../weights/config';
import { UNSCOPED, useCardScope, useDeviceView } from '../../card/session';
import { CardTree } from '../CardTree';
import { CardGrid } from '../CardGrid';
import { useAnalysis } from '../state/analysis';

/**
 * Card → Prepare: the Queue, measured, with an explicit Send.
 *
 * The header is sticky inside the shell's own scroll container, so the run's progress and
 * its actions stay put while the rows scroll under it. There is no second scroll region:
 * the shell owns the only one (front-door §4.1, "it is a run, not a page"), and an inner
 * `overflow-auto` would need a height this area is not given.
 */
export function PrepareArea() {
  const { composing, setupError, weightError, notice, dismissNotice, dismissWeightError } =
    useAnalysis();

  /* A card is open: the real slot or a virtual one. Its own `music/` tree replaces the
   * generic intake, because the directory to add to is now a fact, not a question. */
  const scope = useCardScope();
  const deviceView = useDeviceView();

  return (
    <div className="mx-auto flex w-full max-w-5xl flex-col gap-4 px-4 py-6 sm:px-6">
      {/* <header className="flex shrink-0 flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
        <div className="flex flex-col gap-0.5">
          <h1 className="text-xl font-light tracking-tight">Prepare</h1>
          <p className="max-w-prose text-sm text-muted-foreground">
            Tempo, key, mood and energy from MP3 &amp; FLAC, measured in this browser.
          </p>
        </div>
      </header> */}

      {setupError && (
        <Alert variant="destructive">
          <AlertTitle>The models did not load</AlertTitle>
          <AlertDescription>
            <p>{setupError}</p>
            <p>
              {WEIGHTS_MODE === 'local' ? (
                <>
                  Fetch the weights once:{' '}
                  <code className="bg-muted px-1 py-0.5 font-mono text-xs">
                    cd web &amp;&amp; npm run models
                  </code>
                </>
              ) : (
                'Connect this computer to the internet once, then reload.'
              )}
            </p>
          </AlertDescription>
        </Alert>
      )}

      {weightError && (
        <Alert variant="destructive">
          <AlertTitle>Analysis failed</AlertTitle>
          <AlertDescription>{weightError}</AlertDescription>
          <AlertAction>
            <Button variant="outline" size="sm" onClick={dismissWeightError}>
              Dismiss
            </Button>
          </AlertAction>
        </Alert>
      )}

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

      {/* The card shelf stays in view while the card's tree scrolls under it: which card is
          open is the context every action below depends on. */}
      <div className="sticky top-0 z-20 -mx-4 shrink-0 bg-background px-4 py-2 sm:-mx-6 sm:px-6">
        <CardGrid />
      </div>

      {composing && !deviceView ? (
        <div className="flex min-h-0 flex-1 flex-col gap-3">
          <div className="min-h-0 flex-1">
            <CardTree />
          </div>
        </div>
      ) : (
        <p className="shrink-0 text-xs text-muted-foreground">
          {deviceView
            ? 'The card in the player is a view of the device — its albums are on the Albums page.'
            : scope !== UNSCOPED
              ? 'The SD card in the player is written from a virtual card — pick one to compose.'
              : 'Select a card above — the SD card or a virtual one — to prepare it.'}
        </p>
      )}
    </div>
  );
}

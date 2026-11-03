import { useId } from 'react';
import { ScanLine } from 'lucide-react';
import { Alert, AlertAction, AlertDescription, AlertTitle } from '@/components/reui/alert';
import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import {
  Empty,
  EmptyContent,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from '@/components/ui/empty';
import { Progress } from '@/components/ui/progress';
import { Switch } from '@/components/ui/switch';
import { WEIGHTS_MODE } from '../../weights/config';
import { plural, shortHash } from '../format';
import { QueueList } from '../QueueList';
import { navigate, routeHref } from '../router';
import { useAnalysis } from '../state/analysis';

/**
 * Card → Process: the Queue, measured, with an explicit Send.
 *
 * The header is sticky inside the shell's own scroll container, so the run's progress and
 * its actions stay put while the rows scroll under it. There is no second scroll region:
 * the shell owns the only one (front-door §4.1, "it is a run, not a page"), and an inner
 * `overflow-auto` would need a height this area is not given.
 */
export function ProcessArea() {
  const {
    items,
    done,
    total,
    busy,
    busyStage,
    modelSet,
    setupError,
    weightError,
    notice,
    dismissNotice,
    dismissWeightError,
    mediaCount,
    lastSnapshot,
    saving,
    tagsOnly,
    setTagsOnly,
    confirmingClear,
    setConfirmingClear,
    writtenAt,
    cardLabel,
    clearAll,
    hasCardHandle,
  } = useAnalysis();

  /* The switch's own id lands on Base UI's hidden input, so the label needs a second id for
   * the name: without it the control is announced as an unnamed switch. */
  const tagsOnlyId = useId();
  const tagsOnlyLabelId = useId();

  const percent = total > 0 ? (done / total) * 100 : 0;

  return (
    <div className="mx-auto flex w-full max-w-5xl flex-col gap-4 px-4 py-6 sm:px-6">
      <header className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
        <div className="flex flex-col gap-0.5">
          <h1 className="text-xl font-semibold tracking-tight">Process</h1>
          <p className="text-sm text-muted-foreground">
            Tempo, key, mood and energy from MP3 &amp; FLAC, measured in this browser.
          </p>
        </div>
        {modelSet ? (
          <Badge variant="success-light">models {shortHash(modelSet)}</Badge>
        ) : setupError ? (
          <Badge variant="destructive-light">models missing</Badge>
        ) : (
          <Badge variant="secondary">loading models…</Badge>
        )}
      </header>

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

      <div className="sticky top-0 z-10 flex flex-col gap-2 border-b bg-background py-3">
        {total > 0 && (
          <div className="flex flex-wrap items-center gap-x-4 gap-y-1">
            <Progress
              value={percent}
              aria-label="Run progress"
              className="min-w-40 flex-1"
            />
            <span className="shrink-0 text-sm text-muted-foreground tabular-nums">
              {done} of {total} analysed
            </span>
            {busyStage && (
              <span className="shrink-0 text-xs text-muted-foreground" role="status">
                {busyStage}
              </span>
            )}
          </div>
        )}

        <div className="flex flex-wrap items-center gap-x-4 gap-y-2">
          <div className="flex items-center gap-2">
            <Switch
              id={tagsOnlyId}
              aria-labelledby={tagsOnlyLabelId}
              checked={tagsOnly}
              onCheckedChange={setTagsOnly}
            />
            <label
              id={tagsOnlyLabelId}
              htmlFor={tagsOnlyId}
              className="cursor-pointer text-sm text-muted-foreground"
              title="Use BPM and key from the file's own tags and measure only the tracks that are missing one"
            >
              Only measure what the tags lack
            </label>
          </div>

          <div className="ml-auto flex flex-wrap items-center gap-2">
            {/* Asked in place rather than in a dialog: a modal is 56 kB of the front-door
                partition, which is a lot of flash for one confirmation. */}
            {confirmingClear ? (
              <div
                className="flex flex-wrap items-center gap-2"
                role="group"
                aria-label="Confirm clearing the queue and the cache"
              >
                <span className="text-xs text-muted-foreground">Clear the queue and cache?</span>
                <Button
                  variant="destructive"
                  size="sm"
                  onClick={() => {
                    setConfirmingClear(false);
                    void clearAll();
                  }}
                >
                  Clear everything
                </Button>
                <Button variant="ghost" size="sm" onClick={() => setConfirmingClear(false)}>
                  Keep them
                </Button>
              </div>
            ) : (
              <Button
                variant="ghost"
                size="sm"
                onClick={() => setConfirmingClear(true)}
                disabled={busy || items.length === 0}
                title="Empty the queue and the measurement cache in this browser; the files on the card are not touched"
              >
                Clear
              </Button>
            )}
          </div>
        </div>
      </div>

      <Card size="sm">
        <CardContent className="flex flex-wrap items-center gap-x-2 gap-y-1 text-xs text-muted-foreground">
          <span className="font-medium text-foreground">Card</span>
          <span className="tabular-nums">{cardLabel}</span>
          <span aria-hidden="true">·</span>
          <span>
            {saving
              ? 'writing k7.db…'
              : writtenAt
                ? `k7.db written ${new Date(writtenAt).toLocaleTimeString()}`
                : hasCardHandle
                  ? 'k7.db is written when the run finishes'
                  : 'dropped card — read-only, so k7.db is not written'}
          </span>
          {lastSnapshot ? (
            <>
              <span aria-hidden="true">·</span>
              <span className="tabular-nums">
                {plural(lastSnapshot.tracks, 'track')} in the image
              </span>
            </>
          ) : null}
        </CardContent>
      </Card>

      {items.length === 0 ? (
        <Empty className="border border-dashed py-10">
          <EmptyHeader>
            <EmptyMedia variant="icon">
              <ScanLine aria-hidden="true" />
            </EmptyMedia>
            <EmptyTitle>{lastSnapshot ? 'The queue is empty' : 'No tracks measured yet'}</EmptyTitle>
            <EmptyDescription>
              {lastSnapshot
                ? 'The queue lives in this page, so a reload empties it. What the card holds is the image the last run wrote; scan the card again to measure what is new — nothing already measured is measured twice.'
                : setupError
                  ? 'The models did not load, so nothing can be measured — see the message above.'
                  : modelSet
                    ? 'Scan a card that is in this computer: the queue fills itself and every file is measured here, never uploaded.'
                    : 'Loading the models. The queue can be filled while they load.'}
            </EmptyDescription>
          </EmptyHeader>
          <EmptyContent>
            <Button size="sm" onClick={() => navigate(routeHref({ name: 'scan' }))}>
              Scan a card
            </Button>
          </EmptyContent>
        </Empty>
      ) : (
        <QueueList />
      )}

      {mediaCount > 0 && (
        <p className="text-xs text-muted-foreground">
          {plural(mediaCount, 'cover and tag')} kept in this browser, for the library views
        </p>
      )}
    </div>
  );
}

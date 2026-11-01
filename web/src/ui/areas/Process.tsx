import { useId, useState } from 'react';
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
import { formatBytes, plural, shortHash } from '../format';
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
    exportJson,
    sendToPlayer,
    saveToCard,
    saveSnapshotFile,
    resendLast,
    downloadLast,
    discardLast,
    clearAll,
    canSave,
    hasCardHandle,
  } = useAnalysis();

  /* The switch's own id lands on Base UI's hidden input, so the label needs a second id for
   * the name: without it the control is announced as an unnamed switch. */
  const tagsOnlyId = useId();
  const tagsOnlyLabelId = useId();

  const percent = total > 0 ? (done / total) * 100 : 0;

  /* Which action is running, so the label on the button the user pressed is the truth.
   * `saving` alone cannot say: it is one flag over four different writes. */
  const [pending, setPending] = useState<'send' | 'card' | 'file' | 'resend' | null>(null);
  const run = (action: 'send' | 'card' | 'file' | 'resend', work: () => Promise<void>) => {
    setPending(action);
    void work().finally(() => setPending(null));
  };

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
            <Button
              size="sm"
              onClick={() => run('send', sendToPlayer)}
              disabled={!canSave || saving}
              title="Send the image to the player; it writes it to its own flash and restarts."
            >
              {pending === 'send' ? 'Sending…' : 'Send to player'}
            </Button>
            {hasCardHandle && (
              <Button
                variant="outline"
                size="sm"
                onClick={() => run('card', saveToCard)}
                disabled={!canSave || saving}
                title="Write xtape.db at the card's root, in the folder opened at intake."
              >
                {pending === 'card' ? 'Writing…' : 'Save to card'}
              </Button>
            )}
            <Button
              variant="outline"
              size="sm"
              onClick={() => run('file', saveSnapshotFile)}
              disabled={!canSave || saving}
              title="Download xtape.db and move it to the card's root yourself"
            >
              {pending === 'file' ? 'Preparing…' : 'Download .db'}
            </Button>
            <Button variant="outline" size="sm" onClick={exportJson} disabled={done === 0}>
              Download JSON
            </Button>
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

      {lastSnapshot && (
        <Card size="sm">
          <CardContent className="flex flex-col gap-2 sm:flex-row sm:items-center sm:justify-between">
            <p className="text-xs text-muted-foreground tabular-nums">
              Last image: {plural(lastSnapshot.tracks, 'track')} ·{' '}
              {formatBytes(lastSnapshot.bytes.length)} ·{' '}
              {lastSnapshot.sentAt
                ? `accepted ${new Date(lastSnapshot.sentAt).toLocaleTimeString()}`
                : `built ${new Date(lastSnapshot.at).toLocaleTimeString()}`}
            </p>
            <div className="flex flex-wrap gap-1.5">
              <Button
                variant="ghost"
                size="sm"
                onClick={() => run('resend', resendLast)}
                disabled={saving}
                title="Send the same image again, without measuring anything"
              >
                {pending === 'resend' ? 'Sending…' : 'Send to player'}
              </Button>
              <Button variant="ghost" size="sm" onClick={downloadLast}>
                Download
              </Button>
              <Button variant="ghost" size="sm" onClick={() => void discardLast()}>
                Discard
              </Button>
            </div>
          </CardContent>
        </Card>
      )}

      {items.length === 0 ? (
        <Empty className="border border-dashed py-10">
          <EmptyHeader>
            <EmptyMedia variant="icon">
              <ScanLine aria-hidden="true" />
            </EmptyMedia>
            <EmptyTitle>No tracks measured yet</EmptyTitle>
            <EmptyDescription>
              {setupError
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

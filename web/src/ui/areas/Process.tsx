import { useId, useState } from 'react';
import { Alert, AlertAction, AlertDescription, AlertTitle } from '@/components/reui/alert';
import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent } from '@/components/ui/card';
import { Progress } from '@/components/ui/progress';
import { Switch } from '@/components/ui/switch';
import { WEIGHTS_MODE } from '../../weights/config';
import { plural, shortHash } from '../format';
import { DropZone } from '../DropZone';
import { QueueList } from '../QueueList';
import { useAnalysis } from '../state/analysis';
import { restoreCoversFromDevice } from '../cardWriter';

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
    running,
    pause,
    resume,
    busyStage,
    modelSet,
    setupError,
    weightError,
    notice,
    dismissNotice,
    dismissWeightError,
    mediaCount,
    metadataPending,
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
    canRescan,
    addFound,
    addCard,
    rescanCard,
    changeCard,
  } = useAnalysis();

  const [restoringCovers, setRestoringCovers] = useState(false);
  const [coverNote, setCoverNote] = useState<{ text: string; error: boolean } | null>(null);

  /* The card-in-player path for the cover cache. The card-in-computer path is automatic on
   * the pick; this one is a manual 8 MiB read from the SD card, so it is a click. */
  const restoreCovers = async () => {
    setRestoringCovers(true);
    setCoverNote(null);
    try {
      const count = await restoreCoversFromDevice();
      setCoverNote({
        text: `${plural(count, 'cover')} restored from the player; open Media to see them`,
        error: false,
      });
    } catch (error) {
      setCoverNote({
        text: error instanceof Error ? error.message : String(error),
        error: true,
      });
    } finally {
      setRestoringCovers(false);
    }
  };

  /* The switch's own id lands on Base UI's hidden input, so the label needs a second id for
   * the name: without it the control is announced as an unnamed switch. */
  const tagsOnlyId = useId();
  const tagsOnlyLabelId = useId();

  const percent = total > 0 ? (done / total) * 100 : 0;
  const queued = items.filter((item) => item.status === 'queued').length;

  /* Measuring is not the end of the run: the measured tags and the label are written
   * afterwards, one track at a time, and the image waits for them. One line, in the order
   * the run finishes.
   *
   * The count is kept out of the live region: it changes with every label, and a status that
   * reads "12 left… 11 left… 10 left" is noise. The phase is announced once; the number is
   * for the eyes. */
  const runPhase =
    metadataPending > 0 ? 'writing metadata' : saving ? 'writing k7.db…' : busyStage;
  const runCount =
    metadataPending > 0 ? ` · ${plural(metadataPending, 'track')} left` : null;

  return (
    <div className="mx-auto flex w-full max-w-5xl flex-col gap-4 px-4 py-6 sm:px-6">
      <header className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
        <div className="flex flex-col gap-0.5">
          <h1 className="text-xl font-semibold tracking-tight">Process</h1>
          <p className="max-w-prose text-sm text-muted-foreground">
            Tempo, key, mood and energy from MP3 &amp; FLAC, measured in this browser. A card
            opened read-write gets the values back as tags; a dropped folder can save a tagged
            copy from its row.
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
            {runPhase && (
              <span className="shrink-0 text-xs text-muted-foreground" role="status">
                {runPhase}
                {runCount ? <span aria-hidden="true">{runCount}</span> : null}
              </span>
            )}
            {!running && queued > 0 ? (
              <Button
                size="sm"
                onClick={resume}
                title={`Measure the remaining ${plural(queued, 'track')}`}
              >
                Resume
              </Button>
            ) : busy ? (
              <Button size="sm" variant="outline" onClick={pause}>
                Pause
              </Button>
            ) : null}
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
            {/* While the Queue is empty, the drop zone below carries the rescan: one
                action per state, and that is where a reload lands. */}
            {items.length > 0 && (
              <Button
                variant="outline"
                size="sm"
                onClick={() => void rescanCard()}
                disabled={busy || metadataPending > 0 || !canRescan}
                title={
                  metadataPending > 0
                    ? 'Wait for the card to finish writing its tags and label'
                    : canRescan
                      ? 'Read this card again, now: tracks already measured are recognised, not measured twice'
                      : 'No card is open in this page — use Change card'
                }
              >
                Rescan card
              </Button>
            )}
            <Button
              variant="outline"
              size="sm"
              onClick={() => void changeCard()}
              disabled={busy || metadataPending > 0}
              title={
                metadataPending > 0
                  ? 'Wait for the card to finish writing its tags and label'
                  : 'Open another card; a run in flight is left alone'
              }
            >
              Change card
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

      <Card size="sm">
        <CardContent className="flex flex-wrap items-center gap-x-2 gap-y-1 text-xs text-muted-foreground">
          <span className="font-medium text-foreground">Card</span>
          <span className="tabular-nums">{cardLabel}</span>
          <span aria-hidden="true">·</span>
          <span>
            {saving
              ? 'writing k7.db…'
              : metadataPending > 0
                ? `metadata still being written · ${plural(metadataPending, 'track')} left`
                : writtenAt
                  ? `k7.db written ${new Date(writtenAt).toLocaleTimeString()}`
                  : hasCardHandle
                    ? 'k7.db is written when the run and its metadata finish'
                    : 'k7.db and the file tags are written by a run on a card opened read-write'}
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
        <div className="flex flex-col gap-2">
          {lastSnapshot ? (
            <p className="text-xs text-muted-foreground">
              The queue lives in this page, so a reload empties it — and does not read the card
              on its own. What the card holds is the image the last run wrote; read the card
              again to measure what is new — nothing already measured is measured twice.
            </p>
          ) : null}
          <DropZone
            disabled={!modelSet}
            disabledReason={
              setupError ? 'The models are missing — see the message above.' : undefined
            }
            onFiles={(found, skipped) => addFound(found, skipped, true)}
            onCard={addCard}
            onRescan={canRescan ? () => void rescanCard() : undefined}
            rescanDisabled={busy || metadataPending > 0}
          />
        </div>
      ) : (
        <QueueList />
      )}

      <div className="flex flex-wrap items-center gap-2">
        <Button
          size="sm"
          variant="outline"
          onClick={() => void restoreCovers()}
          disabled={restoringCovers}
        >
          {restoringCovers ? 'Restoring…' : 'Restore covers from player'}
        </Button>
        {coverNote ? (
          <span
            className={`text-xs ${coverNote.error ? 'text-destructive' : 'text-muted-foreground'}`}
          >
            {coverNote.text}
          </span>
        ) : null}
      </div>

      {mediaCount > 0 && (
        <p className="text-xs text-muted-foreground">
          {plural(mediaCount, 'cover and tag')} kept in this browser, for the library views
        </p>
      )}
    </div>
  );
}

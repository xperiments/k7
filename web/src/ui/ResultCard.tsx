import { Badge, type BadgeProps } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import {
  Card,
  CardContent,
  CardDescription,
  CardFooter,
  CardHeader,
  CardTitle,
} from '@/components/ui/card';
import { Progress } from '@/components/ui/progress';
import type { Analysis } from '../domain/analysis';
import { formatBytes, formatDuration, formatPercent, shortHash } from './format';
import { MoodPanel } from './MoodPanel';
import { SourceChip } from './SourceChip';

interface ResultCardProps {
  analysis: Analysis;
  busy: boolean;
  cached: boolean;
  saving: boolean;
  onDeep: () => void;
  onSave: () => void;
}

interface Flag {
  label: string;
  variant: NonNullable<BadgeProps['variant']>;
}

const METRIC_LABEL = 'flex flex-wrap items-center gap-2 text-xs font-medium tracking-wide text-muted-foreground uppercase';

/** One track, measured: three headline numbers, the mood panel and the provenance line. */
export function ResultCard({
  analysis,
  busy,
  cached,
  saving,
  onDeep,
  onSave,
}: ResultCardProps) {
  const { tempo, key, mood, energy, energySource } = analysis;
  const window = analysis.provenance.window;

  /* Only the facts that are not already written next to their own value: a "tagged tempo"
   * badge would repeat the provenance chip beside the tempo, so it is not here. */
  const flags: Flag[] = [];
  if (cached) flags.push({ label: 'cached', variant: 'secondary' });
  if (tempo.engine === 'deep') flags.push({ label: 'deep tempo', variant: 'primary-light' });
  if (tempo.escalated) flags.push({ label: 'auto-escalated', variant: 'outline' });
  if (tempo.suggestEscalation) flags.push({ label: 'low confidence', variant: 'warning-light' });

  return (
    <article>
      <Card>
        <CardHeader>
          <CardTitle className="break-words">{analysis.track.name}</CardTitle>
          <CardDescription className="tabular-nums">
            {analysis.track.path !== analysis.track.name && (
              <span className="break-words">{analysis.track.path} · </span>
            )}
            {analysis.track.format.toUpperCase()} · {formatDuration(analysis.track.duration)} ·{' '}
            {formatBytes(analysis.track.size)}
          </CardDescription>
          {flags.length > 0 && (
            <div className="flex flex-wrap gap-1">
              {flags.map((flag) => (
                <Badge key={flag.label} variant={flag.variant}>
                  {flag.label}
                </Badge>
              ))}
            </div>
          )}
        </CardHeader>

        <CardContent className="flex flex-col gap-4">
          <div className="grid gap-4 border-b pb-4 sm:grid-cols-3">
            <section className="flex min-w-0 flex-col gap-1.5">
              <span className={METRIC_LABEL}>
                Tempo <SourceChip source={tempo.source} />
              </span>
              <div className="text-3xl font-semibold tracking-tight tabular-nums">
                {tempo.bpm.toFixed(1)}
                <small className="ml-1 text-xs font-medium text-muted-foreground">BPM</small>
              </div>
              {tempo.confidence !== null ? (
                <>
                  <Progress value={tempo.confidence * 100} aria-label="Tempo confidence" />
                  <p className="text-xs text-muted-foreground">
                    confidence {formatPercent(tempo.confidence)} · raw{' '}
                    {tempo.rawBpm.toFixed(1)}
                  </p>
                </>
              ) : (
                <p className="text-xs text-muted-foreground">from file tags, not re-measured</p>
              )}
            </section>

            <section className="flex min-w-0 flex-col gap-1.5">
              <span className={METRIC_LABEL}>
                Key <SourceChip source={key.source} />
              </span>
              <div className="text-3xl font-semibold tracking-tight">
                {key.tonic}{' '}
                <small className="ml-1 text-xs font-medium text-muted-foreground">
                  {key.mode}
                </small>
              </div>
              <p className="text-xl font-semibold text-primary tabular-nums">
                {key.camelot || '—'}
              </p>
              {key.strength !== null ? (
                <p className="text-xs text-muted-foreground">strength {key.strength.toFixed(2)}</p>
              ) : (
                <p className="text-xs text-muted-foreground">from file tags</p>
              )}
            </section>

            <section className="flex min-w-0 flex-col gap-1.5">
              <span className={METRIC_LABEL}>
                Energy <SourceChip source={energySource} />
              </span>
              <div className="text-3xl font-semibold tracking-tight tabular-nums">
                {energy !== null ? energy.toFixed(2) : '—'}
              </div>
              {energy !== null && <Progress value={energy * 100} aria-label="Energy" />}
              <p className="text-xs text-muted-foreground">
                {energySource === 'metadata'
                  ? 'from file tags'
                  : energySource === 'measured'
                    ? 'learned arousal'
                    : 'not measured'}
              </p>
            </section>
          </div>

          <MoodPanel mood={mood} />
        </CardContent>

        <CardFooter className="flex-col items-start gap-3 sm:flex-row sm:items-center sm:justify-between">
          <p className="text-xs text-muted-foreground tabular-nums">
            {window
              ? `window ${formatDuration(window.start)}–${formatDuration(window.start + window.duration)} · `
              : ''}
            models {shortHash(analysis.provenance.modelSet)} · analysed{' '}
            {new Date(analysis.provenance.analysedAt).toLocaleTimeString()}
          </p>
          <div className="flex flex-wrap gap-1.5">
            <Button
              variant="ghost"
              size="sm"
              onClick={onSave}
              disabled={saving}
              title="Downloads a copy with BPM, key, mood and energy tags. Browsers cannot overwrite the original file."
            >
              {saving ? 'Saving…' : 'Save tagged copy'}
            </Button>
            <Button
              variant="outline"
              size="sm"
              onClick={onDeep}
              disabled={busy}
              title={
                tempo.source === 'metadata'
                  ? 'Measure tempo with the accurate tracker, replacing the tag'
                  : 'Re-run tempo with the accurate tracker'
              }
            >
              {tempo.source === 'metadata' ? 'Measure tempo' : 'Deep analyse'}
            </Button>
          </div>
        </CardFooter>
      </Card>
    </article>
  );
}

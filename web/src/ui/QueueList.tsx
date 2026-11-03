import { useState } from 'react';
import { ChevronDown } from 'lucide-react';
import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Spinner } from '@/components/ui/spinner';
import { cn } from '@/lib/utils';
import { MOOD_TAGS, type Analysis, type MoodTag } from '../domain/analysis';
import type { QueueItem } from './queue';
import { ResultCard } from './ResultCard';
import { isWorking, statusWord } from './stage';
import { useAnalysis } from './state/analysis';

/**
 * The queue, one line each.
 *
 * Minimal is the only row: a measured Track shows its genre and its numbers on one line and
 * opens into the full card **in place**, because a page of thirty cards is a page nobody can
 * scan. The card did not change — it is the drill-in now instead of the default.
 *
 * One row is open at a time. The list is a run's progress, not a report to browse, and a
 * second open row would push the sticky header's work off the screen.
 */

/** The mood a row shows: the tagged label, else the strongest head, else nothing. */
function moodWord(analysis: Analysis | undefined): string {
  if (!analysis) return '—';
  const { mood } = analysis;
  if (mood.label) return mood.label;
  if (!mood.tags) return '—';
  let best: MoodTag | null = null;
  let score = -1;
  for (const tag of MOOD_TAGS) {
    if (mood.tags[tag] > score) {
      score = mood.tags[tag];
      best = tag;
    }
  }
  return best ?? '—';
}

/** The four numbers a measured row carries, aligned across rows by fixed widths. */
function MeasuredCells({ analysis }: { analysis: Analysis }) {
  const bpm = analysis.tempo.bpm;
  return (
    <>
      <span
        className="w-12 shrink-0 text-right text-sm tabular-nums text-muted-foreground"
        title="Folded BPM"
      >
        {bpm ? bpm.toFixed(1) : '—'}
      </span>
      <span
        className="w-8 shrink-0 text-right text-xs tabular-nums text-muted-foreground"
        title="Camelot key"
      >
        {analysis.key.camelot || '—'}
      </span>
      <span
        className="hidden w-16 shrink-0 truncate text-xs text-muted-foreground sm:block"
        title="Mood"
      >
        {moodWord(analysis)}
      </span>
    </>
  );
}

/**
 * The genre a row shows: the file's own tag, or a dash. It is not a measurement — no model
 * produces it — so it is the one cell a cached Track can show without being re-analysed.
 */
function GenreCell({ genre }: { genre: string | undefined }) {
  return (
    <span
      className="hidden w-24 shrink-0 truncate text-xs text-muted-foreground md:block"
      title={genre ? `Genre: ${genre}` : 'Genre'}
    >
      {genre || '—'}
    </span>
  );
}

interface RowProps {
  item: QueueItem;
  busy: boolean;
  open: boolean;
  onToggle: () => void;
  onDeep: () => void;
  onSave: () => void;
}

function MeasuredRow({ item, busy, open, onToggle, onDeep, onSave }: RowProps) {
  const { analysis } = item;
  if (!analysis) return null;
  return (
    <>
      <Button
        variant="ghost"
        size="default"
        className="w-full justify-start gap-3 text-left font-normal"
        aria-expanded={open}
        onClick={onToggle}
        title={item.path}
      >
        <span className="min-w-0 flex-1 truncate text-sm">{item.name}</span>
        <GenreCell genre={item.tags?.genre} />
        <MeasuredCells analysis={analysis} />
        <span className="w-20 shrink-0 truncate text-right text-xs text-muted-foreground">
          {statusWord(item)}
        </span>
        <ChevronDown
          className={cn(
            'size-4 shrink-0 text-muted-foreground transition-transform duration-200 ease-[cubic-bezier(0.22,1,0.36,1)] motion-reduce:transition-none',
            open && 'rotate-180',
          )}
          aria-hidden="true"
        />
      </Button>
      {open && (
        <div className="pb-4">
          <ResultCard
            analysis={analysis}
            busy={busy}
            cached={Boolean(item.cached)}
            saving={Boolean(item.saving)}
            onDeep={onDeep}
            onSave={onSave}
          />
        </div>
      )}
    </>
  );
}

function WorkingRow({ item }: { item: QueueItem }) {
  return (
    <div className="flex h-8 items-center gap-3 px-2.5">
      <span className="min-w-0 flex-1 truncate text-sm" title={item.path}>
        {item.name}
      </span>
      {isWorking(item) && <Spinner aria-hidden="true" className="size-3.5 text-primary" />}
      <span className="shrink-0 text-xs text-muted-foreground">{statusWord(item)}</span>
    </div>
  );
}

function FailedRow({ item, onRetry }: { item: QueueItem; onRetry: () => void }) {
  return (
    <div className="flex flex-col gap-1 px-2.5 py-1.5">
      <div className="flex items-center gap-2">
        <span className="min-w-0 flex-1 truncate text-sm" title={item.path}>
          {item.name}
        </span>
        <Badge variant="destructive-light">Failed</Badge>
        <Button variant="ghost" size="sm" onClick={onRetry}>
          Retry
        </Button>
      </div>
      {item.error && <p className="text-xs break-words text-destructive">{item.error}</p>}
    </div>
  );
}

/** The rows. The area owns the empty state: an empty queue is not a list of nothing. */
export function QueueList() {
  const { items, busy, deepAnalyse, saveTags, retry } = useAnalysis();
  const [openId, setOpenId] = useState<string | null>(null);

  if (items.length === 0) return null;

  return (
    <ul className="flex flex-col divide-y divide-border" aria-label="The queue">
      {items.map((item) => {
        const measured = Boolean(item.analysis) && item.status !== 'error';
        const open = openId === item.id;
        return (
          <li key={item.id}>
            {item.status === 'error' ? (
              <FailedRow item={item} onRetry={() => retry(item)} />
            ) : measured ? (
              <MeasuredRow
                item={item}
                busy={busy}
                open={open}
                onToggle={() => setOpenId(open ? null : item.id)}
                onDeep={() => void deepAnalyse(item)}
                onSave={() => void saveTags(item)}
              />
            ) : (
              <WorkingRow item={item} />
            )}
          </li>
        );
      })}
    </ul>
  );
}

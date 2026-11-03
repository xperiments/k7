import { MoreHorizontal } from 'lucide-react';
import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
import { Spinner } from '@/components/ui/spinner';
import { cn } from '@/lib/utils';
import {
  MOOD_TAGS,
  type Analysis,
  type MeasurementSource,
  type MoodTag,
} from '../domain/analysis';
import { artistOf, displayName, groupByAlbum, type QueueItem } from './queue';
import { plural } from './format';
import { sourcesOf } from './sources';
import { isWorking, statusWord } from './stage';
import { useAnalysis } from './state/analysis';

/**
 * The queue, one line each.
 *
 * A row is a line, not a page: the name, the genre the file carried, and the three numbers a
 * run produces — tempo, key, mood — aligned by fixed widths so a hundred rows can be scanned
 * in one movement. The row does not open. What was behind the accordion was the analysis
 * explaining itself, which is a page for one Track, not a list of five thousand.
 *
 * **Provenance is paint, not text.** A value the file's own tags carried is written in the
 * foreground colour — the original; a value this App measured is written in the warning
 * colour. The row therefore says *where each number came from* without a word of its own, and
 * a mixed row (tagged tempo, measured key) shows it at a glance.
 *
 * The two actions that used to live at the bottom of the accordion — measure the tempo again,
 * write a tagged copy — stay reachable from the row's own menu.
 */

/** The colour a value is written in, by where it came from. */
function paint(source: MeasurementSource | null | undefined): string {
  if (source === 'measured') return 'text-warning';
  if (source === 'metadata') return 'text-foreground';
  return 'text-muted-foreground';
}

/** The mood a row shows: the tagged label, else the strongest head, else nothing. */
function moodWord(analysis: Analysis): string {
  const { mood } = analysis;
  if (mood.label) return mood.label;
  if (!mood.tags) return '';
  let best: MoodTag | null = null;
  let score = -1;
  for (const tag of MOOD_TAGS) {
    if (mood.tags[tag] > score) {
      score = mood.tags[tag];
      best = tag;
    }
  }
  return best ?? '';
}

interface CellProps {
  /** Where the value came from; `null` paints it as absent. */
  source: MeasurementSource | null | undefined;
  value: string;
  title: string;
  className?: string;
}

/**
 * One aligned value. `tabular-nums` keeps the columns from moving between rows, and an absent
 * value is a muted dash rather than a painted one — "not measured" is not a colour.
 */
function Cell({ source, value, title, className }: CellProps) {
  const present = value !== '';
  return (
    <span
      className={cn(
        'shrink-0 truncate tabular-nums',
        className,
        present ? paint(source) : 'text-muted-foreground',
      )}
      title={present ? `${title}: ${value}` : title}
    >
      {present ? value : '—'}
    </span>
  );
}

function RowMenu({ item, busy, onDeep, onSave }: {
  item: QueueItem;
  busy: boolean;
  onDeep: () => void;
  onSave: () => void;
}) {
  const analysis = item.analysis;
  if (!analysis) return null;
  return (
    <DropdownMenu>
      <DropdownMenuTrigger
        render={
          <Button
            variant="ghost"
            size="icon-sm"
            aria-label={`Actions for ${item.name}`}
            className="shrink-0"
          />
        }
      >
        <MoreHorizontal aria-hidden="true" />
      </DropdownMenuTrigger>
      <DropdownMenuContent align="end" className="w-52">
        <DropdownMenuItem disabled={busy} onClick={onDeep}>
          {analysis.tempo.source === 'metadata' ? 'Measure the tempo' : 'Deep analyse'}
        </DropdownMenuItem>
        <DropdownMenuItem disabled={Boolean(item.saving)} onClick={onSave}>
          {item.saving ? 'Saving…' : 'Save a tagged copy'}
        </DropdownMenuItem>
      </DropdownMenuContent>
    </DropdownMenu>
  );
}

function MeasuredRow({ item, busy, mixed, onDeep, onSave }: {
  item: QueueItem;
  busy: boolean;
  /** The album names more than one artist, so this row says whose track it is. */
  mixed: boolean;
  onDeep: () => void;
  onSave: () => void;
}) {
  const analysis = item.analysis;
  if (!analysis) return null;
  const sources = sourcesOf(analysis);

  return (
    <div className="flex h-8 items-center gap-3 px-2.5" title={item.path}>
      <span className="flex min-w-0 flex-1 items-baseline gap-1.5">
        {mixed ? (
          <span className="shrink-0 text-xs text-muted-foreground">{artistOf(item)} -</span>
        ) : null}
        <span className="truncate text-sm">{displayName(item.name)}</span>
      </span>
      <Cell
        source="metadata"
        value={item.tags?.genre ?? ''}
        title="Genre"
        className="hidden w-24 text-xs md:block"
      />
      <Cell
        source={analysis.tempo.bpm ? sources.tempo : null}
        value={analysis.tempo.bpm ? analysis.tempo.bpm.toFixed(1) : ''}
        title="Folded BPM"
        className="w-12 text-right text-sm"
      />
      <Cell
        source={sources.key}
        value={analysis.key.camelot}
        title="Camelot key"
        className="w-8 text-right text-xs"
      />
      <Cell
        source={sources.mood}
        value={moodWord(analysis)}
        title="Mood"
        className="hidden w-16 text-xs sm:block"
      />
      <span
        className="w-10 shrink-0 text-right text-xs tracking-wide text-muted-foreground uppercase"
        title={`Format: ${item.format}`}
      >
        {item.format}
      </span>
      <RowMenu item={item} busy={busy} onDeep={onDeep} onSave={onSave} />
    </div>
  );
}

function WorkingRow({ item }: { item: QueueItem }) {
  return (
    <div className="flex h-8 items-center gap-3 px-2.5">
      <span className="min-w-0 flex-1 truncate text-sm" title={item.path}>
        {displayName(item.name)}
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
          {displayName(item.name)}
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

  if (items.length === 0) return null;

  return (
    <section className="flex flex-col gap-3" aria-label="The queue">
      {groupByAlbum(items).map((group) => (
        <section
          key={group.album}
          aria-label={
            group.artist === null
              ? `Album: ${group.album}`
              : `Album: ${group.album} / ${group.artist}`
          }
        >
          <h2 className="flex items-baseline gap-1.5 border-b px-2.5 py-1.5 text-xs">
            <span className="min-w-0 truncate font-medium text-foreground">{group.album}</span>
            {group.artist !== null ? (
              <>
                <span aria-hidden="true" className="shrink-0 text-muted-foreground/60">
                  /
                </span>
                <span className="min-w-0 truncate text-muted-foreground">{group.artist}</span>
              </>
            ) : null}
            <span className="ml-auto shrink-0 pl-2 text-muted-foreground tabular-nums">
              {plural(group.items.length, 'track')}
            </span>
          </h2>
          <ul className="flex flex-col divide-y divide-border">
            {group.items.map((item) => (
              <li key={item.id}>
                {item.status === 'error' ? (
                  <FailedRow item={item} onRetry={() => retry(item)} />
                ) : item.analysis ? (
                  <MeasuredRow
                    item={item}
                    busy={busy}
                    mixed={group.mixed}
                    onDeep={() => void deepAnalyse(item)}
                    onSave={() => void saveTags(item)}
                  />
                ) : (
                  <WorkingRow item={item} />
                )}
              </li>
            ))}
          </ul>
        </section>
      ))}
    </section>
  );
}

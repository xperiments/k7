import type { Stage } from '../audio/analyzer';
import { formatBytes } from './format';
import type { QueueItem } from './queue';
import { ResultCard } from './ResultCard';

const STAGE_TEXT: Record<Stage, string> = {
  tempo: 'Finding tempo…',
  deep: 'Refining tempo…',
  key: 'Finding key…',
  mood: 'Measuring mood…',
};

function statusText(item: QueueItem): string {
  if (item.status === 'error') return 'Failed';
  if (item.status === 'analysing') return item.stage ? STAGE_TEXT[item.stage] : 'Analysing…';
  if (item.status === 'tagged') return 'From tags';
  if (item.status === 'hashing') return 'Hashing…';
  if (item.status === 'decoding') return 'Decoding…';
  return 'Queued';
}

interface QueueListProps {
  items: QueueItem[];
  busy: boolean;
  /** One line per file, or the full card for each. */
  compact: boolean;
  onDeep: (item: QueueItem) => void;
  onRetry: (item: QueueItem) => void;
  onSave: (item: QueueItem) => void;
}

export function QueueList({ items, busy, compact, onDeep, onRetry, onSave }: QueueListProps) {
  return (
    <div className="queue">
      {items.map((item) => {
        if (compact && item.analysis) {
          const { analysis } = item;
          const tempo = analysis.tempo.source === 'metadata' ? 'tagged' : 'measured';
          return (
            <div className="queue-row compact" key={item.id}>
              <div className="queue-file">
                <strong>{item.name}</strong>
                {item.path !== item.name && (
                  <span className="muted small queue-path">{item.path}</span>
                )}
              </div>
              <div className="queue-state">
                <span className="muted small">
                  {analysis.tempo.bpm || '—'} BPM · {analysis.key.camelot || '—'} · {tempo}
                </span>
                <button type="button" className="ghost small" onClick={() => onDeep(item)} disabled={busy}>
                  Details
                </button>
              </div>
            </div>
          );
        }

        if (item.status === 'done' && item.analysis) {
          return (
            <ResultCard
              key={item.id}
              analysis={item.analysis}
              busy={busy}
              cached={Boolean(item.cached)}
              saving={Boolean(item.saving)}
              onDeep={() => onDeep(item)}
              onSave={() => onSave(item)}
            />
          );
        }

        const active = ['hashing', 'decoding', 'analysing'].includes(item.status);
        return (
          <div className={`queue-row status-${item.status}`} key={item.id}>
            <div className="queue-file">
              <strong>{item.name}</strong>
              {item.path !== item.name && (
                <span className="muted small queue-path">{item.path}</span>
              )}
              <span className="muted small">
                {item.format.toUpperCase()} · {formatBytes(item.file.size)}
              </span>
            </div>
            <div className="queue-state">
              {active && <span className="spinner" aria-hidden="true" />}
              <span className="muted small">{statusText(item)}</span>
            </div>
            {item.error && <p className="error small">{item.error}</p>}
            {item.status === 'error' && (
              <button className="ghost" onClick={() => onRetry(item)}>
                Retry
              </button>
            )}
          </div>
        );
      })}
    </div>
  );
}

import type { Analysis } from '../domain/analysis';
import { formatBytes, formatDuration, formatPercent, shortHash } from './format';
import { Meter } from './Meter';
import { SourceChip } from './SourceChip';
import { MoodPanel } from './MoodPanel';

interface ResultCardProps {
  analysis: Analysis;
  busy: boolean;
  cached: boolean;
  saving: boolean;
  onDeep: () => void;
  onSave: () => void;
}

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

  return (
    <article className="result-card">
      <header className="result-head">
        <div className="result-title">
          <h2>{analysis.track.name}</h2>
          <p className="muted small">
            {analysis.track.path !== analysis.track.name && (
              <span className="queue-path">{analysis.track.path} · </span>
            )}
            {analysis.track.format.toUpperCase()} · {formatDuration(analysis.track.duration)} ·{' '}
            {formatBytes(analysis.track.size)}
          </p>
        </div>
        <div className="badges">
          {tempo.source === 'metadata' && <span className="badge badge-tag">tagged tempo</span>}
          {tempo.engine === 'deep' && <span className="badge badge-deep">deep tempo</span>}
          {tempo.escalated && <span className="badge">auto-escalated</span>}
          {tempo.engine === 'fast' && <span className="badge">fast tempo</span>}
          {key.source === 'metadata' && <span className="badge badge-tag">tagged key</span>}
          {mood.source === 'metadata' && <span className="badge badge-tag">tagged mood</span>}
          {energySource === 'metadata' && <span className="badge badge-tag">tagged energy</span>}
          {cached && <span className="badge">cached</span>}
        </div>
      </header>

      <div className="primary-grid">
        <section className="metric">
          <span className="metric-label">
            Tempo <SourceChip source={tempo.source} />
          </span>
          <div className="metric-value">
            {tempo.bpm.toFixed(1)}
            <small> BPM</small>
          </div>
          {tempo.confidence !== null ? (
            <>
              <Meter
                value={tempo.confidence}
                label="Tempo confidence"
                warn={tempo.confidence < 0.6}
              />
              <p className="muted small">
                confidence {formatPercent(tempo.confidence)} · raw {tempo.rawBpm.toFixed(1)}
              </p>
            </>
          ) : (
            <p className="muted small">from file tags, not re-measured</p>
          )}
        </section>

        <section className="metric">
          <span className="metric-label">
            Key <SourceChip source={key.source} />
          </span>
          <div className="metric-value">
            {key.tonic} <small>{key.mode}</small>
          </div>
          <p className="camelot">{key.camelot || '—'}</p>
          {key.strength !== null ? (
            <p className="muted small">strength {key.strength.toFixed(2)}</p>
          ) : (
            <p className="muted small">from file tags</p>
          )}
        </section>

        <section className="metric">
          <span className="metric-label">
            Energy <SourceChip source={energySource} />
          </span>
          <div className="metric-value">{energy !== null ? energy.toFixed(2) : '—'}</div>
          {energy !== null && <Meter value={energy} label="Energy" />}
          <p className="muted small">
            {energySource === 'metadata'
              ? 'from file tags'
              : energySource === 'measured'
                ? 'learned arousal'
                : 'not measured'}
          </p>
        </section>
      </div>

      <MoodPanel mood={mood} />

      <footer className="result-foot">
        <span className="muted small">
          {window
            ? `window ${formatDuration(window.start)}–${formatDuration(window.start + window.duration)} · `
            : ''}
          models {shortHash(analysis.provenance.modelSet)} · analysed{' '}
          {new Date(analysis.provenance.analysedAt).toLocaleTimeString()}
        </span>
        <div className="result-actions">
          <button
            className="ghost"
            onClick={onSave}
            disabled={saving}
            title="Downloads a copy with BPM, key, mood and energy tags. Browsers cannot overwrite the original file."
          >
            {saving ? 'Saving…' : 'Save tagged copy'}
          </button>
          <button
            className="ghost"
            onClick={onDeep}
            disabled={busy}
            title={
              tempo.source === 'metadata'
                ? 'Measure tempo with the accurate tracker, replacing the tag'
                : 'Re-run tempo with the accurate tracker'
            }
          >
            {tempo.source === 'metadata' ? 'Measure tempo' : 'Deep analyse'}
          </button>
        </div>
      </footer>
    </article>
  );
}

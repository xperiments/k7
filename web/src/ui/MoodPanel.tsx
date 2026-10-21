import { MOOD_TAGS, type Analysis, type MoodTag } from '../domain/analysis';

const LABELS: Record<MoodTag, string> = {
  happy: 'happy',
  sad: 'sad',
  aggressive: 'aggressive',
  relaxed: 'relaxed',
  party: 'party',
  danceable: 'danceable',
};

export function MoodPanel({ mood }: { mood: Analysis['mood'] }) {
  const axes =
    mood.valence !== null && mood.arousal !== null
      ? { valence: mood.valence, arousal: mood.arousal }
      : null;

  return (
    <div className="mood-panel">
      {mood.tags ? (
        <div className="tag-bars">
          {MOOD_TAGS.map((tag) => (
            <div className="tag-row" key={tag}>
              <span className="tag-name">{LABELS[tag]}</span>
              <div className="bar">
                <div className="bar-fill" style={{ width: `${mood.tags![tag] * 100}%` }} />
              </div>
              <span className="tag-value">{Math.round(mood.tags![tag] * 100)}</span>
            </div>
          ))}
        </div>
      ) : (
        <div className="tagged-mood">
          <span className="metric-label">
            Mood
            <em className="tag">tagged</em>
          </span>
          <p className="tagged-label">{mood.label ?? '—'}</p>
          <p className="muted small">from file tags, not measured</p>
        </div>
      )}

      {axes ? (
        <figure className="va">
          <svg viewBox="0 0 120 120" role="img" aria-label="Valence and arousal">
            <rect x="10" y="10" width="100" height="100" className="va-field" />
            <line x1="60" y1="10" x2="60" y2="110" className="va-axis" />
            <line x1="10" y1="60" x2="110" y2="60" className="va-axis" />
            <circle
              cx={10 + axes.valence * 100}
              cy={110 - axes.arousal * 100}
              r="4"
              className="va-dot"
            />
            <text x="10" y="121" className="va-label">
              dark
            </text>
            <text x="110" y="121" textAnchor="end" className="va-label">
              bright
            </text>
            <text x="9" y="16" className="va-label">
              energetic
            </text>
            <text x="9" y="107" className="va-label">
              calm
            </text>
          </svg>
          <figcaption className="muted small">
            valence {axes.valence.toFixed(2)} · arousal {axes.arousal.toFixed(2)}
          </figcaption>
        </figure>
      ) : (
        <p className="muted small va-empty">Valence and arousal not measured.</p>
      )}
    </div>
  );
}

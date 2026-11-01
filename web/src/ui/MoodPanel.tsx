import { Progress } from '@/components/ui/progress';
import { MOOD_TAGS, type Analysis, type MoodTag } from '../domain/analysis';
import { SourceChip } from './SourceChip';

const LABELS: Record<MoodTag, string> = {
  happy: 'happy',
  sad: 'sad',
  aggressive: 'aggressive',
  relaxed: 'relaxed',
  party: 'party',
  danceable: 'danceable',
};

/**
 * Valence against arousal, drawn at 160 px.
 *
 * The axis labels are set in SVG user units: 9 of them are about 12 rendered pixels, so
 * the figure keeps its labels at the same floor as every other piece of text.
 */
function VaPlot({ valence, arousal }: { valence: number; arousal: number }) {
  return (
    <figure className="flex flex-col items-center gap-1">
      <svg
        viewBox="0 0 120 120"
        role="img"
        aria-label="Valence and arousal"
        className="size-40 overflow-visible"
      >
        <rect x="10" y="10" width="100" height="100" className="fill-muted stroke-border" />
        <line x1="60" y1="10" x2="60" y2="110" className="stroke-border [stroke-dasharray:3_3]" />
        <line x1="10" y1="60" x2="110" y2="60" className="stroke-border [stroke-dasharray:3_3]" />
        <circle
          cx={10 + valence * 100}
          cy={110 - arousal * 100}
          r="4"
          strokeWidth={1.5}
          className="fill-primary stroke-background"
        />
        <text x="10" y="121" className="fill-muted-foreground text-[9px]">
          dark
        </text>
        <text x="110" y="121" textAnchor="end" className="fill-muted-foreground text-[9px]">
          bright
        </text>
        <text x="9" y="16" className="fill-muted-foreground text-[9px]">
          energetic
        </text>
        <text x="9" y="107" className="fill-muted-foreground text-[9px]">
          calm
        </text>
      </svg>
      <figcaption className="text-xs text-muted-foreground tabular-nums">
        valence {valence.toFixed(2)} · arousal {arousal.toFixed(2)}
      </figcaption>
    </figure>
  );
}

/** The six model outputs, a tagged label, or the honest absence of either. */
export function MoodPanel({ mood }: { mood: Analysis['mood'] }) {
  const tags = mood.tags;
  const axes =
    mood.valence !== null && mood.arousal !== null
      ? { valence: mood.valence, arousal: mood.arousal }
      : null;

  return (
    <div className="grid gap-4 md:grid-cols-[minmax(0,1fr)_10rem] md:gap-6">
      {tags ? (
        <div className="flex flex-col gap-2">
          {MOOD_TAGS.map((tag) => (
            <div
              key={tag}
              className="grid grid-cols-[5.5rem_minmax(0,1fr)_2rem] items-center gap-2.5"
            >
              <span className="text-sm text-muted-foreground">{LABELS[tag]}</span>
              <Progress value={tags[tag] * 100} aria-label={`${LABELS[tag]} probability`} />
              <span className="text-right text-xs text-muted-foreground tabular-nums">
                {Math.round(tags[tag] * 100)}
              </span>
            </div>
          ))}
        </div>
      ) : mood.label !== null ? (
        <div className="flex flex-col gap-1">
          <span className="flex items-center gap-2 text-xs font-medium tracking-wide text-muted-foreground uppercase">
            Mood <SourceChip source={mood.source} />
          </span>
          <p className="text-2xl font-semibold tracking-tight break-words">{mood.label}</p>
          <p className="text-xs text-muted-foreground">from file tags, not measured</p>
        </div>
      ) : (
        <div className="flex flex-col gap-1">
          <span className="flex items-center gap-2 text-xs font-medium tracking-wide text-muted-foreground uppercase">
            Mood <SourceChip source={null} />
          </span>
          <p className="text-sm text-muted-foreground">
            The file carried no mood tag and no mood model ran for this track.
          </p>
        </div>
      )}

      {axes ? (
        <VaPlot valence={axes.valence} arousal={axes.arousal} />
      ) : (
        <p className="text-xs text-muted-foreground md:self-center md:text-center">
          Valence and arousal not measured.
        </p>
      )}
    </div>
  );
}

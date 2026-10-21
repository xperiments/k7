export const MIN_BPM = 70;
export const MAX_BPM = 180;

/** The confidence below which the fast tempo is escalated to deep analysis. */
export const ESCALATION_THRESHOLD = 0.6;

/** Fewer beats than this cannot give a trustworthy interval spread. */
export const MIN_BEATS = 8;

export function median(values: readonly number[]): number {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0
    ? (sorted[middle - 1] + sorted[middle]) / 2
    : sorted[middle];
}

/** Moves a pulse into 70–180 BPM by octaves, preserving the pulse's phase. */
export function foldTempo(rawBpm: number): number {
  if (!Number.isFinite(rawBpm) || rawBpm <= 0) return 0;
  let bpm = rawBpm;
  while (bpm < MIN_BPM) bpm *= 2;
  while (bpm > MAX_BPM) bpm /= 2;
  return bpm;
}

export function bpmFromBeats(beats: ArrayLike<number>): number {
  if (beats.length < 2) return 0;
  const times = Array.from(beats);
  const intervals = times.slice(1).map((time, i) => time - times[i]);
  const typical = median(intervals);
  return typical > 0 ? 60 / typical : 0;
}

/**
 * Fraction of inter-beat intervals within ±5% of the median. A steady track
 * scores near 1; a track with a tempo change or an unstable beat tracker
 * scores low and is escalated.
 */
export function tempoConfidence(beats: ArrayLike<number>): number {
  if (beats.length < MIN_BEATS + 1) return 0;
  const times = Array.from(beats);
  const intervals = times.slice(1).map((time, i) => time - times[i]);
  const typical = median(intervals);
  if (typical <= 0) return 0;
  const close = intervals.filter(
    (interval) => Math.abs(interval - typical) / typical <= 0.05,
  ).length;
  return close / intervals.length;
}

export function shouldEscalate(confidence: number, beatCount: number): boolean {
  return confidence < ESCALATION_THRESHOLD || beatCount < MIN_BEATS;
}

/** Essentia's deep confidence is 0–5.32; normalise it to 0–1. */
export function normaliseDeepConfidence(confidence: number): number {
  return Math.max(0, Math.min(1, confidence / 5.32));
}

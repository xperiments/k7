import type { Analysis, MeasurementSource } from '../domain/analysis';

/**
 * Where each value on a row came from, in one place.
 *
 * `null` is the third answer: nothing supplied it. A tag that is absent and a model that was
 * skipped are the same thing to a reader - "not measured" - and different from a zero, which
 * is a measurement that happened to come out flat.
 */
export interface AnalysisSources {
  tempo: MeasurementSource;
  key: MeasurementSource;
  mood: MeasurementSource | null;
  energy: MeasurementSource | null;
}

export function sourcesOf(analysis: Analysis): AnalysisSources {
  const { mood } = analysis;
  return {
    tempo: analysis.tempo.source,
    key: analysis.key.source,
    mood:
      mood.label !== null
        ? mood.source
        : mood.tags !== null
          ? 'measured'
          : null,
    energy: analysis.energySource,
  };
}

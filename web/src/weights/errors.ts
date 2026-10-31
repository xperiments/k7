import type { Weight } from './config';

/** A weight that cannot be produced, kept apart from analysis failures in the UI. */
export class WeightError extends Error {
  readonly weight: Weight;

  constructor(message: string, weight: Weight) {
    super(message);
    this.name = 'WeightError';
    this.weight = weight;
  }
}

/** The network failed or answered with an error, and nothing usable was cached. */
export class WeightUnavailableError extends WeightError {
  constructor(weight: Weight) {
    super(
      `The analyser cannot start without a first download of ${weight.label}. ` +
        'Connect this computer to the internet once, then try again.',
      weight,
    );
    this.name = 'WeightUnavailableError';
  }
}

/** The bytes arrived but did not hash to the pinned value. Refused, not repaired. */
export class WeightIntegrityError extends WeightError {
  constructor(weight: Weight) {
    super(
      `The ${weight.label} download did not match its pinned sha256 and was refused. ` +
        'The analysis did not start.',
      weight,
    );
    this.name = 'WeightIntegrityError';
  }
}

/**
 * A weight failure can cross a worker boundary as a plain Error whose name carries the
 * distinction; this is the one check the UI uses for "the analyser could not start".
 */
export function isWeightError(error: unknown): boolean {
  return error instanceof WeightError || (error instanceof Error && error.name === 'WeightError');
}

/**
 * Recorded and reconstructed model answers, used to test the rubric without a model.
 *
 * `note-§5.1` is a failure the research note or ADR 0084 describes; the raw string is a
 * reconstruction of the answer it describes, not a byte-for-byte capture, because no capture
 * was kept. `synthetic` is a case written while porting, to exercise one axis. Neither is a model
 * result, and the harness's "export fixtures" button is how a real run replaces them.
 */

import answers from './answers.json';

export type FixtureOrigin = 'note-§5.1' | 'synthetic' | 'recorded';

export interface Fixture {
  rowId: string;
  raw: string;
  origin: FixtureOrigin;
  /** What the rubric must say about this answer. */
  verdict: 'pass' | 'fail';
  note?: string;
}

export const FIXTURES: readonly Fixture[] = answers as unknown as readonly Fixture[];

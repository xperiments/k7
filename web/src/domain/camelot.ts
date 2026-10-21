export const PITCH_CLASSES: Record<string, number> = {
  C: 0,
  'C#': 1,
  Db: 1,
  D: 2,
  'D#': 3,
  Eb: 3,
  E: 4,
  F: 5,
  'F#': 6,
  Gb: 6,
  G: 7,
  'G#': 8,
  Ab: 8,
  A: 9,
  'A#': 10,
  Bb: 10,
  B: 11,
};

/**
 * Camelot wheel: 8B is C major, 8A is A minor, increasing by a fifth for
 * majors and by a fourth for minors.
 */
export function camelot(tonic: string, mode: 'major' | 'minor'): string {
  const pitch = PITCH_CLASSES[tonic];
  if (pitch === undefined) return '';
  const relativeMajor = mode === 'minor' ? (pitch + 3) % 12 : pitch;
  const number = (relativeMajor * 7 + 8) % 12 || 12;
  return `${number}${mode === 'major' ? 'B' : 'A'}`;
}

export function formatKey(tonic: string, mode: 'major' | 'minor'): string {
  return `${tonic} ${mode}`;
}

/**
 * The device's key grammar: a Camelot code or a note name, and back.
 *
 * Separate from `parseKeyTag`, which reads a tagger's spelling into a tonic and a mode; this
 * answers the numeric code `snapshot_key_parse` gives, so the image source, the normalizer and
 * the scorer accept exactly what the device accepts.
 */

/** Pitch class to Camelot number, the device's two twelve-entry wheels. */
const MINOR_WHEEL = [5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10];
const MAJOR_WHEEL = [8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1];
const NOTE_PC: Record<string, number> = { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 };

/**
 * The device's key parser (`snapshot_key_parse`): a Camelot code or a note name, 0 when
 * neither. Kept word for word so every caller accepts exactly what the device accepts.
 */
export function keyCode(text: string): number {
  const value = text.trim();
  const camelot = /^(\d{1,2})([AB])$/i.exec(value);
  if (camelot) {
    const number = Number(camelot[1]);
    if (number < 1 || number > 12) return 0;
    return number + (camelot[2].toUpperCase() === 'B' ? 12 : 0);
  }

  const note = /^([A-Ga-g])([#b]?)\s*(.*)$/.exec(value);
  if (!note) return 0;
  let pc = NOTE_PC[note[1].toUpperCase()];
  if (pc === undefined) return 0;
  if (note[2] === '#') pc = (pc + 1) % 12;
  else if (note[2] === 'b') pc = (pc + 11) % 12;

  const mode = note[3].toLowerCase();
  if (mode === '') return MAJOR_WHEEL[pc] + 12;
  if (mode === 'm' || mode === 'mi' || mode === 'min' || mode === 'mino' || mode === 'minor') {
    return MINOR_WHEEL[pc];
  }
  if (mode === 'ma' || mode === 'maj' || mode === 'majo' || mode === 'major') {
    return MAJOR_WHEEL[pc] + 12;
  }
  return 0;
}

/** A Camelot number as the code the device prints: 1..12 A, 13..24 B. */
export function camelotName(code: number): string | null {
  if (code < 1 || code > 24) return null;
  return `${((code - 1) % 12) + 1}${code > 12 ? 'B' : 'A'}`;
}

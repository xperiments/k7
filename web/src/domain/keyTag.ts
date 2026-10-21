import { PITCH_CLASSES, camelot } from './camelot';

export interface ParsedKey {
  tonic: string;
  mode: 'major' | 'minor';
  camelot: string;
}

const CANONICAL = [
  'C',
  'C#',
  'D',
  'D#',
  'E',
  'F',
  'F#',
  'G',
  'G#',
  'A',
  'A#',
  'B',
] as const;

const ENHARMONIC: Record<string, string> = {
  'E#': 'F',
  'B#': 'C',
  Cb: 'B',
  Fb: 'E',
};

/** The wheel both ways, generated so it cannot drift from camelot(). */
const BY_CAMELOT: Record<string, ParsedKey> = Object.fromEntries(
  CANONICAL.flatMap((tonic) =>
    (['major', 'minor'] as const).map((mode) => {
      const code = camelot(tonic, mode);
      return [code, { tonic, mode, camelot: code }];
    }),
  ),
);

/**
 * Parses the key spellings written by DJ software and taggers:
 * Camelot (`8A`), notes (`Am`, `A minor`, `F#m`, `Bb`, `C major`).
 * Returns null for anything it cannot place, so the analyzer falls back
 * to measuring the key instead of trusting a bad tag.
 */
export function parseKeyTag(raw: string): ParsedKey | null {
  const value = raw
    .trim()
    .replace(/♯/g, '#')
    .replace(/♭/g, 'b')
    .replace(/\s+/g, ' ');
  if (!value) return null;

  const wheel = value.match(/^(\d{1,2})([AB])$/i);
  if (wheel) {
    const code = `${Number(wheel[1])}${wheel[2].toUpperCase()}`;
    return BY_CAMELOT[code] ?? null;
  }

  const note = value.match(
    /^([A-Ga-g])([#b]?) ?(minor|min|major|maj|mi|ma|m)?\.?$/,
  );
  if (!note) return null;

  const written = `${note[1].toUpperCase()}${note[2]}`;
  const tonic = ENHARMONIC[written] ?? written;
  if (PITCH_CLASSES[tonic] === undefined) return null;

  const suffix = (note[3] ?? '').toLowerCase();
  const mode = ['m', 'mi', 'min', 'minor'].includes(suffix) ? 'minor' : 'major';
  const code = camelot(tonic, mode);
  return code ? { tonic, mode, camelot: code } : null;
}

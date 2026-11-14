/**
 * The key spellings: the model writes a key in words or symbols, the device reads a small
 * grammar, and one rejected key refuses the whole search.
 */

import type { SearchArgs } from '../../device/mcp';
import { keyCode } from '../../domain/key';

import { hasAny } from './words';

/** The mode words a key may be written with; the device's own forms live in `keyCode`. */
const KEY_MINOR_WORDS = [
  'minor',
  'min',
  'menor',
  'mineur',
  'moll',
  'マイナー',
  '短調',
] as const;
const KEY_MAJOR_WORDS = [
  'major',
  'maj',
  'mayor',
  'majeur',
  'dur',
  'メジャー',
  '長調',
] as const;

const KEY_MINOR = new Set([
  'm',
  'mi',
  'min',
  'mino',
  'minor',
  'menor',
  'mineur',
  'moll',
  'マイナー',
  '短調',
]);
const KEY_MAJOR = new Set([
  'ma',
  'maj',
  'majo',
  'major',
  'mayor',
  'majeur',
  'dur',
  'メジャー',
  '長調',
]);

/** The device's parser as a grammar: a note, an accidental, then a mode. */
const KEY_NOTE = /^([A-Ga-g])[\s-]*([#b♯♭]|sharp|flat)?[\s-]*(.*)$/i;
const BARE_NOTE = /^[A-Ga-g][#b♯♭]?$/;

/** The mode the sentence states, or null when it says nothing or both. */
function sentenceMode(sentence: string): 'major' | 'minor' | null {
  const minor = hasAny(sentence, KEY_MINOR_WORDS);
  const major = hasAny(sentence, KEY_MAJOR_WORDS);
  if (minor && major) return null;
  return minor ? 'minor' : major ? 'major' : null;
}

/** One key in the device's form, or null when it should be left as the model wrote it. */
function normalizedKey(sentence: string, key: string): string | null {
  const match = KEY_NOTE.exec(key.trim());
  if (!match) return null;
  const pitch = match[1].toUpperCase();
  const raw = (match[2] ?? '').toLowerCase();
  const accidental =
    raw === '#' || raw === '♯' || raw === 'sharp'
      ? '#'
      : raw === 'b' || raw === '♭' || raw === 'flat'
        ? 'b'
        : '';
  const trailing = match[3].trim().toLowerCase().replace(/[.!?,;:]+$/, '');

  let mode: 'major' | 'minor' | null;
  if (trailing === '') mode = null;
  else if (KEY_MINOR.has(trailing)) mode = 'minor';
  else if (KEY_MAJOR.has(trailing)) mode = 'major';
  else return null; // a word this does not understand: leave the key alone

  const bare = BARE_NOTE.test(key.trim());
  if (mode === null) {
    /* A key the device already reads, with no mode of its own, is only ambiguous when it is a
     * bare note; the sentence may settle that. A spelled key the device cannot read defaults to
     * the device's own major reading. */
    if (keyCode(key) !== 0 && !bare) return key;
    mode = sentenceMode(sentence) ?? (keyCode(key) === 0 ? 'major' : null);
  }
  if (mode === null) return null;

  /* An explicit mode the model wrote wins: this is a spelling fix, not a correction of what the
   * model meant. A bare note has no mode, so the sentence may settle it. */
  if (keyCode(key) !== 0 && !bare) return key;

  const canonical = `${pitch}${accidental}${mode === 'minor' ? 'm' : ''}`;
  return keyCode(canonical) === 0 ? null : canonical;
}

/**
 * The key filters written the way the device reads them.
 *
 * The model can answer "B flat major", "Bflat" or "F♯m"; the device's parser accepts "Bb",
 * "F#m" and rejects the rest, and a key it rejects refuses the whole search. This rewrites only
 * what `keyCode` cannot read — or a bare note whose mode the sentence settles — never a key the
 * device already understands, and never one whose mode cannot be told.
 */
export function normalizeKeys(sentence: string, intent: SearchArgs): SearchArgs {
  if (!intent.key || intent.key.length === 0) return intent;
  const keys: string[] = [];
  for (const key of intent.key) {
    const next = normalizedKey(sentence, key) ?? key;
    if (!keys.includes(next)) keys.push(next);
  }
  const unchanged =
    keys.length === intent.key.length && keys.every((key, index) => key === intent.key?.[index]);
  return unchanged ? intent : { ...intent, key: keys };
}

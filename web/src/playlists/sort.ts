/**
 * The one-shot sorts the Playlist editor offers.
 *
 * A sort is applied **once**, to the rows as they are at that moment: the editor keeps the
 * order it produced, and later edits do not re-apply the rule. Every sort is stable, so
 * rows with equal keys keep the order the user already had, and every sort puts a row it
 * cannot rank **last** instead of dropping it.
 *
 * The facts come from the device (`library_track`), but this module never fetches them:
 * the editor hands over a map, which keeps the rules pure and testable. Artist and album
 * fall back to the path's own `Artist/Album/` segments, so that sort works before the
 * device has answered.
 */

export type SortRule = 'bpm' | 'camelot' | 'energy' | 'artist' | 'path';

/** The measurements a sort may use; a missing value ranks last. */
export interface SortFacts {
  bpm?: number | null;
  /** Camelot code, e.g. `8A`. A note name is not a Camelot code and ranks last. */
  key?: string | null;
  /** 0–100, as the device reports it. */
  energy?: number | null;
  /** Tagged artist, when the Snapshot has one. */
  artist?: string | null;
  /** Tagged album, when the Snapshot has one. */
  album?: string | null;
}

/** The menu's order: the rules that need the device first, the one that never does last. */
export const SORT_RULES: readonly SortRule[] = ['bpm', 'camelot', 'energy', 'artist', 'path'];

export const SORT_LABELS: Record<SortRule, string> = {
  bpm: 'BPM, rising',
  camelot: 'Camelot wheel',
  energy: 'Energy arc',
  artist: 'Artist / album',
  path: 'File name',
};

/** A rule whose order is meaningless without the device's measurements. */
export function needsFacts(rule: SortRule): boolean {
  return rule === 'bpm' || rule === 'camelot' || rule === 'energy';
}

/** The last segment of a Snapshot path: the file's own name. */
export function baseName(path: string): string {
  const at = path.lastIndexOf('/');
  return at === -1 ? path : path.slice(at + 1);
}

const CAMELOT = /^(\d{1,2})\s*([AB])$/i;

/**
 * `8A` → `{ code: 8, letter: 'A' }`. A key the device reports as a note name (`Am`) is not
 * a Camelot code: it parses to null and therefore sorts last, which is honest — the
 * alternative is guessing which of the two circles `Am` belongs to.
 */
export function parseCamelot(
  key: string | null | undefined,
): { code: number; letter: 'A' | 'B' } | null {
  if (!key) return null;
  const match = CAMELOT.exec(key.trim());
  if (!match) return null;
  const code = Number(match[1]);
  if (!Number.isInteger(code) || code < 1 || code > 12) return null;
  return { code, letter: match[2].toUpperCase() as 'A' | 'B' };
}

/** A number, or +Infinity for "this row cannot be ranked". */
function rank(value: number | null | undefined): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : Number.POSITIVE_INFINITY;
}

function byNumber(left: number | null | undefined, right: number | null | undefined): number {
  const a = rank(left);
  const b = rank(right);
  if (a === b) return 0;
  return a < b ? -1 : 1;
}

/** Case-insensitive, locale-free (a locale-dependent compare is not reproducible). */
function lowered(text: string): string {
  return text.toLocaleLowerCase();
}

function byText(left: string | null | undefined, right: string | null | undefined): number {
  const a = left?.trim() ?? '';
  const b = right?.trim() ?? '';
  if (!a && !b) return 0;
  if (!a) return 1;
  if (!b) return -1;
  const x = lowered(a);
  const y = lowered(b);
  if (x === y) return 0;
  return x < y ? -1 : 1;
}

function segments(path: string): string[] {
  return path.split('/');
}

/** The tagged artist, or the first directory of the path (`Artist/Album/track.mp3`). */
function artistOf(path: string, facts: SortFacts | null | undefined): string | null {
  const tagged = facts?.artist?.trim();
  if (tagged) return tagged;
  const parts = segments(path);
  return parts.length >= 2 ? parts[0] : null;
}

/** The tagged album, or the second directory of the path. */
function albumOf(path: string, facts: SortFacts | null | undefined): string | null {
  const tagged = facts?.album?.trim();
  if (tagged) return tagged;
  const parts = segments(path);
  return parts.length >= 3 ? parts[1] : null;
}

function camelotOrder(left: string | null | undefined, right: string | null | undefined): number {
  const a = parseCamelot(left);
  const b = parseCamelot(right);
  if (!a && !b) return 0;
  if (!a) return 1;
  if (!b) return -1;
  if (a.code !== b.code) return a.code - b.code;
  if (a.letter === b.letter) return 0;
  return a.letter === 'A' ? -1 : 1;
}

/**
 * **Energy arc**: the lowest energy first, the peak in the middle, the highest of the low
 * half climbing to it and the rest falling away. Concretely: order by energy, emit the even
 * positions ascending, then the odd positions descending — `0,2,4,5,3,1` for six rows — so
 * the curve rises to the middle instead of jumping there.
 */
function arc(rows: readonly string[], energyOf: (path: string) => number | null | undefined): string[] {
  const scored = rows.filter((path) => rank(energyOf(path)) !== Number.POSITIVE_INFINITY);
  const unscored = rows.filter((path) => rank(energyOf(path)) === Number.POSITIVE_INFINITY);
  // `sort` is stable, so equal energies keep the authored order.
  scored.sort((a, b) => byNumber(energyOf(a), energyOf(b)));

  const out: string[] = [];
  for (let at = 0; at < scored.length; at += 2) out.push(scored[at]);
  let from = scored.length - 1;
  if (from % 2 === 0) from -= 1;
  for (; from >= 1; from -= 2) out.push(scored[from]);

  return [...out, ...unscored];
}

/**
 * The rows, reordered by one rule. Never mutates its input, never drops a row, and returns
 * a new array so the caller can hand it straight to `updateDraft`.
 */
export function sortRows(
  rows: readonly string[],
  rule: SortRule,
  facts: ReadonlyMap<string, SortFacts | null> = new Map(),
): string[] {
  const order = [...rows];
  const factsOf = (path: string): SortFacts | null | undefined => facts.get(path);

  switch (rule) {
    case 'bpm':
      return order.sort((a, b) => byNumber(factsOf(a)?.bpm, factsOf(b)?.bpm));

    case 'camelot':
      return order.sort((a, b) => camelotOrder(factsOf(a)?.key, factsOf(b)?.key));

    case 'energy':
      return arc(order, (path) => factsOf(path)?.energy ?? null);

    case 'artist':
      return order.sort(
        (a, b) =>
          byText(artistOf(a, factsOf(a)), artistOf(b, factsOf(b))) ||
          byText(albumOf(a, factsOf(a)), albumOf(b, factsOf(b))) ||
          byText(baseName(a), baseName(b)),
      );

    case 'path':
      return order.sort((a, b) => byText(baseName(a), baseName(b)) || byText(a, b));
  }
}

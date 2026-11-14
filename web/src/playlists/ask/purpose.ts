/**
 * The WORD→FILTER table and the normalizer built on it.
 *
 * The first three rows are purposes: they prescribe filters and are the case that over-fills.
 * The rest are qualities, stated as prompt rows only. `normalizePurpose` drops what a purpose
 * sentence never asked for, snaps a kept bound to the table when the model's is too loose, fills
 * the table's filters in when no measurement filter is left, and leaves out a mood the library
 * does not have. The table's value is the App's, not the model's.
 */

import type { SearchArgs } from '../../device/mcp';
import type { LibraryProfile } from '../../domain/snapshot';

import { factsOf, type PromptFacts } from './facts';
import { containsWord, hasAny } from './words';

/** The three purposes the WORD→FILTER table names: they prescribe filters, not a mood word. */
export type PurposeId = 'relax' | 'energise' | 'dance';

export interface PurposeRule {
  id: PurposeId;
  /** The words that mark this purpose, in the five languages Chrome's model covers. */
  words: readonly string[];
  /** The filters the WORD→FILTER table prescribes for the purpose, at this library's numbers. */
  filters(facts: PromptFacts): Partial<SearchArgs>;
}

/**
 * The table's first three rows. They are *purposes*, not qualities: "sad" and "happy" prescribe a
 * valence bound but they are properties of a track, so a sentence may combine them with a tempo
 * ("sad and slow") without the purpose rule dropping that tempo. Only these three rows say what
 * the music is *for*, which is the case that over-fills.
 */
const RELAX_WORDS = [
  'chill',
  'calm',
  'calma',
  'calme',
  'sleep',
  'sleeping',
  'study',
  'studying',
  'relax',
  'relaxing',
  'reading',
  'focus',
  'estudiar',
  'dormir',
  'relajarse',
  'leer',
  'descansar',
  'étudier',
  'lire',
  'relaxer',
  'détendre',
  'lernen',
  'schlafen',
  'lesen',
  'entspannen',
  '勉強',
  '睡眠',
  '寝る',
  '読書',
  '集中',
  'リラックス',
] as const;

const ENERGISING_WORDS = [
  'workout',
  'gym',
  'exercise',
  'run',
  'running',
  'jog',
  'jogging',
  'training',
  'sport',
  'sports',
  'fitness',
  'energetic',
  'intense',
  'cardio',
  'entrenar',
  'correr',
  'gimnasio',
  'ejercicio',
  'courir',
  'entraînement',
  'laufen',
  'ワークアウト',
  '運動',
  'ジョギング',
  '走る',
  '筋トレ',
  'トレーニング',
  'エクササイズ',
] as const;

const DANCE_WORDS = [
  'party',
  'dance',
  'dancing',
  'bailar',
  'fiesta',
  'discoteca',
  'danser',
  'fête',
  'tanzen',
  'feier',
  'パーティー',
  '踊る',
  'ダンス',
] as const;

export const PURPOSE_RULES: readonly PurposeRule[] = [
  {
    id: 'relax',
    words: RELAX_WORDS,
    filters: (facts) => ({ mood: ['relaxed'], energyMax: facts.energy.chill }),
  },
  {
    id: 'energise',
    words: ENERGISING_WORDS,
    filters: (facts) => ({ energyMin: facts.energy.workout, arousalMin: facts.arousal.workout }),
  },
  {
    id: 'dance',
    words: DANCE_WORDS,
    filters: (facts) => ({ mood: ['party'], danceabilityMin: facts.danceability.party }),
  },
];

/**
 * The purpose a sentence states, if any. The first matching row wins: a sentence naming two
 * purposes ("a workout party") is ambiguous, and guessing a union is how an Intent over-fills.
 */
export function purposeOf(sentence: string): PurposeRule | null {
  const text = sentence.toLowerCase();
  for (const rule of PURPOSE_RULES) {
    if (rule.words.some((word) => containsWord(text, word))) return rule;
  }
  return null;
}

const foldMood = (value: string): string => value.trim().toLowerCase();

/**
 * The rule's filters at this profile, with a mood the library does not have left out.
 *
 * Without a profile the table is the table. With one, a prescribed mood is kept only if the
 * profile has it; when it does not, the row's numeric side stands, and a rule with nothing left
 * is dropped. `purposeFilters` and `normalizePurpose` both come through here, so the retry and
 * the shown Intent agree.
 */
function prescribe(
  rule: PurposeRule,
  facts: PromptFacts,
  profile: LibraryProfile | null,
): Partial<SearchArgs> | null {
  const filters = rule.filters(facts);
  if (!profile || !filters.mood) return filters;
  const moods = new Set(profile.moods.map(foldMood));
  const kept = filters.mood.filter((mood) => moods.has(foldMood(mood)));
  if (kept.length === filters.mood.length) return filters;
  const rest: Partial<SearchArgs> = { ...filters };
  delete rest.mood;
  if (kept.length > 0) return { ...rest, mood: kept };
  return Object.keys(rest).length > 0 ? rest : null;
}

/** The filters the sentence's purpose word prescribes at this profile, if it names one. */
export function purposeFilters(
  sentence: string,
  profile: LibraryProfile | null,
): Partial<SearchArgs> | null {
  const rule = purposeOf(sentence);
  return rule ? prescribe(rule, factsOf(profile), profile) : null;
}

/**
 * The words that make an axis *explicit*. A sentence that says "fast" has asked for a tempo, so a
 * BPM bound is not an invention; the same for a valence and a danceability word. Each list is per
 * language on purpose: the corpus is multilingual, and gating on English alone would strip a bound
 * the Spanish or Japanese sentence had justified.
 */
const TEMPO_WORDS = [
  'fast', 'faster', 'fastest', 'slow', 'slower', 'slowest', 'bpm', 'tempo', 'uptempo',
  'rápido', 'rápida', 'rápidos', 'rápidas', 'lento', 'lenta', 'lentos', 'lentas',
  'rapide', 'rapides', 'lent', 'lente', 'lentes',
  'schnell', 'schnelle', 'schneller', 'langsam', 'langsame', 'langsamer',
  '速い', '遅い', 'テンポ', 'アップテンポ',
] as const;

const VALENCE_WORDS = [
  'happy', 'sad', 'upbeat', 'sunny', 'melancholic', 'cheerful', 'joyful', 'gloomy',
  'alegre', 'alegres', 'triste', 'tristes', 'feliz', 'felices', 'contento', 'contenta',
  'heureux', 'heureuse', 'joyeux', 'joyeuse', 'gai', 'gaie',
  'fröhlich', 'fröhliche', 'traurig', 'traurige', 'glücklich', 'glückliche',
  '明るい', '暗い', '悲しい', '楽しい', '陽気',
] as const;

const DANCEABILITY_WORDS = [
  'danceable', 'dance', 'dancing',
  'bailable', 'bailar', 'baila', 'bailando',
  'dansant', 'dansante', 'danser',
  'tanzbar', 'tanzen',
  '踊れる', 'ダンス', '踊り',
] as const;

const SORT_WORDS = [
  'sort', 'sorted', 'sorting', 'order', 'ordered', 'alphabetical', 'alphabetically',
  'first', 'last', 'slowest', 'fastest',
  'by bpm', 'by title', 'by energy', 'by danceability', 'by duration', 'by length',
  'ordenar', 'ordenado', 'alfabético', 'alfabética', 'primero', 'último', 'última',
  'trier', 'trié', 'alphabétique', 'premier', 'première', 'dernier', 'dernière',
  'sortieren', 'sortiert', 'alphabetisch', 'erste', 'erster', 'letzte', 'letzter',
  '順', '並び', '並べ', '順番',
] as const;

/** A stated tempo: a word, or a number written against `bpm`/`tempo`. */
function hasTempo(sentence: string): boolean {
  return hasAny(sentence, TEMPO_WORDS) || /\d\s*(?:bpm|tempo)\b/i.test(sentence);
}

const MEASUREMENT_KEYS = [
  'bpmMin',
  'bpmMax',
  'durationMin',
  'durationMax',
  'energyMin',
  'energyMax',
  'danceabilityMin',
  'danceabilityMax',
  'valenceMin',
  'valenceMax',
  'arousalMin',
  'arousalMax',
] as const;

/**
 * Make the WORD→FILTER table authoritative over a purpose sentence.
 *
 * The table says a purpose word sets *its* filters and the count, and nothing else. So when the
 * sentence names a purpose and a measurement axis is not explicit in the sentence, that axis
 * keeps only the side the purpose row prescribes: a workout keeps `energy_min` and `arousal_min`
 * and loses the invented maxima, a party keeps `danceability_min`, a sleep keeps `energy_max`.
 * An axis the row does not prescribe at all — BPM, valence and danceability for a workout — goes.
 * A sort nobody asked for goes too.
 *
 * A bound the row prescribes is snapped to the table when the model's is too loose: a maximum
 * above the table's, a minimum below it. An explicit axis keeps its other side, but its
 * prescribed side is still the table's.
 *
 * When no measurement filter is left — an empty or a limit-only answer — the row's filters fill
 * the Intent. A mood, a name or a genre the model did give is not overwritten, and the count
 * stays. The values are the table's at this library's facts, not `factsOf(null)`: the prompt
 * stated the same numbers, so the Intent runs what the sentence was promised.
 *
 * A mood the library does not have is left out: the row's prescribed mood is filtered by
 * `prescribe`, and a mood the model gave that the library lacks is dropped here.
 */
export function normalizePurpose(
  sentence: string,
  intent: SearchArgs,
  profile: LibraryProfile | null = null,
): SearchArgs {
  const rule = purposeOf(sentence);
  if (!rule) return intent;

  const prescribed = prescribe(rule, factsOf(profile), profile) ?? {};
  const keeps = (key: keyof SearchArgs): boolean => prescribed[key] !== undefined;
  const out: SearchArgs = { ...intent };

  /* Snap the prescribed side before anything is deleted: a max above the table's is too loose,
   * a min below it is too loose. The other side is the delete pass's business. */
  for (const key of Object.keys(prescribed) as (keyof SearchArgs)[]) {
    const target = prescribed[key];
    const current = out[key];
    if (typeof target !== 'number' || typeof current !== 'number') continue;
    if (key.endsWith('Max') ? current > target : current < target) {
      (out as Record<string, unknown>)[key] = target;
    }
  }

  /* Every measurement axis the table can prescribe: keep only the side it names, unless the
   * sentence itself made the axis explicit. Duration is deliberately not here: a length is the
   * track's, not the purpose's, and "under three minutes for a workout" asked for it. */
  const axes: readonly (readonly [readonly [keyof SearchArgs, keyof SearchArgs], boolean])[] = [
    [['bpmMin', 'bpmMax'], hasTempo(sentence)],
    [['valenceMin', 'valenceMax'], hasAny(sentence, VALENCE_WORDS)],
    [['danceabilityMin', 'danceabilityMax'], hasAny(sentence, DANCEABILITY_WORDS)],
    [['energyMin', 'energyMax'], false],
    [['arousalMin', 'arousalMax'], false],
  ];
  for (const [[minKey, maxKey], explicit] of axes) {
    if (explicit) continue;
    if (!keeps(minKey)) delete out[minKey];
    if (!keeps(maxKey)) delete out[maxKey];
  }

  if (!hasAny(sentence, SORT_WORDS)) delete out.sort;

  /* A mood the model gave that this library does not have can only empty the search. The row's
   * prescribed mood has already been filtered by `prescribe`; this is the model's own. */
  if (profile && out.mood) {
    const allowed = new Set(profile.moods.map(foldMood));
    const kept = out.mood.filter((mood) => allowed.has(foldMood(mood)));
    if (kept.length === 0) delete out.mood;
    else if (kept.length !== out.mood.length) out.mood = kept;
  }

  /* No measurement is left: an empty or limit-only answer. The row fills the Intent; a mood, a
   * name or a genre the model did give stays, and so does the count. */
  if (!MEASUREMENT_KEYS.some((key) => out[key] !== undefined)) {
    for (const [key, value] of Object.entries(prescribed)) {
      const field = key as keyof SearchArgs;
      if (out[field] === undefined) (out as Record<string, unknown>)[field] = value;
    }
  }

  return Object.keys(out).length === 0 ? intent : out;
}

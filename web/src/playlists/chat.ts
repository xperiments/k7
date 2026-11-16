/**
 * What **Ask** says back, in one place.
 *
 * The conversation is the App's, not the model's: the model still produces an **Intent** and
 * nothing else (ADR 0050, ADR 0084). Everything a person reads here — the count, what was
 * understood, the source, the offer to add the rows to the list beside the panel — is computed
 * from the Intent that ran and the search that answered, so no sentence can describe a track
 * that does not exist (ADR 0093).
 *
 * These functions are pure so the copy is tested without React, a model or a device.
 */
import { formatDuration } from '../ui/format';

import type { McpSearch, SearchArgs } from '../device/mcp';

/** The library that answered, and where a playlist for it would be stored. */
export interface AnswerSource {
  /** What the thread names under the answer: the player, or the Last image. */
  label: string;
  kind: 'device' | 'image';
  /**
   * The card scope a playlist from this answer belongs to, or `null` when the App cannot
   * author one: the player's card is not a directory this browser holds.
   */
  draftScope: string | null;
}

/** One answered ask: the filters that ran, the result, and the library that gave it. */
export interface Answer {
  /** The filters that actually ran — relaxed, clamped or hand-set. */
  intent: SearchArgs;
  search: McpSearch;
  source: AnswerSource;
  /** The exact search found nothing and this answer is the retry without its ranges. */
  relaxed?: boolean;
  /** The person ran the filters themselves; no model was asked. */
  manual?: boolean;
  /** The playlist's name if it is saved: the sentence, shortened. */
  name: string;
  /** Set once the person saves it as a list of its own; the draft's id. */
  draftId?: string;
  /** Set once the person adds its rows to the list the panel stands beside. */
  added?: boolean;
  /** The person said not now; the save question goes away. */
  dismissed?: boolean;
}

/** The one retry a failed message offers. */
export type Retry =
  | { kind: 'ask'; sentence: string }
  | { kind: 'search'; intent: SearchArgs };

export interface ChatMessage {
  id: string;
  role: 'user' | 'assistant';
  /** A person's sentence, or the App's note or error. An answer message has none. */
  text?: string;
  tone?: 'error';
  answer?: Answer;
  retry?: Retry;
}

const SORT_LABEL: Record<NonNullable<SearchArgs['sort']>, string> = {
  path: 'path',
  title: 'title',
  bpm: 'BPM',
  energy: 'energy',
  danceability: 'danceability',
  duration: 'length',
};

/** A length is seconds in the Intent and m:ss in the sentence. */
function lengthPair(low: number | undefined, high: number | undefined): string | null {
  if (low !== undefined && high !== undefined) {
    return `length ${formatDuration(low)}–${formatDuration(high)}`;
  }
  if (low !== undefined) return `length from ${formatDuration(low)}`;
  if (high !== undefined) return `length up to ${formatDuration(high)}`;
  return null;
}

function pair(
  low: number | undefined,
  high: number | undefined,
  word: string,
): string | null {
  if (low !== undefined && high !== undefined) return `${word} ${low}–${high}`;
  if (low !== undefined) return `${word} from ${low}`;
  if (high !== undefined) return `${word} up to ${high}`;
  return null;
}

/**
 * The Intent in the words a person used: "techno, 124–132 BPM, key 8A". Paging is left out —
 * the answer's count is its outcome — and so is a limit that only caps rows.
 */
export function filterWords(intent: SearchArgs): string[] {
  const words: string[] = [];
  if (intent.query) words.push(`“${intent.query}”`);
  if (intent.genre) words.push(intent.genre);
  if (intent.mood && intent.mood.length > 0) words.push(intent.mood.join(', '));
  if (intent.key && intent.key.length > 0) words.push(`key ${intent.key.join(', ')}`);

  const bpm = pair(intent.bpmMin, intent.bpmMax, '');
  if (bpm) words.push(`${bpm.trim()} BPM`);
  const length = lengthPair(intent.durationMin, intent.durationMax);
  if (length) words.push(length);
  const energy = pair(intent.energyMin, intent.energyMax, 'energy');
  if (energy) words.push(energy);
  const danceability = pair(
    intent.danceabilityMin,
    intent.danceabilityMax,
    'danceability',
  );
  if (danceability) words.push(danceability);
  const valence = pair(intent.valenceMin, intent.valenceMax, 'valence');
  if (valence) words.push(valence);
  const arousal = pair(intent.arousalMin, intent.arousalMax, 'arousal');
  if (arousal) words.push(arousal);

  if (intent.sort) words.push(`sorted by ${SORT_LABEL[intent.sort]}`);
  return words;
}

/**
 * Why an answer's rows are not the ranges that were asked for: the exact search found nothing,
 * so the retry dropped them. The rows are the tree and the count is the heading, so this is the
 * one thing the panel still has to say about how it read the sentence.
 */
export const RELAXED_NOTE = 'Those ranges found nothing, so I dropped them.';

/** The one line under an answer: which library it came from. */
export function sourceText(source: AnswerSource): string {
  return source.label;
}

/** The assistant's question, or the reason it cannot ask one. */
export function addQuestion(count: number, list: string): string {
  const what = count === 1 ? 'this track' : `these ${count} tracks`;
  return `Add ${what} to “${list}”?`;
}

/** What the answer says once its rows are in the list it was asked beside. */
export function addedNote(count: number, list: string): string {
  const what = count === 1 ? '1 track' : `${count} tracks`;
  return `Added ${what} to “${list}”.`;
}

/** Why there is nothing to add: the answer came from a card this list does not belong to. */
export const READ_ONLY_SOURCE =
  'This answer came from another card, so its tracks cannot go into this list.';

/** Why an answer can no longer be added: the selected card changed under it. */
export const CARD_CHANGED = 'The selected card changed since this answer — ask again to add it.';

/** A saved answer's confirmation: the other choice, a list of its own. */
export const SAVED_NOTE =
  'Saved as a new list on this card. Open it in Playlists, or send it from Prepare.';

/** A playlist name from the sentence: one line, at most 60 characters. */
export function nameFor(sentence: string, fallback = 'Asked set'): string {
  const name = sentence.replace(/\s+/g, ' ').trim().slice(0, 60).trim();
  return name || fallback;
}

/** Three openings for an empty thread: a mood, a name and a count. */
export const OPENERS = [
  'a late-night 8A set',
  'songs by Daft Punk',
  '10 songs for a workout',
] as const;

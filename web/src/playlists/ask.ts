/**
 * The assistant's half of **Ask**: a sentence in, an **Intent** out, and nothing else.
 *
 * ADR 0050 settled the shape and ADR 0053 adopted it: the model produces the
 * `library_search` filters, never the results, because a wrong filter is visible and
 * correctable while a confident paragraph about music that does not exist is not. The
 * library itself is never described to the Provider — the request carries the sentence and
 * the shape instructions, and no track, artist, path or measurement (ADR 0053).
 *
 * `parseIntent` is the boundary: whatever the model says is untrusted text, so every field
 * is checked, unknown keys are dropped, and every number is clamped to the range the
 * device documents (limits from `library_search` in `components/portable/mcp/mcp.c`).
 * `requestIntent` is the only function that touches the network, and it is not tested.
 */

import type { SearchArgs } from '../device/mcp';
import { getSettings, hasProvider, type Settings } from '../settings';

/** The five mood heads the device knows. Danceability is a property, not a mood. */
export const MOOD_VALUES = ['happy', 'sad', 'aggressive', 'relaxed', 'party'] as const;

/** `library_search`'s `sort`, exactly. */
export const SORT_VALUES = ['path', 'title', 'bpm', 'energy', 'danceability'] as const;

/** A failed Intent: one sentence the Ask area can show as it is. */
export interface IntentFailure {
  error: string;
}

function failure(error: string): IntentFailure {
  return { error };
}

/* -------------------------------------------------------------------------- */
/* Validation                                                                  */
/* -------------------------------------------------------------------------- */

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** A non-empty trimmed string, or undefined. */
function text(value: unknown): string | undefined {
  if (typeof value !== 'string') return undefined;
  const trimmed = value.trim();
  return trimmed ? trimmed.slice(0, 120) : undefined;
}

/** A finite number clamped into `[min, max]`; anything else is not a number at all. */
function numeric(value: unknown, min: number, max: number, round = false): number | undefined {
  if (typeof value !== 'number' || !Number.isFinite(value)) return undefined;
  const clamped = Math.min(max, Math.max(min, value));
  return round ? Math.round(clamped) : clamped;
}

/** An array of strings, trimmed, without empties or repeats. */
function stringList(value: unknown): string[] {
  if (!Array.isArray(value)) return [];
  const out: string[] = [];
  for (const item of value) {
    if (typeof item !== 'string') continue;
    const trimmed = item.trim();
    if (trimmed && !out.includes(trimmed)) out.push(trimmed);
  }
  return out;
}

function isMood(value: string): value is (typeof MOOD_VALUES)[number] {
  return (MOOD_VALUES as readonly string[]).includes(value.toLowerCase());
}

function isSort(value: string): value is (typeof SORT_VALUES)[number] {
  return (SORT_VALUES as readonly string[]).includes(value.toLowerCase());
}

type Unwrapped =
  | { ok: true; value: Record<string, unknown> }
  | { ok: false; error: string };

/** The first `{…}` in the text, for the model that wraps its JSON in a sentence. */
function braced(text: string): string {
  const start = text.indexOf('{');
  const end = text.lastIndexOf('}');
  return start >= 0 && end > start ? text.slice(start, end + 1) : '';
}

/**
 * The answer as one object. Code fences are tolerated (a `response_format` is a request,
 * not a promise), prose around the object is tolerated, and anything that is not an object
 * is an error rather than an empty Intent — silence would look like "no filters".
 */
function unwrap(answer: string): Unwrapped {
  const trimmed = answer.trim();
  if (!trimmed) return { ok: false, error: 'The model answered nothing.' };

  const unfenced = trimmed
    .replace(/^```(?:json)?\s*/i, '')
    .replace(/\s*```$/, '')
    .trim();

  for (const candidate of [unfenced, braced(unfenced)]) {
    if (!candidate) continue;
    let parsed: unknown;
    try {
      parsed = JSON.parse(candidate);
    } catch {
      continue;
    }
    if (!isRecord(parsed)) {
      return { ok: false, error: 'The answer was not a JSON object of filters.' };
    }
    return { ok: true, value: parsed };
  }

  return { ok: false, error: 'The answer was not JSON, so there is nothing to search for.' };
}

/* -------------------------------------------------------------------------- */
/* The Intent                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * The model's answer as `library_search` arguments.
 *
 * The model is asked for snake_case keys named after the tool's own arguments; the app
 * hands `searchTracks` camelCase ones. Unknown keys are dropped rather than passed on, and
 * a number outside its documented range is clamped rather than discarded: "140 BPM" and
 * "1400 BPM" both produce something runnable, and the visible Intent shows what happened.
 */
export function parseIntent(answer: string): SearchArgs | IntentFailure {
  const parsed = unwrap(answer);
  if (!parsed.ok) return failure(parsed.error);
  const raw = parsed.value;

  const out: SearchArgs = {};

  const query = text(raw.query);
  if (query) out.query = query;
  const genre = text(raw.genre);
  if (genre) out.genre = genre;

  // BPM: the firmware accepts 0..400 (`arg_bpm`), before folding and before the ×10 encoding.
  const bpmMin = numeric(raw.bpm_min, 0, 400);
  if (bpmMin !== undefined) out.bpmMin = bpmMin;
  const bpmMax = numeric(raw.bpm_max, 0, 400);
  if (bpmMax !== undefined) out.bpmMax = bpmMax;

  // The four regressions are percentages on the wire (0..100 integers).
  const energyMin = numeric(raw.energy_min, 0, 100, true);
  if (energyMin !== undefined) out.energyMin = energyMin;
  const energyMax = numeric(raw.energy_max, 0, 100, true);
  if (energyMax !== undefined) out.energyMax = energyMax;
  const danceabilityMin = numeric(raw.danceability_min, 0, 100, true);
  if (danceabilityMin !== undefined) out.danceabilityMin = danceabilityMin;
  const danceabilityMax = numeric(raw.danceability_max, 0, 100, true);
  if (danceabilityMax !== undefined) out.danceabilityMax = danceabilityMax;
  const valenceMin = numeric(raw.valence_min, 0, 100, true);
  if (valenceMin !== undefined) out.valenceMin = valenceMin;
  const valenceMax = numeric(raw.valence_max, 0, 100, true);
  if (valenceMax !== undefined) out.valenceMax = valenceMax;
  const arousalMin = numeric(raw.arousal_min, 0, 100, true);
  if (arousalMin !== undefined) out.arousalMin = arousalMin;
  const arousalMax = numeric(raw.arousal_max, 0, 100, true);
  if (arousalMax !== undefined) out.arousalMax = arousalMax;

  // Keys are kept as the model wrote them: the device understands Camelot codes and note
  // names, and deciding which one a string is would be guessing on its behalf.
  const keys = stringList(raw.key).slice(0, 24);
  if (keys.length > 0) out.key = keys;

  const moods = stringList(raw.mood).filter(isMood).map((mood) => mood.toLowerCase());
  if (moods.length > 0) out.mood = Array.from(new Set(moods)).slice(0, MOOD_VALUES.length);

  if (typeof raw.sort === 'string' && isSort(raw.sort)) out.sort = raw.sort.toLowerCase() as
    (typeof SORT_VALUES)[number];

  const offset = numeric(raw.offset, 0, 199, true);
  const limit = numeric(raw.limit, 1, 100, true);
  if (offset !== undefined || limit !== undefined) {
    const start = offset ?? 0;
    // The device refuses `offset + limit > 200`; trim the page instead of losing the ask.
    out.limit = Math.max(1, Math.min(limit ?? 200 - start, 200 - start));
    if (offset !== undefined) out.offset = start;
  }

  if (Object.keys(out).length === 0) {
    return failure('The answer had no filters this library understands, so nothing can be run.');
  }
  return out;
}

/* -------------------------------------------------------------------------- */
/* The request                                                                 */
/* -------------------------------------------------------------------------- */

/**
 * The instruction that turns a sentence into filters. It names the tool's arguments, their
 * ranges and their closed sets, and nothing else: there is no track in it, and there is no
 * room in it for one.
 */
const SYSTEM_PROMPT = [
  'You turn one sentence from a listener into arguments for a search over their personal',
  'music library. You have not seen the library and cannot see it: never name a track, an',
  'artist, an album or a genre of your own invention, and never answer with a list of music.',
  '',
  'Answer with ONE JSON object and no prose and no code fences. Every key is optional:',
  '{',
  '  "query": string,              // words to find in a path, title, artist, album or genre',
  '  "genre": string,              // one genre',
  '  "bpm_min": number,            // 0 to 400',
  '  "bpm_max": number,            // 0 to 400',
  '  "energy_min": integer,        // 0 to 100',
  '  "energy_max": integer,        // 0 to 100',
  '  "danceability_min": integer,  // 0 to 100',
  '  "danceability_max": integer,  // 0 to 100',
  '  "valence_min": integer,       // 0 to 100, pleasantness',
  '  "valence_max": integer,       // 0 to 100',
  '  "arousal_min": integer,       // 0 to 100, intensity',
  '  "arousal_max": integer,       // 0 to 100',
  '  "key": string[],              // Camelot codes like "8A" or note names like "Am"',
  '  "mood": string[],             // only: happy, sad, aggressive, relaxed, party',
  '  "sort": string,               // only: path, title, bpm, energy, danceability',
  '  "offset": integer,            // 0 to 199',
  '  "limit": integer              // 1 to 100',
  '}',
  '',
  'Use only the keys that the sentence asks for. A number of tracks becomes "limit"; a',
  'duration becomes a limit of about fifteen tracks per hour, never more than 100. When the',
  'sentence names no musical constraints, answer with an empty object {}.',
].join('\n');

/** The text of an OpenAI-compatible chat completion, or undefined when there is none. */
function answerOf(body: unknown): string | undefined {
  if (!isRecord(body) || !Array.isArray(body.choices) || body.choices.length === 0) return undefined;
  const first = body.choices[0];
  if (!isRecord(first) || !isRecord(first.message)) return undefined;
  const content = first.message.content;
  if (typeof content === 'string') return content;
  // A few providers return parts instead of one string; the text parts are the answer.
  if (Array.isArray(content)) {
    const parts = content
      .filter((part): part is { text: string } => isRecord(part) && typeof part.text === 'string')
      .map((part) => part.text);
    return parts.length > 0 ? parts.join('\n') : undefined;
  }
  return undefined;
}

const TIMEOUT_MS = 30_000;

/**
 * Ask the Provider for an Intent.
 *
 * Every failure is a sentence the area can show as it is, and none of them is fatal: the
 * filter form stays usable, which is the whole point of the Intent being visible.
 */
export async function requestIntent(
  sentence: string,
  settings: Settings = getSettings(),
): Promise<SearchArgs | IntentFailure> {
  if (!hasProvider(settings)) {
    return failure('A Provider is needed to ask the model. Set its base URL, model and key in Settings.');
  }
  const asked = sentence.trim();
  if (!asked) return failure('Say what you are looking for first.');

  const base = settings.providerBaseUrl
    .trim()
    .replace(/\/+$/, '')
    .replace(/\/chat\/completions$/, '');

  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), TIMEOUT_MS);

  let response: Response;
  try {
    response = await fetch(`${base}/chat/completions`, {
      method: 'POST',
      headers: {
        'content-type': 'application/json',
        authorization: `Bearer ${settings.providerKey}`,
      },
      body: JSON.stringify({
        model: settings.providerModel,
        temperature: 0,
        response_format: { type: 'json_object' },
        messages: [
          { role: 'system', content: SYSTEM_PROMPT },
          { role: 'user', content: asked },
        ],
      }),
      signal: controller.signal,
    });
  } catch {
    return failure(
      'The Provider could not be reached. Check its base URL in Settings and that this computer is online.',
    );
  } finally {
    clearTimeout(timeout);
  }

  if (response.status === 401 || response.status === 403) {
    return failure('The Provider refused the key. Check it in Settings.');
  }
  if (response.status === 429) {
    return failure('The Provider is rate-limiting this key. Try again in a moment.');
  }
  if (!response.ok) {
    return failure(`The Provider answered ${response.status}, so there are no filters to run.`);
  }

  let body: unknown;
  try {
    body = await response.json();
  } catch {
    return failure('The Provider answered something this page cannot read.');
  }

  const answer = answerOf(body);
  if (!answer) return failure('The Provider returned no answer to read.');
  return parseIntent(answer);
}

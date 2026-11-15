/**
 * The ask: one sentence in, one Intent out, with the one repair each failure earns.
 *
 * `requestIntent` is the only function that asks the model, and it is not tested: the built-in
 * model is Chrome's and CI has none. The schema it passes and the parser it trusts are both
 * tested (ADR 0084).
 */

import { askModel, assistantFailure } from '../../builtin-model';
import type { SearchArgs } from '../../device/mcp';
import type { LibraryProfile } from '../../domain/snapshot';

import { failure, type IntentFailure } from './contract';
import { normalizeKeys } from './keys';
import { isNameQuery, nameFrom, parseIntent } from './parse';
import { buildSystemPrompt, intentExamples, intentSchema } from './prompt';
import { normalizePurpose, purposeFilters } from './purpose';
import { applySentenceFilters, sentenceFilters } from './sentence';
import { hasAny } from './words';

/** The imperative verbs that ask the App to change the library, not to search it. */
const ACTION_WORDS = ['add', 'save', 'put', 'remove', 'delete', 'rename', 'create'] as const;

function isAction(sentence: string): boolean {
  return hasAny(sentence, ACTION_WORDS);
}

/** One model call, as the evaluation harness sees it. Never passed by the App. */
export interface IntentTrace {
  call: 1 | 2;
  /** `first` for the first ask, otherwise the repair that made a second one necessary. */
  reason: 'first' | 'notJson' | 'queryNotName';
  answer: string;
  ms: number;
}

/**
 * Ask the built-in model for an Intent.
 *
 * Every failure is a sentence the area can show as it is, and none of them is fatal: the filter
 * form stays usable, which is the whole point of the Intent being visible. One repair follows an
 * answer that was not a usable object — an empty Intent is the model's correct answer to a
 * sentence with no musical constraint, and asking twice would only repeat it — and one follows
 * a query that is not a name the sentence used, because the sentence says which name it meant.
 *
 * `trace` is the harness's sink and nothing in the App passes it: with it, each model call reports
 * its answer and how long it took; without it, the function asks exactly what it asked before.
 *
 * The sentence's own numbers and key are applied before the normalizers, so a stated count, a
 * length, a tempo or a key does not depend on the model (ADR 0088).
 */
export async function requestIntent(
  sentence: string,
  profile: LibraryProfile | null = null,
  trace?: (attempt: IntentTrace) => void,
  signal?: AbortSignal,
): Promise<SearchArgs | IntentFailure> {
  const asked = sentence.trim();
  if (!asked) return failure('Say what you are looking for first.');
  const fromSentence = sentenceFilters(asked, profile);

  const system = buildSystemPrompt(profile);
  const schema = intentSchema(profile);
  const examples = intentExamples(profile);

  const ask = async (
    call: 1 | 2,
    reason: IntentTrace['reason'],
    askedSentence: string,
  ): Promise<string> => {
    if (!trace) {
      return askModel({ system, schema, sentence: askedSentence, examples, signal });
    }
    const started = performance.now();
    const response = await askModel({ system, schema, sentence: askedSentence, examples, signal });
    trace({ call, reason, answer: response, ms: performance.now() - started });
    return response;
  };

  let answer: string;
  try {
    answer = await ask(1, 'first', asked);
  } catch (error) {
    return failure(assistantFailure(error));
  }

  /* The purpose normalizer is the last filter step: it runs on whatever the model finally said,
   * including a repaired answer, so the Intent the App shows is the one the search will run. */
  const finish = (result: SearchArgs | IntentFailure): SearchArgs | IntentFailure =>
    'error' in result
      ? result
      : normalizePurpose(
          asked,
          applySentenceFilters(normalizeKeys(asked, result), fromSentence),
          profile,
        );

  /* Finish a repaired answer, or rebuild an empty one from what the sentence itself stated: the
   * purpose table when it names one, and the sentence's own count, session, length, BPM, offset
   * or key. The model's `{}` is its no-filter answer, not a refusal, so the app applies the
   * sentence's filters; an instruction to change the library ("add 5 songs to the playlist") and
   * a sentence with neither a purpose nor a stated filter still refuse. */
  const recover = (result: SearchArgs | IntentFailure): SearchArgs | IntentFailure => {
    if (!('error' in result)) return finish(result);
    if (result.retryable) return result;
    if (isAction(asked)) return result;
    const table = purposeFilters(asked, profile);
    const stated = Object.keys(fromSentence).length > 0;
    if ((!table || Object.keys(table).length === 0) && !stated) return result;
    return finish({ ...(table ?? {}) });
  };

  const intent = parseIntent(answer);

  if (!('error' in intent)) {
    const query = intent.query;
    if (!query || isNameQuery(asked, query)) return finish(intent);

    /* The sentence usually names the thing: take that name, or drop the bad query when another
     * filter remains, before spending a second model call. */
    const name = nameFrom(asked);
    if (name) return finish({ ...intent, query: name });

    const otherFilter =
      Object.keys(intent).some((key) => key !== 'query') ||
      Object.keys(fromSentence).length > 0;
    if (otherFilter) {
      const withoutQuery: SearchArgs = { ...intent };
      delete withoutQuery.query;
      return finish(withoutQuery);
    }

    return finish(await repairQuery(asked, answer, query, ask));
  }

  if (!intent.retryable) return recover(intent);

  let repaired: string;
  try {
    repaired = await ask(
      2,
      'notJson',
      `${asked}\n\nYour previous answer was: ${answer}\nIt failed because: ${intent.error}\nAnswer again with one JSON object and nothing else.`,
    );
  } catch (error) {
    return failure(assistantFailure(error));
  }
  return recover(parseIntent(repaired));
}

/** The one call a repair makes, and the trace it reports. */
type AskOnce = (call: 1 | 2, reason: IntentTrace['reason'], sentence: string) => Promise<string>;

/**
 * The model sometimes answers a name with a word the sentence did not mark as one ("songs" for
 * "songs by Daft Punk", "workout" for "10 songs for a workout"). The parser cannot know the
 * name, but this function has the sentence: ask once more with the specific failure. If the
 * word comes back and is still not a name, drop it so it cannot narrow the search.
 */
async function repairQuery(
  asked: string,
  answer: string,
  query: string,
  ask: AskOnce,
): Promise<SearchArgs | IntentFailure> {
  let repaired: string;
  try {
    repaired = await ask(
      2,
      'queryNotName',
      `${asked}\n\nYour previous answer was: ${answer}\nIts "query" is "${query}", which is not a name the sentence used. If the sentence names an artist, an album or a title, put that name in "query"; otherwise leave "query" out. Answer again with one JSON object.`,
    );
  } catch (error) {
    return failure(assistantFailure(error));
  }

  const fixed = parseIntent(repaired);
  if ('error' in fixed) return fixed;
  if (!fixed.query || isNameQuery(asked, fixed.query)) return fixed;

  delete fixed.query;
  if (Object.keys(fixed).length === 0) {
    return failure(
      'The model answered with a word that is not a name the sentence used and nothing else, so there are no filters to run.',
    );
  }
  return fixed;
}

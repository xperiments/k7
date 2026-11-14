/**
 * What to search when the exact Intent found nothing: one retry without the numeric ranges.
 *
 * This is search policy, not intent: a hand-run Intent passes no options and keeps the mood-only
 * rule, while an ask may pass the pair the sentence's purpose word prescribes.
 */

import type { SearchArgs } from '../device/mcp';

import { NUMERIC_AXES, NUMERIC_FILTERS } from './ask/contract';

/** The purpose's number pairs only: a retry broadens the sound, it does not re-add a mood. */
function purposePairs(purpose: Partial<SearchArgs>): SearchArgs {
  const out: SearchArgs = {};
  for (const key of NUMERIC_FILTERS) {
    const value = purpose[key];
    if (value !== undefined) out[key] = value;
  }
  return out;
}

/**
 * A retry of an Intent whose exact search found nothing.
 *
 * Without `options` this is the rule Ask has always had: a mood left to search on, and every
 * numeric range dropped. With `options` — the ask path, where the sentence is known — a
 * range-heavy Intent may also retry without a mood: with the pair the sentence's purpose word
 * prescribes, or with no ranges at all when the sentence has no purpose word. A hand-run Intent
 * passes no options, so the filters a person set by hand keep the old behaviour.
 *
 * Three measurement ranges is the threshold: below it the sentence asked for a narrow thing and
 * dropping the ranges would answer a different question.
 */
export function relaxRanges(
  intent: SearchArgs,
  options?: { purpose?: Partial<SearchArgs> | null },
): SearchArgs | null {
  const hasRanges = NUMERIC_FILTERS.some((key) => intent[key] !== undefined);
  if (!hasRanges) return null;

  const withoutRanges = (): SearchArgs => {
    const relaxed: SearchArgs = { ...intent };
    for (const key of NUMERIC_FILTERS) delete relaxed[key];
    return relaxed;
  };

  if (intent.mood && intent.mood.length > 0) return withoutRanges();
  if (!options) return null;

  const ranges = NUMERIC_AXES.filter((keys) => keys.some((key) => intent[key] !== undefined)).length;
  if (ranges < 3) return null;

  const retry = withoutRanges();
  if (options.purpose) Object.assign(retry, purposePairs(options.purpose));
  return retry;
}

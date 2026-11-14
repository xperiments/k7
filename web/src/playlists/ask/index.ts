/**
 * The assistant's half of **Ask**: a sentence in, an **Intent** out, and nothing else.
 *
 * ADR 0050 settled the shape and ADR 0053 adopted it: the model produces the
 * `library_search` filters, never the results, because a wrong filter is visible and
 * correctable while a confident paragraph about music that does not exist is not. The
 * library's per-track data is never described to the model: the request carries the sentence,
 * the shape instructions, and — when this browser built the image the player is running — the
 * library's aggregates, never a track, an artist, a path or a per-track measurement.
 *
 * `parseIntent` is the boundary: whatever the model says is untrusted text, so every field
 * is checked, unknown keys are dropped, and every number is clamped to the range the
 * device documents (limits from `library_search` in `components/portable/mcp/mcp.c`).
 *
 * `requestIntent` is the only function that asks the model, and it is not tested: the
 * built-in model is Chrome's and CI has none. The schema it passes and the parser it trusts
 * are both tested (ADR 0084).
 *
 * What Ask says back is not this module's: the count, the reading, the source and the save
 * question are computed in `chat.ts` from the Intent and the search, so the model never writes a
 * sentence about a track (ADR 0086).
 */

export { requestIntent } from './request';
export { purposeFilters } from './purpose';
export { relaxRanges } from '../relax';
export { clampBpm, MOOD_VALUES, SORT_VALUES } from './contract';
export type { IntentFailure } from './contract';

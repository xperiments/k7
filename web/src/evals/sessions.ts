/**
 * The session measurement: create vs clone per ask, and the two Prompt API options a web page can
 * ask for. It is a dev harness, not the App: the App still creates one session per ask and
 * destroys it (ADR 0084), and nothing here is imported by `src/main.tsx` or `src/ui/**`.
 *
 * The API facts this harness is built on (Chrome 157, the shipping web IDL):
 * - `session.clone()` and `omitResponseConstraintInput` are in the stable Prompt API. A clone
 *   preserves the initial prompt and the conversation context (Chrome's session-management docs).
 * - `temperature` and `topK` are extension-only; a web page ignores them, so the harness never
 *   passes them.
 * - `samplingMode` is the Sampling Parameters origin trial. The probe reads it back from a real
 *   session; without the trial the session reports no mode and that strategy is not offered.
 */

import type { SearchArgs } from '../device/mcp';
import type { LibraryProfile } from '../domain/snapshot';
import type { IntentFailure } from '../playlists/ask/contract';
import { parseIntent } from '../playlists/ask/parse';
import { buildSystemPrompt, intentExamples, intentSchema } from '../playlists/ask/prompt';

import { CORPUS } from './corpus';
import { FIELD_NAMES, FIELD_STATES, type FieldName, type FieldTally } from './fields';
import { digest } from './harness';
import { scoreRow, type ScoredRun } from './score';

/* -------------------------------------------------------------------------- */
/* The plan and the strategies                                                 */
/* -------------------------------------------------------------------------- */

export interface SessionPlan {
  profile: LibraryProfile | null;
  system: string;
  schema: object;
  examples: readonly (readonly [string, string])[];
  initialPrompts: { role: 'system' | 'user' | 'assistant'; content: string }[];
  systemSha: string;
  schemaSha: string;
  examplesSha: string;
}

/** What every arm sends, regardless of how its session is made. */
export function sessionPlan(profile: LibraryProfile | null = null): SessionPlan {
  const system = buildSystemPrompt(profile);
  const schema = intentSchema(profile);
  const examples = intentExamples(profile);
  return {
    profile,
    system,
    schema,
    examples,
    initialPrompts: [
      { role: 'system', content: system },
      ...examples.flatMap(([asked, answered]) => [
        { role: 'user' as const, content: asked },
        { role: 'assistant' as const, content: answered },
      ]),
    ],
    systemSha: digest(system),
    schemaSha: digest(JSON.stringify(schema)),
    examplesSha: digest(examples.map(([asked, answered]) => `${asked}\n${answered}`).join('\n')),
  };
}

export const BASE_STRATEGIES = ['create', 'clone', 'create-omit', 'clone-omit'] as const;
export type Strategy = (typeof BASE_STRATEGIES)[number] | 'sampling';

/** The trial-only arm is added only when a session actually reports a sampling mode. */
export function strategiesFor(samplingAvailable: boolean): Strategy[] {
  return samplingAvailable ? [...BASE_STRATEGIES, 'sampling'] : [...BASE_STRATEGIES];
}

export function strategyLabel(strategy: Strategy): string {
  switch (strategy) {
    case 'create':
      return 'create';
    case 'clone':
      return 'clone';
    case 'create-omit':
      return 'create + omit';
    case 'clone-omit':
      return 'clone + omit';
    case 'sampling':
      return 'create + sampling';
  }
}

export function strategyDescription(strategy: Strategy): string {
  switch (strategy) {
    case 'create':
      return 'one LanguageModel.create per ask, destroy after; the shipped shape';
    case 'clone':
      return 'one base session, session.clone() per ask, destroy; base destroyed at the end';
    case 'create-omit':
      return 'create per ask with omitResponseConstraintInput: true on the prompt';
    case 'clone-omit':
      return 'clone per ask with omitResponseConstraintInput: true on the prompt';
    case 'sampling':
      return "create with samplingMode: 'creative' (Sampling Parameters origin trial)";
  }
}

/* -------------------------------------------------------------------------- */
/* One ask, however its session is made                                        */
/* -------------------------------------------------------------------------- */

function model(): LanguageModelStatic {
  if (typeof LanguageModel === 'undefined') {
    throw new Error('The built-in model is not available in this browser.');
  }
  return LanguageModel;
}

function described(error: unknown): string {
  if (error instanceof Error) {
    return error.name === 'AbortError'
      ? 'the call timed out or was stopped'
      : `${error.name}: ${error.message}`;
  }
  return String(error);
}

async function askWith(
  strategy: Strategy,
  plan: SessionPlan,
  base: LanguageModelSession | undefined,
  sentence: string,
  signal: AbortSignal,
): Promise<string> {
  const session =
    strategy === 'clone' || strategy === 'clone-omit'
      ? await base!.clone()
      : await model().create({
          initialPrompts: plan.initialPrompts,
          ...(strategy === 'sampling' ? { samplingMode: 'creative' as const } : {}),
        });
  try {
    return await session.prompt(sentence, {
      responseConstraint: plan.schema,
      ...(strategy.endsWith('omit') ? { omitResponseConstraintInput: true } : {}),
      signal,
    });
  } finally {
    session.destroy();
  }
}

/* -------------------------------------------------------------------------- */
/* A run                                                                       */
/* -------------------------------------------------------------------------- */

export interface SessionRecord {
  strategy: Strategy;
  rowId: string;
  sentence: string;
  rep: 1 | 2 | 3;
  answer: string;
  ms: number;
  parsed: SearchArgs | IntentFailure;
  /** Absent only when the model call itself threw (timeout, stop, no model); the row is an error. */
  scored?: ScoredRun;
  error?: string;
}

export interface SessionSetup {
  strategy: Strategy;
  ms: number;
  samplingMode?: string;
}

export interface RunOptions {
  timeoutMs: number;
  signal?: AbortSignal;
  onRecord?: (record: SessionRecord) => void;
  onSetup?: (setup: SessionSetup) => void;
}

async function runOne(
  strategy: Strategy,
  plan: SessionPlan,
  base: LanguageModelSession | undefined,
  row: (typeof CORPUS)[number],
  rep: 1 | 2 | 3,
  options: RunOptions,
): Promise<SessionRecord> {
  const started = performance.now();
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), options.timeoutMs);
  const signal = options.signal
    ? AbortSignal.any([controller.signal, options.signal])
    : controller.signal;
  try {
    const answer = await askWith(strategy, plan, base, row.sentence, signal);
    const parsed = parseIntent(answer);
    return {
      strategy,
      rowId: row.id,
      sentence: row.sentence,
      rep,
      answer,
      ms: performance.now() - started,
      parsed,
      scored: scoreRow(row, answer, parsed, { profile: plan.profile }),
    };
  } catch (error) {
    const message = described(error);
    return {
      strategy,
      rowId: row.id,
      sentence: row.sentence,
      rep,
      answer: '',
      ms: performance.now() - started,
      parsed: { error: message, retryable: true },
      error: message,
    };
  } finally {
    clearTimeout(timer);
  }
}

/**
 * Run one strategy over the frozen corpus, three repetitions, stopping on the page's signal.
 * The clone arms create one base session and clone it per ask; the base is never prompted, so
 * every clone starts from the same initial prompt and no context from an earlier ask leaks in.
 */
export async function runStrategy(
  strategy: Strategy,
  plan: SessionPlan,
  options: RunOptions,
): Promise<SessionRecord[]> {
  const started = performance.now();
  let base: LanguageModelSession | undefined;
  if (strategy === 'clone' || strategy === 'clone-omit') {
    base = await model().create({ initialPrompts: plan.initialPrompts });
  }
  options.onSetup?.({
    strategy,
    ms: performance.now() - started,
    samplingMode: base?.samplingMode,
  });

  const records: SessionRecord[] = [];
  try {
    for (const row of CORPUS) {
      for (const rep of [1, 2, 3] as const) {
        if (options.signal?.aborted) return records;
        const record = await runOne(strategy, plan, base, row, rep, options);
        records.push(record);
        options.onRecord?.(record);
      }
    }
  } finally {
    base?.destroy();
  }
  return records;
}

/* -------------------------------------------------------------------------- */
/* The clone sees the same prompt: a probe, not a promise                      */
/* -------------------------------------------------------------------------- */

export interface CloneProbe {
  sentence: string;
  baseAnswer: string;
  cloneAnswer: string;
  sameIntent: boolean;
  baseSamplingMode?: string;
  cloneSamplingMode?: string;
}

async function withTurn(
  session: LanguageModelSession,
  sentence: string,
  schema: object,
  timeoutMs: number,
  signal?: AbortSignal,
): Promise<string> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  const combined = signal ? AbortSignal.any([controller.signal, signal]) : controller.signal;
  try {
    return await session.prompt(sentence, {
      responseConstraint: schema,
      signal: combined,
    });
  } finally {
    clearTimeout(timer);
  }
}

/**
 * Ask a fresh session and a clone the same sentence. The clone is taken before the base is
 * prompted, so both start from the initial prompts; equal Intents are the check the task asks
 * for, and a difference is a sampling difference to read, not proof the prompt was lost.
 */
export async function probeClone(
  plan: SessionPlan,
  timeoutMs: number,
  signal?: AbortSignal,
): Promise<CloneProbe> {
  const sentence = plan.examples[0]?.[0] ?? 'play something';
  const base = await model().create({ initialPrompts: plan.initialPrompts });
  try {
    const clone = await base.clone();
    try {
      const [baseAnswer, cloneAnswer] = await Promise.all([
        withTurn(base, sentence, plan.schema, timeoutMs, signal),
        withTurn(clone, sentence, plan.schema, timeoutMs, signal),
      ]);
      const baseParsed = parseIntent(baseAnswer);
      const cloneParsed = parseIntent(cloneAnswer);
      return {
        sentence,
        baseAnswer,
        cloneAnswer,
        sameIntent: JSON.stringify(baseParsed) === JSON.stringify(cloneParsed),
        baseSamplingMode: base.samplingMode,
        cloneSamplingMode: clone.samplingMode,
      };
    } finally {
      clone.destroy();
    }
  } finally {
    base.destroy();
  }
}

/* -------------------------------------------------------------------------- */
/* The report                                                                  */
/* -------------------------------------------------------------------------- */

export interface StrategySummary {
  strategy: Strategy;
  records: number;
  rows: number;
  pass: number;
  fail: number;
  unstable: number;
  unscored: number;
  incomplete: number;
  errors: number;
  meanMs: number;
  medianMs: number;
  p95Ms: number;
  stdMs: number;
  fields: Record<FieldName, FieldTally>;
}

type RowState = 'pass' | 'fail' | 'unstable' | 'unscored' | 'incomplete';

function rowState(verdicts: readonly (ScoredRun['verdict'] | undefined)[]): RowState {
  const known = verdicts.filter((verdict) => verdict !== undefined);
  if (known.length < 3) return 'incomplete';
  if (known.every((verdict) => verdict === 'pass')) return 'pass';
  if (known.every((verdict) => verdict === 'fail')) return 'fail';
  if (known.every((verdict) => verdict === 'unscored')) return 'unscored';
  return 'unstable';
}

function emptyTally(): FieldTally {
  return Object.fromEntries(FIELD_STATES.map((state) => [state, 0])) as FieldTally;
}

function latency(values: readonly number[]): { mean: number; median: number; p95: number; std: number } {
  if (values.length === 0) return { mean: 0, median: 0, p95: 0, std: 0 };
  const sorted = [...values].sort((a, b) => a - b);
  const mean = values.reduce((sum, value) => sum + value, 0) / values.length;
  const median =
    sorted.length % 2 === 1
      ? sorted[(sorted.length - 1) / 2]
      : (sorted[sorted.length / 2 - 1] + sorted[sorted.length / 2]) / 2;
  const p95 = sorted[Math.max(0, Math.ceil(sorted.length * 0.95) - 1)];
  const variance = values.reduce((sum, value) => sum + (value - mean) ** 2, 0) / values.length;
  return { mean, median, p95, std: Math.sqrt(variance) };
}

/** The 3/3 row verdicts, the latency distribution and the per-field states for one strategy. */
export function summarize(records: readonly SessionRecord[], strategy: Strategy): StrategySummary {
  const runs = records.filter((record) => record.strategy === strategy);
  const rows = new Map<string, (ScoredRun['verdict'] | undefined)[]>();
  const byRow = new Map<string, SessionRecord[]>();
  for (const record of runs) {
    if (!byRow.has(record.rowId)) byRow.set(record.rowId, []);
    byRow.get(record.rowId)!.push(record);
  }
  for (const [rowId, rowRuns] of byRow) {
    rows.set(
      rowId,
      rowRuns.map((record) => record.scored?.verdict),
    );
  }

  const states = { pass: 0, fail: 0, unstable: 0, unscored: 0, incomplete: 0 };
  for (const verdicts of rows.values()) states[rowState(verdicts)] += 1;

  const withScored = runs.filter((record) => record.scored !== undefined);
  const ms = latency(withScored.map((record) => record.ms));
  const fields = Object.fromEntries(
    FIELD_NAMES.map((field) => [field, emptyTally()]),
  ) as Record<FieldName, FieldTally>;
  for (const record of withScored) {
    for (const field of record.scored!.fields) fields[field.field][field.state] += 1;
  }

  return {
    strategy,
    records: runs.length,
    rows: rows.size,
    ...states,
    errors: runs.length - withScored.length,
    meanMs: ms.mean,
    medianMs: ms.median,
    p95Ms: ms.p95,
    stdMs: ms.std,
    fields,
  };
}

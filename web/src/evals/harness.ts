/**
 * The dev-only harness's engine: what to run, how one run is recorded, and how a run survives a
 * reload.
 *
 * It imports the shipped path — `requestIntent`, `parseIntent`, `buildSystemPrompt`,
 * `intentSchema`, `intentExamples` — and never reassembles it. The two control arms are
 * deliberately *not* the shipped path: they are the 10/12 frame (system prompt alone) and the
 * same frame with the few-shot turns, so the first run can compare the old input and the new one
 * on one machine on one day.
 *
 * Nothing here is imported by the App. `vite.config.ts` emits only `site` and `app`, and
 * `scripts/check-artifact.mjs` fails if an `evals` entry ever appears in `dist`.
 */

import { assistantState, askModel, type AssistantState } from '../builtin-model';
import type { LibraryProfile } from '../domain/snapshot';
import type { McpSearch, SearchArgs } from '../device/mcp';
import {
  profileFromRows,
  readImageRows,
  searchImage,
  type ImageRow,
} from '../library/imageSource';
import {
  buildSystemPrompt,
  intentExamples,
  intentSchema,
  parseIntent,
  requestIntent,
  type IntentFailure,
  type IntentTrace,
} from '../playlists/ask';
import { listMedia, type CachedMedia } from '../storage/media';
import { newestSnapshot, type StoredSnapshot } from '../storage/snapshots';

import { CORPUS, type CorpusRow } from './corpus';
import { scoreRow, type RepairKind, type ScoredRun } from './score';

/* -------------------------------------------------------------------------- */
/* The matrix                                                                  */
/* -------------------------------------------------------------------------- */

export type Arm = 'systemOnly' | 'systemExamples' | 'requestIntent';
export type Scope = 'static' | 'image' | 'imageSearch';

export interface Combo {
  arm: Arm;
  scope: Scope;
}

/** The frozen matrix: five combinations, not the 3×3 cross product (spec §6). */
export const MATRIX: readonly Combo[] = [
  { arm: 'systemOnly', scope: 'static' },
  { arm: 'systemExamples', scope: 'static' },
  { arm: 'requestIntent', scope: 'static' },
  { arm: 'requestIntent', scope: 'image' },
  { arm: 'requestIntent', scope: 'imageSearch' },
];

export const REPETITIONS = 3;

export function comboKey(combo: Combo): string {
  return `${combo.arm}@${combo.scope}`;
}

export function comboLabel(combo: Combo): string {
  return `${combo.arm} · ${combo.scope}`;
}

/** One run's identity: a combination, a sentence and a repetition. */
export function runKey(combo: Combo, rowId: string, rep: number): string {
  return `${comboKey(combo)}:${rowId}:${rep}`;
}

/**
 * The repetitions of one combination that already have a valid record. A resume skips these and
 * re-runs everything else, including a row the model failed to answer: a second `Run` must not
 * append a second copy of a repetition it already has.
 */
export function finishedReps(records: readonly RunRecord[], combo: Combo): Set<string> {
  return new Set(
    records
      .filter(
        (record) =>
          record.arm === combo.arm &&
          record.scope === combo.scope &&
          !record.invalid &&
          record.scored !== undefined,
      )
      .map((record) => `${record.rowId}:${record.rep}`),
  );
}

/* -------------------------------------------------------------------------- */
/* What a scope reads                                                          */
/* -------------------------------------------------------------------------- */

export interface Target {
  profile: LibraryProfile | null;
  snapshot?: StoredSnapshot;
  rows?: readonly ImageRow[];
  media?: readonly CachedMedia[];
}

/**
 * The library a scope runs against. `static` is nothing; `image` and `imageSearch` read the Last
 * image this browser built or sent, with the stored profile when it has one and the rows' own
 * aggregates when an older record does not. No device and no MCP.
 */
export async function loadTarget(scope: Scope): Promise<Target> {
  if (scope === 'static') return { profile: null };
  const snapshot = await newestSnapshot();
  if (!snapshot) throw new Error('No Last image in this browser: build one on a card first.');
  const rows = readImageRows(snapshot.bytes);
  const profile = snapshot.profile ?? profileFromRows(rows);
  if (scope === 'image') return { profile, snapshot, rows };
  const media = await listMedia(snapshot.scope);
  return { profile, snapshot, rows, media };
}

/* -------------------------------------------------------------------------- */
/* Machine                                                                     */
/* -------------------------------------------------------------------------- */

export interface MachineInfo {
  userAgent: string;
  hardwareConcurrency: number | null;
  deviceMemory: number | null;
  /** The `chrome://on-device-internals` variant the human pastes; JS cannot read it. */
  onDeviceInternals: string;
}

export function machineInfo(): MachineInfo {
  const nav = typeof navigator === 'undefined' ? undefined : (navigator as Navigator & {
    deviceMemory?: number;
  });
  return {
    userAgent: nav?.userAgent ?? 'unknown',
    hardwareConcurrency: nav?.hardwareConcurrency ?? null,
    deviceMemory: nav?.deviceMemory ?? null,
    onDeviceInternals: '',
  };
}

/** A deterministic 32-bit hash: enough to notice a prompt, schema or example set changing. */
export function digest(value: string): string {
  let hash = 0x811c9dc5;
  for (let index = 0; index < value.length; index += 1) {
    hash ^= value.charCodeAt(index);
    hash = Math.imul(hash, 0x01000193);
  }
  return (hash >>> 0).toString(16).padStart(8, '0');
}

/* -------------------------------------------------------------------------- */
/* One run                                                                     */
/* -------------------------------------------------------------------------- */

export type HumanVerdict = 'right' | 'wrong' | 'unsure';

export interface Attempt {
  call: 1 | 2;
  reason: IntentTrace['reason'];
  answer: string;
  ms: number;
}

export interface RunBase {
  id: string;
  rowId: string;
  set: CorpusRow['set'];
  sentence: string;
  arm: Arm;
  scope: Scope;
  rep: 1 | 2 | 3;
  profile: { kind: 'none' | 'image'; generation?: number; tracks?: number };
  examples: { count: number; sha: string };
  systemSha: string;
  schemaSha: string;
  systemChars: number;
  /** What Chrome said before this run, not after it. */
  availability: AssistantState;
  at: string;
}

export interface RunRecord extends RunBase {
  raw: string;
  attempts: Attempt[];
  calls: number;
  repair: RepairKind;
  parsed: SearchArgs | IntentFailure | null;
  final: SearchArgs | IntentFailure | null;
  /** The harness's own check of `raw` against the same `intentSchema` the model was sent. */
  schemaOk: boolean;
  /** The purpose normalizer changed the Intent the model returned. Never model quality. */
  normalized: boolean;
  latencyMs: number;
  search?: { matched: number; tracks: number; truncated: boolean };
  scored?: ScoredRun;
  verdictHuman: HumanVerdict | null;
  note: string;
  error?: string;
  invalid: boolean;
}

function failureRecord(base: RunBase, message: string): RunRecord {
  return {
    ...base,
    raw: '',
    attempts: [],
    calls: 0,
    repair: 'none',
    parsed: null,
    final: null,
    schemaOk: false,
    normalized: false,
    latencyMs: 0,
    verdictHuman: null,
    note: '',
    error: message,
    invalid: true,
  };
}

function described(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

/** Run one sentence, once, and record everything §3.4 asks for. */
export async function runOne(
  combo: Combo,
  target: Target,
  row: CorpusRow,
  rep: 1 | 2 | 3,
): Promise<RunRecord> {
  const availability = await assistantState().catch(() => 'unavailable' as AssistantState);
  const system = buildSystemPrompt(target.profile);
  const schema = intentSchema(target.profile);
  /* The control arm with no examples sends none; the other two send intentExamples, which
   * requestIntent builds for itself. The record has to describe what the arm actually sent. */
  const examples = combo.arm === 'systemOnly' ? [] : intentExamples(target.profile);
  const base: RunBase = {
    id: runKey(combo, row.id, rep),
    rowId: row.id,
    set: row.set,
    sentence: row.sentence,
    arm: combo.arm,
    scope: combo.scope,
    rep,
    profile: {
      kind: target.profile ? 'image' : 'none',
      generation: target.snapshot?.generation,
      tracks: target.profile?.tracks,
    },
    examples: {
      count: examples.length,
      sha: digest(examples.map(([asked, answered]) => `${asked}\n${answered}`).join('\n')),
    },
    systemSha: digest(system),
    schemaSha: digest(JSON.stringify(schema)),
    systemChars: system.length,
    availability,
    at: new Date().toISOString(),
  };

  if (availability !== 'available') {
    return failureRecord(base, `the model is ${availability}; this row was not asked`);
  }

  let attempts: Attempt[] = [];
  let raw = '';
  let parsed: SearchArgs | IntentFailure;
  let final: SearchArgs | IntentFailure;
  let latencyMs = 0;
  let repair: RepairKind = 'none';

  try {
    if (combo.arm === 'requestIntent') {
      final = await requestIntent(row.sentence, target.profile, (attempt) => {
        attempts.push({ ...attempt });
      });
      raw = attempts.at(-1)?.answer ?? '';
      latencyMs = attempts.reduce((sum, attempt) => sum + attempt.ms, 0);
      parsed = parseIntent(raw);
      if (attempts.length === 0) {
        return failureRecord(
          base,
          'error' in final ? final.error : 'the model did not answer this sentence',
        );
      }
      if (attempts.length > 1) {
        repair = attempts[1].reason === 'notJson' ? 'notJson' : 'queryNotName';
      }
    } else {
      const started = performance.now();
      raw = await askModel({ system, schema, sentence: row.sentence, examples });
      latencyMs = performance.now() - started;
      parsed = parseIntent(raw);
      final = parsed;
      attempts = [{ call: 1, reason: 'first', answer: raw, ms: latencyMs }];
    }
  } catch (error) {
    return failureRecord(base, described(error));
  }

  let search: RunRecord['search'];
  if (combo.scope === 'imageSearch' && target.rows && target.media && !('error' in final)) {
    const answered: McpSearch = searchImage(target.rows, target.media, final);
    search = {
      matched: answered.matched,
      tracks: answered.tracks.length,
      truncated: answered.truncated,
    };
  }

  const normalized =
    combo.arm === 'requestIntent' &&
    !('error' in parsed) &&
    !('error' in final) &&
    intentChanged(final, parsed);

  const scored = {
    ...scoreRow(row, raw, parsed, {
      profile: target.profile,
      matched: search?.matched,
    }),
    normalized,
  };

  return {
    ...base,
    raw,
    attempts,
    calls: attempts.length,
    repair,
    parsed,
    final,
    schemaOk: scored.axes.shape,
    normalized,
    latencyMs,
    search,
    scored,
    verdictHuman: null,
    note: '',
    invalid: false,
  };
}

/**
 * Whether the Intent that ran differs from the one `parseIntent` produced. This is how the
 * harness reports the purpose normalizer separately from model quality: the model's answer is
 * `parsed`, the search ran `final`, and a difference here is app-side.
 */
export function intentChanged(a: SearchArgs, b: SearchArgs): boolean {
  const keys = new Set([...Object.keys(a), ...Object.keys(b)]);
  for (const key of keys) {
    const left = (a as Record<string, unknown>)[key];
    const right = (b as Record<string, unknown>)[key];
    if (JSON.stringify(left) !== JSON.stringify(right)) return true;
  }
  return false;
}

/* -------------------------------------------------------------------------- */
/* Storage                                                                     */
/* -------------------------------------------------------------------------- */

const KEY = 'k7-ask-evals-v1';

/**
 * One record per run identity. A resume replaces a run instead of appending a second copy of the
 * same repetition, which is what a naive append does; the last answer wins.
 */
export function upsertRecord(records: readonly RunRecord[], record: RunRecord): RunRecord[] {
  const index = records.findIndex((item) => item.id === record.id);
  if (index < 0) return [...records, record];
  const next = [...records];
  next[index] = record;
  return next;
}

/** A saved run from before the upsert could hold duplicates; keep the last copy of each run. */
export function dedupeRecords(records: readonly RunRecord[]): RunRecord[] {
  const byId = new Map<string, RunRecord>();
  for (const record of records) byId.set(record.id, record);
  return [...byId.values()];
}

export interface StoredRun {
  startedAt: string;
  machine: MachineInfo;
  records: RunRecord[];
}

export function savedRun(): StoredRun | null {
  if (typeof localStorage === 'undefined') return null;
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as StoredRun;
    if (!Array.isArray(parsed.records) || typeof parsed.startedAt !== 'string') return null;
    return { ...parsed, records: dedupeRecords(parsed.records) };
  } catch {
    return null;
  }
}

/** Save after every finished run; a long sitting must survive a reload. */
export function saveRun(run: StoredRun): boolean {
  if (typeof localStorage === 'undefined') return false;
  try {
    localStorage.setItem(KEY, JSON.stringify(run));
    return true;
  } catch {
    return false;
  }
}

export function clearRun(): void {
  if (typeof localStorage === 'undefined') return;
  try {
    localStorage.removeItem(KEY);
  } catch {
    /* nothing to clear */
  }
}

/* -------------------------------------------------------------------------- */
/* Export                                                                      */
/* -------------------------------------------------------------------------- */

export function scoredRunsFor(records: readonly RunRecord[], combo: Combo): ScoredRun[] {
  return records
    .filter(
      (record) =>
        record.arm === combo.arm &&
        record.scope === combo.scope &&
        record.scored !== undefined,
    )
    .map((record) => ({
      ...record.scored!,
      repair: record.repair,
      normalized: record.normalized,
    }));
}

export function exportPayload(run: StoredRun): string {
  return `${JSON.stringify(
    {
      kind: 'k7-ask-evals',
      version: 1,
      at: new Date().toISOString(),
      startedAt: run.startedAt,
      machine: run.machine,
      matrix: MATRIX,
      repetitions: REPETITIONS,
      rows: CORPUS.length,
      records: run.records,
    },
    null,
    2,
  )}\n`;
}

export interface RecordedFixture {
  rowId: string;
  raw: string;
  origin: 'recorded';
  verdict: 'pass' | 'fail';
  note: string;
}

/**
 * The shipped arm's answers, as a fixture file for `src/evals/fixtures/answers.json`. Every run
 * is exported, duplicates included: three runs that disagree are three facts about the model.
 */
export function fixturesFrom(records: readonly RunRecord[]): RecordedFixture[] {
  return records
    .filter(
      (record) =>
        record.arm === 'requestIntent' &&
        record.scope === 'static' &&
        !record.invalid &&
        record.scored !== undefined &&
        record.scored.verdict !== 'unscored',
    )
    .map((record) => ({
      rowId: record.rowId,
      raw: record.raw,
      origin: 'recorded' as const,
      verdict: record.scored!.verdict === 'pass' ? ('pass' as const) : ('fail' as const),
      note: `recorded ${record.at}, run ${record.rep} of ${REPETITIONS}`,
    }));
}

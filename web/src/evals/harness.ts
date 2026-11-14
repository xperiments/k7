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

import { CORPUS, rowById, type CorpusRow } from './corpus';
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

/** Where a human column came from: a person, or a stable automatic verdict they accepted. */
export type VerdictSource = 'human' | 'accepted-auto';

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
  profile: {
    kind: 'none' | 'image';
    generation?: number;
    tracks?: number;
    /** The aggregates the prompt and the scorer read, so a saved run can be re-scored. */
    library?: LibraryProfile;
  };
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
  /** The reference search refused the Intent; it is scored as a failure, not a stopped run. */
  searchError?: string;
  scored?: ScoredRun;
  /** The same rubric on the Intent that actually ran (after a repair or the normalizer). */
  effective?: ScoredRun;
  verdictHuman: HumanVerdict | null;
  /** `human` is an independent judgement; `accepted-auto` is bookkeeping, not an audit. */
  verdictSource?: VerdictSource;
  /** This row is in the deterministic audit sample. */
  sample?: true;
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

/** The page's default ceiling; the ADR's worst warm case is a few seconds per answer. */
export const DEFAULT_TIMEOUT_MS = 90_000;

/**
 * A model call that never settles must not hold the run: the prompt API has no timeout of its
 * own, so the harness gives up, records the row, and lets a resume ask again. The abandoned
 * promise is ignored; the session it holds is destroyed when Chrome finally settles it.
 */
export function withTimeout<T>(work: Promise<T>, ms: number, onTimeout?: () => void): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => {
      onTimeout?.();
      reject(new Error(`no answer after ${Math.round(ms / 1000)} s`));
    }, ms);
    work.then(
      (value) => {
        clearTimeout(timer);
        resolve(value);
      },
      (error) => {
        clearTimeout(timer);
        reject(error);
      },
    );
  });
}

/**
 * Chrome can answer `unavailable` for a moment while it swaps a model component. Asking a
 * sentence once and then declaring the model gone wastes a sitting, so the state is read up to
 * three times before the run stops.
 */
async function availableNow(): Promise<AssistantState> {
  let state: AssistantState = 'unavailable';
  for (let attempt = 0; attempt < 3; attempt += 1) {
    state = await withTimeout(assistantState(), 5_000).catch(
      () => 'unavailable' as AssistantState,
    );
    if (state === 'available') return state;
    if (attempt < 2) await new Promise((resolve) => setTimeout(resolve, 700));
  }
  return state;
}

/** Run one sentence, once, and record everything §3.4 asks for. */
export async function runOne(
  combo: Combo,
  target: Target,
  row: CorpusRow,
  rep: 1 | 2 | 3,
  timeoutMs: number = DEFAULT_TIMEOUT_MS,
): Promise<RunRecord> {
  const availability = await availableNow();
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
      ...(target.profile ? { library: target.profile } : {}),
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

  /* A hung prompt must be cancelled, not merely ignored: the session is destroyed in the
   * askModel finally only when its own promise settles, and an abandoned one can wedge the next
   * call. The timeout aborts it. */
  const controller = new AbortController();
  let attempts: Attempt[] = [];
  let raw = '';
  let parsed: SearchArgs | IntentFailure;
  let final: SearchArgs | IntentFailure;
  let latencyMs = 0;
  let repair: RepairKind = 'none';

  try {
    if (combo.arm === 'requestIntent') {
      final = await withTimeout(
        requestIntent(
          row.sentence,
          target.profile,
          (attempt) => {
            attempts.push({ ...attempt });
          },
          controller.signal,
        ),
        timeoutMs,
        () => controller.abort(),
      );
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
      raw = await withTimeout(
        askModel({ system, schema, sentence: row.sentence, examples, signal: controller.signal }),
        timeoutMs,
        () => controller.abort(),
      );
      latencyMs = performance.now() - started;
      parsed = parseIntent(raw);
      final = parsed;
      attempts = [{ call: 1, reason: 'first', answer: raw, ms: latencyMs }];
    }
  } catch (error) {
    return failureRecord(base, described(error));
  }

  let search: RunRecord['search'];
  let searchError: string | undefined;
  if (combo.scope === 'imageSearch' && target.rows && target.media && !('error' in final)) {
    try {
      const answered: McpSearch = searchImage(target.rows, target.media, final);
      search = {
        matched: answered.matched,
        tracks: answered.tracks.length,
        truncated: answered.truncated,
      };
    } catch (error) {
      /* The image source validates a key the way the device does. An Intent it refuses is a
       * failed row, not a lost model: record the reason and keep going. */
      searchError = described(error);
    }
  }

  const normalized =
    combo.arm === 'requestIntent' &&
    !('error' in parsed) &&
    !('error' in final) &&
    intentChanged(final, parsed);

  const context = {
    profile: target.profile,
    matched: search?.matched,
    searchError,
  };
  const scored = {
    ...scoreRow(row, raw, parsed, context),
    normalized,
  };
  /* The Intent that ran is not always the one the model parsed: a repair drops a bad query and
   * the normalizer drops invented filters. Score that one too, but only as a report — the
   * frozen verdict above stays the model's Intent. */
  const effective =
    !('error' in parsed) && !('error' in final) && intentChanged(final, parsed)
      ? scoreRow(row, raw, final, context)
      : undefined;

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
    searchError,
    scored,
    effective,
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

/**
 * The 10% audit sample: a deterministic hash of the combination and the row, so the same rows
 * are sampled every time and a re-export can be compared with the last one.
 */
export function isSampled(rowId: string, combo: Combo): boolean {
  return parseInt(digest(`${comboKey(combo)}:${rowId}`), 16) % 10 === 0;
}

export type ReviewKind = 'stable' | 'sample' | 'unstable' | 'unscored' | 'gap';

/**
 * What the human column should do with one row.
 *
 * `stable` — all three verdicts agree; the automatic verdict is the answer, and it may be
 * accepted in bulk (recorded as `accepted-auto`, never as a human judgement).
 * `sample` — stable, and this row is in the audit sample: a person must mark it.
 * `unstable` — the three runs disagree, or the row has not run three times.
 * `unscored` — the reference search could not decide it.
 * `gap` — a contract or profile gap; no answer could satisfy it, so no verdict is expected.
 */
export function reviewKind(
  rowId: string,
  group: readonly RunRecord[],
  combo: Combo,
): ReviewKind {
  if (
    group.some(
      (record) => record.scored?.contractGap === true || record.scored?.profileGap !== undefined,
    )
  ) {
    return 'gap';
  }
  const verdicts = group.map((record) => record.scored?.verdict);
  if (verdicts.length < REPETITIONS || new Set(verdicts).size !== 1) return 'unstable';
  if (verdicts[0] === 'unscored' || verdicts[0] === undefined) return 'unscored';
  if (isSampled(rowId, combo)) return 'sample';
  return 'stable';
}

/**
 * Fill the human column for every stable row from its automatic verdict. Sampled rows,
 * unstable/unscored rows, gaps and anything a person already marked are left alone.
 */
export function acceptStableVerdicts(
  records: readonly RunRecord[],
  combo: Combo,
): { records: RunRecord[]; accepted: number } {
  const byRow = new Map<string, RunRecord[]>();
  for (const record of records) {
    if (record.arm !== combo.arm || record.scope !== combo.scope) continue;
    const group = byRow.get(record.rowId) ?? [];
    group.push(record);
    byRow.set(record.rowId, group);
  }

  const accepted = new Map<string, HumanVerdict>();
  for (const [rowId, group] of byRow) {
    const marked = group.some(
      (record) =>
        record.verdictSource === 'human' ||
        (record.verdictSource === undefined && record.verdictHuman),
    );
    if (marked) continue;
    if (reviewKind(rowId, group, combo) !== 'stable') continue;
    const verdict = group[0].scored?.verdict;
    if (verdict !== 'pass' && verdict !== 'fail') continue;
    accepted.set(rowId, verdict === 'pass' ? 'right' : 'wrong');
  }

  const next = records.map((record) => {
    if (record.arm !== combo.arm || record.scope !== combo.scope) return record;
    const verdict = accepted.get(record.rowId);
    if (!verdict) return record;
    return {
      ...record,
      verdictHuman: verdict,
      verdictSource: 'accepted-auto' as const,
      sample: undefined,
    };
  });
  return { records: next, accepted: accepted.size };
}

/**
 * Re-score every stored answer under the current rubric, without a model. A run saved before a
 * rubric change can be brought forward: the raw answers and Intents are the evidence, and the
 * profile is either on the record or the Last image's aggregates.
 */
export interface RescoreResult {
  records: RunRecord[];
  rescored: number;
  /** Image runs that cannot be re-scored because their profile is gone or a different one. */
  skipped: number;
}

export function rescoreRun(
  records: readonly RunRecord[],
  profile: LibraryProfile | null,
  generation?: number,
): RescoreResult {
  let rescored = 0;
  let skipped = 0;
  const next = records.map((record) => {
    if (record.invalid || !record.parsed) return record;
    const row = rowById(record.rowId);
    if (!row) return record;

    const image = record.scope !== 'static';
    const library = image ? (profile ?? record.profile.library ?? null) : null;
    if (image && !library) {
      skipped += 1;
      return record;
    }
    if (
      image &&
      generation !== undefined &&
      record.profile.generation !== undefined &&
      generation !== record.profile.generation
    ) {
      skipped += 1;
      return record;
    }

    const context = {
      profile: library,
      matched: record.search?.matched,
      searchError: record.searchError,
    };
    const scored = scoreRow(row, record.raw, record.parsed, context);
    const final = record.final;
    const effective =
      final &&
      !('error' in final) &&
      !('error' in record.parsed) &&
      intentChanged(final, record.parsed)
        ? scoreRow(row, record.raw, final, context)
        : record.effective;
    rescored += 1;
    return { ...record, scored, effective };
  });
  return { records: next, rescored, skipped };
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

export interface RunSettings {
  /** The page's ceiling for one model answer, in seconds. */
  timeoutSeconds: number;
}

export interface StoredRun {
  startedAt: string;
  machine: MachineInfo;
  settings: RunSettings;
  /** The profile the Last image's scopes ran against, once a re-score has attached it. */
  library?: LibraryProfile;
  records: RunRecord[];
}

export const DEFAULT_SETTINGS: RunSettings = { timeoutSeconds: 90 };

export function savedRun(): StoredRun | null {
  if (typeof localStorage === 'undefined') return null;
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as StoredRun;
    if (!Array.isArray(parsed.records) || typeof parsed.startedAt !== 'string') return null;
    return {
      ...parsed,
      settings: { ...DEFAULT_SETTINGS, ...parsed.settings },
      records: dedupeRecords(parsed.records),
    };
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
      rubric: 2,
      at: new Date().toISOString(),
      startedAt: run.startedAt,
      machine: run.machine,
      settings: run.settings,
      library: run.library ?? null,
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

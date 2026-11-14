/**
 * The dev-only harness page.
 *
 * It is not the App and cannot be shipped: `vite.config.ts` emits only `site` and `app`, nothing
 * under `src/ui` or `src/main.tsx` imports it, and `scripts/check-artifact.mjs` fails on an
 * `evals` entry in `dist`. It imports the real `requestIntent` and the real parser, and it never
 * reassembles the shipped path.
 *
 * The table is a form, not a product data grid: each row needs three automatic verdicts, a human
 * verdict and a note, which is why it is a semantic table with the App's installed controls rather
 * than a grid component.
 */

import { Fragment, useEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';

import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { Progress } from '@/components/ui/progress';
import { Separator } from '@/components/ui/separator';

import {
  assistantSentence,
  assistantState,
  downloadModel,
  type AssistantState,
} from '../builtin-model';
import { CORPUS } from './corpus';
import {
  comboKey,
  comboLabel,
  clearRun,
  exportPayload,
  fixturesFrom,
  loadTarget,
  machineInfo,
  MATRIX,
  REPETITIONS,
  runOne,
  savedRun,
  saveRun,
  scoredRunsFor,
  type Combo,
  type HumanVerdict,
  type RunRecord,
  type StoredRun,
} from './harness';
import { aggregate, type Verdict } from './score';

import '../index.css';

const TOTAL = CORPUS.length * REPETITIONS;

function freshRun(): StoredRun {
  return { startedAt: new Date().toISOString(), machine: machineInfo(), records: [] };
}

function stamp(): string {
  return new Date().toISOString().replace(/[:.]/g, '-');
}

function download(name: string, text: string): void {
  const blob = new Blob([text], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = name;
  anchor.click();
  URL.revokeObjectURL(url);
}

function percent(part: number, whole: number): number {
  return whole === 0 ? 0 : Math.round((part / whole) * 100);
}

function fraction(value: number | null): string {
  return value === null ? '—' : value.toFixed(2);
}

function verdictVariant(verdict: Verdict): 'default' | 'alert' | 'outline' {
  if (verdict === 'pass') return 'default';
  if (verdict === 'fail') return 'alert';
  return 'outline';
}

export function Harness() {
  const [run, setRun] = useState<StoredRun>(() => savedRun() ?? freshRun());
  const runRef = useRef<StoredRun>(run);
  const busy = useRef(false);
  const stop = useRef(false);
  const saveFailed = useRef(false);

  const [availability, setAvailability] = useState<AssistantState | null>(null);
  const [downloadPercent, setDownloadPercent] = useState<number | null>(null);
  const [running, setRunning] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [tab, setTab] = useState(0);
  const [open, setOpen] = useState<string | null>(null);

  function commit(next: StoredRun): void {
    runRef.current = next;
    setRun(next);
    if (!saveRun(next) && !saveFailed.current) {
      saveFailed.current = true;
      setError('This browser would not save the run; keep the tab open and export before reloading.');
    }
  }

  useEffect(() => {
    let alive = true;
    const check = async () => {
      try {
        const state = await assistantState();
        if (alive) setAvailability(state);
      } catch {
        if (alive) setAvailability('unavailable');
      }
    };
    void check();
    const id = window.setInterval(check, 5000);
    return () => {
      alive = false;
      window.clearInterval(id);
    };
  }, []);

  async function setup(): Promise<void> {
    setDownloadPercent(0);
    try {
      await downloadModel((loaded) => setDownloadPercent(Math.round(loaded * 100)));
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : String(caught));
    } finally {
      setDownloadPercent(null);
      setAvailability(await assistantState().catch(() => 'unavailable' as AssistantState));
    }
  }

  async function runMany(combos: readonly Combo[]): Promise<void> {
    if (busy.current) return;
    busy.current = true;
    stop.current = false;
    setError(null);

    for (const combo of combos) {
      if (stop.current) break;
      let target;
      try {
        target = await loadTarget(combo.scope);
      } catch (caught) {
        setError(caught instanceof Error ? caught.message : String(caught));
        break;
      }
      setRunning(comboKey(combo));
      for (const row of CORPUS) {
        if (stop.current) break;
        for (let rep = 1; rep <= REPETITIONS; rep += 1) {
          if (stop.current) break;
          const record = await runOne(combo, target, row, rep as 1 | 2 | 3);
          commit({ ...runRef.current, records: [...runRef.current.records, record] });
          setAvailability(record.availability);
          if (record.invalid && record.availability !== 'available') {
            setError(record.error ?? 'the model stopped answering');
            stop.current = true;
          }
        }
      }
      if (stop.current) break;
    }

    setRunning(null);
    busy.current = false;
  }

  const combo = MATRIX[tab];
  const records = run.records.filter(
    (record) => record.arm === combo.arm && record.scope === combo.scope,
  );
  const byRow = new Map<string, RunRecord[]>();
  for (const record of records) {
    const group = byRow.get(record.rowId) ?? [];
    group.push(record);
    byRow.set(record.rowId, group);
  }
  for (const group of byRow.values()) group.sort((a, b) => a.rep - b.rep);

  const summary = aggregate(scoredRunsFor(run.records, combo));
  const first = records[0];

  function mark(rowId: string, verdict: HumanVerdict | null): void {
    const updated = runRef.current.records.map((record) =>
      record.arm === combo.arm && record.scope === combo.scope && record.rowId === rowId
        ? { ...record, verdictHuman: verdict }
        : record,
    );
    commit({ ...runRef.current, records: updated });
  }

  function note(rowId: string, value: string): void {
    const updated = runRef.current.records.map((record) =>
      record.arm === combo.arm && record.scope === combo.scope && record.rowId === rowId
        ? { ...record, note: value }
        : record,
    );
    commit({ ...runRef.current, records: updated });
  }

  function machineNote(value: string): void {
    commit({ ...runRef.current, machine: { ...runRef.current.machine, onDeviceInternals: value } });
  }

  function reset(): void {
    if (!window.confirm('Clear every recorded run in this browser?')) return;
    clearRun();
    commit(freshRun());
    setTab(0);
    setOpen(null);
    setError(null);
  }

  return (
    <main className="mx-auto flex min-h-screen w-full max-w-[1500px] flex-col gap-6 p-6 text-sm">
      <header className="flex flex-wrap items-end justify-between gap-3">
        <div className="flex flex-col gap-1">
          <h1 className="font-heading text-base leading-snug font-light tracking-tight">
            Ask evaluation
          </h1>
          <p className="max-w-3xl text-sm text-muted-foreground">
            The frozen corpus from <code>src/evals/corpus.ts</code>, scored by{' '}
            <code>src/evals/score.ts</code>. This page is dev-only; it cannot ship. No model number
            lives in the repository until you run it.
          </p>
        </div>
        <div className="flex flex-wrap items-center gap-2">
          <Badge variant={availability === 'available' ? 'default' : 'outline'} size="sm">
            {availability ?? 'checking'}
          </Badge>
          <Badge variant="outline" size="sm">
            {run.records.length} records
          </Badge>
        </div>
      </header>

      {availability !== null && availability !== 'available' ? (
        <Card size="sm">
          <CardHeader>
            <CardTitle>The model is not ready</CardTitle>
            <CardDescription>{assistantSentence(availability)}</CardDescription>
          </CardHeader>
          <CardContent className="flex flex-wrap items-center gap-2">
            {availability === 'downloadable' ? (
              <Button size="sm" variant="solid" onClick={() => void setup()} disabled={downloadPercent !== null}>
                Set up the model
              </Button>
            ) : null}
            {availability === 'downloading' || downloadPercent !== null ? (
              <Progress value={downloadPercent ?? 0} className="max-w-64" />
            ) : null}
            <Button size="sm" variant="ghost" onClick={() => void assistantState().then(setAvailability)}>
              Check again
            </Button>
          </CardContent>
        </Card>
      ) : null}

      <Card size="sm">
        <CardHeader>
          <CardTitle>The matrix</CardTitle>
          <CardDescription>
            {CORPUS.length} rows × {REPETITIONS} runs × {MATRIX.length} combinations = {TOTAL * MATRIX.length}{' '}
            model calls. Each sentence is scored only when all three verdicts agree.
          </CardDescription>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          <div className="flex flex-wrap gap-2">
            <Button
              size="sm"
              variant="solid"
              onClick={() => void runMany(MATRIX)}
              disabled={running !== null || availability !== 'available'}
            >
              Run all five
            </Button>
            <Button size="sm" variant="ghost" onClick={() => (stop.current = true)} disabled={running === null}>
              Stop
            </Button>
            <Button size="sm" variant="ghost" onClick={reset} disabled={running !== null}>
              Clear
            </Button>
          </div>
          <Separator />
          {MATRIX.map((item, index) => {
            const count = run.records.filter(
              (record) => record.arm === item.arm && record.scope === item.scope,
            ).length;
            const active = running === comboKey(item);
            return (
              <div key={comboKey(item)} className="flex flex-wrap items-center gap-3">
                <span className="w-72 text-sm">{comboLabel(item)}</span>
                <Progress value={percent(count, TOTAL)} className="max-w-48" />
                <span className="w-20 text-xs text-muted-foreground tabular-nums">
                  {count}/{TOTAL}
                </span>
                <Button
                  size="xs"
                  variant="outline"
                  onClick={() => void runMany([item])}
                  disabled={running !== null || availability !== 'available'}
                >
                  {active ? 'Running…' : count > 0 ? 'Resume' : 'Run'}
                </Button>
                <Button
                  size="xs"
                  variant="ghost"
                  aria-pressed={tab === index}
                  onClick={() => setTab(index)}
                >
                  View
                </Button>
              </div>
            );
          })}
        </CardContent>
      </Card>

      <Card size="sm">
        <CardHeader>
          <CardTitle>Machine</CardTitle>
          <CardDescription>
            Paste the model variant and version from <code>chrome://on-device-internals</code>. JS
            cannot read that page; a result without it is a result about a machine, not about Ask.
          </CardDescription>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          <textarea
            aria-label="chrome://on-device-internals"
            className="min-h-20 w-full border border-border bg-transparent p-2 text-xs break-all"
            placeholder="e.g. Gemini Nano · 4B · version …"
            value={run.machine.onDeviceInternals}
            onChange={(event) => machineNote(event.target.value)}
          />
          <p className="text-xs break-all text-muted-foreground">
            {run.machine.userAgent} · {run.machine.hardwareConcurrency ?? '?'} cores ·{' '}
            {run.machine.deviceMemory ?? '?'} GB
          </p>
        </CardContent>
      </Card>

      {error ? (
        <p className="border border-alert p-2 text-sm text-alert" role="alert">
          {error}
        </p>
      ) : null}

      <Card size="sm">
        <CardHeader>
          <CardTitle>{comboLabel(combo)}</CardTitle>
          <CardDescription className="flex flex-wrap gap-3">
            <span>
              {records.length}/{TOTAL} runs
            </span>
            <span>
              {summary.passed}/{summary.modelRows} rows pass 3/3
            </span>
            <span>{summary.unstable} unstable</span>
            <span>
              {summary.contractGaps.rows} contract gap{summary.contractGaps.rows === 1 ? '' : 's'}
            </span>
            <span>recall {fraction(summary.meanRecall)}</span>
            <span>precision {fraction(summary.meanPrecision)}</span>
            <span>
              repairs {summary.repairs.notJson + summary.repairs.queryNotName} · normalized{' '}
              {summary.normalizedRuns}
            </span>
            {first ? (
              <span>
                system {first.systemSha} · schema {first.schemaSha} · examples {first.examples.count}{' '}
                ({first.examples.sha})
              </span>
            ) : null}
          </CardDescription>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          <div className="flex flex-wrap gap-2">
            <Button size="sm" variant="outline" onClick={() => download(`k7-ask-evals-${stamp()}.json`, exportPayload(runRef.current))} disabled={run.records.length === 0}>
              Export run
            </Button>
            <Button
              size="sm"
              variant="ghost"
              onClick={() =>
                download('answers.json', `${JSON.stringify(fixturesFrom(runRef.current.records), null, 2)}\n`)
              }
              disabled={run.records.length === 0}
            >
              Export fixtures
            </Button>
          </div>
          <Separator />
          <div className="overflow-x-auto">
            <table className="w-full border-collapse text-sm">
              <caption className="sr-only">
                Corpus rows with their automatic verdicts, human verdict and note
              </caption>
              <thead>
                <tr className="border-b border-border text-xs text-muted-foreground">
                  <th className="py-2 pr-2 text-left font-medium">#</th>
                  <th className="py-2 pr-2 text-left font-medium">Sentence</th>
                  <th className="py-2 pr-2 text-left font-medium">Auto (1 · 2 · 3)</th>
                  <th className="py-2 pr-2 text-left font-medium">Human</th>
                  <th className="py-2 pr-2 text-left font-medium">Note</th>
                  <th className="py-2 text-left font-medium">·</th>
                </tr>
              </thead>
              <tbody>
                {CORPUS.map((row) => {
                  const answers = byRow.get(row.id) ?? [];
                  const human = answers[0]?.verdictHuman ?? null;
                  const verdicts = answers.map((record) => record.scored?.verdict);
                  const disagrees =
                    (human === 'right' && verdicts.includes('fail')) ||
                    (human === 'wrong' && verdicts.includes('pass'));
                  return (
                    <Fragment key={row.id}>
                      <tr className="border-b border-border/60 align-top">
                        <td className="py-2 pr-2 text-xs text-muted-foreground tabular-nums">{row.id}</td>
                        <td className="py-2 pr-2">{row.sentence}</td>
                        <td className="py-2 pr-2">
                          <div className="flex flex-wrap items-center gap-1">
                            {answers.length === 0 ? (
                              <span className="text-xs text-muted-foreground">—</span>
                            ) : (
                              answers.map((record) => (
                                <span key={record.rep} title={record.scored?.reasons.join('; ')}>
                                  <Badge
                                    variant={
                                      record.invalid
                                        ? 'alert'
                                        : verdictVariant(record.scored?.verdict ?? 'unscored')
                                    }
                                    size="sm"
                                  >
                                    {record.invalid ? 'invalid' : (record.scored?.verdict ?? '—')}
                                  </Badge>
                                </span>
                              ))
                            )}
                            {disagrees ? (
                              <Badge variant="alert" size="sm">
                                rubric bug
                              </Badge>
                            ) : null}
                          </div>
                        </td>
                        <td className="py-2 pr-2">
                          <div className="flex flex-wrap gap-1">
                            {(['right', 'wrong', 'unsure'] as const).map((value) => (
                              <Button
                                key={value}
                                size="xs"
                                variant="outline"
                                aria-pressed={human === value}
                                disabled={answers.length === 0}
                                onClick={() => mark(row.id, human === value ? null : value)}
                              >
                                {value === 'right' ? 'Right' : value === 'wrong' ? 'Wrong' : 'Unsure'}
                              </Button>
                            ))}
                          </div>
                        </td>
                        <td className="py-2 pr-2">
                          <Input
                            aria-label={`Note for row ${row.id}`}
                            placeholder="what you saw"
                            value={answers[0]?.note ?? ''}
                            disabled={answers.length === 0}
                            onChange={(event) => note(row.id, event.target.value)}
                          />
                        </td>
                        <td className="py-2">
                          <Button
                            size="xs"
                            variant="ghost"
                            aria-pressed={open === row.id}
                            onClick={() => setOpen(open === row.id ? null : row.id)}
                            disabled={answers.length === 0}
                          >
                            Detail
                          </Button>
                        </td>
                      </tr>
                      {open === row.id ? (
                        <tr className="border-b border-border/60">
                          <td colSpan={6} className="py-2">
                            <pre className="max-h-72 overflow-auto whitespace-pre-wrap break-all text-xs">
                              {JSON.stringify(
                                answers.map((record) => ({
                                  rep: record.rep,
                                  calls: record.calls,
                                  repair: record.repair,
                                  latencyMs: Math.round(record.latencyMs),
                                  availability: record.availability,
                                  search: record.search,
                                  parsed: record.parsed,
                                  final: record.final,
                                  normalized: record.normalized,
                                  reasons: record.scored?.reasons,
                                  axes: record.scored?.axes,
                                  attempts: record.attempts,
                                  error: record.error,
                                })),
                                null,
                                1,
                              )}
                            </pre>
                          </td>
                        </tr>
                      ) : null}
                    </Fragment>
                  );
                })}
              </tbody>
            </table>
          </div>
        </CardContent>
      </Card>

      <footer className="text-xs text-muted-foreground">
        Started {run.startedAt}. Saved after every run in this browser; exported blobs are never
        committed. The rubric is <code>docs/design/assistant-evaluation/spec.md</code>.
      </footer>
    </main>
  );
}

const root = document.getElementById('root');
if (!root) throw new Error('Missing #root');
createRoot(root).render(<Harness />);

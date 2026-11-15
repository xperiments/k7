/**
 * The dev-only session measurement page: create vs clone, omitResponseConstraintInput, and the
 * Sampling Parameters trial, on the frozen corpus. It is not the App and is not in the build:
 * `vite.config.ts` emits only `site` and `app`.
 *
 * Surface: operational · Archetype: stacked sections (the eval page's Card-led family) · Density:
 * sm · Regions: header, model, machine, strategies, report, footer · States: model-not-ready,
 * empty, running, error · Motion: none.
 */

import { useEffect, useMemo, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';

import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import {
  Empty,
  EmptyContent,
  EmptyDescription,
  EmptyHeader,
  EmptyTitle,
} from '@/components/ui/empty';
import { Input } from '@/components/ui/input';
import { Progress } from '@/components/ui/progress';
import { Separator } from '@/components/ui/separator';
import { Switch } from '@/components/ui/switch';

import {
  assistantSentence,
  assistantState,
  downloadModel,
  type AssistantState,
} from '../builtin-model';
import { CORPUS } from './corpus';
import { machineInfo, type MachineInfo } from './harness';
import {
  probeClone,
  runStrategy,
  sessionPlan,
  strategiesFor,
  strategyDescription,
  strategyLabel,
  summarize,
  type CloneProbe,
  type SessionRecord,
  type SessionSetup,
  type Strategy,
} from './sessions';

import '../index.css';

function download(name: string, payload: unknown): void {
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = name;
  link.click();
  URL.revokeObjectURL(url);
}

function Sessions() {
  const [availability, setAvailability] = useState<AssistantState | null>(null);
  const [downloadPercent, setDownloadPercent] = useState<number | null>(null);
  const [samplingAvailable, setSamplingAvailable] = useState<boolean | null>(null);
  const [samplingValue, setSamplingValue] = useState<string | undefined>();
  const [machine, setMachine] = useState<MachineInfo>(() => machineInfo());
  const [plan] = useState(() => sessionPlan(null));
  const [selected, setSelected] = useState<Strategy[]>([]);
  const [records, setRecords] = useState<SessionRecord[]>([]);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [status, setStatus] = useState<string | null>(null);
  const [probe, setProbe] = useState<CloneProbe | null>(null);
  const [sessionSetup, setSessionSetup] = useState<SessionSetup | null>(null);
  const [runStart, setRunStart] = useState(0);
  const stop = useRef<AbortController | null>(null);

  const available = useMemo(() => strategiesFor(samplingAvailable === true), [samplingAvailable]);
  const summary = useMemo(
    () => available.map((strategy) => summarize(records, strategy)),
    [available, records],
  );
  const total = selected.length * CORPUS.length * 3;
  const done = Math.max(0, records.length - runStart);
  const progress = total === 0 ? 0 : Math.min(100, (done / total) * 100);

  useEffect(() => {
    let alive = true;
    void (async () => {
      const state = await assistantState().catch(() => 'unavailable' as AssistantState);
      if (!alive) return;
      setAvailability(state);
      if (state !== 'available' || typeof LanguageModel === 'undefined') {
        setSamplingAvailable(false);
        return;
      }
      try {
        const session = await LanguageModel.create({ samplingMode: 'balanced' });
        const value = session.samplingMode;
        session.destroy();
        if (alive) {
          setSamplingAvailable(value !== undefined);
          setSamplingValue(value);
        }
      } catch {
        if (alive) setSamplingAvailable(false);
      }
    })();
    return () => {
      alive = false;
      stop.current?.abort();
    };
  }, []);

  useEffect(() => {
    setSelected((current) => {
      const next = current.filter((strategy) => available.includes(strategy));
      return next.length === 0 ? available : next;
    });
  }, [available]);

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

  function toggle(strategy: Strategy, checked: boolean): void {
    setSelected((current) =>
      checked ? [...current, strategy] : current.filter((item) => item !== strategy),
    );
  }

  async function start(): Promise<void> {
    if (running || selected.length === 0) return;
    setRunning(true);
    setError(null);
    setProbe(null);
    setRunStart(records.length);
    const controller = new AbortController();
    stop.current = controller;
    try {
      if (selected.some((strategy) => strategy === 'clone' || strategy === 'clone-omit')) {
        setStatus('Checking the clone…');
        setProbe(await probeClone(plan, 90_000, controller.signal));
      }
      for (const strategy of selected) {
        if (controller.signal.aborted) break;
        setStatus(`Running ${strategyLabel(strategy)}…`);
        await runStrategy(strategy, plan, {
          timeoutMs: 90_000,
          signal: controller.signal,
          onRecord: (record) => setRecords((previous) => [...previous, record]),
          onSetup: (value) => setSessionSetup(value),
        });
      }
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : String(caught));
    } finally {
      setRunning(false);
      setStatus(null);
      stop.current = null;
    }
  }

  function reset(): void {
    setRecords([]);
    setProbe(null);
    setSessionSetup(null);
    setError(null);
    setStatus(null);
    setRunStart(0);
  }

  function exportRun(): void {
    download(`k7-sessions-${new Date().toISOString().replace(/[:.]/g, '-')}.json`, {
      kind: 'k7-session-eval',
      at: new Date().toISOString(),
      machine,
      sampling: { available: samplingAvailable, value: samplingValue },
      plan: {
        profile: null,
        systemSha: plan.systemSha,
        schemaSha: plan.schemaSha,
        examplesSha: plan.examplesSha,
        examples: plan.examples.length,
      },
      strategies: selected,
      probe,
      setup: sessionSetup,
      summary,
      records,
    });
  }

  return (
    <main className="mx-auto flex min-h-svh w-full max-w-5xl flex-col gap-6 p-6">
      <header className="flex flex-wrap items-start justify-between gap-4">
        <div className="flex max-w-2xl flex-col gap-1">
          <h1 className="text-lg font-semibold tracking-tight">Ask sessions</h1>
          <p className="text-sm text-muted-foreground">
            Create vs clone per ask, <code>omitResponseConstraintInput</code> and the sampling
            trial, on the frozen {CORPUS.length}-row corpus. This page is dev-only; the App still
            creates one session per ask.
          </p>
        </div>
        <div className="flex flex-wrap items-center gap-2">
          <Badge variant={availability === 'available' ? 'default' : 'outline'} size="sm">
            {availability ?? 'checking'}
          </Badge>
          <Badge variant={samplingAvailable === true ? 'default' : 'outline'} size="sm">
            {samplingAvailable === null
              ? 'sampling ?'
              : samplingAvailable
                ? `sampling ${samplingValue ?? ''}`
                : 'no sampling trial'}
          </Badge>
          <Badge variant="outline" size="sm">
            {records.length} records
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
              <Button
                size="sm"
                variant="solid"
                onClick={() => void setup()}
                disabled={downloadPercent !== null}
              >
                Set up the model
              </Button>
            ) : null}
            {availability === 'downloading' || downloadPercent !== null ? (
              <Progress value={downloadPercent ?? 0} className="max-w-64" />
            ) : null}
            <Button
              size="sm"
              variant="ghost"
              onClick={() => void assistantState().then(setAvailability)}
            >
              Check again
            </Button>
          </CardContent>
        </Card>
      ) : null}

      <Card size="sm">
        <CardHeader>
          <CardTitle>The machine</CardTitle>
          <CardDescription>
            The model variant is not readable from JavaScript; copy it from{' '}
            <code>chrome://on-device-internals</code>.
          </CardDescription>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          <Input
            value={machine.onDeviceInternals}
            onChange={(event) =>
              setMachine({ ...machine, onDeviceInternals: event.target.value })
            }
            placeholder="nano_v3_gpu_high_tier_model"
            aria-label="Model variant"
          />
          <p className="text-xs text-muted-foreground">
            {machine.userAgent} · {machine.hardwareConcurrency ?? '?'} cores ·{' '}
            {machine.deviceMemory ?? '?'} GB
          </p>
        </CardContent>
      </Card>

      <Card size="sm">
        <CardHeader>
          <CardTitle>The strategies</CardTitle>
          <CardDescription>
            {CORPUS.length} rows × 3 runs × {selected.length || available.length} strategies ={' '}
            {(selected.length || available.length) * CORPUS.length * 3} model calls. Each sentence
            is scored only when all three verdicts agree.
          </CardDescription>
        </CardHeader>
        <CardContent className="flex flex-col gap-4">
          <div className="flex flex-col gap-3">
            {available.map((strategy) => (
              <div key={strategy} className="flex items-start justify-between gap-4">
                <div className="flex flex-col gap-0.5">
                  <span className="text-sm font-medium">{strategyLabel(strategy)}</span>
                  <span className="text-xs text-muted-foreground">
                    {strategyDescription(strategy)}
                  </span>
                </div>
                <Switch
                  aria-label={`Run ${strategyLabel(strategy)}`}
                  size="sm"
                  checked={selected.includes(strategy)}
                  onCheckedChange={(checked) => toggle(strategy, checked)}
                  disabled={running}
                />
              </div>
            ))}
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <Button
              size="sm"
              variant="solid"
              onClick={() => void start()}
              disabled={running || availability !== 'available' || selected.length === 0}
            >
              Run {selected.length} strategies
            </Button>
            <Button
              size="sm"
              variant="ghost"
              onClick={() => stop.current?.abort()}
              disabled={!running}
            >
              Stop
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={reset}
              disabled={running || records.length === 0}
            >
              Clear
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={exportRun}
              disabled={records.length === 0}
            >
              Export
            </Button>
          </div>

          {running ? <Progress value={progress} /> : null}
          {status ? <p className="text-xs text-muted-foreground">{status}</p> : null}
          {sessionSetup ? (
            <p className="text-xs text-muted-foreground">
              {strategyLabel(sessionSetup.strategy)} setup {Math.round(sessionSetup.ms)} ms
              {sessionSetup.samplingMode ? ` · ${sessionSetup.samplingMode}` : ''}
            </p>
          ) : null}
          {error ? <p className="text-sm text-destructive">{error}</p> : null}
          {probe ? (
            <p className="text-xs text-muted-foreground">
              Clone check: {probe.sameIntent ? 'the same Intent' : 'different Intents'} on “
              {probe.sentence}”{probe.baseSamplingMode ? ` · ${probe.baseSamplingMode}` : ''}.
            </p>
          ) : null}

          <Separator />
          <p className="text-xs text-muted-foreground">
            Prompt {plan.systemSha} · schema {plan.schemaSha} · examples {plan.examplesSha} (
            {plan.examples.length}).
          </p>
        </CardContent>
      </Card>

      <Card size="sm">
        <CardHeader>
          <CardTitle>The report</CardTitle>
          <CardDescription>
            3/3 row verdicts, latency and errors. The export carries every record and the
            per-field tallies.
          </CardDescription>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          {records.length === 0 ? (
            <Empty>
              <EmptyHeader>
                <EmptyTitle>No records yet</EmptyTitle>
                <EmptyDescription>
                  Pick the strategies and run them; each sentence is scored only when all three
                  verdicts agree.
                </EmptyDescription>
              </EmptyHeader>
              <EmptyContent>
                <Button
                  size="sm"
                  variant="solid"
                  onClick={() => void start()}
                  disabled={running || availability !== 'available' || selected.length === 0}
                >
                  Run {selected.length} strategies
                </Button>
              </EmptyContent>
            </Empty>
          ) : (
            <div className="overflow-x-auto">
              <table className="w-full text-sm">
                <thead>
                  <tr className="border-b text-left text-xs text-muted-foreground">
                    <th className="py-2 pr-3 font-medium">Strategy</th>
                    <th className="py-2 pr-3 font-medium">
                      Rows 3/3 (pass / fail / unstable / incomplete)
                    </th>
                    <th className="py-2 pr-3 font-medium">Mean</th>
                    <th className="py-2 pr-3 font-medium">Median</th>
                    <th className="py-2 pr-3 font-medium">p95</th>
                    <th className="py-2 font-medium">Errors</th>
                  </tr>
                </thead>
                <tbody>
                  {summary.map((row) => (
                    <tr key={row.strategy} className="border-b last:border-0">
                      <td className="py-2 pr-3 font-medium">{strategyLabel(row.strategy)}</td>
                      <td className="py-2 pr-3 tabular-nums">
                        {row.pass} / {row.fail} / {row.unstable} / {row.incomplete}
                      </td>
                      <td className="py-2 pr-3 tabular-nums">{Math.round(row.meanMs)} ms</td>
                      <td className="py-2 pr-3 tabular-nums">{Math.round(row.medianMs)} ms</td>
                      <td className="py-2 pr-3 tabular-nums">{Math.round(row.p95Ms)} ms</td>
                      <td className="py-2 tabular-nums">{row.errors}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </CardContent>
      </Card>

      <footer className="text-xs text-muted-foreground">
        The model is Chrome's; this page is dev-only and never shipped. Nothing is uploaded. Keep
        the tab open or export; a raw blob belongs in <code>.scratch/</code>, not the repository.
      </footer>
    </main>
  );
}

const root = document.getElementById('root');
if (!root) throw new Error('Missing #root');
createRoot(root).render(<Sessions />);

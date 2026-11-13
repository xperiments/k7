import { useId, useState } from 'react';
import { Cpu, RefreshCw } from 'lucide-react';

import { Badge } from '@/components/reui/badge';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuRadioGroup,
  DropdownMenuRadioItem,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu';
import { Spinner } from '@/components/ui/spinner';
import { autoWorkerCount, coresOf, memoryOf } from '../../audio/workers';
import { isAbort, searchVideos } from '../../media/youtube';
import {
  getSettings,
  MAX_ANALYSIS_WORKERS,
  setSettings,
  type AnalysisWorkers,
  type Settings,
} from '../../settings';
import { plural } from '../format';
import { useWorkerCapacity } from '../state/analysis';

/**
 * What this browser remembers for itself.
 *
 * Two settings, neither of which the device ever sees: the Invidious **Instance** the video
 * search talks to, and the size of the analysis worker pool. The assistant has no setting — it
 * is Chrome's **Built-in model** (ADR 0084) — and this browser stores no key for anything.
 *
 * Clearing site data loses both, which is the honest cost of putting nothing in the device's
 * flash.
 */

interface Notice {
  kind: 'ok' | 'error';
  text: string;
}

/** The Invidious lookup is one GET; eight seconds is already generous for it. */
const INSTANCE_TEST_TIMEOUT_MS = 8000;

function Field({
  label,
  htmlFor,
  hint,
  children,
}: {
  label: string;
  htmlFor: string;
  hint?: string;
  children: React.ReactNode;
}) {
  return (
    <div className="flex flex-col gap-1">
      <label htmlFor={htmlFor} className="text-xs text-muted-foreground">
        {label}
      </label>
      {children}
      {hint ? <p className="text-xs text-muted-foreground">{hint}</p> : null}
    </div>
  );
}

function NoticeLine({ notice }: { notice: Notice }) {
  return (
    <div role="status" className="flex items-start gap-2 text-sm">
      <Badge variant={notice.kind === 'ok' ? 'success-light' : 'destructive-light'}>
        {notice.kind === 'ok' ? 'ok' : 'failed'}
      </Badge>
      <span className="min-w-0 break-words">{notice.text}</span>
    </div>
  );
}

export function SettingsArea() {
  const capacity = useWorkerCapacity();
  const [saved, setSaved] = useState<Settings>(() => getSettings());

  const suggested = capacity?.suggested ?? autoWorkerCount();
  const cores = capacity?.cores ?? coresOf();
  const memory = capacity?.memoryGb ?? memoryOf();
  /* Compare the count, not the label: auto and a pinned count that happen to be the same
   * number need no reload, because the pool would be identical. */
  const savedRequested =
    capacity === null ? null : saved.workers === 'auto' ? capacity.suggested : saved.workers;
  const bootMatches = capacity !== null && capacity.requested === savedRequested;
  const workerValue = saved.workers === 'auto' ? 'auto' : String(saved.workers);
  const bootBadge: 'secondary' | 'success-light' | 'warning-light' =
    capacity === null
      ? 'secondary'
      : capacity.started === capacity.requested
        ? 'success-light'
        : 'warning-light';

  const chooseWorkers = (value: string) => {
    const workers: AnalysisWorkers = value === 'auto' ? 'auto' : Number(value);
    if (
      workers !== 'auto' &&
      (!Number.isInteger(workers) || workers < 1 || workers > MAX_ANALYSIS_WORKERS)
    ) {
      return;
    }
    setSettings({ workers });
    setSaved(getSettings());
  };

  const [instance, setInstance] = useState(saved.invidious);

  const [instanceNotice, setInstanceNotice] = useState<Notice | null>(null);
  const [testingInstance, setTestingInstance] = useState(false);

  const instanceId = useId();

  const saveInstance = () => {
    const value = instance.trim();
    setSettings({ invidious: value });
    setSaved(getSettings());
    setInstanceNotice({
      kind: 'ok',
      text: value ? 'Saved in this browser.' : 'Cleared — the video search has no instance to ask.',
    });
  };

  const runInstanceTest = async () => {
    setTestingInstance(true);
    setInstanceNotice(null);
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), INSTANCE_TEST_TIMEOUT_MS);
    try {
      const found = await searchVideos('test', instance, controller.signal);
      setInstanceNotice({
        kind: 'ok',
        text: `The instance answered with ${plural(found.length, 'video')}.`,
      });
    } catch (error) {
      if (isAbort(error)) {
        setInstanceNotice({ kind: 'error', text: 'The instance did not answer within eight seconds.' });
      } else {
        setInstanceNotice({
          kind: 'error',
          text: error instanceof Error ? error.message : String(error),
        });
      }
    } finally {
      clearTimeout(timer);
      setTestingInstance(false);
    }
  };

  return (
    <div className="flex flex-col gap-4 p-4 sm:gap-6 sm:p-6">
      <header className="flex flex-col gap-0.5">
        <h1 className="text-xl font-light tracking-tight">Settings</h1>
        <p className="text-sm text-muted-foreground">
          What this browser remembers for itself. None of it is sent to the device.
        </p>
      </header>

      <Card>
        <CardHeader>
          <CardTitle>Analysis workers</CardTitle>
          <p className="text-sm text-muted-foreground">
            Measuring runs in worker threads. Each worker loads its own copy of the models, so
            more is not always faster. Auto takes half the logical cores, capped by memory; at
            boot the app starts that many and keeps the ones that answer.
          </p>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          <div className="flex flex-wrap items-center gap-2 text-xs text-muted-foreground">
            <span>
              {cores} logical {cores === 1 ? 'core' : 'cores'}
              {memory !== null ? ` · ${memory} GB` : ''}
            </span>
            <Badge variant={bootBadge}>
              {capacity ? `${capacity.workers} in the pool` : 'checking workers…'}
            </Badge>
          </div>

          <div className="flex flex-wrap items-center gap-2">
            <DropdownMenu>
              <DropdownMenuTrigger render={<Button variant="outline" size="sm" />}>
                <Cpu aria-hidden="true" />
                {saved.workers === 'auto'
                  ? `Auto (${suggested})`
                  : plural(saved.workers, 'worker')}
              </DropdownMenuTrigger>
              <DropdownMenuContent align="start">
                <DropdownMenuRadioGroup value={workerValue} onValueChange={chooseWorkers}>
                  <DropdownMenuRadioItem value="auto">
                    Auto — {suggested} on this machine
                  </DropdownMenuRadioItem>
                  {Array.from({ length: MAX_ANALYSIS_WORKERS }, (_, index) => index + 1).map(
                    (count) => (
                      <DropdownMenuRadioItem key={count} value={String(count)}>
                        {plural(count, 'worker')}
                      </DropdownMenuRadioItem>
                    ),
                  )}
                </DropdownMenuRadioGroup>
              </DropdownMenuContent>
            </DropdownMenu>

            {!bootMatches && capacity ? (
              <Button variant="outline" size="sm" onClick={() => window.location.reload()}>
                <RefreshCw aria-hidden="true" />
                Reload to apply
              </Button>
            ) : null}
          </div>

          <p className="text-xs text-muted-foreground" role="status">
            {capacity
              ? capacity.started === capacity.requested
                ? `The boot check started all ${plural(capacity.requested, 'worker')}.`
                : `The boot check started ${capacity.started} of ${plural(capacity.requested, 'worker')}; the pool uses ${capacity.workers}.`
              : 'Starting the workers and checking how many answer…'}
          </p>
          {!bootMatches && capacity ? (
            <p className="text-xs text-muted-foreground">
              Saved; the new count applies after a reload.
            </p>
          ) : null}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Invidious instance</CardTitle>
          <p className="text-sm text-muted-foreground">
            The server the video search asks. Public instances come and go, so this is a setting
            rather than a constant.
          </p>
        </CardHeader>
        <CardContent className="flex flex-col gap-3">
          <Field label="Instance" htmlFor={instanceId} hint="An https address, no trailing slash needed">
            <Input
              id={instanceId}
              type="url"
              value={instance}
              placeholder="https://inv.nadeko.net"
              autoComplete="off"
              spellCheck={false}
              onChange={(event) => setInstance(event.target.value)}
            />
          </Field>

          <div className="flex flex-wrap items-center gap-2">
            <Button variant="solid" size="sm" onClick={saveInstance}>
              Save
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={() => void runInstanceTest()}
              disabled={testingInstance}
            >
              Test
            </Button>
            {testingInstance ? <Spinner aria-hidden="true" className="size-3.5" /> : null}
          </div>

          {instanceNotice ? <NoticeLine notice={instanceNotice} /> : null}
        </CardContent>
      </Card>

      <p className="text-xs text-muted-foreground">
        These live in this browser&apos;s local storage and are never written to the device. Clearing
        site data loses them.
      </p>
    </div>
  );
}

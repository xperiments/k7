import { MAX_ANALYSIS_WORKERS, type AnalysisWorkers } from '../settings';
import { AnalysisClient } from './client';

/**
 * How many analysis workers this machine gets, and the boot check that answers it.
 *
 * A worker is a thread and each one loads its own sessions, so the count is a real trade:
 * too few leave cores idle, too many compete for memory. Auto takes half the logical cores,
 * capped by the memory the browser admits to, and never asks for more than four. A pinned
 * count is allowed up to the maximum in Settings.
 *
 * At boot the app starts exactly that many workers and sends each a small probe. The probe
 * runs JavaScript but does not load the models, so it is cheap; it proves the worker script
 * starts and answers, and the ones that do not are dropped. It cannot know whether the
 * machine has memory for eight copies of the models — the memory cap is the heuristic for
 * that — but it does answer "how many workers can this browser really start".
 */

export interface WorkerCapacity {
  /** The setting this pool was started with. */
  setting: AnalysisWorkers;
  /** Logical processors the browser reports. */
  cores: number;
  /** Device memory in GB, when the browser reports it (Chrome does). */
  memoryGb: number | null;
  /** What auto picks on this machine. */
  suggested: number;
  /** What the setting asked for: auto's suggestion, or the pinned count. */
  requested: number;
  /** How many workers answered the boot check. */
  started: number;
  /** How many clients the pool holds: at least one, even when the check failed. */
  workers: number;
  /** When the check ran, ISO. */
  checkedAt: string;
}

export function coresOf(): number {
  if (typeof navigator === 'undefined') return 1;
  const cores = navigator.hardwareConcurrency;
  return typeof cores === 'number' && Number.isFinite(cores) && cores >= 1
    ? Math.floor(cores)
    : 1;
}

export function memoryOf(): number | null {
  if (typeof navigator === 'undefined') return null;
  const memory = (navigator as Navigator & { deviceMemory?: number }).deviceMemory;
  return typeof memory === 'number' && Number.isFinite(memory) && memory > 0
    ? memory
    : null;
}

/**
 * Auto's count: half the logical cores, capped by the memory the browser reports, and at
 * four in any case. The model set is loaded per worker; more lanes stop helping long before
 * the core count does.
 */
export function autoWorkerCount(cores = coresOf(), memoryGb = memoryOf()): number {
  const byCores = Math.max(1, Math.min(4, Math.floor(cores / 2)));
  const byMemory =
    memoryGb === null ? 4 : memoryGb >= 8 ? 4 : memoryGb >= 4 ? 3 : memoryGb >= 2 ? 2 : 1;
  return Math.max(1, Math.min(MAX_ANALYSIS_WORKERS, byCores, byMemory));
}

const PROBE_TIMEOUT_MS = 3000;

/**
 * Start the pool and keep the workers that answer the boot check. The clients are the
 * analysis workers themselves, not a separate probe: nothing starts twice, and a worker
 * that cannot run is disposed before the queue ever reaches it.
 */
export async function startWorkerPool(
  setting: AnalysisWorkers,
): Promise<{ clients: AnalysisClient[]; capacity: WorkerCapacity }> {
  const cores = coresOf();
  const memoryGb = memoryOf();
  const suggested = autoWorkerCount(cores, memoryGb);
  const requested =
    setting === 'auto'
      ? suggested
      : Math.max(1, Math.min(MAX_ANALYSIS_WORKERS, Math.round(setting)));

  const candidates = Array.from({ length: requested }, () => new AnalysisClient());
  const probes = await Promise.allSettled(
    candidates.map((client) => client.probe(PROBE_TIMEOUT_MS)),
  );

  const clients: AnalysisClient[] = [];
  candidates.forEach((client, index) => {
    if (probes[index]?.status === 'fulfilled') clients.push(client);
    else client.dispose();
  });

  const started = clients.length;
  /* A pool with no worker would leave the queue queued forever. Keep one so the first row
   * reports the failure it hits, which is a sentence the Process page can show. */
  if (clients.length === 0) clients.push(new AnalysisClient());

  return {
    clients,
    capacity: {
      setting,
      cores,
      memoryGb,
      suggested,
      requested,
      started,
      workers: clients.length,
      checkedAt: new Date().toISOString(),
    },
  };
}

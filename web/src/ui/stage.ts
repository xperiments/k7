import type { Stage } from '../audio/analyzer';
import type { QueueItem } from './queue';

/**
 * The words a run says about itself.
 *
 * One table, because the row list and the run header name the same work: a row showing
 * "Finding key…" is doing exactly what the header is reporting for the run, and two
 * spellings of it would make the header look like a different process.
 */
export const STAGE_TEXT: Record<Stage, string> = {
  tempo: 'Finding tempo…',
  deep: 'Refining tempo…',
  key: 'Finding key…',
  mood: 'Measuring mood…',
};

/** One row's state in words: what it is doing, or what it came to. */
export function statusWord(item: QueueItem): string {
  if (item.status === 'error') return 'Failed';
  if (item.status === 'analysing') return item.stage ? STAGE_TEXT[item.stage] : 'Analysing…';
  if (item.status === 'tagged') return 'From tags';
  if (item.status === 'hashing') return 'Hashing…';
  if (item.status === 'decoding') return 'Decoding…';
  if (item.status === 'done') return item.skipped ? 'Already measured' : 'Measured';
  return 'Queued';
}

/** A row a worker is holding right now. */
export function isWorking(item: QueueItem): boolean {
  return item.status === 'hashing' || item.status === 'decoding' || item.status === 'analysing';
}

/**
 * What the run is doing, for the header: the stage of the first row still working.
 *
 * Three lanes run at once, so this is a sample and not a census — it names the work in
 * flight, which is what a reader looking at a moving bar wants to know.
 */
export function runStage(items: readonly QueueItem[]): string | null {
  const working = items.find(isWorking);
  return working ? statusWord(working) : null;
}

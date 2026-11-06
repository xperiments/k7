/**
 * The page's side of the AVIF cover worker.
 *
 * The worker exists only while something needs it: a scan without covers, or a legacy-cache
 * migration, never constructs it. One worker handles the whole run; the request map is keyed
 * by a counter because the encoder is stateless but the worker answers in order of completion.
 */
import type { CachedCover, CoverRecord } from '../storage/media';

interface Reply {
  id: number;
  ok: boolean;
  avif?: ArrayBuffer;
  width?: number;
  height?: number;
  error?: string;
}

let worker: Worker | null = null;
let nextId = 1;
const pending = new Map<
  number,
  { resolve: (reply: { avif: ArrayBuffer; width: number; height: number }) => void; reject: (error: Error) => void }
>();

function workerOrThrow(): Worker {
  if (worker) return worker;
  if (typeof Worker === 'undefined' || typeof OffscreenCanvas === 'undefined') {
    throw new Error('this browser has no worker or OffscreenCanvas for AVIF');
  }
  worker = new Worker(new URL('./avifWorker.ts', import.meta.url), { type: 'module' });
  worker.onmessage = (event: MessageEvent<Reply>) => {
    const reply = event.data;
    const request = pending.get(reply.id);
    if (!request) return;
    pending.delete(reply.id);
    if (reply.ok && reply.avif && reply.width && reply.height) {
      request.resolve({ avif: reply.avif, width: reply.width, height: reply.height });
    } else {
      request.reject(new Error(reply.error ?? 'the AVIF encoder refused the picture'));
    }
  };
  worker.onerror = (event) => {
    const error = new Error(event.message || 'the AVIF worker failed');
    for (const request of pending.values()) request.reject(error);
    pending.clear();
    worker?.terminate();
    worker = null;
  };
  return worker;
}

/** Encode one source picture to a 422px AVIF. Throws; the storage layer falls back to the source. */
export async function encodeCover(source: CachedCover): Promise<CoverRecord> {
  const bytes = source.bytes.slice() as Uint8Array<ArrayBuffer>;
  const buffer = bytes.buffer;
  const id = nextId++;
  const reply = await new Promise<{ avif: ArrayBuffer; width: number; height: number }>(
    (resolve, reject) => {
      pending.set(id, { resolve, reject });
      workerOrThrow().postMessage({ id, source: buffer, mime: source.format }, [buffer]);
    },
  );
  return {
    bytes: new Uint8Array(reply.avif),
    format: 'image/avif',
    width: reply.width,
    height: reply.height,
  };
}

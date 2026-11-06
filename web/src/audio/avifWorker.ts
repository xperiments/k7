/**
 * One source picture in, one 422px AVIF out.
 *
 * AVIF is not a format a canvas can encode: `canvas.toBlob('image/avif')` returns a PNG in
 * every current browser. libavif is compiled to WebAssembly (`@jsquash/avif`) and lives in
 * this worker so the encoder never blocks the scan's UI; the page sends the source bytes and
 * gets the encoded bytes back, once per distinct cover.
 */
import encode from '@jsquash/avif/encode';

const MAX_EDGE = 422;
const QUALITY = 55;
const SPEED = 8;

interface Request {
  id: number;
  source: ArrayBuffer;
  mime: string;
}

type Response =
  | { id: number; ok: true; avif: ArrayBuffer; width: number; height: number }
  | { id: number; ok: false; error: string };

interface WorkerScope {
  onmessage: ((event: MessageEvent<Request>) => void) | null;
  postMessage(message: Response, transfer?: Transferable[]): void;
}

const scope = self as unknown as WorkerScope;

let queue: Promise<void> = Promise.resolve();

scope.onmessage = (event: MessageEvent<Request>) => {
  // libavif's wasm module is one instance; one encode at a time.
  queue = queue.then(() => handle(event.data)).catch(() => undefined);
};

async function handle({ id, source, mime }: Request): Promise<void> {
  let bitmap: ImageBitmap | null = null;
  try {
    bitmap = await createImageBitmap(new Blob([source], { type: mime || 'image/jpeg' }));
    const scale = Math.min(1, MAX_EDGE / Math.max(bitmap.width, bitmap.height));
    const width = Math.max(1, Math.round(bitmap.width * scale));
    const height = Math.max(1, Math.round(bitmap.height * scale));

    const canvas = new OffscreenCanvas(width, height);
    const context = canvas.getContext('2d');
    if (!context) throw new Error('no 2D canvas for the cover');
    context.imageSmoothingEnabled = true;
    context.imageSmoothingQuality = 'high';
    context.drawImage(bitmap, 0, 0, width, height);

    const avif = await encode(context.getImageData(0, 0, width, height), {
      quality: QUALITY,
      speed: SPEED,
    });
    const message: Response = { id, ok: true, avif, width, height };
    scope.postMessage(message, [avif]);
  } catch (error) {
    const message: Response = {
      id,
      ok: false,
      error: error instanceof Error ? error.message : String(error),
    };
    scope.postMessage(message);
  } finally {
    bitmap?.close();
  }
}

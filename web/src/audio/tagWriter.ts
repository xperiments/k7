import type { TagLib } from 'taglib-wasm';
import type { Analysis, TrackFormat } from '../domain/analysis';
import { analysisTags } from '../domain/storedAnalysis';
import { loadWeightBytes } from '../weights/loader';
import { TAGLIB_WASM } from '../weights/manifest';

let taglibPromise: Promise<TagLib> | null = null;

function loadTaglib(): Promise<TagLib> {
  if (!taglibPromise) {
    taglibPromise = (async () => {
      const { TagLib } = await import('taglib-wasm');

      // The Node tier has no page origin to fetch a weight from; the package resolves its
      // own wasm there. In the browser the binary is a verified weight, and handing it
      // over keeps TagLib from fetching a second, unchecked copy from beside its module.
      if (typeof window === 'undefined') return TagLib.initialize();

      const wasmBinary = await loadWeightBytes(TAGLIB_WASM);
      return TagLib.initialize({ wasmBinary });
    })().catch((error: unknown) => {
      taglibPromise = null;
      throw error;
    });
  }
  return taglibPromise;
}

/**
 * Writes standard tags plus the XTAPE manifest into a copy of the file and
 * returns the new bytes. Browsers cannot overwrite a dropped file in place;
 * the caller offers the result as a download.
 */
export async function writeAnalysisTags(
  file: File,
  analysis: Analysis,
): Promise<Uint8Array> {
  const taglib = await loadTaglib();
  const bytes = new Uint8Array(await file.arrayBuffer());
  return taglib.edit(bytes, (audio) => {
    for (const [key, value] of Object.entries(analysisTags(analysis))) {
      audio.setProperty(key, value);
    }
  });
}

export function downloadTaggedCopy(
  name: string,
  bytes: Uint8Array,
  format: TrackFormat,
): void {
  const type = format === 'mp3' ? 'audio/mpeg' : 'audio/flac';
  const copy = new ArrayBuffer(bytes.byteLength);
  new Uint8Array(copy).set(bytes);
  const blob = new Blob([copy], { type });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = name;
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 10_000);
}

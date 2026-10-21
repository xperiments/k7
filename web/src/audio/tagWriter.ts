import type { TagLib } from 'taglib-wasm';
import type { Analysis, TrackFormat } from '../domain/analysis';
import { analysisTags } from '../domain/storedAnalysis';

let taglibPromise: Promise<TagLib> | null = null;

function loadTaglib(): Promise<TagLib> {
  if (!taglibPromise) {
    taglibPromise = (async () => {
      const { TagLib } = await import('taglib-wasm');
      return TagLib.initialize();
    })();
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

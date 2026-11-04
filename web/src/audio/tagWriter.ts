import type { Picture, PictureType, TagLib } from 'taglib-wasm';
import type { Analysis, TrackFormat } from '../domain/analysis';
import { analysisTags } from '../domain/storedAnalysis';
import {
  coverSource,
  LABEL_DESCRIPTION,
  mimeOf,
  renderLabel,
  type EmbeddablePicture,
  type LabelRenderer,
} from './covers';
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

export type LabelWrite =
  /** The file was rewritten: these bytes carry the label this build makes. */
  | { kind: 'written'; bytes: Uint8Array }
  /** The picture in the file is already the one this build makes; nothing was touched. */
  | { kind: 'complete' }
  /** The file has no art of its own, so there is nothing to make the label from. */
  | { kind: 'no-source' };

function sameBytes(left: Uint8Array, right: Uint8Array): boolean {
  if (left.byteLength !== right.byteLength) return false;
  for (let at = 0; at < left.byteLength; at += 1) {
    if (left[at] !== right[at]) return false;
  }
  return true;
}

/** The pictures a file carries, in the shape `covers.ts` reasons about. */
async function picturesOf(
  taglib: TagLib,
  input: Uint8Array,
): Promise<EmbeddablePicture[]> {
  const audio = await taglib.open(input);
  try {
    return audio.getPictures().map((picture) => ({
      description: picture.description,
      type: picture.type,
      mimeType: picture.mimeType,
      data: picture.data,
    }));
  } finally {
    audio.dispose();
  }
}

/**
 * Put the label this build makes into a file, replacing whatever label is there.
 *
 * **The picture is always rendered again**, because a size is not a record of a look: the
 * wear, the fit and the encoder are code, and any of them can change between builds while
 * the file still answers "402x188, k7label". Rendering and comparing is what makes a changed
 * filter reach a card that was already processed; the comparison then decides whether there
 * is a byte worth writing, so a pass with nothing new leaves every file alone.
 *
 * The label is made from the Track's own front cover; a file with no art of its own is left
 * alone, since there is nothing to fit.
 *
 * The read and the write are one edit when the caller already has the pictures (a measuring
 * run has just parsed them); otherwise the file is opened once to look and compare.
 */
export async function writeTrackLabel(
  file: File,
  pictures?: readonly EmbeddablePicture[],
  options: { bytes?: Uint8Array; render?: LabelRenderer } = {},
): Promise<LabelWrite> {
  const taglib = await loadTaglib();
  const input = options.bytes ?? new Uint8Array(await file.arrayBuffer());
  const existing = pictures ?? (await picturesOf(taglib, input));

  const source = coverSource(existing);
  if (source === null) return { kind: 'no-source' };

  const render = options.render ?? renderLabel;
  const rendered = await render({ data: source.data, mimeType: mimeOf(source) });

  /* The file is the record: the same bytes mean the same picture, so there is nothing to
   * write. Anything else - a missing label, an older build's, a different encoder's - is
   * this pass's to replace. */
  const current = existing.find((picture) => picture.description === LABEL_DESCRIPTION);
  if (current !== undefined && sameBytes(current.data, rendered)) {
    return { kind: 'complete' };
  }

  const label: Picture = {
    type: 'Other' as PictureType,
    mimeType: 'image/jpeg',
    data: rendered,
    description: LABEL_DESCRIPTION,
  };

  const bytes = await taglib.edit(input, (audio) => {
    // The label this pass is replacing is dropped first; everything else — the Track's own
    // front cover above all — is kept in place.
    const kept = audio
      .getPictures()
      .filter((picture) => picture.description !== LABEL_DESCRIPTION);
    audio.setPictures([...kept, label]);
  });

  return { kind: 'written', bytes };
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

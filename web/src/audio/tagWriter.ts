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

/**
 * What a settled file carries, as one number for the card's record.
 *
 * A media row remembers this after a finalize has finished, so the next scan can tell that
 * the file already says everything this build would write — its standard tags, its XTAPE
 * manifest and its label — and does not have to open, re-encode or rewrite it to find out.
 * Bump it when the bytes a settled file should carry change: a tag this App starts writing,
 * a different label, a different encoder. A bump is one pass that rewrites every measured
 * file, deliberately, exactly like an analyser bump is one pass of measuring.
 */
export const METADATA_WRITE_VERSION = 1;

let taglibPromise: Promise<TagLib> | null = null;

function loadTaglib(): Promise<TagLib> {
  if (!taglibPromise) {
    taglibPromise = (async () => {
      const { TagLib } = await import('taglib-wasm');

      // The Node tier has no page origin to fetch a weight from; the package resolves its
      // own wasm there. A browser *worker* also has no window, but it has a page origin and
      // IndexedDB, so it takes the verified weight like the page does.
      const node = typeof process !== 'undefined' && Boolean(process.versions?.node);
      if (node) return TagLib.initialize();

      const wasmBinary = await loadWeightBytes(TAGLIB_WASM);
      return TagLib.initialize({ wasmBinary });
    })().catch((error: unknown) => {
      taglibPromise = null;
      throw error;
    });
  }
  return taglibPromise;
}

async function writeTags(
  taglib: TagLib,
  input: Uint8Array,
  tags: Record<string, string>,
): Promise<Uint8Array> {
  return taglib.edit(input, (audio) => {
    for (const [key, value] of Object.entries(tags)) audio.setProperty(key, value);
  });
}

/** Whether the file already carries exactly the values this analysis would write. */
async function tagsDiffer(
  taglib: TagLib,
  input: Uint8Array,
  tags: Record<string, string>,
): Promise<boolean> {
  const audio = await taglib.open(input);
  try {
    for (const [key, value] of Object.entries(tags)) {
      try {
        const current = audio.getProperty(key);
        // A TagLib property is a list; this App writes one value and compares the first.
        if ((current?.[0] ?? '') !== value) return true;
      } catch {
        // A property this format cannot read is one worth writing.
        return true;
      }
    }
    return false;
  } finally {
    audio.dispose();
  }
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
  return writeTags(taglib, bytes, analysisTags(analysis));
}

/**
 * Write the measured values and the worn label into one set of bytes, in as few edits as
 * need the file. This is the automatic path for a card opened read-write: the standard tags
 * (BPM, key, mood, energy) are what make a later run quick, and the XTAPE manifest is what
 * lets this App read the whole measurement back instead of running the models again.
 *
 * `complete` means the file already says everything this analysis says and carries the label
 * this build makes; `no-source` is impossible when the tags changed, because a file with no
 * art still gets its tags. The caller writes the returned bytes to the card once.
 */
export async function writeTrackMetadata(
  file: File,
  analysis: Analysis,
  pictures?: readonly EmbeddablePicture[],
  options: { bytes?: Uint8Array; render?: LabelRenderer } = {},
): Promise<LabelWrite> {
  const taglib = await loadTaglib();
  const input = options.bytes ?? new Uint8Array(await file.arrayBuffer());
  const desired = analysisTags(analysis);

  let tagsChanged = true;
  try {
    tagsChanged = await tagsDiffer(taglib, input, desired);
  } catch {
    tagsChanged = true;
  }

  if (!tagsChanged) {
    const label = await writeTrackLabel(file, pictures, { ...options, bytes: input });
    /* No art means no label will ever exist; the tags are already right, so there is nothing
     * left to write. */
    return label.kind === 'no-source' ? { kind: 'complete' } : label;
  }

  const tagged = await writeTags(taglib, input, desired);
  /* The label is drawn from art and can fail on a bad encoder; the measured tags are not
   * allowed to be lost with it. */
  const label = await writeTrackLabel(file, pictures, { ...options, bytes: tagged }).catch(
    () => null,
  );
  /* The label may be complete, may have no source to draw from, or may have failed; the tags
   * themselves are still a write this pass owes the card. */
  return label?.kind === 'written' ? label : { kind: 'written', bytes: tagged };
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

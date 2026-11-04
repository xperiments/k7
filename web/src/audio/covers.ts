/**
 * The label this App prints on a Track, sized for the cassette it belongs to.
 *
 * The white page's Card is 426x272 and the print on it - frame[0]'s rect - is 402x188
 * (`docs/adr/0039-the-cassette-is-a-card.md`). A picture already embedded in a file is
 * whatever the ripper chose, usually a square hundred kilobytes, so the card in the computer
 * is the only place it can be fitted. This makes the result: one JPEG, described `k7label`,
 * fitted cover-style from the Track's own art and passed through the worn-print filter
 * (`audio/worn.ts`).
 *
 * **Cover, not contain.** The art fills the box and the overflow is cropped, so a square
 * cover is never letterboxed into a label with pillars. This is the same fit the player's own
 * decoder gives a picture (`artwork_loader.c`).
 *
 * The 426x272 cassette picture is not made at the moment; a file that still carries one from
 * an earlier pass keeps it, and it is never used as the art either picture is made from.
 */

import { wearLabel } from './worn';

/**
 * Written by an earlier pass. No longer produced, but still recognised: it must never be
 * mistaken for the Track's own art and re-fitted into the label.
 */
export const COVER_DESCRIPTION = 'k7cover';
export const LABEL_DESCRIPTION = 'k7label';

/** The print on the Card: frame[0]'s rect. */
export const LABEL = { width: 402, height: 188 } as const;

/** The one JPEG the player's decoder accepts; the encoders above it are happy with 0.9. */
const JPEG_QUALITY = 0.9;

/**
 * The shape both readers give a picture: taglib names the type `FrontCover`, music-metadata
 * `Cover (front)`, and each calls the MIME type something else. Everything here is optional
 * except the bytes, so either one fits without a mapping layer.
 */
export interface EmbeddablePicture {
  /** The APIC/PICTURE description: how this App tells its own picture from the Track's art. */
  description?: string;
  type?: string;
  mimeType?: string;
  /** music-metadata's name for `mimeType`. */
  format?: string;
  data: Uint8Array<ArrayBufferLike>;
}

/** A source picture, and the label made from it. */
export type LabelRenderer = (source: {
  data: Uint8Array<ArrayBufferLike>;
  mimeType: string;
}) => Promise<Uint8Array>;

export function mimeOf(picture: EmbeddablePicture): string {
  return picture.mimeType ?? picture.format ?? 'image/jpeg';
}

function isFrontCover(type: string | undefined): boolean {
  if (type === undefined) return false;
  return type === 'FrontCover' || type.toLowerCase().startsWith('cover (front)');
}

function isOurs(picture: EmbeddablePicture): boolean {
  return (
    picture.description === COVER_DESCRIPTION || picture.description === LABEL_DESCRIPTION
  );
}

/**
 * The width and height a JPEG or PNG header declares, or null for anything else.
 *
 * The size comes from the container, never from decoding: a check that this pass already
 * wrote the label must not cost a decode per Track. A format that cannot be read this way is
 * treated as the wrong size and regenerated, which is the safe direction.
 */
export function imageSize(bytes: Uint8Array<ArrayBufferLike>): {
  width: number;
  height: number;
} | null {
  return jpegSize(bytes) ?? pngSize(bytes);
}

function jpegSize(bytes: Uint8Array<ArrayBufferLike>): { width: number; height: number } | null {
  if (bytes.length < 4 || bytes[0] !== 0xff || bytes[1] !== 0xd8) return null;
  let at = 2;
  while (at + 1 < bytes.length) {
    if (bytes[at] !== 0xff) {
      at += 1;
      continue;
    }
    const marker = bytes[at + 1];
    // Standalone markers carry no length: the next byte is another marker.
    if (marker === 0xff || marker === 0x01 || (marker >= 0xd0 && marker <= 0xd9)) {
      at += 2;
      continue;
    }
    if (at + 4 > bytes.length) return null;
    const length = (bytes[at + 2] << 8) | bytes[at + 3];
    if (length < 2) return null;
    const sof =
      marker >= 0xc0 &&
      marker <= 0xcf &&
      marker !== 0xc4 && // DHT
      marker !== 0xc8 && // JPG
      marker !== 0xcc; // DAC
    if (sof) {
      if (at + 9 > bytes.length) return null;
      const height = (bytes[at + 5] << 8) | bytes[at + 6];
      const width = (bytes[at + 7] << 8) | bytes[at + 8];
      return width > 0 && height > 0 ? { width, height } : null;
    }
    if (marker === 0xda) return null; // the scan starts: no frame header seen
    at += 2 + length;
  }
  return null;
}

const PNG_SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a] as const;

function pngSize(bytes: Uint8Array<ArrayBufferLike>): { width: number; height: number } | null {
  if (bytes.length < 24) return null;
  if (!PNG_SIGNATURE.every((byte, at) => bytes[at] === byte)) return null;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const width = view.getUint32(16);
  const height = view.getUint32(20);
  return width > 0 && height > 0 ? { width, height } : null;
}

/**
 * The art the label is made from: the Track's own front cover, or its first picture that is
 * not one of ours. Ours are never a source - a second pass would wear a wear.
 */
export function coverSource(
  pictures: readonly EmbeddablePicture[],
): EmbeddablePicture | null {
  const art = pictures.filter((picture) => !isOurs(picture) && picture.data.byteLength > 0);
  return art.find((picture) => isFrontCover(picture.type)) ?? art[0] ?? null;
}

/** The size a picture of `width x height` takes when it covers `boxWidth x boxHeight`. */
export function coverFit(
  width: number,
  height: number,
  boxWidth: number,
  boxHeight: number,
): { width: number; height: number } {
  const scale = Math.max(boxWidth / width, boxHeight / height);
  return { width: width * scale, height: height * scale };
}

type Drawing = CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D;

interface Painting {
  canvas: HTMLCanvasElement | OffscreenCanvas;
  context: Drawing;
  jpeg(): Promise<Uint8Array>;
}

/** A canvas this browser can give, and a JPEG out of it. */
function painting(width: number, height: number): Painting {
  if (typeof OffscreenCanvas === 'function') {
    const canvas = new OffscreenCanvas(width, height);
    const context = canvas.getContext('2d');
    if (context === null) throw new Error('This browser gave no 2D canvas for the label.');
    return {
      canvas,
      context,
      jpeg: async () => {
        const blob = await canvas.convertToBlob({ type: 'image/jpeg', quality: JPEG_QUALITY });
        return new Uint8Array(await blob.arrayBuffer());
      },
    };
  }

  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext('2d');
  if (context === null) throw new Error('This browser gave no 2D canvas for the label.');
  return {
    canvas,
    context,
    jpeg: () =>
      new Promise((resolve, reject) => {
        canvas.toBlob(
          (blob) => {
            if (blob === null) {
              reject(new Error('The label could not be encoded as a JPEG.'));
              return;
            }
            void blob.arrayBuffer().then(
              (buffer) => resolve(new Uint8Array(buffer)),
              reject,
            );
          },
          'image/jpeg',
          JPEG_QUALITY,
        );
      }),
  };
}

/** Paint `bitmap` over the whole box, centre-cropped. */
function paint(
  context: Drawing,
  bitmap: ImageBitmap,
  box: { width: number; height: number },
): void {
  const { width, height } = coverFit(bitmap.width, bitmap.height, box.width, box.height);
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = 'high';
  context.drawImage(bitmap, (box.width - width) / 2, (box.height - height) / 2, width, height);
}

function blobOf(data: Uint8Array<ArrayBufferLike>, type: string): Blob {
  const copy = data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) as ArrayBuffer;
  return new Blob([copy], { type });
}

/**
 * The browser's renderer: decode once, fit the art to 402x188, wear it, encode it as JPEG.
 *
 * A source this browser cannot decode throws, and the caller treats that as "no label for
 * this Track" rather than as a measurement failure - the picture is a convenience. A browser
 * that cannot run the wear shader still gets its label, unworn.
 */
export async function renderLabel(source: {
  data: Uint8Array<ArrayBufferLike>;
  mimeType: string;
}): Promise<Uint8Array> {
  const bitmap = await createImageBitmap(blobOf(source.data, source.mimeType));
  try {
    const label = painting(LABEL.width, LABEL.height);
    paint(label.context, bitmap, LABEL);
    const worn = wearLabel(label.canvas, LABEL.width, LABEL.height);
    if (worn !== null) {
      label.context.putImageData(
        new ImageData(worn, LABEL.width, LABEL.height),
        0,
        0,
      );
    }
    return await label.jpeg();
  } finally {
    bitmap.close();
  }
}

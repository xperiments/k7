import { readFile } from 'node:fs/promises';
import { describe, expect, it } from 'vitest';
import {
  COVER_DESCRIPTION,
  coverFit,
  coverSource,
  imageSize,
  LABEL,
  LABEL_DESCRIPTION,
  type EmbeddablePicture,
} from '../src/audio/covers';

/** A JPEG that carries a frame header and nothing else: enough to declare a size. */
function jpeg(width: number, height: number): Uint8Array {
  return new Uint8Array([
    0xff, 0xd8, // SOI
    0xff, 0xc0, 0x00, 0x11, 0x08, // SOF0, length 17, precision 8
    height >> 8, height & 0xff, width >> 8, width & 0xff,
    0x03, // three components
    0x01, 0x11, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00,
    0xff, 0xd9, // EOI
  ]);
}

/** A PNG signature and IHDR: the size lives at a fixed offset and no pixels follow. */
function png(width: number, height: number): Uint8Array {
  const bytes = new Uint8Array(33);
  bytes.set([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a], 0);
  const view = new DataView(bytes.buffer);
  view.setUint32(8, 13); // IHDR length
  bytes.set([0x49, 0x48, 0x44, 0x52], 12); // "IHDR"
  view.setUint32(16, width);
  view.setUint32(20, height);
  return bytes;
}

function picture(description: string, width: number, height: number): EmbeddablePicture {
  return { description, type: 'Other', mimeType: 'image/jpeg', data: jpeg(width, height) };
}

describe('reading a picture’s size', () => {
  it('reads a real JPEG’s frame header', async () => {
    const cover = new Uint8Array(
      await readFile(new URL('../../assets/labels/cover.jpg', import.meta.url)),
    );
    expect(imageSize(cover)).toEqual({ width: 800, height: 480 });
  });

  it('reads a PNG’s IHDR', () => {
    expect(imageSize(png(402, 188))).toEqual({ width: 402, height: 188 });
  });

  it('is null for bytes it cannot read, so they are regenerated rather than trusted', () => {
    expect(imageSize(new Uint8Array([1, 2, 3, 4]))).toBeNull();
    expect(imageSize(new Uint8Array())).toBeNull();
  });
});

describe('which picture the two are made from', () => {
  it('prefers the Track’s front cover', () => {
    const front = { description: '', type: 'FrontCover', data: jpeg(600, 600) };
    const leaflet = { description: '', type: 'LeafletPage', data: jpeg(600, 600) };
    expect(coverSource([leaflet, front])).toBe(front);
  });

  it('never uses the App’s own pictures as a source', () => {
    const front = { description: '', type: 'FrontCover', data: jpeg(600, 600) };
    expect(
      coverSource([
        picture(COVER_DESCRIPTION, 426, 272),
        picture(LABEL_DESCRIPTION, LABEL.width, LABEL.height),
      ]),
    ).toBeNull();
    expect(coverSource([picture(COVER_DESCRIPTION, 426, 272), front])).toBe(front);
  });
});

describe('cover fitting', () => {
  it('fills the box and crops the overflow, never stretching the picture', () => {
    // A square cover in the label: it grows until it covers the width.
    expect(coverFit(600, 600, 402, 188)).toEqual({ width: 402, height: 402 });
    // A wide picture: it grows until it covers the height.
    const wide = coverFit(1200, 400, 402, 188);
    expect(wide.width).toBeCloseTo(564, 5);
    expect(wide.height).toBe(188);
  });
});

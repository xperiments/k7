/**
 * The PWA icons, drawn here so the repository does not carry a drawing dependency.
 * Everything is rasterised at 4x and box-filtered, which is enough to keep the circles
 * from looking stepped at 192px.
 */
import { deflateSync } from 'node:zlib';
import { mkdir, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const out = join(here, '..', 'public', 'icons');

const BACKGROUND = [0x0e, 0x0f, 0x11];
const BODY = [0x17, 0x18, 0x1b];
const LINE = [0x2a, 0x2c, 0x31];
const AMBER = [0xf0, 0xb4, 0x29];
const DARK = [0x0e, 0x0f, 0x11];

function roundedRect(x, y, left, top, right, bottom, radius) {
  const cx = Math.max(left + radius, Math.min(x, right - radius));
  const cy = Math.max(top + radius, Math.min(y, bottom - radius));
  return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2;
}

function circle(x, y, cx, cy, radius) {
  return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2;
}

/** The colour at one sample point, in 0..1 coordinates. */
function sample(x, y) {
  if (circle(x, y, 0.35, 0.47, 0.105) || circle(x, y, 0.65, 0.47, 0.105)) {
    if (circle(x, y, 0.35, 0.47, 0.034) || circle(x, y, 0.65, 0.47, 0.034)) return DARK;
    return AMBER;
  }
  if (roundedRect(x, y, 0.22, 0.62, 0.78, 0.75, 0.025)) return DARK;
  if (roundedRect(x, y, 0.1, 0.18, 0.9, 0.82, 0.07)) {
    return roundedRect(x, y, 0.115, 0.195, 0.885, 0.805, 0.055) ? BODY : LINE;
  }
  return BACKGROUND;
}

/**
 * A launcher may crop a maskable icon to any shape inside the safe circle, which is 80% of
 * the canvas, so the artwork has to sit inside it. The cassette's bounding box is 0.8 x 0.64
 * of the canvas, putting its corners 0.512 from the centre; 0.75 brings that to 0.384,
 * inside the 0.4 radius, and what the shrink leaves around it is background (A9).
 */
const MASKABLE_SHRINK = 0.75;

/** One canvas point. `shrink` pulls the artwork towards the centre, background outside. */
function colourAt(x, y, shrink) {
  if (shrink === 1) return sample(x, y);
  return sample((x - 0.5) / shrink + 0.5, (y - 0.5) / shrink + 0.5);
}

function raster(size, shrink) {
  const scale = 4;
  const rgba = Buffer.alloc(size * size * 4);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      let r = 0;
      let g = 0;
      let b = 0;
      for (let sy = 0; sy < scale; sy++) {
        for (let sx = 0; sx < scale; sx++) {
          const colour = colourAt(
            (x + (sx + 0.5) / scale) / size,
            (y + (sy + 0.5) / scale) / size,
            shrink,
          );
          r += colour[0];
          g += colour[1];
          b += colour[2];
        }
      }
      const i = (y * size + x) * 4;
      rgba[i] = Math.round(r / scale ** 2);
      rgba[i + 1] = Math.round(g / scale ** 2);
      rgba[i + 2] = Math.round(b / scale ** 2);
      rgba[i + 3] = 255;
    }
  }
  return rgba;
}

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

function crc32(buffer) {
  let crc = 0xffffffff;
  for (const byte of buffer) crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length, 0);
  const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body), 0);
  return Buffer.concat([length, body, crc]);
}

function png(size, shrink) {
  const pixels = raster(size, shrink);
  const raw = Buffer.alloc((size * 4 + 1) * size);
  for (let y = 0; y < size; y++) {
    raw[y * (size * 4 + 1)] = 0; // filter: none
    pixels.copy(raw, y * (size * 4 + 1) + 1, y * size * 4, (y + 1) * size * 4);
  }

  const header = Buffer.alloc(13);
  header.writeUInt32BE(size, 0);
  header.writeUInt32BE(size, 4);
  header[8] = 8; // bit depth
  header[9] = 6; // RGBA
  header[10] = 0;
  header[11] = 0;
  header[12] = 0;

  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

const ICONS = [
  { name: 'icon-192.png', size: 192, shrink: 1 },
  { name: 'icon-512.png', size: 512, shrink: 1 },
  // The same drawing, shrunk: an Android launcher crops the maskable one, and the cassette
  // must survive the crop.
  { name: 'icon-512-maskable.png', size: 512, shrink: MASKABLE_SHRINK },
];

await mkdir(out, { recursive: true });
for (const { name, size, shrink } of ICONS) {
  await writeFile(join(out, name), png(size, shrink));
  console.log(`icons: ${name}`);
}

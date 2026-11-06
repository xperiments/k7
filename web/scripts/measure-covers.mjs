// Measure the covers embedded in a card's music directory: how many are distinct, how
// large they are at source, and (with ffmpeg) what a 422x422 AVIF copy would weigh.
//
//   node web/scripts/measure-covers.mjs /Volumes/CARD/Music /path/to/out
//
// The App's selection rule is reproduced: a picture whose description is k7label or
// k7cover is the App's own, never the source; otherwise the front cover, else the first.
import { createHash } from 'node:crypto';
import { mkdir, readFile, readdir, stat, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { parseFile } from 'music-metadata';

const root = process.argv[2];
const out = process.argv[3] ?? path.join(os.tmpdir(), 'k7-covers-measure');
if (!root) {
  console.error('usage: measure-covers.mjs <Music dir> [out dir]');
  process.exit(2);
}
const AUDIO = new Set(['.mp3', '.flac', '.m4a', '.ogg', '.opus', '.wav', '.aac']);

async function walk(dir) {
  const out = [];
  for (const e of await readdir(dir, { withFileTypes: true })) {
    if (e.name === '.DS_Store') continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) out.push(...(await walk(p)));
    else if (AUDIO.has(path.extname(e.name).toLowerCase())) out.push(p);
  }
  return out;
}

function imageSize(bytes) {
  if (bytes.length >= 4 && bytes[0] === 0xff && bytes[1] === 0xd8) {
    let at = 2;
    while (at + 1 < bytes.length) {
      if (bytes[at] !== 0xff) { at += 1; continue; }
      const marker = bytes[at + 1];
      if (marker === 0xff || marker === 0x01 || (marker >= 0xd0 && marker <= 0xd9)) { at += 2; continue; }
      const len = (bytes[at + 2] << 8) | bytes[at + 3];
      if (len < 2) return null;
      if (marker >= 0xc0 && marker <= 0xcf && marker !== 0xc4 && marker !== 0xc8 && marker !== 0xcc) {
        return { width: (bytes[at + 7] << 8) | bytes[at + 8], height: (bytes[at + 5] << 8) | bytes[at + 6] };
      }
      if (marker === 0xda) return null;
      at += 2 + len;
    }
    return null;
  }
  if (bytes.length >= 24 && bytes[0] === 0x89 && bytes[1] === 0x50 && bytes[2] === 0x4e && bytes[3] === 0x47) {
    const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    return { width: v.getUint32(16), height: v.getUint32(20) };
  }
  return null;
}

function choose(pictures) {
  const ours = (p) => p.description === 'k7label' || p.description === 'k7cover';
  const art = pictures.filter((p) => !ours(p) && p.data?.byteLength);
  return art.find((p) => (p.type ?? '').toLowerCase().includes('front')) ?? art[0] ?? null;
}

const files = await walk(root);
const byHash = new Map();
let withCover = 0, selected = 0, allBytes = 0, uniqueBytes = 0, oursSeen = 0;
let totalWidth = 0, totalHeight = 0;
for (let i = 0; i < files.length; i++) {
  let meta;
  try { meta = await parseFile(files[i], { skipCovers: false }); } catch { continue; }
  const pictures = meta.common.picture ?? [];
  if (pictures.length) withCover++;
  oursSeen += pictures.filter((p) => p.description === 'k7label' || p.description === 'k7cover').length;
  const pic = choose(pictures);
  if (!pic) continue;
  selected++;
  const data = Buffer.from(pic.data);
  allBytes += data.length;
  const hash = createHash('sha256').update(data).digest('hex');
  if (!byHash.has(hash)) {
    byHash.set(hash, { hash, data, mime: pic.mimeType ?? pic.format ?? 'image/jpeg', width: 0, height: 0 });
    uniqueBytes += data.length;
    const d = imageSize(data);
    if (d) { totalWidth += d.width; totalHeight += d.height; }
  }
  if ((i + 1) % 250 === 0) process.stderr.write(`  ${i + 1}/${files.length}\n`);
}

const uniq = [...byHash.values()];
for (const u of uniq) {
  const d = imageSize(u.data);
  if (d) { u.width = d.width; u.height = d.height; }
}
await mkdir(path.join(out, 'unique'), { recursive: true });
for (const u of uniq) {
  const ext = u.mime.includes('png') ? '.png' : u.mime.includes('webp') ? '.webp' : '.jpg';
  await writeFile(path.join(out, 'unique', u.hash + ext), u.data);
}
const byDim = uniq.reduce((m, u) => { const k = `${u.width}x${u.height}`; m[k] = (m[k] ?? 0) + 1; return m; }, {});
const largest = uniq.sort((a, b) => b.data.length - a.data.length).slice(0, 8).map((u) => ({ hash: u.hash.slice(0, 12), bytes: u.data.length, w: u.width, h: u.height, mime: u.mime }));
const report = {
  root, files: files.length, withCover, selected, unique: uniq.length, oursSeen,
  perTrackBytes: allBytes, uniqueSourceBytes: uniqueBytes,
  avgSourcePerTrack: selected ? Math.round(allBytes / selected) : 0,
  avgUniqueSource: uniq.length ? Math.round(uniqueBytes / uniq.length) : 0,
  dims: byDim, largest,
};
await writeFile(path.join(out, 'source.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2));

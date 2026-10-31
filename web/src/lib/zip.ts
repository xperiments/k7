/**
 * A ZIP with no compression, because there is nothing to compress.
 *
 * The only archive this app builds is a folder of album covers, and those are JPEG or PNG
 * — already compressed, so STORE is both the smallest useful output and the simplest one.
 * Writing it here rather than adding a dependency keeps the shell's rule intact: every
 * kilobyte in the partition is a kilobyte of ours.
 *
 * The timestamps are fixed at 1980-01-01 so the same input produces the same archive, and a
 * test can compare bytes rather than dates.
 */
const SIG_LOCAL = 0x04034b50;
const SIG_CENTRAL = 0x02014b50;
const SIG_END = 0x06054b50;
/** General-purpose flag bit 11: names are UTF-8, which every unarchiver reads. */
const UTF8 = 0x0800;
const DOS_DATE = 0x0021; // 1980-01-01

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let byte = 0; byte < 256; byte += 1) {
    let value = byte;
    for (let bit = 0; bit < 8; bit += 1) {
      value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
    }
    table[byte] = value >>> 0;
  }
  return table;
})();

export function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

export interface ZipEntry {
  /** Path inside the archive, with forward slashes. */
  path: string;
  bytes: Uint8Array;
}

export function zipStore(entries: readonly ZipEntry[]): Uint8Array {
  const encoder = new TextEncoder();
  const names = entries.map((entry) => encoder.encode(entry.path));
  const crcs = entries.map((entry) => crc32(entry.bytes));

  const localSize = entries.reduce((sum, entry, i) => sum + 30 + names[i].length + entry.bytes.length, 0);
  const centralSize = entries.reduce((sum, _entry, i) => sum + 46 + names[i].length, 0);
  const out = new Uint8Array(localSize + centralSize + 22);
  const view = new DataView(out.buffer);
  let at = 0;

  const offsets: number[] = [];
  entries.forEach((entry, i) => {
    offsets.push(at);
    view.setUint32(at, SIG_LOCAL, true);
    view.setUint16(at + 4, 20, true);
    view.setUint16(at + 6, UTF8, true);
    view.setUint16(at + 8, 0, true); // STORE
    view.setUint16(at + 10, 0, true); // time
    view.setUint16(at + 12, DOS_DATE, true);
    view.setUint32(at + 14, crcs[i], true);
    view.setUint32(at + 18, entry.bytes.length, true);
    view.setUint32(at + 22, entry.bytes.length, true);
    view.setUint16(at + 26, names[i].length, true);
    view.setUint16(at + 28, 0, true);
    at += 30;
    out.set(names[i], at);
    at += names[i].length;
    out.set(entry.bytes, at);
    at += entry.bytes.length;
  });

  const centralStart = at;
  entries.forEach((entry, i) => {
    view.setUint32(at, SIG_CENTRAL, true);
    view.setUint16(at + 4, 20, true); // made by
    view.setUint16(at + 6, 20, true); // needed
    view.setUint16(at + 8, UTF8, true);
    view.setUint16(at + 10, 0, true);
    view.setUint16(at + 12, 0, true);
    view.setUint16(at + 14, DOS_DATE, true);
    view.setUint32(at + 16, crcs[i], true);
    view.setUint32(at + 20, entry.bytes.length, true);
    view.setUint32(at + 24, entry.bytes.length, true);
    view.setUint16(at + 28, names[i].length, true);
    view.setUint16(at + 30, 0, true); // extra
    view.setUint16(at + 32, 0, true); // comment
    view.setUint16(at + 34, 0, true); // disk
    view.setUint16(at + 36, 0, true); // internal attrs
    view.setUint32(at + 38, 0, true); // external attrs
    view.setUint32(at + 42, offsets[i], true);
    at += 46;
    out.set(names[i], at);
    at += names[i].length;
  });

  const centralSizeActual = at - centralStart;
  view.setUint32(at, SIG_END, true);
  view.setUint16(at + 4, 0, true);
  view.setUint16(at + 6, 0, true);
  view.setUint16(at + 8, entries.length, true);
  view.setUint16(at + 10, entries.length, true);
  view.setUint32(at + 12, centralSizeActual, true);
  view.setUint32(at + 16, centralStart, true);
  view.setUint16(at + 20, 0, true);

  void centralSize;
  return out;
}

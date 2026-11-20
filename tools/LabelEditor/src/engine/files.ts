import type { Editor } from "./editor";
import { tplText } from "./tpl";

import { crc32 } from "./crc";

export interface ZipEntry {
  name: string;
  bytes: Uint8Array;
}

export function makeZip(entries: ZipEntry[]): Blob {
  const enc = new TextEncoder();
  const locals: Uint8Array[] = [];
  const centrals: Uint8Array[] = [];
  let off = 0;
  for (const e of entries) {
    const nm = enc.encode(e.name);
    const crc = crc32(e.bytes);
    const size = e.bytes.length;
    const lh = new DataView(new ArrayBuffer(30));
    lh.setUint32(0, 0x04034b50, true);
    lh.setUint16(4, 20, true);
    lh.setUint16(6, 0x0800, true);
    lh.setUint16(8, 0, true);
    lh.setUint16(10, 0, true);
    lh.setUint16(12, 0x21, true);
    lh.setUint32(14, crc, true);
    lh.setUint32(18, size, true);
    lh.setUint32(22, size, true);
    lh.setUint16(26, nm.length, true);
    lh.setUint16(28, 0, true);
    locals.push(new Uint8Array(lh.buffer), nm, e.bytes);
    const ch = new DataView(new ArrayBuffer(46));
    ch.setUint32(0, 0x02014b50, true);
    ch.setUint16(4, 20, true);
    ch.setUint16(6, 20, true);
    ch.setUint16(8, 0x0800, true);
    ch.setUint16(10, 0, true);
    ch.setUint16(12, 0, true);
    ch.setUint16(14, 0x21, true);
    ch.setUint32(16, crc, true);
    ch.setUint32(20, size, true);
    ch.setUint32(24, size, true);
    ch.setUint16(28, nm.length, true);
    ch.setUint32(42, off, true);
    centrals.push(new Uint8Array(ch.buffer), nm);
    off += 30 + nm.length + size;
  }
  const cdSize = centrals.reduce((n, a) => n + a.length, 0);
  const eocd = new DataView(new ArrayBuffer(22));
  eocd.setUint32(0, 0x06054b50, true);
  eocd.setUint16(8, entries.length, true);
  eocd.setUint16(10, entries.length, true);
  eocd.setUint32(12, cdSize, true);
  eocd.setUint32(16, off, true);
  return new Blob([...locals, ...centrals, new Uint8Array(eocd.buffer)] as unknown as BlobPart[], {
    type: "application/zip",
  });
}

/** Save a blob as a file. The browser's own download; the tool is a local page. */
export function downloadBlob(filename: string, blob: Blob): boolean {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  a.rel = "noopener";
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 4000);
  return true;
}

export interface ExportResult {
  blob: Blob | null;
  filenames: string[];
  errors: string[];
}

/**
 * Export every open design: its K7Art JSON, a PNG preview and labels.json in one zip. Refused
 * as a whole if any design has errors, because a half-packed sheet is worse than none.
 */
export async function exportAll(editor: Editor): Promise<ExportResult> {
  const back = editor.active;
  const { built, errs } = editor.buildAll();
  if (errs.length) {
    editor.switchDoc(back);
    return { blob: null, filenames: [], errors: errs };
  }
  const entries: ZipEntry[] = [];
  const filenames: string[] = [];
  for (const b of built) {
    editor.switchDoc(b.i);
    const png = await editor.previewPng();
    if (!png) {
      editor.switchDoc(back);
      return {
        blob: null,
        filenames: [],
        errors: [
          "The LVGL preview is not loaded yet, so the PNG previews cannot be made. Try again in a moment.",
        ],
      };
    }
    entries.push({
      name: `${b.doc.id}.json`,
      bytes: new TextEncoder().encode(tplText(b.doc)),
    });
    entries.push({
      name: `${b.doc.id.replace(/^k7\./, "")}.png`,
      bytes: new Uint8Array(await png.arrayBuffer()),
    });
    filenames.push(`${b.doc.id}.json`);
  }
  const sorted = [...built].sort((a, b) => (a.doc.id < b.doc.id ? -1 : a.doc.id > b.doc.id ? 1 : 0));
  const manifest = {
    v: 1,
    templates: sorted.map((b) => ({
      id: b.doc.id,
      file: `${b.doc.id}.json`,
      preview: `${b.doc.id.replace(/^k7\./, "")}.png`,
    })),
  };
  entries.push({
    name: "labels.json",
    bytes: new TextEncoder().encode(JSON.stringify(manifest, null, 2) + "\n"),
  });
  filenames.push("labels.json");
  editor.switchDoc(back);
  return {
    blob: makeZip(entries),
    filenames: [`k7art.zip`, ...filenames],
    errors: [],
  };
}

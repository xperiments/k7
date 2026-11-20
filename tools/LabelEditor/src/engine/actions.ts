import type { Editor } from "./editor";
import { downloadBlob } from "./files";
import { buildArtPack, PackError, type ArtPack, type PackMeta } from "./labelPack";
import { DEFAULT_CANVAS } from "./tpl";
import { PRINT_H, PRINT_W } from "./vocab";

/** The document as the editor's own file: reopens here, and is not a label template. */
export function saveDocument(ed: Editor): string {
  const body = {
    v: 1,
    w: PRINT_W,
    h: PRINT_H,
    bg: ed.bg,
    bgs: ed.bgs,
    bgi: ed.bgi,
    objs: ed.objs,
    groups: ed.groups,
    mask: ed.mask,
    tpl: ed.tpl,
    jcard: ed.jcard,
  };
  const name = `${ed.doc?.name ?? "Untitled"}.lvgl.json`;
  downloadBlob(
    name,
    new Blob([JSON.stringify(body, null, 1)], { type: "application/json" }),
  );
  return name;
}

/** One design as a K7Art v1; it refuses what the packer and the App would refuse. */
export function exportTemplate(ed: Editor): { name?: string; errs: string[] } {
  const r = ed.buildTemplate();
  if (r.errs.length) return { errs: r.errs };
  const name = `${r.doc.id}.json`;
  downloadBlob(name, new Blob([ed.templateText()], { type: "application/json" }));
  return { name, errs: [] };
}

/**
 * Publish a `.k7artpack` from a subset of the open designs (docs/specs/art-packs.md §3).
 * The pack is built in the browser; the editor has no device client and downloads the file
 * for the App's Art packs page.
 */
export function publishPack(
  ed: Editor,
  indices: number[],
  meta: PackMeta,
): { pack?: ArtPack; errs: string[] } {
  const built = ed.buildSelected(indices);
  if (built.errs.length) return { errs: built.errs };
  try {
    const pack = buildArtPack(
      [...built.built].sort((a, b) => a.i - b.i).map((b) => b.doc),
      meta,
    );
    return { pack, errs: [] };
  } catch (err) {
    if (err instanceof PackError) return { errs: err.errors };
    throw err;
  }
}

/** Download a published pack and return its file name. */
export function downloadPack(pack: ArtPack): string {
  downloadBlob(
    pack.filename,
    new Blob([pack.bytes as unknown as BlobPart], { type: "application/octet-stream" }),
  );
  return pack.filename;
}

/**
 * Open a K7Art, a retired `label-template v1` (migrated into a K7Art) or an editor document
 * into a new tab. The file's shape decides: a container names `format`; a retired template is
 * `v` with `prims` beside it; an editor document has `objs`.
 */
export async function openFile(ed: Editor, file: File): Promise<string> {
  const txt = await file.text();
  const d = JSON.parse(txt) as {
    format?: string;
    objs?: import("./types").Obj[];
    prims?: unknown[];
    w?: number;
    h?: number;
    bg?: string;
    bgs?: string[];
    bgi?: number;
    groups?: import("./types").Group[];
    mask?: import("./types").Mask | null;
    tpl?: import("./types").Tpl | null;
    jcard?: import("./types").JCardDoc | null;
  };
  const k7art = d.format === "k7art";
  if (k7art || Array.isArray(d.prims)) {
    const back = ed.active;
    ed.newDoc(undefined, file.name.replace(/\.json$/, ""));
    try {
      ed.importTemplate(txt);
    } catch (err) {
      ed.closeDoc(ed.active);
      if (back !== ed.active) ed.switchDoc(back);
      throw err;
    }
    return k7art ? "K7Art opened" : "Label template migrated to a K7Art";
  }
  if (!Array.isArray(d.objs)) throw new Error("not a K7Art, a label template or an editor document");
  const objs = d.objs;
  ed.newDoc(
    {
      w: d.w ?? PRINT_W,
      h: d.h ?? PRINT_H,
      bg: d.bg || DEFAULT_CANVAS,
      bgs: d.bgs,
      bgi: d.bgi,
      objs,
      groups: d.groups || [],
      mask: d.mask ?? null,
      nid: Math.max(0, ...objs.map((o) => o.id)),
      tpl: d.tpl ?? undefined,
      jcard: d.jcard ?? undefined,
    },
    file.name.replace(/\.lvgl\.json$|\.json$/, ""),
  );
  return "Document opened";
}

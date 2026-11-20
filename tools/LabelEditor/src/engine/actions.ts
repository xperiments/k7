import type { Editor } from "./editor";
import { downloadBlob } from "./files";
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
  };
  const name = `${ed.doc?.name ?? "Untitled"}.lvgl.json`;
  downloadBlob(
    name,
    new Blob([JSON.stringify(body, null, 1)], { type: "application/json" }),
  );
  return name;
}

/** One design as `label-template v1`; it refuses what the packer would refuse. */
export function exportTemplate(ed: Editor): { name?: string; errs: string[] } {
  const r = ed.buildTemplate();
  if (r.errs.length) return { errs: r.errs };
  const name = `${r.doc.id}.json`;
  downloadBlob(name, new Blob([ed.templateText()], { type: "application/json" }));
  return { name, errs: [] };
}

/** Open a `.lvgl.json` document or a `label-template v1` file into a new tab. */
export async function openFile(ed: Editor, file: File): Promise<string> {
  const txt = await file.text();
  const d = JSON.parse(txt) as {
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
  };
  if (Array.isArray(d.prims)) {
    const back = ed.active;
    ed.newDoc(undefined, file.name.replace(/\.json$/, ""));
    try {
      ed.importTemplate(txt);
    } catch (err) {
      ed.closeDoc(ed.active);
      if (back !== ed.active) ed.switchDoc(back);
      throw err;
    }
    return "Template imported";
  }
  if (!Array.isArray(d.objs)) throw new Error("not a layout or a template");
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
    },
    file.name.replace(/\.lvgl\.json$|\.json$/, ""),
  );
  return "Document opened";
}

import {
  COORDS,
  DEFAULT_BANDS,
  FROLES,
  LIMITS,
  PRINT_H,
  PRINT_W,
  ROLES,
  SHEET_FONTS,
  SLOT_CEILING,
  TEXT_ROLES,
  WIN,
  onSheet,
  slotOf,
} from "./vocab";
import { VOCAB } from "./vocab.generated";
import type { CanvasDoc, FontRole, Group, Mask, Obj, Tpl } from "./types";

/** The panel's white page at the two-finger pose (`k_col_white`): a new design starts here. */
export const DEFAULT_CANVAS = "#909090";

const HEX = /^#[0-9a-f]{6}$/i;

/**
 * The canvas a document has: its presets and the active one. A document made before the
 * presets existed carries one `bg`; that becomes its one preset.
 */
export function canvasOf(data: { bg?: string; bgs?: string[]; bgi?: number }): CanvasDoc {
  const backgrounds =
    Array.isArray(data.bgs) && data.bgs.length ? data.bgs.slice() : [data.bg || DEFAULT_CANVAS];
  const active = Number.isInteger(data.bgi)
    ? Math.min(Math.max(0, data.bgi!), backgrounds.length - 1)
    : 0;
  return { backgrounds, active };
}

/** The design a document starts from: its id, name, inks, font roles, slots and sims. */
export function defaultTpl(): Tpl {
  return {
    id: "label.untitled",
    name: "Untitled",
    inks: {
      ink: "#151515",
      band: DEFAULT_BANDS.slice(0, VOCAB.bands),
      rule: "#8e7e4a",
    },
    fonts: JSON.parse(JSON.stringify(VOCAB.defaults.fonts)) as Tpl["fonts"],
    slots: {},
    sim: {},
  };
}

export function alignOf(o: Obj): "left" | "center" | "right" {
  return o.align === "right" || o.align === "center" ? o.align : "left";
}

export const roleColorOf = (r: string, mask: Mask | null, tpl: Tpl) =>
  r === "paper"
    ? mask
      ? mask.color
      : "#ffffff"
    : r === "ink"
      ? tpl.inks.ink
      : r === "rule"
        ? tpl.inks.rule
        : /^band\.[0-7]$/.test(r || "")
          ? tpl.inks.band[+r.slice(5)]
          : null;

/** A text's preview in the editor: its test value, else its Slot's default. */
export const simText = (o: Obj, tpl: Tpl) =>
  o.static
    ? (o.text ?? "")
    : o.sim !== undefined && o.sim !== ""
      ? o.sim
      : (tpl.sim[o.name] ?? slotOf(o.name)?.sim ?? "");

/**
 * A document made before the font roles were set keeps the old Montserrat defaults: move
 * them to the current ones. A role whose face and size are not a pair the sheet carries
 * cannot be packed, so it is reset to the role's default here instead of being refused at
 * Export.
 */
export function migrateFonts(tpl: Tpl): void {
  if (tpl && !tpl.sim) tpl.sim = {};
  const f = tpl && tpl.fonts;
  if (!f) return;
  if (FROLES.every((k) => f[k] && f[k].ff === "")) tpl.fonts = defaultTpl().fonts;
  const d = defaultTpl().fonts;
  for (const k of FROLES) {
    const r = tpl.fonts[k] || ({} as { ff: string; px: number });
    if (!onSheet(r.ff, r.px || 0)) tpl.fonts[k] = { ...d[k] };
  }
}

/**
 * A document saved before the palette had eight bands carries four: pad the rest from the
 * current default so the template panel has a colour for every control.
 */
export function migrateBands(tpl: Tpl): void {
  if (!tpl || !tpl.inks) return;
  const d = defaultTpl().inks.band;
  const b = Array.isArray(tpl.inks.band) ? tpl.inks.band.slice(0, 8) : [];
  while (b.length < 8) b.push(d[b.length]);
  tpl.inks.band = b;
}

/** The role of a colour, if it is exactly one of the template's role colours. */
export function roleOfColour(
  c: string | undefined,
  mask: Mask | null,
  tpl: Tpl,
): string | null {
  c = (c || "").toLowerCase();
  if (!c) return null;
  if (mask && mask.color.toLowerCase() === c) return "paper";
  if (tpl.inks.ink.toLowerCase() === c) return "ink";
  if (tpl.inks.rule.toLowerCase() === c) return "rule";
  const i = tpl.inks.band.findIndex((b) => b.toLowerCase() === c);
  return i >= 0 ? `band.${i}` : null;
}

/** Layers whose own colour is already a role's colour take that role. */
export function nearestRole(hex: string, mask: Mask | null, tpl: Tpl): string {
  const rgb = (h: string) => {
    const m = /^#?([0-9a-f]{6})$/i.exec(h || "");
    if (!m) return null;
    const n = parseInt(m[1], 16);
    return [(n >> 16) & 255, (n >> 8) & 255, n & 255] as [number, number, number];
  };
  const c = rgb(hex);
  if (!c) return "band.0";
  let best = "band.0";
  let bd = Infinity;
  ROLES.forEach((r) => {
    const rc = rgb(roleColorOf(r, mask, tpl) ?? "");
    if (!rc) return;
    const d = (rc[0] - c[0]) ** 2 + (rc[1] - c[1]) ** 2 + (rc[2] - c[2]) ** 2;
    if (d < bd) {
      bd = d;
      best = r;
    }
  });
  return best;
}

/** Text roles and shape roles differ: a text prints in the ink or a band, never paper. */
export function normalizeRoles(objs: Obj[], mask: Mask | null, tpl: Tpl): void {
  objs.forEach((o) => {
    if (o.ap) return;
    if (o.type === "text") {
      if (!TEXT_ROLES.includes(o.role ?? "")) o.role = "ink";
      if (o.s && o.s !== 1) {
        o.w = Math.round(o.w * o.s);
        delete o.s;
      }
      return;
    }
    if (!ROLES.includes(o.role ?? "")) o.role = nearestRole(o.color ?? "", mask, tpl);
  });
}

/** Slots follow the text layers: a named text declares its slot, an unused slot is dropped. */
export function syncSlots(objs: Obj[], tpl: Tpl): void {
  if (!tpl) return;
  const used: Record<string, 1> = {};
  objs.forEach((o) => {
    if (o.type === "text" && !o.static && slotOf(o.name)) {
      used[o.name] = 1;
      tpl.slots[o.name] = { max: SLOT_CEILING };
    }
  });
  Object.keys(tpl.slots).forEach((k) => {
    if (!used[k]) delete tpl.slots[k];
  });
}

/** The aperture's cut-out: the paper with the Card's window removed (even-odd). */
export function aperturePath(ap: Obj): string {
  const x = ap.x;
  const y = ap.y;
  const w = ap.w;
  const h = ap.h ?? 0;
  const r = Math.min(ap.r || 0, w / 2, h / 2);
  return `M0 0H${PRINT_W}V${PRINT_H}H0Z M${x + r} ${y}H${x + w - r}A${r} ${r} 0 0 1 ${x + w} ${y + r}V${y + h - r}A${r} ${r} 0 0 1 ${x + w - r} ${y + h}H${x + r}A${r} ${r} 0 0 1 ${x} ${y + h - r}V${y + r}A${r} ${r} 0 0 1 ${x + r} ${y}Z`;
}

export interface TemplateDoc {
  v: 1;
  id: string;
  name: string;
  space: { w: number; h: number };
  aperture?: { x: number; y: number; w: number; h: number; r: number };
  canvas?: CanvasDoc;
  slots: Record<string, { max: number }>;
  inks: { paper: string | null; ink: string; rule: string; band: string[] };
  fonts: Record<FontRole, { ff: string; px: number }>;
  notes?: string[];
  prims: Array<Record<string, unknown>>;
}

/**
 * The template JSON (label-template v1) and its validation, from the document. The
 * document is the model; this is one view of it, and the packer is the other.
 */
export function buildTemplate(
  objs: Obj[],
  mask: Mask | null,
  tpl: Tpl,
  canvas: CanvasDoc = { backgrounds: [DEFAULT_CANVAS], active: 0 },
): { doc: TemplateDoc; errs: string[] } {
  // The slots are the text layers' own list: settle it before the file is read.
  syncSlots(objs, tpl);
  const errs: string[] = [];
  const W0 = PRINT_W;
  const H0 = PRINT_H;
  const ap = objs.find((o) => o.ap && o.type === "rect") || null;
  const paperOn = !!mask;
  const prims: Array<Record<string, unknown>> = [];
  const seen: Record<string, 1> = {};

  const tagOf = (o: Obj) =>
    o.type === "text"
      ? o.static
        ? `static text "${(o.text || "").slice(0, 24)}"`
        : o.name
          ? `dynamic text (slot "${o.name}")`
          : "dynamic text"
      : o.label || `${o.type} ${o.id}`;

  if (!/^label\.[a-z0-9][a-z0-9-]*$/.test(tpl.id))
    errs.push(`id "${tpl.id}" must look like label.my-name`);
  if (ap && !paperOn) errs.push("aperture: needs a LayerMask (the paper it cuts)");

  // The aperture is the Card's window, not the design's: the mask already cuts it, so the
  // file either omits it or names exactly that window.
  const AB = ap
    ? {
        x: Math.round(ap.x),
        y: Math.round(ap.y),
        w: Math.round(ap.w),
        h: Math.round(ap.h || 0),
      }
    : null;
  if (
    AB &&
    (AB.x !== WIN.x ||
      AB.y !== WIN.y ||
      AB.w !== WIN.w ||
      AB.h !== WIN.h ||
      Math.round(ap!.r || 0) !== WIN.r)
  )
    errs.push(
      `aperture: the Card's window is ${WIN.x},${WIN.y} ${WIN.w}x${WIN.h} r${WIN.r}, not ${AB.x},${AB.y} ${AB.w}x${AB.h} r${Math.round(ap!.r || 0)}`,
    );

  // The canvas presets are the editor's; the active colour is what the sheet carries. Check
  // both here, so a design that cannot be packed says so at Export rather than at the build.
  const bgs = Array.isArray(canvas?.backgrounds) ? canvas.backgrounds : [];
  if (bgs.length < 1 || bgs.length > VOCAB.canvas)
    errs.push(
      `canvas: ${bgs.length} backgrounds, and a design carries one to ${VOCAB.canvas}`,
    );
  bgs.forEach((c, i) => {
    if (!HEX.test(c || "")) errs.push(`canvas.backgrounds[${i}] "${c}" is not #RRGGBB`);
  });
  const canvasActive = canvas?.active ?? 0;
  if (!Number.isInteger(canvasActive) || canvasActive < 0 || canvasActive >= bgs.length)
    errs.push(
      `canvas.active ${canvasActive} is not an index into ${bgs.length} backgrounds`,
    );

  // A role's face and size are the design's, but only the sheet's pairs exist as compiled fonts.
  FROLES.forEach((k) => {
    const f = tpl.fonts[k] || ({} as { ff: string; px: number });
    if (!onSheet(f.ff, f.px || 0))
      errs.push(
        `font role ${k} is "${f.ff || "(none)"}" ${f.px || 0} px, and assets/labels/vocabulary.json carries ${SHEET_FONTS.map(([ff, px]) => `${ff} ${px}`).join(", ")}`,
      );
  });

  if (paperOn)
    prims.push({
      t: "rect",
      x: 0,
      y: 0,
      w: W0,
      h: H0,
      r: Math.round(mask!.radius || 0),
      fill: "paper",
      still: true,
    });

  objs.forEach((o) => {
    if (o.ap) return;
    const tag = tagOf(o);
    const X = Math.round(o.x);
    const Y = Math.round(o.y);
    const Wd = Math.round(o.w);
    const Ht = Math.round(o.h || 0);
    // The record's own bounds: x and y are int16, w and h uint16, so a value the sheet
    // cannot hold is refused here rather than at the build.
    if (X < COORDS.x[0] || X > COORDS.x[1] || Y < COORDS.y[0] || Y > COORDS.y[1])
      errs.push(
        `${tag}: sits at ${X},${Y}, outside the record's ${COORDS.x[0]}..${COORDS.x[1]} by ${COORDS.y[0]}..${COORDS.y[1]}`,
      );
    if (
      Wd < COORDS.w[0] ||
      Wd > COORDS.w[1] ||
      (o.type !== "text" && (Ht < COORDS.h[0] || Ht > COORDS.h[1]))
    )
      errs.push(
        `${tag}: is ${Wd}x${Ht}, and the record holds w up to ${COORDS.w[1]} and h up to ${COORDS.h[1]}`,
      );

    if (o.type === "text") {
      if (o.static) {
        if (!(o.text || "").trim()) errs.push(`${tag}: static text is empty`);
      } else if (!o.name)
        errs.push(`${tag}: dynamic text needs a slot name (choose a field from the list)`);
      else if (!slotOf(o.name)) errs.push(`${tag}: "${o.name}" is not on the list of fields`);
      else if (!tpl.slots[o.name]) errs.push(`${tag}: slot "${o.name}" is not declared`);
      else seen[o.name] = 1;
      if (o.fr !== undefined && !FROLES.includes(o.fr))
        errs.push(`${tag}: font role must be title, numeral or caption`);
      if (o.s && o.s !== 1) errs.push(`${tag}: scaled text is not in template v1`);
      const pr: Record<string, unknown> = o.static
        ? { t: "text", x: X, y: Y, w: Wd }
        : { t: "text", slot: o.name || "", x: X, y: Y, w: Wd };
      if (o.rot) pr.rot = Math.round(o.rot);
      pr.font = FROLES.includes(o.fr as FontRole) ? o.fr : "caption";
      if (!TEXT_ROLES.includes(o.role ?? ""))
        errs.push(`${tag}: text colour must be ink or one of the eight bands`);
      pr.color = TEXT_ROLES.includes(o.role ?? "") ? o.role : "ink";
      pr.align = alignOf(o);
      if (o.static) pr.text = o.text || "";
      else pr.preview = simText(o, tpl);
      prims.push(pr);
    } else {
      const pr: Record<string, unknown> = { t: "rect", x: X, y: Y, w: Wd, h: Ht };
      if (o.type === "circle") pr.r = Math.floor(Math.min(Wd, Ht) / 2);
      else if (o.r) pr.r = Math.round(o.r);
      if (o.rot) pr.rot = Math.round(o.rot);
      if (o.role) {
        if (!ROLES.includes(o.role)) errs.push(`${tag}: unknown role ${o.role}`);
        pr.fill = o.role;
      } else errs.push(`${tag}: choose a role (paper, band.0-7 or rule)`);
      pr.still = true;
      // A rect may bleed past the print: the print crops it. Only a record the sheet cannot
      // hold is refused, and that is the packer's check.
      if (
        AB &&
        X <= AB.x &&
        Y <= AB.y &&
        X + Wd >= AB.x + AB.w &&
        Y + Ht >= AB.y + AB.h
      )
        errs.push(`${tag}: its box covers the whole aperture`);
      prims.push(pr);
    }
  });

  const noteList: string[] = [];

  Object.keys(tpl.slots).forEach((k) => {
    if (!seen[k]) errs.push(`slot "${k}" is declared but no dynamic text uses it`);
  });

  // The sheet's caps, so a design that is too big says so at Export instead of failing the
  // firmware build. They are the vocabulary's, not numbers written twice.
  if (prims.length > LIMITS.prims)
    errs.push(`${prims.length} layers, and the sheet holds ${LIMITS.prims} per design`);
  if (Object.keys(tpl.slots).length > LIMITS.slots)
    errs.push(
      `${Object.keys(tpl.slots).length} fields, and the sheet holds ${LIMITS.slots} per design`,
    );

  // The key order is the file's: v, id, name, space, aperture, slots, inks, fonts, notes,
  // prims. A design written here is byte-identical to the one the one-page builder wrote.
  const doc = {
    v: 1,
    id: tpl.id,
    name: tpl.name,
    space: { w: W0, h: H0 },
  } as TemplateDoc;
  // The aperture is absent when the design has none: "no key" and "exactly the window" are
  // the only two answers the packer reads.
  if (AB)
    doc.aperture = { x: AB.x, y: AB.y, w: AB.w, h: AB.h, r: Math.round(ap!.r || 0) };
  // The canvas travels with the design: the active preset is what the sheet carries, the
  // rest are the editor's. A design without one leaves the panel's page alone.
  doc.canvas = {
    backgrounds: bgs.slice(),
    active:
      Number.isInteger(canvasActive) && canvasActive >= 0 && canvasActive < bgs.length
        ? canvasActive
        : 0,
  };
  doc.slots = {};
  // The colourway and the typography travel with the design: a prim names a role, and a
  // role's colour and face are the design's, not the firmware's. paper is the LayerMask's
  // colour and is null when the design has no mask.
  doc.inks = {
    paper: mask ? mask.color : null,
    ink: tpl.inks.ink,
    rule: tpl.inks.rule,
    band: tpl.inks.band.slice(0, 8),
  };
  doc.fonts = {
    title: { ff: tpl.fonts.title.ff, px: tpl.fonts.title.px },
    numeral: { ff: tpl.fonts.numeral.ff, px: tpl.fonts.numeral.px },
    caption: { ff: tpl.fonts.caption.ff, px: tpl.fonts.caption.px },
  };
  Object.keys(tpl.slots).forEach((k) => {
    doc.slots[k] = { max: tpl.slots[k].max };
  });
  if (noteList.length) doc.notes = noteList;
  doc.prims = prims;
  return { doc, errs };
}

export const tplText = (d: unknown) => JSON.stringify(d, null, 2) + "\n";

export interface ImportResult {
  tpl: Tpl;
  mask: Mask | null;
  objs: Obj[];
  groups: Group[];
  nid: number;
  name: string;
  canvas: CanvasDoc;
}

/**
 * Load a label-template file into a fresh document (the inverse of buildTemplate).
 * It throws what the packer would refuse, so a file the sheet cannot compile is refused
 * here rather than carried until Export.
 */
export function importTemplate(text: string, fallbackPaper: string): ImportResult {
  const d = JSON.parse(text) as TemplateDoc & {
    notes?: string[];
    aperture?: { x: number; y: number; w: number; h: number; r: number };
  };
  if (d.v !== 1) throw new Error("not a label template (v1)");
  const tpl = defaultTpl();
  tpl.id = d.id;
  tpl.name = d.name;
  // The colourway and the font roles come back with the design; without this a re-import
  // silently repaints it and re-faces its type.
  if (d.inks) {
    if (d.inks.ink) tpl.inks.ink = d.inks.ink;
    if (d.inks.rule) tpl.inks.rule = d.inks.rule;
    if (Array.isArray(d.inks.band)) {
      const b = d.inks.band.slice(0, 8);
      while (b.length < 8) b.push(defaultTpl().inks.band[b.length]);
      tpl.inks.band = b;
    }
  }
  if (d.fonts)
    for (const k of FROLES) if (d.fonts[k]) tpl.fonts[k] = { ff: d.fonts[k].ff, px: d.fonts[k].px };
  FROLES.forEach((k) => {
    const f = tpl.fonts[k] || ({} as { ff: string; px: number });
    if (!onSheet(f.ff, f.px || 0))
      throw new Error(
        `font role ${k} is "${f.ff}" ${f.px} px, and the sheet carries ${SHEET_FONTS.map(([ff, px]) => `${ff} ${px}`).join(", ")}`,
      );
  });
  if (
    d.aperture &&
    (d.aperture.x !== WIN.x ||
      d.aperture.y !== WIN.y ||
      d.aperture.w !== WIN.w ||
      d.aperture.h !== WIN.h ||
      (d.aperture.r || 0) !== WIN.r)
  )
    throw new Error(
      `aperture ${d.aperture.x},${d.aperture.y} ${d.aperture.w}x${d.aperture.h} r${d.aperture.r || 0} is not the Card's window ${WIN.x},${WIN.y} ${WIN.w}x${WIN.h} r${WIN.r}`,
    );
  if (!d.space || d.space.w !== PRINT_W || d.space.h !== PRINT_H)
    throw new Error(
      `space is ${d.space ? `${d.space.w}x${d.space.h}` : "missing"}, not ${PRINT_W}x${PRINT_H}`,
    );
  // The canvas is optional in the file - a design without one leaves the panel's page alone -
  // but a declared one is the packer's problem too, so it is refused here at Open.
  let canvas: CanvasDoc = canvasOf({});
  if (d.canvas !== undefined) {
    const c = d.canvas;
    if (
      !c ||
      !Array.isArray(c.backgrounds) ||
      c.backgrounds.length < 1 ||
      c.backgrounds.length > VOCAB.canvas
    )
      throw new Error(
        `canvas: ${c && Array.isArray(c.backgrounds) ? c.backgrounds.length : "no"} backgrounds, and a design carries one to ${VOCAB.canvas}`,
      );
    c.backgrounds.forEach((v, i) => {
      if (typeof v !== "string" || !HEX.test(v))
        throw new Error(`canvas.backgrounds[${i}] ${JSON.stringify(v)} is not #RRGGBB`);
    });
    if (!Number.isInteger(c.active) || c.active < 0 || c.active >= c.backgrounds.length)
      throw new Error(
        `canvas.active ${c.active} is not an index into ${c.backgrounds.length} backgrounds`,
      );
    canvas = { backgrounds: c.backgrounds.slice(), active: c.active };
  }
  Object.keys(d.slots).forEach((k) => {
    tpl.slots[k] = { max: d.slots[k].max };
  });
  const paper = d.prims.find((p) => p.t === "rect" && p.fill === "paper") as
    | Record<string, unknown>
    | undefined;
  if (d.aperture && !paper) throw new Error("aperture without a paper");
  const mask: Mask | null = paper
    ? {
        color: (d.inks && d.inks.paper) || fallbackPaper || "#c7a867",
        radius: (paper.r as number) || 0,
        collapsed: false,
      }
    : null;

  const out: Obj[] = [];
  let id = 0;
  const mk = (o: Obj) => {
    if (mask) o.m = true;
    return o;
  };
  d.prims.forEach((p) => {
    if (p === paper) return;
    if (p.t === "text") {
      const isStatic = p.slot === undefined;
      const o: Obj = {
        id: ++id,
        type: "text",
        name: isStatic ? "" : (p.slot as string),
        x: p.x as number,
        y: p.y as number,
        w: p.w as number,
        text: isStatic ? (p.text as string) : (p.preview as string),
        font: 20,
        tc: "#151515",
        ff: "",
        fr: p.font as FontRole,
        align: (p.align as Obj["align"]) || "left",
      };
      if (isStatic) o.static = true;
      if (!isStatic) o.sim = p.preview as string;
      if (p.rot) o.rot = p.rot as number;
      if (TEXT_ROLES.includes(p.color as string)) o.role = p.color as string;
      else throw new Error(`text colour "${p.color}" is not ink or one of the eight bands`);
      out.push(mk(o));
    } else {
      const o: Obj = {
        id: ++id,
        type: "rect",
        name: "",
        x: p.x as number,
        y: p.y as number,
        w: p.w as number,
        h: p.h as number,
        r: (p.r as number) || 0,
        color: "#1f2937",
      };
      if (p.rot) o.rot = p.rot as number;
      if (ROLES.includes(p.fill as string)) o.role = p.fill as string;
      else throw new Error(`fill "${p.fill}" is not a role`);
      out.push(mk(o));
    }
  });
  if (d.aperture) {
    const a = d.aperture;
    out.push(
      mk({
        id: ++id,
        type: "rect",
        name: "",
        x: a.x,
        y: a.y,
        w: a.w,
        h: a.h,
        r: a.r || 0,
        color: "#000000",
        ap: true,
      }),
    );
  }
  return { tpl, mask, objs: out, groups: [], nid: id, name: d.name || "", canvas };
}

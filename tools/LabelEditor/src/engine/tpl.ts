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
import {
  buildJCard,
  importJCard,
  migrateJCard,
  starterJCard,
  type JCardFile,
} from "./jcard";
import type {
  CanvasDoc,
  DocData,
  FontRole,
  Group,
  JCardDoc,
  Mask,
  Obj,
  Tpl,
} from "./types";

/** The panel's white page at the two-finger pose (`k_col_white`): a new design starts here. */
export const DEFAULT_CANVAS = "#909090";

/** The paper a design without a LayerMask still has, because the J-card is paper either way. */
export const DEFAULT_PAPER = "#c7a867";

const HEX = /^#[0-9a-f]{6}$/i;
const K7_ID = /^k7\.[a-z0-9][a-z0-9-]*$/;
const LABEL_ID = /^label\.([a-z0-9][a-z0-9-]*)$/;

/** A design id is `k7.<slug>`; a retired label template's `label.<slug>` becomes one. */
export function migrateId(id: string): string {
  const m = LABEL_ID.exec(id || "");
  return m ? `k7.${m[1]}` : id;
}

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
    id: "k7.untitled",
    name: "Untitled",
    inks: {
      paper: DEFAULT_PAPER,
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
      : tpl.inks.paper || "#ffffff"
    : r === "ink"
      ? tpl.inks.ink
      : r === "rule"
        ? tpl.inks.rule
        : /^band\.[0-7]$/.test(r || "")
          ? tpl.inks.band[+r.slice(5)]
          : null;

/**
 * A text's preview in the editor: the fields' test values, one per field. A layer has no
 * test value of its own - two texts naming one field could then preview differently, which
 * is the thing a test value exists to rule out.
 */
export const simText = (o: Obj, tpl: Tpl) =>
  o.static ? (o.text ?? "") : (tpl.sim[o.name] ?? slotOf(o.name)?.sim ?? "");

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

/**
 * The paper is one colour on both media. The label's LayerMask is the label's paper, so it is
 * the container's; a document made before the J-card existed carries no paper at all, and gets
 * the default one so the J-card has a sheet and the K7Art always names a colour.
 */
export function migratePaper(tpl: Tpl, mask: Mask | null): void {
  if (!tpl.inks) tpl.inks = defaultTpl().inks;
  if (mask && HEX.test(mask.color || "")) tpl.inks.paper = mask.color;
  else if (!HEX.test(tpl.inks.paper || "")) tpl.inks.paper = DEFAULT_PAPER;
}

/**
 * A document stored before the container existed: it gets the K7Art's id, its paper and its
 * J-card (the starter, from its own label's paper and faces), so every loaded document is a
 * K7Art without a second save. Idempotent: a document already migrated is unchanged.
 */
export function migrateDocData(data: DocData): DocData {
  if (!data.tpl) data.tpl = defaultTpl();
  const tpl = data.tpl;
  migrateFonts(tpl);
  migrateBands(tpl);
  migratePaper(tpl, data.mask ?? null);
  tpl.id = migrateId(tpl.id);
  data.jcard = migrateJCard(data.jcard, tpl);
  return data;
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

/** The label section: everything the sheet reads, minus the container's identity and inks. */
export interface LabelSection {
  space: { w: number; h: number };
  aperture?: { x: number; y: number; w: number; h: number; r: number };
  canvas?: CanvasDoc;
  fonts: Record<FontRole, { ff: string; px: number }>;
  slots: Record<string, { max: number }>;
  notes?: string[];
  prims: Array<Record<string, unknown>>;
}

/**
 * The file: one K7Art v1 (docs/specs/k7art.md §1). One design, one id, two sections - the
 * label the panel prints and the J-card the webapp draws. The key order is the file's:
 * `format`, `v`, `id`, `name`, `inks`, `label`, `jcard`; the sections follow their spec.
 */
export interface K7ArtDoc {
  format: "k7art";
  v: 1;
  id: string;
  name: string;
  inks: { paper: string; ink: string; rule: string; band: string[] };
  label: LabelSection;
  jcard: JCardFile;
}

/** The retired `label-template v1` file: read only to migrate it into a K7Art (k7art.md §8). */
export interface RetiredTemplateDoc {
  v: 1;
  id: string;
  name: string;
  space: { w: number; h: number };
  aperture?: { x: number; y: number; w: number; h: number; r: number };
  canvas?: CanvasDoc;
  slots: Record<string, { max: number }>;
  inks?: { paper?: string | null; ink: string; rule: string; band: string[] };
  fonts?: Record<FontRole, { ff: string; px: number }>;
  notes?: string[];
  prims: Array<Record<string, unknown>>;
}

/**
 * The K7Art and its validation, from the document. The document is the model; this is one
 * view of it, and the packer (which reads only the label section) is the other.
 */
export function buildTemplate(
  objs: Obj[],
  mask: Mask | null,
  tpl: Tpl,
  canvas: CanvasDoc = { backgrounds: [DEFAULT_CANVAS], active: 0 },
  jcard: JCardDoc = starterJCard(tpl),
): { doc: K7ArtDoc; errs: string[] } {
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

  if (!K7_ID.test(tpl.id))
    errs.push(`id "${tpl.id}" must look like k7.my-name`);
  if (ap && !paperOn) errs.push("aperture: needs a LayerMask (the paper it cuts)");

  // The container's paper is one colour on both media: the label's LayerMask when it has one,
  // and the J-card's sheet either way. A K7Art always names it.
  const paper = mask ? mask.color : tpl.inks.paper || DEFAULT_PAPER;
  if (!HEX.test(paper))
    errs.push(`inks.paper "${paper}" is not #RRGGBB`);

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
    if (o.ap || o.hidden) return;
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
  if (!prims.length) errs.push("no layers: a design needs at least one prim to print");
  if (prims.length > LIMITS.prims)
    errs.push(`${prims.length} layers, and the sheet holds ${LIMITS.prims} per design`);
  if (Object.keys(tpl.slots).length > LIMITS.slots)
    errs.push(
      `${Object.keys(tpl.slots).length} fields, and the sheet holds ${LIMITS.slots} per design`,
    );

  /* The container comes along for the J-card's previews: a label Slot's test value is the
   * label's, and the file carries what the editor shows. */
  const jc = buildJCard(jcard, tpl);
  errs.push(...jc.errs);

  // The label section's key order is the section's own: space, aperture, canvas, fonts,
  // slots, notes, prims. Its fields are the retired template's, minus the identity and the
  // inks that moved to the container.
  const slots: Record<string, { max: number }> = {};
  Object.keys(tpl.slots).forEach((k) => {
    slots[k] = { max: tpl.slots[k].max };
  });
  const label: LabelSection = {
    space: { w: W0, h: H0 },
    // The aperture is absent when the design has none: "no key" and "exactly the window" are
    // the only two answers the packer reads.
    ...(AB
      ? { aperture: { x: AB.x, y: AB.y, w: AB.w, h: AB.h, r: Math.round(ap!.r || 0) } }
      : {}),
    // The canvas travels with the design: the active preset is what the sheet carries, the
    // rest are the editor's. A design without one leaves the panel's page alone.
    canvas: {
      backgrounds: bgs.slice(),
      active:
        Number.isInteger(canvasActive) && canvasActive >= 0 && canvasActive < bgs.length
          ? canvasActive
          : 0,
    },
    fonts: {
      title: { ff: tpl.fonts.title.ff, px: tpl.fonts.title.px },
      numeral: { ff: tpl.fonts.numeral.ff, px: tpl.fonts.numeral.px },
      caption: { ff: tpl.fonts.caption.ff, px: tpl.fonts.caption.px },
    },
    slots,
    ...(noteList.length ? { notes: noteList } : {}),
    prims,
  };

  // The container's key order is the file's: format, v, id, name, inks, label, jcard. The
  // inks are the design's eleven roles, and the label section is byte-stable.
  const doc: K7ArtDoc = {
    format: "k7art",
    v: 1,
    id: tpl.id,
    name: tpl.name,
    inks: {
      paper,
      ink: tpl.inks.ink,
      rule: tpl.inks.rule,
      band: tpl.inks.band.slice(0, 8),
    },
    label,
    jcard: jc.doc,
  };
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
  /** The design's J-card: read from a K7Art, or the starter a retired file migrates to. */
  jcard: JCardDoc;
}

const isObject = (v: unknown): v is Record<string, unknown> =>
  typeof v === "object" && v !== null && !Array.isArray(v);

const isNum = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);

/**
 * The container's palette, read from the file. `paper` is the one colour the container may
 * leave out when the label has no mask; a retired template's null paper takes the fallback.
 * The band list is padded to the sheet's eight, because the editor's controls carry eight.
 */
function applyInks(
  tpl: Tpl,
  inks: unknown,
  fallbackPaper: string,
  required: boolean,
): string {
  if (!isObject(inks)) {
    if (required) throw new Error("no inks -- a design without colours is shapes only");
    tpl.inks.paper = HEX.test(tpl.inks.paper) ? tpl.inks.paper : fallbackPaper || DEFAULT_PAPER;
    return tpl.inks.paper;
  }
  let paper = fallbackPaper || DEFAULT_PAPER;
  if (inks.paper !== undefined && inks.paper !== null) {
    if (typeof inks.paper !== "string" || !HEX.test(inks.paper))
      throw new Error(`inks.paper ${JSON.stringify(inks.paper)} is not #RRGGBB`);
    paper = inks.paper;
  }
  if (typeof inks.ink !== "string" || !HEX.test(inks.ink))
    throw new Error(`inks.ink ${JSON.stringify(inks.ink)} is not #RRGGBB`);
  if (typeof inks.rule !== "string" || !HEX.test(inks.rule))
    throw new Error(`inks.rule ${JSON.stringify(inks.rule)} is not #RRGGBB`);
  const band = inks.band;
  if (!Array.isArray(band) || band.length < 4 || band.length > VOCAB.bands)
    throw new Error(
      `inks.band has ${Array.isArray(band) ? band.length : "no"} colours, and a design has four to ${VOCAB.bands}`,
    );
  band.forEach((c, i) => {
    if (typeof c !== "string" || !HEX.test(c))
      throw new Error(`inks.band[${i}] ${JSON.stringify(c)} is not #RRGGBB`);
  });
  // The sheet always carries eight slots; a design that declares four leaves the rest 0.
  const padded = band.slice();
  while (padded.length < VOCAB.bands) padded.push(defaultTpl().inks.band[padded.length]);
  tpl.inks.paper = paper;
  tpl.inks.ink = inks.ink;
  tpl.inks.rule = inks.rule;
  tpl.inks.band = padded;
  return paper;
}

/**
 * The label section's reader: the inverse of the label half of buildTemplate. It throws
 * what the sheet cannot hold, so a file the packer would refuse does not open.
 */
function importLabelSection(
  label: Record<string, unknown>,
  tpl: Tpl,
  paperColour: string,
  requireFonts: boolean,
): { mask: Mask | null; objs: Obj[]; nid: number; canvas: CanvasDoc } {
  const space = label.space;
  if (!isObject(space) || space.w !== PRINT_W || space.h !== PRINT_H)
    throw new Error(
      `space is ${isObject(space) ? `${space.w}x${space.h}` : "missing"}, not ${PRINT_W}x${PRINT_H}`,
    );

  const aperture = label.aperture;
  if (aperture !== undefined) {
    if (!isObject(aperture))
      throw new Error("aperture must be an object or absent");
    if (
      aperture.x !== WIN.x ||
      aperture.y !== WIN.y ||
      aperture.w !== WIN.w ||
      aperture.h !== WIN.h ||
      (aperture.r || 0) !== WIN.r
    )
      throw new Error(
        `aperture ${aperture.x},${aperture.y} ${aperture.w}x${aperture.h} r${aperture.r || 0} is not the Card's window ${WIN.x},${WIN.y} ${WIN.w}x${WIN.h} r${WIN.r}`,
      );
  }

  // The canvas is optional in the file - a design without one leaves the panel's page alone -
  // but a declared one is the packer's problem too, so it is refused here at Open.
  let canvas: CanvasDoc = canvasOf({});
  if (label.canvas !== undefined) {
    const c = label.canvas;
    if (
      !isObject(c) ||
      !Array.isArray(c.backgrounds) ||
      c.backgrounds.length < 1 ||
      c.backgrounds.length > VOCAB.canvas
    )
      throw new Error(
        `canvas: ${isObject(c) && Array.isArray(c.backgrounds) ? c.backgrounds.length : "no"} backgrounds, and a design carries one to ${VOCAB.canvas}`,
      );
    c.backgrounds.forEach((v, i) => {
      if (typeof v !== "string" || !HEX.test(v))
        throw new Error(`canvas.backgrounds[${i}] ${JSON.stringify(v)} is not #RRGGBB`);
    });
    if (
      !Number.isInteger(c.active) ||
      (c.active as number) < 0 ||
      (c.active as number) >= c.backgrounds.length
    )
      throw new Error(
        `canvas.active ${c.active} is not an index into ${c.backgrounds.length} backgrounds`,
      );
    canvas = { backgrounds: (c.backgrounds as string[]).slice(), active: c.active as number };
  }

  // The colourway and the font roles come back with the design; without this a re-import
  // silently repaints it and re-faces its type.
  const fonts = label.fonts;
  if (!isObject(fonts) && requireFonts)
    throw new Error("no fonts -- a design without faces is type with no glyphs");
  if (isObject(fonts)) {
    for (const k of FROLES) {
      const f = fonts[k];
      if (f === undefined) {
        if (requireFonts) throw new Error(`font role ${k} is missing`);
        continue;
      }
      if (!isObject(f) || typeof f.ff !== "string" || !isNum(f.px))
        throw new Error(`font role ${k} is not a face and a size`);
      tpl.fonts[k] = { ff: f.ff, px: f.px };
    }
  }
  FROLES.forEach((k) => {
    const f = tpl.fonts[k] || ({} as { ff: string; px: number });
    if (!onSheet(f.ff, f.px || 0))
      throw new Error(
        `font role ${k} is "${f.ff}" ${f.px} px, and the sheet carries ${SHEET_FONTS.map(([ff, px]) => `${ff} ${px}`).join(", ")}`,
      );
  });

  const slots = label.slots;
  if (!isObject(slots)) throw new Error("slots is missing -- a design's fields are its own");
  Object.keys(slots).forEach((k) => {
    if (!slotOf(k)) throw new Error(`slot "${k}" is not in the vocabulary`);
    const spec = slots[k];
    const max = isObject(spec) ? spec.max : undefined;
    if (!Number.isInteger(max) || (max as number) < 1 || (max as number) > SLOT_CEILING)
      throw new Error(`slot "${k}".max ${JSON.stringify(max)} is not 1..${SLOT_CEILING}`);
    tpl.slots[k] = { max: max as number };
  });

  const prims = label.prims;
  if (!Array.isArray(prims)) throw new Error("prims is missing -- there is nothing to print");

  const paper = prims.find(
    (p) => isObject(p) && p.t === "rect" && p.fill === "paper",
  ) as Record<string, unknown> | undefined;
  if (aperture !== undefined && !paper) throw new Error("aperture without a paper");
  const mask: Mask | null = paper
    ? { color: paperColour, radius: isNum(paper.r) ? paper.r : 0, collapsed: false }
    : null;

  const out: Obj[] = [];
  let id = 0;
  const mk = (o: Obj) => {
    if (mask) o.m = true;
    return o;
  };
  prims.forEach((p) => {
    if (p === paper) return;
    if (!isObject(p)) throw new Error(`prim ${id + 1}: not an object`);
    const kind = p.t;
    if (kind !== "rect" && kind !== "text")
      throw new Error(`prim ${id + 1}: t must be rect or text`);
    if (!isNum(p.x) || !isNum(p.y) || !isNum(p.w))
      throw new Error(`prim ${id + 1}: x, y and w must be numbers`);
    if (kind === "rect" && (!isNum(p.h) || p.h < 1))
      throw new Error(`prim ${id + 1}: h must be one or more design pixels`);
    if (p.rot !== undefined && !isNum(p.rot))
      throw new Error(`prim ${id + 1}: rot must be a number`);
    if (kind === "text") {
      const isStatic = p.slot === undefined;
      if (!isStatic && !slotOf(p.slot as string))
        throw new Error(`prim ${id + 1}: "${p.slot}" is not on the list of fields`);
      if (!FROLES.includes(p.font as FontRole))
        throw new Error(`prim ${id + 1}: font role must be title, numeral or caption`);
      const align = p.align ?? "left";
      if (align !== "left" && align !== "center" && align !== "right")
        throw new Error(`prim ${id + 1}: align must be left, center or right`);
      if (!TEXT_ROLES.includes(p.color as string))
        throw new Error(
          `text colour "${p.color}" is not ink or one of the eight bands`,
        );
      const o: Obj = {
        id: ++id,
        type: "text",
        name: isStatic ? "" : (p.slot as string),
        x: p.x,
        y: p.y,
        w: p.w,
        text: isStatic ? (p.text as string) : (p.preview as string),
        font: 20,
        tc: "#151515",
        ff: "",
        fr: p.font as FontRole,
        align: align as Obj["align"],
        role: p.color as string,
      };
      if (isStatic) o.static = true;
      else if (p.preview !== undefined) o.sim = p.preview as string;
      if (p.rot) o.rot = p.rot;
      out.push(mk(o));
    } else {
      if (!ROLES.includes(p.fill as string))
        throw new Error(`fill "${p.fill}" is not a role`);
      const o: Obj = {
        id: ++id,
        type: "rect",
        name: "",
        x: p.x,
        y: p.y,
        w: p.w,
        h: p.h as number,
        r: isNum(p.r) ? p.r : 0,
        color: "#1f2937",
        role: p.fill as string,
      };
      if (p.rot) o.rot = p.rot;
      out.push(mk(o));
    }
  });
  if (aperture !== undefined) {
    const a = aperture as { x: number; y: number; w: number; h: number; r?: number };
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
  // The vocabulary is one table: a declared slot no text prints would be a field the panel
  // shows and the label never uses (docs/specs/label-templates.md §7).
  const used = new Set(
    out.filter((o) => o.type === "text" && !o.static).map((o) => o.name),
  );
  Object.keys(tpl.slots).forEach((k) => {
    if (!used.has(k)) throw new Error(`slot "${k}" is declared but no dynamic text uses it`);
  });
  return { mask, objs: out, nid: id, canvas };
}

/** One K7Art file, read into a fresh document (the inverse of buildTemplate). */
function importK7Art(d: Record<string, unknown>, fallbackPaper: string): ImportResult {
  if (d.v !== 1) throw new Error(`not a K7Art v1 (v is ${JSON.stringify(d.v)})`);
  if (typeof d.id !== "string" || !K7_ID.test(d.id))
    throw new Error(`id ${JSON.stringify(d.id)} must look like k7.my-name`);
  if (typeof d.name !== "string" || !d.name.trim())
    throw new Error(
      "name is missing -- the design is named in the sheet's log and the App's pack list",
    );
  const tpl = defaultTpl();
  tpl.id = d.id;
  tpl.name = d.name;
  const paperColour = applyInks(tpl, d.inks, fallbackPaper, true);
  if (!isObject(d.label)) throw new Error("no label -- the design is the pair");
  const label = importLabelSection(d.label, tpl, paperColour, true);
  const jcard = importJCard(d.jcard, tpl);
  return { tpl, ...label, groups: [], name: d.name, jcard };
}

/**
 * The retired `label-template v1`: its label becomes the section, its identity and inks move
 * to the container, and it gets the starter J-card (docs/specs/k7art.md §8). The id's
 * `label.` prefix becomes `k7.`.
 */
function importRetired(d: Record<string, unknown>, fallbackPaper: string): ImportResult {
  if (d.v !== 1) throw new Error("not a label template (v1)");
  const tpl = defaultTpl();
  tpl.id = migrateId(typeof d.id === "string" ? d.id : "");
  tpl.name = typeof d.name === "string" ? d.name : "";
  const paperColour = applyInks(tpl, d.inks, fallbackPaper, false);
  const label = importLabelSection(d, tpl, paperColour, false);
  return {
    tpl,
    ...label,
    groups: [],
    name: tpl.name,
    jcard: starterJCard(tpl),
  };
}

/**
 * Open a K7Art, or a retired label template (migrated into one). It throws what the packer
 * would refuse, so a file the sheet cannot compile is refused here rather than carried until
 * Export.
 */
export function importTemplate(text: string, fallbackPaper: string): ImportResult {
  const raw: unknown = JSON.parse(text);
  if (!isObject(raw)) throw new Error("not a JSON object, so not a K7Art");
  if (raw.format === "k7art") return importK7Art(raw, fallbackPaper);
  if (raw.format !== undefined)
    throw new Error(
      `not a K7Art: format is ${JSON.stringify(raw.format)}, and the file and the App read only "k7art"`,
    );
  if (raw.v === 1 && Array.isArray(raw.prims)) return importRetired(raw, fallbackPaper);
  throw new Error(
    "not a K7Art (it needs format, v, id, name, inks, label and jcard)",
  );
}

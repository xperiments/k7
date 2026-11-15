import {
  BUILTIN_FONTS,
  FROLES,
  PAL,
  PALETTE,
  PRINT_H,
  PRINT_W,
  SZ,
  sheetSizes,
  slotOf,
} from "./vocab";
import {
  alignOf,
  buildTemplate,
  defaultTpl,
  importTemplate,
  migrateBands,
  migrateFonts,
  normalizeRoles,
  roleColorOf,
  simText,
  syncSlots,
  tplText,
  type TemplateDoc,
} from "./tpl";
import type {
  Doc,
  DocData,
  FontRec,
  Group,
  Mask,
  Obj,
  Tpl,
  Tool,
} from "./types";

export const STORE_KEY = "label-editor.docs.v2";
export const LEGACY_STORE_KEY = "lvgl-canvas.docs.v1";
const OUTSIDE_KEY = "label-editor.outside";

const clamp = (n: number, lo: number, hi: number) => Math.min(Math.max(lo, n), hi);

const uid = () => crypto.randomUUID();

/** A pointer position in design coordinates, and which layer or handle it hit. */
export interface Hit {
  kind: "empty" | "obj" | "rotate" | "resize";
  id?: number;
}

interface Gesture {
  drag: { dx: number; dy: number; x0: number; y0: number } | null;
  group: { ox: number; oy: number; m: Array<{ o: Obj; x: number; y: number }> } | null;
  resize: boolean;
  ra: { x: number; y: number } | null;
  rotate: {
    cx: number;
    cy: number;
    a0: number;
    mem: Array<{ o: Obj; x: number; y: number; h: number; rot: number }>;
  } | null;
}

/** The one canvas: design state, the documents that hold it, and every edit of it. */
export class Editor {
  docs: Doc[] = [];
  active = 0;
  objs: Obj[] = [];
  nid = 0;
  bg = "#ffffff";
  groups: Group[] = [];
  mask: Mask | null = null;
  tpl: Tpl = defaultTpl();
  sel: Obj | null = null;
  selSet = new Set<number>();
  selGroup: string | null = null;
  selMask = false;
  tool: Tool = "select";
  run = false;
  zoom = 1;
  outside = true;
  clip: Obj | null = null;
  undo: string[] = [];
  redo: string[] = [];
  cur: string | null = null;
  rv: Record<number, { text?: string; color?: string; font?: number }> = {};
  fonts: FontRec[] = [];
  lvgl: {
    canvas: HTMLCanvasElement | null;
    ctx: CanvasRenderingContext2D | null;
    module: any;
    loading: boolean;
    error: string | null;
  } = { canvas: null, ctx: null, module: null, loading: true, error: null };

  private listeners = new Set<() => void>();
  private version = 0;
  private saveT: ReturnType<typeof setTimeout> | null = null;
  private lastT = 0;
  private gesture: Gesture = {
    drag: null,
    group: null,
    resize: false,
    ra: null,
    rotate: null,
  };
  private mctx: CanvasRenderingContext2D | null = null;
  private storeWarned = false;

  constructor() {
    this.loadAll();
    this.commit(false);
    this.loadFonts();
    this.loadLvgl();
    // A debounced save must not be the one the closing tab loses.
    window.addEventListener("beforeunload", () => this.persist());
  }

  // ---- the React seam: a version, and everyone who cares about it ----
  subscribe = (fn: () => void) => {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  };
  getVersion = () => this.version;
  emit() {
    this.version++;
    this.listeners.forEach((fn) => fn());
  }

  // ---- derived state ----
  get doc(): Doc | undefined {
    return this.docs[this.active];
  }
  get selIds(): number[] {
    return this.selSet.size ? [...this.selSet] : this.sel ? [this.sel.id] : [];
  }
  selMembers(): Obj[] {
    if (this.selGroup && this.sel && this.sel.g === this.selGroup)
      return this.objs.filter((o) => o.g === this.selGroup);
    const ids = this.selIds;
    return this.objs.filter((o) => ids.includes(o.id));
  }
  roleColor(r: string): string | null {
    return roleColorOf(r, this.mask, this.tpl);
  }
  fillOf(o: Obj): string {
    return (o.role && this.roleColor(o.role)) || (o.type === "text" ? o.tc : o.color) || "#000000";
  }
  frOf(o: Obj) {
    return this.tpl.fonts[FROLES.includes(o.fr!) ? o.fr! : "caption"];
  }
  ffOf(o: Obj): string {
    const f = this.frOf(o);
    return f ? f.ff : (o.ff ?? "");
  }
  pxOf(o: Obj): number {
    const f = this.frOf(o);
    return f ? f.px : (o.font ?? 20);
  }
  apOf(): Obj | null {
    return this.objs.find((o) => o.ap && o.type === "rect") || null;
  }
  simOf(id: string): string {
    return this.tpl.sim[id] !== undefined ? this.tpl.sim[id] : (slotOf(id)?.sim ?? "");
  }
  simText(o: Obj): string {
    return simText(o, this.tpl);
  }
  outOfSpace(o: Obj): boolean {
    if (o.ap) return false;
    if (o.type === "text") {
      if (o.x < 0 || o.x + o.w > PRINT_W) return true;
      const ink = this.textInk(o);
      return ink ? ink.top < 0 || ink.bottom > PRINT_H : o.y < 0 || o.y > PRINT_H;
    }
    return o.x < 0 || o.y < 0 || o.x + o.w > PRINT_W || o.y + (o.h || 0) > PRINT_H;
  }
  /** Design-space bounds of the selection: the pivot a group turns about. */
  selBounds(mem: Obj[]) {
    let x0 = Infinity;
    let y0 = Infinity;
    let x1 = -Infinity;
    let y1 = -Infinity;
    for (const o of mem) {
      const hh = o.h || this.textHeight(o);
      x0 = Math.min(x0, o.x);
      y0 = Math.min(y0, o.y);
      x1 = Math.max(x1, o.x + o.w);
      y1 = Math.max(y1, o.y + hh);
    }
    return { x0, y0, x1, y1, cx: (x0 + x1) / 2, cy: (y0 + y1) / 2 };
  }
  blockOf(o: Obj) {
    const i = this.objs.indexOf(o);
    if (i < 0) return { lo: 0, hi: -1 };
    if (!o.g) return { lo: i, hi: i };
    let lo = i;
    let hi = i;
    while (lo > 0 && this.objs[lo - 1].g === o.g) lo--;
    while (hi < this.objs.length - 1 && this.objs[hi + 1].g === o.g) hi++;
    return { lo, hi };
  }
  maskRange(): { lo: number; hi: number } | null {
    const idx = this.objs
      .map((o, i) => (o.m && this.mask ? i : -1))
      .filter((i) => i >= 0);
    return idx.length ? { lo: idx[0], hi: idx[idx.length - 1] } : null;
  }
  private textHeight(o: Obj): number {
    return Math.round(this.pxOf(o) * 1.2);
  }
  private textFont(o: Obj): string {
    return `${this.pxOf(o)}px "${this.ffOf(o)}", sans-serif`;
  }
  private inkOf(o: Obj) {
    this.mctx = this.mctx || document.createElement("canvas").getContext("2d");
    if (!this.mctx) return null;
    this.mctx.font = this.textFont(o);
    return this.mctx.measureText(o.text || "");
  }
  /**
   * A static text's ink, measured: the line box carries air above the capitals and below
   * the baseline, and the print clips the ink, not the air.
   */
  textInk(o: Obj): { top: number; bottom: number } | null {
    if (o.type !== "text" || !o.static || !(o.text || "").trim()) return null;
    const m = this.inkOf(o);
    if (!m) return null;
    const px = this.pxOf(o);
    const asc = m.fontBoundingBoxAscent !== undefined ? m.fontBoundingBoxAscent : px * 0.8;
    const inkAsc = m.actualBoundingBoxAscent || asc;
    const inkDesc = m.actualBoundingBoxDescent || 0;
    const top = o.y + (asc - inkAsc);
    return { top, bottom: top + inkAsc + inkDesc };
  }
  /**
   * A dynamic text's box is a promise — it holds the longest value the slot can print — but
   * a static string has nothing to hold beyond its own ink. So a static text's box is
   * measured and its layer moved by the same amount, which leaves the glyphs where they
   * were and makes the box, the selection and the out-of-print mark hug the text.
   */
  fitStaticText(): boolean {
    let dirty = false;
    for (const o of this.objs) {
      if (o.type !== "text" || !o.static) continue;
      const txt = o.text || "";
      if (!txt.trim()) continue;
      this.mctx = this.mctx || document.createElement("canvas").getContext("2d");
      if (!this.mctx) continue;
      this.mctx.font = this.textFont(o);
      const ink = Math.ceil(this.mctx.measureText(txt).width) + 1;
      if (Math.abs(ink - o.w) < 1) continue;
      const d = o.w - ink;
      const a = alignOf(o);
      if (a === "center") o.x = Math.round(o.x + d / 2);
      else if (a === "right") o.x = Math.round(o.x + d);
      o.w = ink;
      dirty = true;
    }
    return dirty;
  }

  // ---- fonts: the three faces the sheet compiles, embedded and registered ----
  async loadFonts() {
    await Promise.all(
      BUILTIN_FONTS.map(async (f) => {
        try {
          const res = await fetch(`./fonts/${f.file}`);
          if (!res.ok) throw new Error(`${res.status}`);
          const buf = new Uint8Array(await res.arrayBuffer());
          this.fonts.push({ name: f.name, role: f.role, file: f.file, buf, id: 0 });
          const face = new FontFace(f.name, buf.slice().buffer as ArrayBuffer);
          document.fonts.add(face);
          await face.load().catch(() => {});
        } catch (err) {
          console.warn(`font ${f.name} unavailable`, err);
        }
      }),
    );
    this.emit();
  }
  async addFont(file: File) {
    const buf = new Uint8Array(await file.arrayBuffer());
    const name = file.name.replace(/\.[^.]+$/, "");
    const ex = this.fonts.find((x) => x.name === name);
    const face = new FontFace(name, buf.slice().buffer as ArrayBuffer);
    document.fonts.add(face);
    await face.load().catch(() => {});
    if (ex) {
      ex.buf = buf;
      ex.id = 0;
    } else this.fonts.push({ name, file: file.name, buf, id: 0 });
    this.registerFonts();
    this.emit();
  }

  // ---- documents: several open at once, persisted to localStorage ----
  private docNew(name: string, data?: DocData): Doc {
    return {
      id: uid(),
      name,
      data:
        data ||
        ({
          w: PRINT_W,
          h: PRINT_H,
          bg: "#ffffff",
          objs: [],
          nid: 0,
          mask: { color: "#c7a867", radius: 10, collapsed: false },
          tpl: defaultTpl(),
        } as DocData),
      undo: [],
      redo: [],
      cur: null,
    };
  }
  private snapDoc() {
    const d = this.doc;
    if (!d) return;
    d.data = {
      w: PRINT_W,
      h: PRINT_H,
      bg: this.bg,
      objs: this.objs,
      nid: this.nid,
      groups: this.groups,
      mask: this.mask,
      tpl: this.tpl,
    };
    d.undo = this.undo;
    d.redo = this.redo;
    d.cur = this.cur;
  }
  private loadAll() {
    let st: { active?: number; docs?: Array<{ id: string; name: string; data: DocData }> } | null =
      null;
    // A document made in the one-page builder opens here: same shape, new key.
    for (const key of [STORE_KEY, LEGACY_STORE_KEY]) {
      try {
        const raw = localStorage.getItem(key);
        if (!raw) continue;
        st = JSON.parse(raw);
        if (st && Array.isArray(st.docs) && st.docs.length) break;
        st = null;
      } catch {
        st = null;
      }
    }
    this.docs =
      st && Array.isArray(st.docs) && st.docs.length
        ? st.docs.map((d) => ({
            ...this.docNew(d.name, d.data),
            id: d.id || uid(),
          }))
        : [
            this.docNew("Untitled 1", {
              w: PRINT_W,
              h: PRINT_H,
              bg: "#ffffff",
              objs: this.defaultObjs(),
              nid: 4,
              mask: { color: "#c7a867", radius: 10, collapsed: false },
              tpl: defaultTpl(),
            }),
          ];
    const taken: Record<string, 1> = {};
    this.docs.forEach((d) => {
      const t = d.data && d.data.tpl;
      if (!t) return;
      let id = t.id;
      let n = 2;
      while (taken[id]) id = `${t.id}-${n++}`;
      t.id = id;
      taken[id] = 1;
    });
    this.active = st && Number.isInteger(st.active) ? clamp(st.active!, 0, this.docs.length - 1) : 0;
    try {
      this.outside = localStorage.getItem(OUTSIDE_KEY) !== "0";
    } catch {
      this.outside = true;
    }
    this.applyDoc(this.docs[this.active]);
  }
  private defaultObjs(): Obj[] {
    return [
      { id: 1, type: "rect", name: "", x: 20, y: 20, w: 708, h: 319, color: "#1f2937", r: 16 },
      { id: 2, type: "circle", name: "", x: 50, y: 60, w: 200, h: 200, color: "#ff5b1f" },
      {
        id: 3,
        type: "text",
        name: "song_title",
        x: 280,
        y: 60,
        w: 420,
        text: "",
        sim: "Neon Skyline",
        font: 36,
        tc: "#ffffff",
        fr: "title",
        role: "band.6",
      },
      {
        id: 4,
        type: "text",
        name: "bpm",
        x: 280,
        y: 150,
        w: 400,
        text: "",
        sim: "124",
        font: 48,
        tc: "#ff5b1f",
        fr: "numeral",
        role: "band.0",
      },
    ];
  }
  applyDoc(d: Doc) {
    this.bg = d.data.bg || "#ffffff";
    this.objs = d.data.objs;
    this.nid = d.data.nid || 0;
    this.groups = d.data.groups || [];
    this.mask = d.data.mask || null;
    this.selMask = false;
    this.fixMask();
    this.tpl = d.data.tpl || defaultTpl();
    migrateFonts(this.tpl);
    migrateBands(this.tpl);
    normalizeRoles(this.objs, this.mask, this.tpl);
    this.undo = d.undo;
    this.redo = d.redo;
    this.cur = d.cur;
    this.sel = null;
    this.selSet = new Set();
    this.selGroup = null;
    this.rv = {};
    this.emit();
  }
  persist() {
    try {
      this.snapDoc();
      localStorage.setItem(
        STORE_KEY,
        JSON.stringify({
          active: this.active,
          docs: this.docs.map((d) => ({ id: d.id, name: d.name, data: d.data })),
        }),
      );
    } catch (err) {
      if (!this.storeWarned) {
        this.storeWarned = true;
        console.warn("localStorage unavailable or full", err);
      }
    }
  }
  private schedulePersist() {
    if (this.saveT) clearTimeout(this.saveT);
    this.saveT = setTimeout(() => this.persist(), 300);
  }
  freeTplId(base = "label.untitled"): string {
    const used = new Set(
      [...this.docs.map((d) => d.data && d.data.tpl && d.data.tpl.id), this.tpl && this.tpl.id].filter(
        Boolean,
      ) as string[],
    );
    let id = base;
    let n = 2;
    while (used.has(id)) id = `${base}-${n++}`;
    return id;
  }
  newDoc(data?: DocData, name?: string) {
    this.persist();
    const d = this.docNew(name || `Untitled ${this.docs.length + 1}`, data);
    if (!d.data.tpl) d.data.tpl = defaultTpl();
    d.data.tpl.id = this.freeTplId(
      d.data.tpl.id === defaultTpl().id ? "label.untitled" : d.data.tpl.id,
    );
    this.docs.push(d);
    this.active = this.docs.length - 1;
    this.applyDoc(this.docs[this.active]);
    this.commit(false);
    this.emit();
    this.persist();
  }
  renameDoc(i: number, name: string) {
    const d = this.docs[i];
    if (!d || !name.trim()) return;
    d.name = name.trim();
    this.persist();
    this.emit();
  }
  closeDoc(i: number) {
    const d = this.docs[i];
    if (!d) return;
    this.docs.splice(i, 1);
    if (!this.docs.length) this.docs.push(this.docNew("Untitled 1"));
    if (i === this.active || this.active >= this.docs.length) {
      this.active = clamp(i - 1, 0, this.docs.length - 1);
      this.applyDoc(this.docs[this.active]);
    } else if (i < this.active) this.active--;
    this.emit();
    this.persist();
  }
  switchDoc(i: number) {
    if (i === this.active || !this.docs[i]) return;
    this.persist();
    this.active = i;
    this.applyDoc(this.docs[i]);
    this.persist();
  }

  // ---- undo: a snapshot of the whole design, coalesced while a field is being dragged ----
  private snapNow() {
    return JSON.stringify({
      w: PRINT_W,
      h: PRINT_H,
      bg: this.bg,
      objs: this.objs,
      groups: this.groups,
      mask: this.mask,
      tpl: this.tpl,
    });
  }
  commit(coalesce: boolean) {
    syncSlots(this.objs, this.tpl);
    const s = this.snapNow();
    if (this.cur === null) {
      this.cur = s;
      return;
    }
    if (s === this.cur) return;
    // Wall-clock coalescing: a field dragged within 700 ms of the last step is one undo.
    const now = Date.now();
    if (!(coalesce && this.undo.length && now - this.lastT < 700)) {
      this.undo.push(this.cur);
      if (this.undo.length > 200) this.undo.shift();
    }
    this.redo = [];
    this.lastT = now;
    this.cur = s;
    this.schedulePersist();
    this.emit();
  }
  private restore(s: string) {
    const d = JSON.parse(s) as {
      bg: string;
      objs: Obj[];
      groups?: Group[];
      mask?: Mask | null;
      tpl?: Tpl;
    };
    this.bg = d.bg;
    this.objs = d.objs;
    this.groups = d.groups || [];
    this.mask = d.mask || null;
    if (!this.mask) this.selMask = false;
    this.tpl = d.tpl || defaultTpl();
    this.sel = this.sel ? this.objs.find((o) => o.id === this.sel!.id) || null : null;
    this.selSet = new Set([...this.selSet].filter((id) => this.objs.some((o) => o.id === id)));
    this.schedulePersist();
    this.emit();
  }
  undoAct() {
    if (!this.undo.length || this.run) return;
    this.redo.push(this.cur!);
    this.cur = this.undo.pop()!;
    this.lastT = 0;
    this.restore(this.cur);
  }
  redoAct() {
    if (!this.redo.length || this.run) return;
    this.undo.push(this.cur!);
    this.cur = this.redo.pop()!;
    this.lastT = 0;
    this.restore(this.cur);
  }
  copyAct() {
    if (this.sel && !this.run) this.clip = JSON.parse(JSON.stringify(this.sel));
    this.emit();
  }
  pasteAct() {
    if (!this.clip || this.run) return;
    const o = {
      ...JSON.parse(JSON.stringify(this.clip)),
      id: ++this.nid,
      name: this.clip.name ? `${this.clip.name}_copy` : "",
      label: this.clip.label ? `${this.clip.label} copy` : "",
      locked: false,
      g: undefined,
    } as Obj;
    this.insertNew(o);
    this.sel = o;
    this.selSet = new Set();
    this.tool = "select";
    this.commit(false);
    this.emit();
  }

  // ---- tools, adding, and the gesture on the canvas ----
  setTool(t: Tool) {
    this.tool = t;
    this.emit();
  }
  add(t: Tool, p: { x: number; y: number }) {
    const type = t === "square" ? "rect" : t;
    const [w, hh] = SZ[t];
    const o: Obj = {
      id: ++this.nid,
      type: type === "text" ? "text" : "rect",
      name: "",
      x: Math.round(p.x),
      y: Math.round(p.y),
      w,
      h: hh,
      color: PAL[this.nid % PAL.length],
      r: 8,
      text: "Text",
      font: 20,
      tc: "#151515",
      ff: "",
      static: type === "text" ? true : undefined,
      role: type === "text" ? "ink" : "band.0",
      fr: type === "text" ? "caption" : undefined,
    };
    if (t === "circle") o.type = "circle";
    if (this.mask) o.m = true;
    this.insertNew(o);
    this.sel = o;
    this.selSet = new Set();
    this.selGroup = null;
    this.tool = "select";
    this.commit(false);
    this.emit();
  }
  pointerDown(hit: Hit, p: { x: number; y: number }, mods: { add: boolean }) {
    if (this.run) return;
    if (hit.kind === "rotate") {
      this.gesture.rotate = this.rotCtx(p);
      return;
    }
    if (hit.kind === "resize") {
      if (!this.sel) return;
      this.gesture.resize = true;
      const g = this.V(this.sel);
      const b = this.RT(g, g.hw, g.hhs);
      this.gesture.ra = { x: g.cx - b.x, y: g.cy - b.y };
      return;
    }
    if (this.tool !== "select") {
      this.add(this.tool, p);
      return;
    }
    if (hit.kind === "empty" || hit.id === undefined) {
      this.sel = null;
      this.selSet = new Set();
      this.selGroup = null;
      this.emit();
      return;
    }
    const obj = this.objs.find((o) => o.id === hit.id);
    if (!obj) return;
    if (mods.add) {
      if (!this.selSet.size && this.sel) this.selSet.add(this.sel.id);
      if (this.selSet.has(obj.id)) this.selSet.delete(obj.id);
      else this.selSet.add(obj.id);
      this.sel = obj;
      this.selGroup = null;
      this.emit();
      return;
    }
    // a click on a layer already in the selection keeps the selection, so it can be dragged
    // or rotated as one; a click anywhere else starts a new one
    const inSel = obj === this.sel || this.selSet.has(obj.id) || (this.selGroup && obj.g === this.selGroup);
    if (!inSel) {
      this.sel = obj;
      this.selSet = new Set();
      this.selGroup = null;
    } else this.sel = obj;
    const mem = this.selMembers();
    this.gesture.group =
      mem.length > 1 && !mem.some((o) => o.locked)
        ? {
            ox: obj.x,
            oy: obj.y,
            m: mem.filter((o) => o !== obj).map((o) => ({ o, x: o.x, y: o.y })),
          }
        : null;
    if (!obj.locked && !mem.some((o) => o.locked))
      this.gesture.drag = { dx: p.x - obj.x, dy: p.y - obj.y, x0: obj.x, y0: obj.y };
    this.emit();
  }
  pointerMove(p: { x: number; y: number }, shift: boolean) {
    const g = this.gesture;
    if (g.rotate) {
      let deg = ((Math.atan2(p.y - g.rotate.cy, p.x - g.rotate.cx) - g.rotate.a0) * 180) / Math.PI;
      if (shift) deg = Math.round(deg / 15) * 15;
      const r = (deg * Math.PI) / 180;
      const cos = Math.cos(r);
      const sin = Math.sin(r);
      for (const m of g.rotate.mem) {
        const o = m.o;
        const mcx = m.x + o.w / 2;
        const mcy = m.y + (m.h || 0) / 2;
        const dx = mcx - g.rotate.cx;
        const dy = mcy - g.rotate.cy;
        o.x = Math.round(g.rotate.cx + dx * cos - dy * sin - o.w / 2);
        o.y = Math.round(g.rotate.cy + dx * sin + dy * cos - (m.h || 0) / 2);
        o.rot = Math.round((((m.rot + deg) % 360) + 360) % 360);
      }
      this.emit();
      return;
    }
    if (g.resize && this.sel) {
      const sel = this.sel;
      const v = this.V(sel);
      const dx = p.x - g.ra!.x;
      const dy = p.y - g.ra!.y;
      const ex = this.RT(v, 1, 0);
      const ey = this.RT(v, 0, 1);
      const wv = Math.max(0.1, dx * ex.x + dy * ex.y);
      const hv = Math.max(0.1, dx * ey.x + dy * ey.y);
      sel.w = Math.max(8, Math.round(wv));
      if (sel.type !== "text") sel.h = Math.max(8, Math.round(hv));
      else sel.w = Math.max(8, Math.round(wv));
      const v2 = this.V(sel);
      const q = this.RT(v2, v2.hw, v2.hhs);
      sel.x = Math.round(g.ra!.x + q.x - sel.w / 2);
      sel.y = Math.round(g.ra!.y + q.y - v2.hh / 2);
      this.emit();
      return;
    }
    if (!g.drag || !this.sel) return;
    // A drag is free: the pasteboard is where a layer can be put outside the print, and what
    // will not print is the packer's refusal at build, not a wall here.
    const starts = g.group
      ? [{ o: this.sel, x: g.group.ox, y: g.group.oy }, ...g.group.m]
      : [{ o: this.sel, x: g.drag.x0, y: g.drag.y0 }];
    const dx = Math.round(p.x - g.drag.dx - g.drag.x0);
    const dy = Math.round(p.y - g.drag.dy - g.drag.y0);
    for (const s of starts) {
      s.o.x = s.x + dx;
      s.o.y = s.y + dy;
    }
    this.emit();
  }
  pointerUp() {
    const wasEditing = this.gesture.drag || this.gesture.resize || this.gesture.rotate;
    this.gesture = { drag: null, group: null, resize: false, ra: null, rotate: null };
    if (wasEditing) this.commit(false);
  }
  private V(o: Obj) {
    const sc = o.type === "text" ? o.s || 1 : 1;
    const hh = o.type === "text" ? this.textHeight(o) : o.h || 0;
    const r = ((o.rot || 0) * Math.PI) / 180;
    return {
      sc,
      hh,
      hw: (sc * o.w) / 2,
      hhs: (sc * hh) / 2,
      cx: o.x + o.w / 2,
      cy: o.y + hh / 2,
      c: Math.cos(r),
      s: Math.sin(r),
    };
  }
  private RT(g: { c: number; s: number }, vx: number, vy: number) {
    return { x: vx * g.c - vy * g.s, y: vx * g.s + vy * g.c };
  }
  private rotCtx(p: { x: number; y: number }) {
    const mem = this.selMembers().filter((o) => !o.locked);
    if (!mem.length) return null;
    const b = this.selBounds(mem);
    return {
      cx: b.cx,
      cy: b.cy,
      a0: Math.atan2(p.y - b.cy, p.x - b.cx),
      mem: mem.map((o) => ({ o, x: o.x, y: o.y, h: o.h || this.textHeight(o), rot: o.rot || 0 })),
    };
  }

  // ---- masks: members must stay one contiguous run ----
  private maskContig(arr: Obj[], inM: (o: Obj) => boolean) {
    const idx = arr.map((o, i) => (inM(o) ? i : -1)).filter((i) => i >= 0);
    return idx.length < 2 || idx[idx.length - 1] - idx[0] === idx.length - 1;
  }
  fixMask() {
    if (this.mask && !this.objs.some((o) => "m" in o)) this.objs.forEach((o) => (o.m = true));
    if (!this.mask) this.objs.forEach((o) => delete o.m);
  }
  insertNew(o: Obj) {
    const mr = this.mask && o.m ? this.maskRange() : null;
    if (mr) this.objs.splice(mr.hi + 1, 0, o);
    else this.objs.push(o);
  }
  setMembers(list: Obj[], flag: boolean) {
    if (!this.mask) return;
    const S = new Set<Obj>();
    list.forEach((o) => {
      S.add(o);
      if (o.g)
        this.objs.forEach((x) => {
          if (x.g === o.g) S.add(x);
        });
    });
    const block = this.objs.filter((o) => S.has(o));
    if (!block.length || block.some((o) => o.locked)) return;
    const rest = this.objs.filter((o) => !S.has(o));
    const mi = rest.map((o, i) => (o.m ? i : -1)).filter((i) => i >= 0);
    const ins = mi.length
      ? mi[mi.length - 1] + 1
      : this.objs.slice(0, this.objs.indexOf(block[block.length - 1])).filter((o) => !S.has(o)).length;
    const next = [...rest.slice(0, ins), ...block, ...rest.slice(ins)];
    if (!this.maskContig(next, (o) => !!(this.mask && (S.has(o) ? flag : o.m)))) return;
    block.forEach((o) => {
      if (flag) o.m = true;
      else delete o.m;
    });
    this.objs = next;
    this.sel = block[block.length - 1];
    this.selSet = new Set();
    this.commit(false);
    this.emit();
  }
  toggleSelMembers() {
    const ids = this.selIds;
    const list = this.objs.filter((o) => ids.includes(o.id));
    if (!list.length) return;
    this.setMembers(list, !list.every((o) => o.m));
  }
  addMask() {
    this.mask = { color: PALETTE[7].c, radius: 10, collapsed: false };
    this.objs.forEach((o) => (o.m = true));
    this.selMask = true;
    this.sel = null;
    this.selSet = new Set();
    this.commit(false);
    this.emit();
  }
  removeMask() {
    this.mask = null;
    this.objs.forEach((o) => delete o.m);
    this.selMask = false;
    this.commit(false);
    this.emit();
  }
  toggleMask() {
    if (this.mask) this.removeMask();
    else this.addMask();
  }

  // ---- groups: a contiguous run of layers sharing g ----
  private groupsContig(arr: Obj[], gOf: (o: Obj) => string | undefined) {
    const ids = new Set(arr.map(gOf).filter(Boolean) as string[]);
    for (const id of ids) {
      const idx = arr.map((o, i) => (gOf(o) === id ? i : -1)).filter((i) => i >= 0);
      if (idx[idx.length - 1] - idx[0] !== idx.length - 1) return false;
    }
    return true;
  }
  moveBlock(a: number, b: number, insertAt: number, flag?: boolean, gid?: string | null) {
    if (a < 0 || b >= this.objs.length || a > b) return;
    const block = this.objs.slice(a, b + 1);
    if (block.some((o) => o.locked)) return;
    if (typeof gid === "string" && !this.groups.some((g) => g.id === gid)) return;
    if (
      typeof gid === "string" &&
      this.groups.some((g) => {
        const mem = this.objs.filter((o) => o.g === g.id);
        return g.id !== gid && mem.length > 1 && mem.every((o) => block.includes(o));
      })
    )
      return;
    if (insertAt >= a && insertAt <= b) {
      if (flag === undefined || !this.mask) return;
      if (!this.maskContig(this.objs, (o) => !!(block.includes(o) ? flag : o.m))) return;
      block.forEach((o) => {
        if (flag) o.m = true;
        else delete o.m;
      });
      this.commit(false);
      this.emit();
      return;
    }
    const next = [...this.objs];
    next.splice(a, block.length);
    next.splice(insertAt > b ? insertAt - block.length : insertAt, 0, ...block);
    const gOf = (o: Obj) =>
      block.includes(o)
        ? gid === undefined
          ? o.g
          : gid === null
            ? undefined
            : gid
        : o.g;
    if (!this.groupsContig(next, gOf)) return;
    if (
      !this.maskContig(
        next,
        (o) => !!(this.mask && (block.includes(o) && flag !== undefined ? flag : o.m)),
      )
    )
      return;
    block.forEach((o) => {
      if (gid === null) delete o.g;
      else if (typeof gid === "string") o.g = gid;
    });
    if (flag !== undefined && this.mask)
      block.forEach((o) => {
        if (flag) o.m = true;
        else delete o.m;
      });
    this.objs = next;
    this.groups = this.groups.filter((g) => this.objs.some((o) => o.g === g.id));
    this.sel = block[block.length - 1];
    this.selSet = new Set();
    this.commit(false);
    this.emit();
  }
  moveLayer(s: number, insertAt: number) {
    this.moveBlock(s, s, insertAt);
  }
  groupWhy(): string {
    const ids = this.selIds;
    const mem = this.objs.filter((o) => ids.includes(o.id));
    if (mem.length < 2) return "Select two or more layers first (Cmd-click them in Layers).";
    if (this.mask && new Set(mem.map((m) => !!m.m)).size > 1)
      return "A group cannot mix layers inside and outside the LayerMask.";
    const topIdx = Math.max(...mem.map((m) => this.objs.indexOf(m)));
    const rest = this.objs.filter((o) => !mem.includes(o));
    const ins = this.objs.slice(0, topIdx).filter((o) => !mem.includes(o)).length;
    const block = this.objs.filter((o) => mem.includes(o));
    const nextG = [...rest.slice(0, ins), ...block, ...rest.slice(ins)];
    if (this.mask && !this.maskContig(nextG, (o) => !!o.m))
      return "Grouping these would split the LayerMask run in two.";
    return "";
  }
  groupSel() {
    if (this.groupWhy()) return;
    const ids = this.selIds;
    const mem = this.objs.filter((o) => ids.includes(o.id));
    const topIdx = Math.max(...mem.map((m) => this.objs.indexOf(m)));
    const rest = this.objs.filter((o) => !mem.includes(o));
    const ins = this.objs.slice(0, topIdx).filter((o) => !mem.includes(o)).length;
    const gid = "g" + crypto.randomUUID();
    mem.forEach((m) => (m.g = gid));
    const block = this.objs.filter((o) => mem.includes(o));
    this.objs = [...rest.slice(0, ins), ...block, ...rest.slice(ins)];
    this.groups = this.groups.filter((g) => this.objs.some((o) => o.g === g.id));
    this.groups.push({ id: gid, name: `Group ${this.groups.length + 1}`, collapsed: false });
    this.sel = block[block.length - 1];
    this.selSet = new Set(block.map((b) => b.id));
    this.commit(false);
    this.emit();
  }
  ungroupSel() {
    const ids = this.selIds;
    const gids = new Set(this.objs.filter((o) => o.g && ids.includes(o.id)).map((o) => o.g!));
    if (!gids.size) return;
    this.objs.forEach((o) => {
      if (o.g && gids.has(o.g)) delete o.g;
    });
    this.groups = this.groups.filter((g) => !gids.has(g.id));
    this.commit(false);
    this.emit();
  }
  renameGroup(id: string, name: string) {
    const g = this.groups.find((x) => x.id === id);
    if (!g || !name.trim()) return;
    g.name = name.trim();
    this.commit(false);
    this.emit();
  }
  toggleGroupCollapsed(id: string) {
    const g = this.groups.find((x) => x.id === id);
    if (!g) return;
    g.collapsed = !g.collapsed;
    this.commit(false);
    this.emit();
  }
  groupLock(gid: string) {
    const mem = this.objs.filter((o) => o.g === gid);
    const all = mem.every((m) => m.locked);
    mem.forEach((m) => (m.locked = !all));
    this.commit(false);
    this.emit();
  }
  mv(d: 1 | -1) {
    if (!this.sel) return;
    const b = this.blockOf(this.sel);
    const mr = this.maskRange();
    if (d > 0) {
      const j = b.hi + 1;
      if (j >= this.objs.length) return;
      const nb = mr && !this.sel.m && this.objs[j].m ? mr : this.blockOf(this.objs[j]);
      this.moveBlock(b.lo, b.hi, nb.hi + 1);
    } else {
      const j = b.lo - 1;
      if (j < 0) return;
      const nb = mr && !this.sel.m && this.objs[j].m ? mr : this.blockOf(this.objs[j]);
      this.moveBlock(b.lo, b.hi, nb.lo);
    }
  }
  toggleLock(o: Obj) {
    o.locked = !o.locked;
    this.commit(false);
    this.emit();
  }
  delSel() {
    const ids = this.selIds;
    const targets = this.objs.filter((o) => ids.includes(o.id) && !o.locked);
    if (!targets.length) return;
    this.objs = this.objs.filter((o) => !targets.includes(o));
    this.groups = this.groups.filter((g) => this.objs.some((o) => o.g === g.id));
    this.sel = null;
    this.selSet = new Set();
    this.selGroup = null;
    this.commit(false);
    this.emit();
  }
  nudgeSel(dx: number, dy: number) {
    if (!this.sel) return;
    const mem = this.selMembers();
    if (!mem.length || mem.some((o) => o.locked)) return;
    mem.forEach((o) => {
      o.x += dx;
      o.y += dy;
    });
    this.commit(true);
    this.emit();
  }
  select(obj: Obj, opts: { additive?: boolean } = {}) {
    if (opts.additive) {
      if (!this.selSet.size && this.sel) this.selSet.add(this.sel.id);
      if (this.selSet.has(obj.id)) this.selSet.delete(obj.id);
      else this.selSet.add(obj.id);
      this.sel = obj;
      this.selGroup = null;
    } else {
      this.selSet = new Set();
      this.sel = obj;
      this.selGroup = null;
    }
    this.selMask = false;
    this.emit();
  }
  selectGroup(id: string) {
    const g = this.groups.find((x) => x.id === id);
    if (!g) return;
    const mem = this.objs.filter((o) => o.g === id);
    this.selGroup = id;
    this.selSet = new Set(mem.map((m) => m.id));
    this.sel = mem[mem.length - 1] ?? null;
    this.selMask = false;
    this.emit();
  }
  selectMask() {
    this.selMask = true;
    this.sel = null;
    this.selSet = new Set();
    this.selGroup = null;
    this.emit();
  }
  /** Select one or more layers by id, as a Cmd-click selection would. */
  selectLayers(ids: number[]) {
    const mem = this.objs.filter((o) => ids.includes(o.id));
    if (!mem.length) return;
    this.selSet = new Set(mem.map((o) => o.id));
    this.sel = mem[mem.length - 1];
    this.selGroup = null;
    this.selMask = false;
    this.emit();
  }
  toggleMaskCollapsed() {
    if (!this.mask) return;
    this.mask.collapsed = !this.mask.collapsed;
    this.commit(false);
    this.emit();
  }
  clearSelection() {
    this.sel = null;
    this.selSet = new Set();
    this.selGroup = null;
    this.selMask = false;
    this.emit();
  }

  // ---- inspector edits ----
  setObjField<K extends keyof Obj>(o: Obj, k: K, v: Obj[K], coalesce = true) {
    if (!o) return;
    if (o.locked && k !== "locked") return;
    o[k] = v;
    this.commit(coalesce);
    this.emit();
  }
  setRoleColor(role: string, c: string, coalesce = true) {
    if (role === "paper") {
      if (this.mask) this.mask.color = c;
    } else if (role === "ink") this.tpl.inks.ink = c;
    else if (role === "rule") this.tpl.inks.rule = c;
    else if (/^band\.[0-7]$/.test(role)) this.tpl.inks.band[+role.slice(5)] = c;
    this.commit(coalesce);
    this.emit();
  }
  setTplField(k: string, v: string, coalesce = false) {
    if (k === "id") this.tpl.id = v;
    else if (k === "name") this.tpl.name = v;
    else if (k === "ink") this.tpl.inks.ink = v;
    else if (k === "rule") this.tpl.inks.rule = v;
    else if (k === "paper") {
      if (this.mask) this.mask.color = v;
    } else if (k.startsWith("band")) this.tpl.inks.band[+k.slice(4)] = v;
    else if (k.startsWith("ff:")) {
      const r = k.slice(3) as keyof Tpl["fonts"];
      const sizes = sheetSizes(v);
      this.tpl.fonts[r].ff = v;
      if (!sizes.includes(this.tpl.fonts[r].px))
        this.tpl.fonts[r].px = sizes[0] || this.tpl.fonts[r].px;
      this.commit(true);
      this.emit();
      return;
    } else if (k.startsWith("px:")) {
      const r = k.slice(3) as keyof Tpl["fonts"];
      this.tpl.fonts[r].px = +v || 0;
    }
    this.commit(coalesce);
    this.emit();
  }
  setSim(slot: string, v: string) {
    if (v.trim() === "") delete this.tpl.sim[slot];
    else this.tpl.sim[slot] = v;
    this.commit(true);
    this.emit();
  }
  setMaskField(k: "color" | "radius", v: string | number) {
    if (!this.mask) return;
    if (k === "radius") this.mask.radius = +v || 0;
    else this.mask.color = String(v);
    this.commit(true);
    this.emit();
  }
  setBg(v: string) {
    this.bg = v;
    this.commit(true);
    this.emit();
  }
  setZoom(z: number) {
    this.zoom = z;
    this.emit();
  }
  toggleOutside() {
    this.outside = !this.outside;
    try {
      localStorage.setItem(OUTSIDE_KEY, this.outside ? "1" : "0");
    } catch {
      /* a private window has no storage; the toggle still works for the session */
    }
    this.emit();
  }
  setRun(run: boolean) {
    this.run = run;
    this.sel = null;
    this.selSet = new Set();
    this.selGroup = null;
    if (run && this.lvgl.module) this.lvBuild();
    this.emit();
  }
  setRunValue(id: number, k: "text" | "color" | "font", v: string) {
    this.rv[id] = this.rv[id] || {};
    if (k === "font") this.rv[id].font = +v;
    else this.rv[id][k] = v;
    if (this.run && this.lvgl.module) this.lvBuild();
    this.emit();
  }

  // ---- the template as a file ----
  buildTemplate(): { doc: TemplateDoc; errs: string[] } {
    // A static text's box hugs its ink: settle it before the file is written, so Export
    // never carries a width the canvas has not measured.
    this.fitStaticText();
    syncSlots(this.objs, this.tpl);
    return buildTemplate(this.objs, this.mask, this.tpl);
  }
  templateText(): string {
    return tplText(this.buildTemplate().doc);
  }
  importTemplate(text: string) {
    const r = importTemplate(text, PALETTE[7].c);
    this.tpl = r.tpl;
    this.mask = r.mask;
    this.objs = r.objs;
    this.nid = r.nid;
    this.groups = [];
    this.sel = null;
    this.selSet = new Set();
    this.selMask = false;
    this.selGroup = null;
    this.rv = {};
    if (this.doc && r.name) this.doc.name = r.name;
    this.commit(false);
    this.emit();
  }
  /** Every open design validated without switching tabs: what Export would refuse. */
  validateAll(): Array<{ name: string; errs: string[] }> {
    this.snapDoc();
    return this.docs.map((d) => {
      const tpl = JSON.parse(JSON.stringify(d.data.tpl ?? defaultTpl())) as Tpl;
      const objs = d.data.objs;
      syncSlots(objs, tpl);
      return {
        name: d.name,
        errs: buildTemplate(objs, d.data.mask ?? null, tpl).errs,
      };
    });
  }

  /** Every open document, validated together; refused as a whole if any has errors. */
  buildAll(): { built: Array<{ i: number; doc: TemplateDoc }>; errs: string[] } {
    const built: Array<{ i: number; doc: TemplateDoc }> = [];
    const errs: string[] = [];
    for (let i = 0; i < this.docs.length; i++) {
      if (i !== this.active) this.switchDoc(i);
      const r = this.buildTemplate();
      if (r.errs.length) errs.push(...r.errs.map((e) => `${this.docs[i].name}: ${e}`));
      else built.push({ i, doc: r.doc });
    }
    const seen: Record<string, number> = {};
    built.forEach((b) => {
      if (seen[b.doc.id] !== undefined)
        errs.push(
          `duplicate id ${b.doc.id}: ${this.docs[seen[b.doc.id]].name} and ${this.docs[b.i].name}. Change the Id in Design of one of them.`,
        );
      else seen[b.doc.id] = b.i;
    });
    return { built, errs };
  }

  // ---- the LVGL preview: real LVGL (v9.6) compiled to WebAssembly ----
  async loadLvgl() {
    try {
      await loadScript("./lvgl.js");
      const create = (window as unknown as { createLvgl?: () => Promise<any> }).createLvgl;
      if (!create) throw new Error("createLvgl missing");
      const m = await create();
      this.lvgl.module = m;
      this.lvgl.loading = false;
      this.registerFonts();
      this.commit(false);
    } catch (err) {
      this.lvgl.loading = false;
      this.lvgl.error = String((err as Error)?.message || err);
      console.warn("LVGL wasm unavailable", err);
    }
    this.emit();
  }
  attachCanvas(canvas: HTMLCanvasElement | null) {
    this.lvgl.canvas = canvas;
    this.lvgl.ctx = canvas ? canvas.getContext("2d") : null;
    if (canvas && this.run && this.lvgl.module) this.lvBuild();
  }
  private registerFonts() {
    const M = this.lvgl.module;
    if (!M) return;
    this.fonts.forEach((f) => {
      if (f.id) return;
      const p = M.ccall("lvp_alloc", "number", ["number"], [f.buf.length]);
      M.HEAPU8.set(f.buf, p);
      f.id = M.ccall("lvp_font_add", "number", ["number", "number"], [p, f.buf.length]);
    });
  }
  private call(f: string, r: string | null, a: string[], v: unknown[]) {
    return this.lvgl.module.ccall(f, r, a, v);
  }
  lvBuild() {
    const lv = this.lvgl.canvas;
    const M = this.lvgl.module;
    const ctx = this.lvgl.ctx;
    if (!lv || !M || !ctx) return;
    const w = PRINT_W;
    const hh = PRINT_H;
    if (lv.width !== w) lv.width = w;
    if (lv.height !== hh) lv.height = hh;
    lv.style.width = `${w}px`;
    lv.style.height = `${hh}px`;
    this.registerFonts();
    this.call("lvp_begin", null, ["number", "number", "number"], [w, hh, hex(this.bg)]);
    let ti = 0;
    let inM = false;
    const mr = this.maskRange();
    if (this.mask && !mr) {
      this.call("lvp_mask", null, ["number", "number"], [this.mask.radius, hex(this.mask.color)]);
      this.call("lvp_root", null, [], []);
    }
    for (const o of this.objs) {
      const e = this.eff(o);
      if (this.mask) {
        if (o.m && !inM) {
          this.call("lvp_mask", null, ["number", "number"], [this.mask.radius, hex(this.mask.color)]);
          inM = true;
        } else if (!o.m && inM) {
          this.call("lvp_root", null, [], []);
          inM = false;
        }
      }
      if (o.ap) continue;
      if (o.type === "text") {
        this.call(
          "lvp_label",
          null,
          ["number", "number", "number", "number", "number", "number", "number", "number", "number", "number"],
          [
            ti,
            o.x,
            o.y,
            o.w,
            e.fid,
            this.pxOf(o),
            hex(e.color),
            Math.round((o.s || 1) * 256),
            r10(o),
            { left: 0, right: 1, center: 2 }[alignOf(o)],
          ],
        );
        this.call("lvp_text", null, ["number", "string"], [ti, e.text]);
        ti++;
      } else {
        this.call(
          "lvp_box",
          null,
          ["number", "number", "number", "number", "number", "number", "number"],
          [
            o.x,
            o.y,
            o.w,
            o.h,
            hex(e.color),
            o.type === "circle" ? 0x7fff : o.r || 0,
            r10(o),
          ],
        );
      }
    }
    const ap = this.apOf();
    if (this.mask && ap)
      this.call("lvp_aperture", null, ["number", "number", "number", "number", "number"], [
        ap.x,
        ap.y,
        ap.w,
        ap.h,
        ap.r || 0,
      ]);
    this.call("lvp_end", null, [], []);
    const p = this.call("lvp_framebuffer", "number", [], []);
    const px = M.HEAPU8.subarray(p, p + w * hh * 4);
    const img = ctx.createImageData(w, hh);
    const d = img.data;
    for (let i = 0; i < px.length; i += 4) {
      d[i] = px[i + 2];
      d[i + 1] = px[i + 1];
      d[i + 2] = px[i];
      d[i + 3] = 255;
    }
    ctx.putImageData(img, 0, 0);
  }
  eff(o: Obj): { text: string; color: string; fid: number } {
    const r = this.rv[o.id] || {};
    const f = this.fonts.find((x) => x.name === this.ffOf(o));
    return {
      text: r.text ?? this.simText(o),
      color: r.color ?? this.fillOf(o),
      fid: r.font ?? f?.id ?? 0,
    };
  }
  /** The preview as a PNG, for Export all's per-design image. */
  async previewPng(): Promise<Blob | null> {
    if (!this.lvgl.module) return null;
    const prev = { canvas: this.lvgl.canvas, ctx: this.lvgl.ctx };
    if (!this.lvgl.canvas) {
      const canvas = document.createElement("canvas");
      this.lvgl.canvas = canvas;
      this.lvgl.ctx = canvas.getContext("2d");
    }
    this.lvBuild();
    const blob = await new Promise<Blob | null>((res) => {
      this.lvgl.canvas?.toBlob((b) => res(b), "image/png");
    });
    if (!prev.canvas) {
      this.lvgl.canvas = null;
      this.lvgl.ctx = null;
    } else if (this.run) {
      this.lvBuild();
    }
    return blob;
  }
  retryLvgl() {
    this.lvgl.error = null;
    this.lvgl.loading = true;
    this.emit();
    this.loadLvgl();
  }
}

const hex = (c: string) => parseInt(c.slice(1), 16);
const r10 = (o: Obj) => Math.round((((o.rot || 0) % 360) + 360) % 360 * 10);

let scriptPromise: Promise<void> | null = null;
/** Load a classic script once; the LVGL module is a global, as it was in the one-page builder. */
export function loadScript(src: string): Promise<void> {
  if (scriptPromise) return scriptPromise;
  scriptPromise = new Promise<void>((resolve, reject) => {
    const s = document.createElement("script");
    s.src = src;
    s.async = true;
    s.onload = () => resolve();
    s.onerror = () => reject(new Error(`could not load ${src}`));
    document.head.append(s);
  });
  return scriptPromise;
}

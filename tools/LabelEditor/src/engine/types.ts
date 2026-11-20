import { VOCAB } from "./vocab.generated";

/** What a layer is: the two shapes the sheet prints and its type. */
export type ObjType = "rect" | "circle" | "text";
export type FontRole = keyof typeof VOCAB.defaults.fonts;

/** One layer of the design: the editor's object, and one prim of the template. */
export interface Obj {
  id: number;
  type: ObjType;
  /** The Slot a dynamic text prints; empty for a rect and for a static text. */
  name: string;
  /** The layer's own name in the Layers panel. */
  label?: string;
  x: number;
  y: number;
  w: number;
  h?: number;
  /** Static text's string. A dynamic text's preview comes from its Slot's simulation. */
  text?: string;
  /** The test value a dynamic text shows in the editor (empty = the Slot's default). */
  sim?: string;
  /** A static text: no Slot, the string is the design. */
  static?: boolean;
  /** The colour role the prim prints in; the colourway lives on the template. */
  role?: string;
  fr?: FontRole;
  align?: "left" | "center" | "right";
  r?: number;
  rot?: number;
  /** Pre-role documents carry a literal colour; moved to a role on load. */
  color?: string;
  tc?: string;
  ff?: string;
  font?: number;
  s?: number;
  locked?: boolean;
  /** Inside the LayerMask: masked layers must be one contiguous run. */
  m?: boolean;
  /** The group this layer belongs to. */
  g?: string;
  /** The aperture: the one window cut out of the paper (a placement aid, not a prim). */
  ap?: boolean;
  /** Per-layer LVGL preview overrides, edited in Preview. */
  ex?: Record<string, boolean>;
  note?: string;
}

export interface Group {
  id: string;
  name: string;
  collapsed: boolean;
}

export interface Mask {
  color: string;
  radius: number;
  collapsed: boolean;
}

/** The canvas backgrounds a design carries, and the one it shows while the tape is up. */
export interface CanvasDoc {
  backgrounds: string[];
  active: number;
}

export interface Tpl {
  id: string;
  name: string;
  inks: { ink: string; band: string[]; rule: string };
  fonts: Record<FontRole, { ff: string; px: number }>;
  slots: Record<string, { max: number }>;
  sim: Record<string, string>;
}

export interface DocData {
  w: number;
  h: number;
  /** The active canvas background; `bgs` carries the presets and `bgi` the active one. */
  bg: string;
  /** The canvas background presets. Absent on a document made before they existed. */
  bgs?: string[];
  /** The active preset's index into `bgs`. */
  bgi?: number;
  objs: Obj[];
  nid: number;
  groups?: Group[];
  mask?: Mask | null;
  tpl?: Tpl | null;
}

export interface Doc {
  id: string;
  name: string;
  data: DocData;
  undo: string[];
  redo: string[];
  cur: string | null;
}

export type Tool = "select" | "rect" | "square" | "circle" | "text";

export interface FontRec {
  name: string;
  role?: string;
  file: string;
  buf: Uint8Array;
  id: number;
}

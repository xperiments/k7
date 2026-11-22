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
  /** The layer's own colour, `#RRGGBB`: a shape's fill or a text's type. */
  color?: string;
  fr?: FontRole;
  align?: "left" | "center" | "right";
  r?: number;
  rot?: number;
  /** A retired text colour, read only by the v1 migration. */
  tc?: string;
  /** Retired type fields, read only by the v1 migration. */
  ff?: string;
  font?: number;
  s?: number;
  locked?: boolean;
  /** Hidden: not drawn and not exported, but still in the document and in the panel. */
  hidden?: boolean;
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

/**
 * One J-card layer: the editor's object for a prim of the 1300x1280 sheet. It keeps the label
 * object's field names so a surface's selection, undo and tools can be one code path; the
 * differences are the J-card's three fields, its own type sizes and no LayerMask.
 */
export interface JObj {
  id: number;
  type: ObjType;
  /** The layer's own colour, `#RRGGBB`: a shape's fill or a text's type. */
  color?: string;
  /** The J-card type role. */
  fr?: FontRole;
  align?: "left" | "center" | "right";
  x: number;
  y: number;
  w: number;
  h?: number;
  r?: number;
  rot?: number;
  /** The J-card field a dynamic text prints (`title`, `artist` or `meta`); empty otherwise. */
  name: string;
  /** A static text's string. */
  text?: string;
  static?: boolean;
  /** The test value a dynamic text shows in the editor (the file's `preview`). */
  sim?: string;
  /** The layer's name in the Layers panel. */
  label?: string;
  locked?: boolean;
  /** Hidden: not drawn and not exported, but still in the document and in the panel. */
  hidden?: boolean;
  /** The group it belongs to: one list of groups, a run of layers on either surface. */
  g?: string;
}

/**
 * The J-card section as the editor holds it: the three sizes, the sheet's layers and the
 * three test values the Fields panel edits. The values are the editor's only; the file
 * carries each text's `preview`, which the panel writes through.
 */
export interface JCardDoc {
  /** The sheet's own colour, painted under the prims; independent of the label's mask. */
  background: string;
  fonts: Record<FontRole, { ff: string; size: number }>;
  prims: JObj[];
  nid: number;
  sims?: Record<string, string>;
}

/** The two surfaces of one document: the label's print and the J-card's one sheet. */
export type Surface = "label" | "jcard";

/** The J-card's focus: the whole sheet, or one of the three panels. */
export type JCardFocus = "sheet" | "flap" | "spine" | "cover";

export interface Tpl {
  id: string;
  name: string;
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
  /** The design's J-card section. A document stored before it existed gets a starter. */
  jcard?: JCardDoc | null;
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

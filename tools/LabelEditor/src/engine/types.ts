// <generated:label-vocabulary>
// assets/labels/vocabulary.json is the source. tools/label_pack.py generates this block
// into the one-page builder today; this file carries the same block so the editor, the
// packer and the firmware cannot drift apart. Do not edit it by hand.
export const VOCAB = {
  space: { w: 748, h: 359 },
  aperture: { x: 95, y: 101, w: 556, h: 166, r: 83 },
  bands: 8,
  value: 96,
  limits: { prims: 48, slots: 16, templates: 32 },
  coords: {
    x: [-32768, 32767],
    y: [-32768, 32767],
    w: [1, 65535],
    h: [1, 65535],
  },
  slots: [
    { id: "song_title", label: "Song Title", sim: "Neon Skyline" },
    { id: "song_artist", label: "Song Artist", sim: "The Analog Sons" },
    { id: "song_album", label: "Song Album", sim: "Night Drive" },
    { id: "genre", label: "Genre", sim: "Synthwave" },
    { id: "bpm", label: "BPM", sim: "124" },
    { id: "musical_key", label: "Musical Key", sim: "8A" },
    { id: "energy", label: "Energy", sim: "70" },
    { id: "danceability", label: "Danceability", sim: "62" },
    { id: "valence", label: "Valence", sim: "55" },
    { id: "arousal", label: "Arousal", sim: "48" },
    { id: "mood", label: "Mood", sim: "relaxed" },
  ],
  fonts: [
    { name: "Jura", px: 20, symbol: "jura_20" },
    { name: "Jura", px: 28, symbol: "jura_28" },
    { name: "Jura", px: 36, symbol: "jura_36" },
    {
      name: "Archivo Narrow 600",
      px: 32,
      symbol: "archivo_narrow_600_32",
      editor: true,
    },
    {
      name: "Barlow Condensed 700",
      px: 78,
      symbol: "barlow_condensed_700_78",
      editor: true,
    },
    {
      name: "IBM Plex Mono 500",
      px: 18,
      symbol: "ibm_plex_mono_500_18",
      editor: true,
    },
  ],
  defaults: {
    fonts: {
      title: { ff: "Archivo Narrow 600", px: 32 },
      numeral: { ff: "Barlow Condensed 700", px: 78 },
      caption: { ff: "IBM Plex Mono 500", px: 18 },
    },
  },
} as const;
// </generated:label-vocabulary>

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
  bg: string;
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

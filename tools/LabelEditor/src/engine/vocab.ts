import { VOCAB } from "./types";

/**
 * The default palette: a 70s Pantone set. New layers cycle through it, and the
 * template panel's swatches pick from it. These are the label's colours — content,
 * not chrome — so they sit outside the one-skin rule that governs the editor itself.
 */
export const PALETTE = [
  { n: "Pantone 158 C · Orange Brûlé", c: "#df7b35" },
  { n: "Pantone 124 CC · Moutarde / Curry", c: "#c68b2e" },
  { n: "Pantone 7588 C · Terracotta", c: "#a24e2c" },
  { n: "Pantone 7596 C · Marron Chocolat", c: "#8a5d34" },
  { n: "Pantone 377 · Vert Avocat Clair", c: "#6b7a2f" },
  { n: "Pantone 378 · Vert Avocat Vif", c: "#5c6d27" },
  { n: "Pantone 468 · Vert Olive", c: "#8e7e4a" },
  { n: "Pantone · Belge Chaud", c: "#c7a867" },
  { n: "Pantone 286 · Bleu Roi", c: "#1f45b8" },
  { n: "Pantone 294 · Bleu Marine", c: "#1b3a63" },
  { n: "Pantone 7462 C · Bleu Pétrole", c: "#1f5a7a" },
  { n: "Pantone 5483 C · Bleu Grisé", c: "#6d93b2" },
  { n: "Pantone 2597 · Violet Intense", c: "#4a2a7f" },
  { n: "Pantone 235 · Magenta", c: "#8b3a6e" },
  { n: "Pantone 213 · Rose Fuchsia", c: "#c23b66" },
  { n: "Pantone · Noir Profond", c: "#241a12" },
];

export const PAL = PALETTE.map((p) => p.c);

export const SZ: Record<string, [number, number]> = {
  rect: [160, 100],
  square: [100, 100],
  circle: [120, 120],
  text: [200, 24],
};

export const DEFAULT_BANDS = [
  "#c68b2e",
  "#a24e2c",
  "#8a5d34",
  "#6b7a2f",
  "#3b6e8f",
  "#7a3b52",
  "#b9ac8e",
  "#2f4b3a",
];

// The vocabulary, read the way the editor needs it.
export const SLOTS = VOCAB.slots;
export const SLOT_CEILING = VOCAB.value;
export const WIN = VOCAB.aperture;
export const LIMITS = VOCAB.limits;
export const COORDS = VOCAB.coords;
export const BAND_ROLES = Array.from(
  { length: VOCAB.bands },
  (_, i) => `band.${i}`,
);
export const TEXT_ROLES = ["ink", ...BAND_ROLES];
export const ROLES = ["paper", "ink", ...BAND_ROLES, "rule"];
export const FROLES = Object.keys(VOCAB.defaults.fonts) as Array<
  keyof typeof VOCAB.defaults.fonts
>;

/** The faces the sheet carries, at the sizes it carries them: what this page can draw. */
export const SHEET_FONTS = VOCAB.fonts
  .filter((f) => "editor" in f && f.editor)
  .map((f) => [f.name, f.px] as [string, number]);
export const sheetFaces = [...new Set(SHEET_FONTS.map((f) => f[0]))];

export const sheetSizes = (ff: string) =>
  SHEET_FONTS.filter((f) => f[0] === ff)
    .map((f) => f[1])
    .sort((a, b) => a - b);

export const onSheet = (ff: string, px: number) =>
  SHEET_FONTS.some((f) => f[0] === ff && f[1] === px);

export const slotOf = (id: string) =>
  SLOTS.find((s) => s.id === id) ?? null;

// The label is the print's box, fixed by the panel: 748x359 at (25,19) in the 800x480
// screen. The editor no longer offers another size; the packer would refuse it.
export const PRINT_W = VOCAB.space.w;
export const PRINT_H = VOCAB.space.h;
export const SCREEN_W = 800;
export const SCREEN_H = 480;
export const PRINT_X = 25;
export const PRINT_Y = 19;

/** The three faces the sheet compiles, served from this origin, embedded in LVGL too. */
export const BUILTIN_FONTS = [
  {
    name: "Barlow Condensed 700",
    role: "SIDE A / B",
    file: "barlow-condensed-700.ttf",
  },
  {
    name: "Archivo Narrow 600",
    role: "Track / Artist",
    file: "archivo-narrow-600.ttf",
  },
  {
    name: "IBM Plex Mono 500",
    role: "BPM / KEY / TIME",
    file: "ibm-plex-mono-500.ttf",
  },
];

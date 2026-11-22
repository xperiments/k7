import type { CSSProperties } from 'react';

import {
  JCARD_HEIGHT,
  JCARD_PARTS,
  JCARD_WIDTH,
  type JcardDesign,
  type JcardFields,
  type JcardPart,
  type JcardPrim,
} from './model';

/**
 * The J-card as a surface, without React: given a design, the three words and the panel the
 * surface shows, return the sheet's geometry and one node per prim. Keeping the geometry
 * here (and not in the component) is what lets the renderer be tested in the App's node
 * environment: colours, sizes, rotation, wrap and the two clips are asserted on these values.
 *
 * The sheet is always the whole 1300x1280 design; the surface clips it to the panel. Every
 * length a prim uses is a share of the sheet's width, so a size of 56 is 56/1300 of the
 * drawing at any pixel size (k7art.md §5).
 */

export type JcardFit = 'fill' | 'cover';

export interface JcardNode {
  key: string;
  kind: 'rect' | 'text';
  style: CSSProperties;
  /** A text's words, already bound to the J-card field or read from its static string. */
  content?: string;
}

export interface JcardFrame {
  /** The 1300x1280 sheet, positioned over the surface. The surface clips it. */
  sheet: CSSProperties;
  nodes: JcardNode[];
}

/** LVGL's label line height is the face's own box; the editor draws the same 1.2. */
const LINE_HEIGHT = 1.2;

/** The faces' CSS names, with the plain family as the fallback the page may load instead. */
const FACE_CSS: Record<string, { family: string; weight: number }> = {
  'Archivo Narrow 600': {
    family: '"Archivo Narrow 600", "Archivo Narrow", sans-serif',
    weight: 600,
  },
  'Barlow Condensed 700': {
    family: '"Barlow Condensed 700", "Barlow Condensed", sans-serif',
    weight: 700,
  },
  'IBM Plex Mono 500': {
    family: '"IBM Plex Mono 500", "IBM Plex Mono", monospace',
    weight: 500,
  },
};

function rotate(prim: JcardPrim): CSSProperties {
  if (!prim.rot) return {};
  return { transform: `rotate(${prim.rot}deg)`, transformOrigin: '50% 50%' };
}

function rectNode(
  prim: Extract<JcardPrim, { t: 'rect' }>,
  viewWidth: number,
  key: string,
): JcardNode {
  const color = prim.fill;
  return {
    key,
    kind: 'rect',
    style: {
      left: `${(prim.x / JCARD_WIDTH) * 100}%`,
      top: `${(prim.y / JCARD_HEIGHT) * 100}%`,
      width: `${(prim.w / JCARD_WIDTH) * 100}%`,
      height: `${(prim.h / JCARD_HEIGHT) * 100}%`,
      background: color,
      ...(prim.r === undefined
        ? {}
        : { borderRadius: `${(prim.r / viewWidth) * 100}cqi` }),
      ...rotate(prim),
    },
  };
}

function textNode(
  prim: Extract<JcardPrim, { t: 'text' }>,
  design: JcardDesign,
  fields: JcardFields,
  viewWidth: number,
  key: string,
): JcardNode {
  const font = design.jcard.fonts[prim.font];
  const face = FACE_CSS[font.ff] ?? { family: 'sans-serif', weight: 400 };
  const content = prim.slot ? fields[prim.slot] : (prim.text ?? '');

  /*
   * A text's own box has a width and no height (k7art.md §4). The sheet's bottom is
   * therefore its only bound: it wraps to `w` and grows downward, and a value taller than
   * the rest of the sheet is clamped to the lines that fit and ends in an ellipsis, as the
   * label's printed box is cropped. `lines` is in design units; the cap is the same number
   * as a share of the sheet.
   */
  const lines = Math.max(1, Math.floor((JCARD_HEIGHT - prim.y) / (font.size * LINE_HEIGHT)));
  const color = prim.color;

  return {
    key,
    kind: 'text',
    content,
    style: {
      left: `${(prim.x / JCARD_WIDTH) * 100}%`,
      top: `${(prim.y / JCARD_HEIGHT) * 100}%`,
      width: `${(prim.w / JCARD_WIDTH) * 100}%`,
      /* A cap, not a height: the box is the text's own, which is what its rotation turns
       * about (the editor's canvas draws the same box). A stretched box would swing a
       * rotated line off its panel. */
      maxHeight: `${((lines * font.size * LINE_HEIGHT) / JCARD_HEIGHT) * 100}%`,
      color,
      fontFamily: face.family,
      fontWeight: face.weight,
      /* The sheet's width is the size's base: 56 units on 1300 is 4.31% of it, whatever the
       * surface's pixel size. `cqi` is the surface's width, and the sheet is 100/viewWidth of
       * it per design unit. */
      fontSize: `${(font.size / viewWidth) * 100}cqi`,
      lineHeight: LINE_HEIGHT,
      letterSpacing: 'normal',
      textAlign: prim.align,
      whiteSpace: 'pre-wrap',
      overflowWrap: 'break-word',
      overflow: 'hidden',
      textOverflow: 'ellipsis',
      display: '-webkit-box',
      WebkitBoxOrient: 'vertical',
      WebkitLineClamp: lines,
      margin: 0,
      padding: 0,
      border: 0,
      background: 'none',
      ...rotate(prim),
    },
  };
}

/**
 * The frame for one panel. `fill` stretches the panel over a surface whose proportions are
 * the case's (a 16:25 front against the cover part's 0.640625 - the 0.1% difference is the
 * case's own); `cover` scales the drawing uniformly and centres it, which is how a square
 * surface shows exactly the cover's safe square (k7art.md §5).
 */
export function jcardFrame(
  design: JcardDesign,
  fields: JcardFields,
  part: JcardPart = 'cover',
  fit: JcardFit = 'fill',
): JcardFrame {
  const view = JCARD_PARTS[part];

  const width = `${(JCARD_WIDTH / view.w) * 100}%`;
  const left = `${(-view.x / view.w) * 100}%`;
  const height =
    fit === 'cover'
      ? /* A length, not a percentage: the sheet keeps its 1300:1280 proportions and is as
         * tall as the surface's width makes it. */
        `${(JCARD_HEIGHT / view.w) * 100}cqi`
      : `${(JCARD_HEIGHT / view.h) * 100}%`;
  const top =
    fit === 'cover'
      ? /* Centre the uniformly scaled sheet on the surface. */
        `calc(50% - ${(((view.y + view.h / 2) / view.w) * 100)}cqi)`
      : `${(-view.y / view.h) * 100}%`;

  const nodes = design.jcard.prims.map((prim, index) =>
    prim.t === 'rect'
      ? rectNode(prim, view.w, `${index}:rect`)
      : textNode(prim, design, fields, view.w, `${index}:text`),
  );

  return {
    sheet: {
      position: 'absolute',
      left,
      top,
      width,
      height,
      /* The sheet is paper, and its colour is the design's own: the case's face under it is
       * the chrome's orange, so a design that does not lay a `paper` rect over the sheet
       * used to show the moulding through it. */
      background: design.jcard.background,
    },
    nodes,
  };
}

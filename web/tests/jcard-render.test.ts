import { describe, expect, it } from 'vitest';

import {
  readJcardDesign,
  readJcardPack,
  type JcardDesign,
} from '../src/jcard/model';
import { jcardFrame } from '../src/jcard/render';

/** The case's fold lines, as a K7Art stores them. */
const PARTS = {
  flap: { x: 0, y: 0, w: 320, h: 1280 },
  spine: { x: 320, y: 0, w: 160, h: 1280 },
  cover: { x: 480, y: 0, w: 820, h: 1280 },
};
const FONTS = {
  title: { ff: 'Archivo Narrow 600', size: 56 },
  numeral: { ff: 'Barlow Condensed 700', size: 136 },
  caption: { ff: 'IBM Plex Mono 500', size: 31 },
};
const INKS = {
  paper: '#f5f1e6',
  ink: '#242424',
  rule: '#8e7e4a',
  band: [
    '#c68b2e',
    '#a24e2c',
    '#8a5d34',
    '#6b7a2f',
    '#3b6e8f',
    '#7a3b52',
    '#b9ac8e',
    '#2f4b3a',
  ],
};
const FIELDS = { title: 'Neon Skyline', artist: 'The Analog Sons', meta: 'Side A · Side B' };

/** A readable design around a set of prims, through the same reader the factory uses. */
function design(prims: unknown[], fonts = FONTS): JcardDesign {
  const read = readJcardDesign({
    format: 'k7art',
    v: 1,
    id: 'k7.test',
    name: 'Test',
    inks: INKS,
    jcard: {
      space: { w: 1300, h: 1280, unit: 320 },
      parts: PARTS,
      fonts,
      prims,
    },
  });
  expect(read).not.toBeNull();
  return read as JcardDesign;
}

function number(value: unknown): number {
  return parseFloat(String(value));
}

describe('the J-card renderer', () => {
  it('paints every shape and text with the container ink its role names', () => {
    const drawn = design([
      { t: 'rect', x: 0, y: 0, w: 1300, h: 1280, fill: 'paper' },
      { t: 'rect', x: 0, y: 100, w: 1300, h: 4, fill: 'rule' },
      { t: 'rect', x: 0, y: 120, w: 1300, h: 10, fill: 'band.4' },
      { t: 'text', slot: 'title', x: 540, y: 900, w: 700, font: 'numeral', color: 'ink', align: 'left' },
      { t: 'text', text: 'A · B', x: 540, y: 1120, w: 700, font: 'caption', color: 'band.2', align: 'center' },
    ]);
    const { nodes } = jcardFrame(drawn, FIELDS, 'cover');

    expect(nodes).toHaveLength(5);
    expect(nodes[0]).toMatchObject({ kind: 'rect', style: { background: INKS.paper } });
    expect(nodes[1]).toMatchObject({ kind: 'rect', style: { background: INKS.rule } });
    expect(nodes[2]).toMatchObject({ kind: 'rect', style: { background: INKS.band[4] } });
    expect(nodes[3]).toMatchObject({ kind: 'text', content: 'Neon Skyline', style: { color: INKS.ink } });
    expect(nodes[4]).toMatchObject({ kind: 'text', content: 'A · B', style: { color: INKS.band[2] } });
  });

  it('sizes a text by its role size as a share of the sheet width', () => {
    const drawn = design([
      { t: 'text', slot: 'title', x: 540, y: 900, w: 700, font: 'numeral', color: 'ink', align: 'left' },
      { t: 'text', slot: 'artist', x: 540, y: 1060, w: 700, font: 'title', color: 'ink', align: 'left' },
    ]);

    /* The cover part is 820 units wide, so the numeral's 136 is 136/820 of that surface. */
    const cover = jcardFrame(drawn, FIELDS, 'cover');
    expect(number(cover.nodes[0]?.style.fontSize)).toBeCloseTo((136 / 820) * 100);
    expect(number(cover.nodes[1]?.style.fontSize)).toBeCloseTo((56 / 820) * 100);
    expect(cover.nodes[0]?.style.fontWeight).toBe(700);
    expect(String(cover.nodes[0]?.style.fontFamily)).toContain('Barlow Condensed 700');
    expect(String(cover.nodes[1]?.style.fontFamily)).toContain('Archivo Narrow 600');

    /* The spine is 160 units wide, so the same 136 is a larger share of that surface. */
    const spine = jcardFrame(drawn, FIELDS, 'spine');
    expect(number(spine.nodes[0]?.style.fontSize)).toBeCloseTo((136 / 160) * 100);
  });

  it('lays the sheet in the design paper, with or without a paper rect', () => {
    /* The sheet is paper: a design that never paints a `paper` rect still has one, and the
       case's orange face is the chrome's, not the J-card's. */
    const bare = design([
      { t: 'text', slot: 'title', x: 540, y: 900, w: 700, font: 'numeral', color: 'ink', align: 'left' },
    ]);
    const { sheet } = jcardFrame(bare, FIELDS, 'cover');
    expect(sheet.background).toBe(INKS.paper);
  });

  it('rotates a prim about its own box, which for a text is its lines', () => {
    const drawn = design([
      { t: 'rect', x: 320, y: 40, w: 160, h: 1200, fill: 'band.1', rot: 90 },
      { t: 'text', slot: 'title', x: 540, y: 900, w: 700, font: 'numeral', color: 'ink', align: 'left', rot: -12 },
    ]);
    const { nodes } = jcardFrame(drawn, FIELDS, 'spine');

    expect(nodes[0]?.style.transform).toBe('rotate(90deg)');
    expect(nodes[1]?.style.transform).toBe('rotate(-12deg)');
    expect(nodes[1]?.style.transformOrigin).toBe('50% 50%');
    /* The box the rotation turns about is the text's own: the sheet's bottom caps it, it
       does not stretch it. A stretched box would swing the rotated line off its panel. */
    expect(nodes[1]?.style.height).toBeUndefined();
    expect(number(nodes[1]?.style.maxHeight)).toBeGreaterThan(0);
  });

  it('wraps a text to its width and ellipsises the lines that run off the sheet', () => {
    const long = 'The Analog Sons and the whole of the harbour authority';
    const drawn = design([
      { t: 'text', slot: 'artist', x: 540, y: 1060, w: 700, font: 'title', color: 'ink', align: 'left' },
    ]);
    const { nodes } = jcardFrame(drawn, { ...FIELDS, artist: long }, 'cover');
    const node = nodes[0];

    expect(node?.content).toBe(long);
    expect(node?.style.whiteSpace).toBe('pre-wrap');
    expect(node?.style.overflowWrap).toBe('break-word');
    expect(node?.style.overflow).toBe('hidden');
    expect(node?.style.textOverflow).toBe('ellipsis');

    /* 220 units are left below y=1060; a 56-unit title needs 67.2 a line, so three fit. */
    const lines = Math.floor((1280 - 1060) / (56 * 1.2));
    expect(node?.style.WebkitLineClamp).toBe(lines);
    expect(number(node?.style.maxHeight)).toBeCloseTo(((lines * 56 * 1.2) / 1280) * 100);
  });

  it('ignores the editor preview and binds a slot to the App field', () => {
    const drawn = design([
      {
        t: 'text',
        slot: 'artist',
        preview: 'The Analog Sons',
        x: 540,
        y: 1060,
        w: 700,
        font: 'title',
        color: 'ink',
        align: 'right',
      },
    ]);
    const { nodes } = jcardFrame(drawn, { title: 'T', artist: 'A real artist', meta: 'M' }, 'cover');
    expect(nodes[0]?.content).toBe('A real artist');
    expect(nodes[0]?.style.textAlign).toBe('right');
  });

  it('clips the cover at x=480 and x=1300', () => {
    const drawn = design([
      { t: 'rect', x: 480, y: 0, w: 10, h: 10, fill: 'ink' },
      { t: 'rect', x: 290, y: 0, w: 10, h: 10, fill: 'ink' },
    ]);
    const { sheet, nodes } = jcardFrame(drawn, FIELDS, 'cover', 'fill');

    /* The sheet is 1300 units across and starts 480 units to the left of the surface. */
    expect(number(sheet.left)).toBeCloseTo(-(480 / 820) * 100);
    expect(number(sheet.width)).toBeCloseTo((1300 / 820) * 100);
    expect(number(sheet.top)).toBe(0);
    expect(number(sheet.height)).toBe(100);
    expect(number(nodes[0]?.style.left)).toBeCloseTo((480 / 1300) * 100);
    expect(number(nodes[1]?.style.left)).toBeCloseTo((290 / 1300) * 100);
    /* The design's x=480 lands on the surface's left edge; x=1300 on its right. */
    const surfaceX = (x: number) => number(sheet.left) + (x / 1300) * number(sheet.width);
    expect(surfaceX(480)).toBeCloseTo(0);
    expect(surfaceX(1300)).toBeCloseTo(100);
  });

  it('clips the spine at x=320 and x=480', () => {
    const drawn = design([{ t: 'rect', x: 0, y: 0, w: 1300, h: 1280, fill: 'paper' }]);
    const { sheet } = jcardFrame(drawn, FIELDS, 'spine', 'fill');

    expect(number(sheet.left)).toBeCloseTo(-200);
    expect(number(sheet.width)).toBeCloseTo(812.5);
    expect(number(sheet.height)).toBe(100);
    const surfaceX = (x: number) => number(sheet.left) + (x / 1300) * number(sheet.width);
    expect(surfaceX(320)).toBeCloseTo(0);
    expect(surfaceX(480)).toBeCloseTo(100);
  });

  it('shows exactly the safe square when a square surface covers the cover part', () => {
    const drawn = design([{ t: 'rect', x: 480, y: 230, w: 820, h: 820, fill: 'ink' }]);
    const { sheet } = jcardFrame(drawn, FIELDS, 'cover', 'cover');

    /* The sheet is uniform, so a square surface is 100cqi tall; its top centring is the
       `calc` the browser evaluates. The safe square is y 230..1050 of the 1280. */
    const height = number(sheet.height);
    const offset = /calc\(50% - ([\d.]+)cqi\)/.exec(String(sheet.top));
    expect(offset).not.toBeNull();
    const topAt = (y: number) => 50 - number(offset?.[1]) + (y / 1280) * height;
    expect(topAt(230)).toBeCloseTo(0);
    expect(topAt(1050)).toBeCloseTo(100);
    expect(height).toBeCloseTo((1280 / 820) * 100);
  });
});

describe('the J-card reader', () => {
  it('refuses a sheet that is not the case, its parts, or its faces', () => {
    const raw = {
      format: 'k7art',
      v: 1,
      id: 'k7.test',
      name: 'Test',
      inks: INKS,
      jcard: {
        space: { w: 1200, h: 1280, unit: 320 },
        parts: PARTS,
        fonts: FONTS,
        prims: [],
      },
    };
    expect(readJcardDesign(raw)).toBeNull();

    expect(
      readJcardDesign({
        ...raw,
        jcard: { ...raw.jcard, space: { w: 1300, h: 1280, unit: 320 }, parts: { cover: PARTS.cover } },
      }),
    ).toBeNull();

    expect(
      readJcardDesign({
        ...raw,
        jcard: {
          ...raw.jcard,
          space: { w: 1300, h: 1280, unit: 320 },
          fonts: { ...FONTS, title: { ff: 'Comic Sans', size: 56 } },
        },
      }),
    ).toBeNull();

    expect(
      readJcardDesign({
        ...raw,
        jcard: {
          ...raw.jcard,
          space: { w: 1300, h: 1280, unit: 320 },
          fonts: { ...FONTS, caption: { ff: 'IBM Plex Mono 500', size: 3 } },
        },
      }),
    ).toBeNull();

    expect(readJcardDesign({ ...raw, format: 'label-template' })).toBeNull();
  });

  it('drops a prim whose ink, font, field or colour it cannot draw', () => {
    const drawn = design([
      { t: 'rect', x: 0, y: 0, w: 10, h: 10, fill: 'paper' },
      { t: 'rect', x: 0, y: 10, w: 10, h: 10, fill: 'band.9' },
      { t: 'rect', x: 0, y: 20, w: 10, h: 10, fill: '#ff0000' },
      { t: 'text', slot: 'song_title', x: 0, y: 30, w: 10, font: 'title', color: 'ink', align: 'left' },
      { t: 'text', slot: 'title', x: 0, y: 40, w: 10, font: 'nope', color: 'ink', align: 'left' },
      { t: 'text', slot: 'title', x: 0, y: 50, w: 10, font: 'title', color: 'paper', align: 'left' },
      { t: 'text', text: '', x: 0, y: 60, w: 10, font: 'title', color: 'ink', align: 'left' },
    ]);

    expect(drawn.jcard.prims).toHaveLength(1);
    expect(drawn.jcard.prims[0]).toMatchObject({ t: 'rect', fill: 'paper' });
  });

  it('refuses a section over the App prim cap', () => {
    const many = Array.from({ length: 65 }, (_, at) => ({
      t: 'rect',
      x: 0,
      y: at,
      w: 10,
      h: 1,
      fill: 'paper',
    }));
    const raw = {
      format: 'k7art',
      v: 1,
      id: 'k7.test',
      name: 'Test',
      inks: INKS,
      jcard: {
        space: { w: 1300, h: 1280, unit: 320 },
        parts: PARTS,
        fonts: FONTS,
        prims: many,
      },
    };
    expect(readJcardDesign(raw)).toBeNull();
  });

  it('reads a pack of J-cards, keeping the good designs and refusing the others', () => {
    const one = {
      id: 'k7.one',
      name: 'One',
      inks: INKS,
      jcard: {
        space: { w: 1300, h: 1280, unit: 320 },
        parts: PARTS,
        fonts: FONTS,
        prims: [{ t: 'rect', x: 0, y: 0, w: 1300, h: 1280, fill: 'paper' }],
      },
    };
    const bad = { ...one, id: 'nope' };

    const designs = readJcardPack({ v: 1, designs: [one, bad] });
    expect(designs).toHaveLength(1);
    expect(designs?.[0]?.id).toBe('k7.one');

    expect(readJcardPack({ v: 2, designs: [one] })).toBeNull();
    expect(readJcardPack({ designs: 'no' })).toBeNull();
    expect(readJcardPack([one])).toHaveLength(1);
  });
});

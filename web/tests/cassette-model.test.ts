import { describe, expect, it } from 'vitest';

import {
  createCassetteModel,
  HUB_FRAME_COUNT,
  HUB_RING_D,
  packDiameter,
  packFills,
  packRings,
  positionAt,
  stepCassette,
  strandPoints,
} from '../src/ui/cassette-model';

/**
 * The panel's tape, as the Player draws it. The laws are the firmware's
 * (`components/portable/cassette/cassette.c` and `cassette_layout.c`), so these pin the
 * arithmetic a change to the SVG must not quietly move: the fill split, the square-root
 * pack, the reel phase and the 8 s rewind.
 */

const TRACK = {
  playing: true,
  durationMs: 180000,
  trackKey: 'music/a.mp3',
};

describe('the pack', () => {
  it('rings the hub at both ends of a side', () => {
    expect(packDiameter(0)).toBe(168);
    expect(packDiameter(1000)).toBe(356);
  });

  it('grows by the square root of the fill, not by the fill', () => {
    const half = packDiameter(500);
    expect(half).toBeGreaterThan(168);
    expect(half).toBeLessThan(356);
    /* A linear law would be 262; the area law lands higher because the disc grows fast when
       it is small. */
    expect(half).toBe(278);
    expect(packDiameter(250)).toBeLessThan(packDiameter(750));
  });

  it('keeps the diameter even, so two packs of one fill are one width', () => {
    for (let fill = 0; fill <= 1000; fill += 37) expect(packDiameter(fill) % 2).toBe(0);
  });

  it('draws no ring at the empty end and ten at the full one', () => {
    expect(packRings(HUB_RING_D)).toEqual([]);
    const full = packRings(356);
    expect(full).toHaveLength(10);
    expect(full[0]).toBe(178);
    expect(full[10 - 1]).toBe(88);
  });
});

describe('the position', () => {
  it('splits the tape either side of the minimum load', () => {
    expect(packFills(0)).toEqual([1000, 80]);
    expect(packFills(500)).toEqual([540, 540]);
    expect(packFills(1000)).toEqual([80, 1000]);
  });

  it('is the wire permille, and the nominal side when the length is unknown', () => {
    expect(positionAt(0, 180000)).toBe(0);
    expect(positionAt(90000, 180000)).toBe(500);
    expect(positionAt(180000, 180000)).toBe(1000);
    expect(positionAt(180000, 0)).toBe(100);
  });
});

describe('the strands', () => {
  it('leaves the pack on the outer tangent and lands on the run', () => {
    for (const i of [0, 1] as const) {
      const points = strandPoints(i, 356);
      expect(points).toHaveLength(12);
      const hubX = i === 0 ? 208 : 592;
      const first = points[0];
      expect(Math.hypot(first.x - hubX, first.y - 206)).toBeCloseTo(178, 5);
      const last = points[points.length - 1];
      expect(last.y).toBeCloseTo(450, 5);
      expect(last.x).toBeCloseTo(i === 0 ? 39 : 761, 5);
    }
  });
});

describe('the hubs', () => {
  it('has 36 drawn frames', () => {
    expect(HUB_FRAME_COUNT).toBe(36);
  });
});

describe('the reel model', () => {
  /** One step at `now`, with the status anchored at `receivedAt`. */
  function step(
    model: ReturnType<typeof createCassetteModel>,
    now: number,
    overrides: Partial<Parameters<typeof stepCassette>[1]> = {},
  ) {
    return stepCassette(model, {
      ...TRACK,
      elapsedMs: 0,
      receivedAt: 0,
      nowMs: now,
      ...overrides,
    });
  }

  it('turns the two reels at their own speeds, counter-clockwise', () => {
    const model = createCassetteModel();
    step(model, 0);
    const frame = step(model, 1000);

    /* An empty take-up pack turns faster than a full supply one: v/r. */
    expect(frame.hubFrame[1]).toBeGreaterThan(frame.hubFrame[0]);
    expect(frame.hubFrame[0]).toBeGreaterThan(0);
    /* The first second of a 3-minute track is 5 permille: the supply is still full. */
    expect(frame.packFill[0]).toBeGreaterThan(frame.packFill[1]);
  });

  it('holds the reels when the broadcast is not playing', () => {
    const model = createCassetteModel();
    step(model, 0);
    const moving = step(model, 1000);
    const held = step(model, 2000, { playing: false, elapsedMs: 1000 });
    expect(held.hubFrame).toEqual(moving.hubFrame);
  });

  it('rewinds for 8 s on a track change and lands on the new side', () => {
    const model = createCassetteModel();
    step(model, 0);
    const before = step(model, 100000, { elapsedMs: 100000, receivedAt: 100000 });
    expect(before.permille).toBe(555);

    /* The new track arrives with its own clock, and the window opens on the frame that was
       already on screen rather than jumping to the start. */
    const change = step(model, 100000, {
      elapsedMs: 0,
      receivedAt: 100000,
      trackKey: 'music/b.mp3',
    });
    expect(change.rewinding).toBe(true);
    expect(change.permille).toBe(555);

    const middle = step(model, 104000, {
      elapsedMs: 0,
      receivedAt: 100000,
      trackKey: 'music/b.mp3',
    });
    expect(middle.rewinding).toBe(true);
    expect(middle.permille).toBeGreaterThan(0);
    expect(middle.permille).toBeLessThan(555);

    /* The window closes on the wall clock as well as the track's, and normal play shows the
       new side's own position (8 s into a 3-minute track). */
    const after = step(model, 108000, {
      elapsedMs: 0,
      receivedAt: 100000,
      trackKey: 'music/b.mp3',
    });
    expect(after.rewinding).toBe(false);
    expect(after.permille).toBe(44);
  });

  it('does not open a window for a status that jitters', () => {
    const model = createCassetteModel();
    step(model, 0);
    step(model, 100000, { elapsedMs: 100000, receivedAt: 100000 });
    /* A poll answers 300 ms behind the wall clock: still the same side. */
    const jitter = step(model, 105000, { elapsedMs: 104700, receivedAt: 105000 });
    expect(jitter.rewinding).toBe(false);
  });
});

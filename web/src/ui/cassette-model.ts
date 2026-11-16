/**
 * The cassette screen's tape, ported from the panel.
 *
 * The device's main screen draws a tape body (`cassette_tapes.c` on the 800x480 grid of
 * `cassette_layout.h`) from a frame the portable model (`cassette.c`) derives from the
 * broadcast's status. This module is the second half of that pair: the same design grid,
 * the same pack law, and the same reel model, in numbers the SVG can use.
 *
 * The laws are the device's, not an approximation of them:
 *
 *   pack fill    80..1000, split either side of the position by `MIN_FILL`
 *   pack radius  sqrt(hub² + (full² - hub²) × fill): area, and therefore tape length
 *   reel speed   tape speed / (2πr), integrated per reel, so the supply and take-up
 *                turn at their own speeds
 *   direction    counter-clockwise, in the drawn hub's 36 frames (10°)
 *   track change  the device's 8 s rewind: 11 revolutions with a 1.2 s easing tail, and
 *                packs that step through 24 fills rather than sliding
 *
 * The pose is the entry pose (256/256), so design units are the panel's pixels. Nothing
 * here knows about a view or an SVG.
 */

/** The panel's design grid. */
export const DESIGN_W = 800;
export const DESIGN_H = 480;

/** The two hub centres and the drawn rim, from `cassette_layout.h`. */
export const HUB_CENTRES = [
  { x: 208, y: 206 },
  { x: 592, y: 206 },
] as const;
export const HUB_RING_D = 168;
export const HUB_SPOKE_R0 = 66;
export const HUB_SPOKE_R1 = 72;
export const HUB_FRAME_COUNT = 36;

/** The pack ring stack: the reference's 245/310 range, drawn as concentric lines. */
export const PACK_MIN_D = 168;
export const PACK_MAX_D = 356;
export const PACK_RING_GAP = 10;
/** The most circles a full pack draws (the device's own loop has the same bound). */
export const PACK_RING_MAX = 10;

/** The guide under each strand, in design pixels: 38 across with its bottom on the run. */
export const GUIDE = [
  { x: 39, y: 431 },
  { x: 761, y: 431 },
] as const;
export const GUIDE_R = 19;
export const RUN_Y = 450;

/* The portable model's constants (`cassette.h`). */
const MIN_FILL = 80;
const NOMINAL_SIDE_MS = 1_800_000;
const HUB_RADIUS_MM = 11;
const FULL_RADIUS_MM = 24;
const TAPE_SPEED_MM_S = 47.625;
const TWO_PI = Math.PI * 2;
const REWIND_MS = 8000;
const REWIND_EASE_MS = 1200;
const REWIND_REVS = 11;
const REWIND_PACK_STEPS = 24;
/** How far the anchor must fall before a same-track clock is read as a restart. */
const CLOCK_RESET_MS = 2000;

/** One frame of the screen: what the SVG has to paint. */
export interface CassetteFrame {
  /** The two packs' fills, 80..1000: [supply, take-up]. */
  packFill: [number, number];
  /** The divider's frame, 0..35, for [supply, take-up]. */
  hubFrame: [number, number];
  /** The position the packs are showing, 0..1000. */
  permille: number;
  /** Whether the track-change window is open. */
  rewinding: boolean;
}

/** The status the model has to reason about, plus the clocks. */
export interface CassetteInput {
  /** True while the broadcast carries a track. */
  playing: boolean;
  /** The device's `elapsed_ms` in the last status answer. */
  elapsedMs: number;
  /** The track's `duration_ms`; 0 when the device does not know it. */
  durationMs: number;
  /** The track's identity. A change opens the rewind. */
  trackKey: string;
  /** `performance.now()` when that status arrived. */
  receivedAt: number;
  /** `performance.now()` for this step. */
  nowMs: number;
}

/** The model's mutable state: one per mounted screen. */
export interface CassetteModel {
  phase: [number, number];
  shownPhase: [number, number];
  shownQ: number;
  rewinding: boolean;
  rewindStartQ: number;
  rewindOffset: [number, number];
  rewindAudio0: number;
  rewindWall0: number;
  anchorElapsed: number;
  trackKey: string;
  haveAnchor: boolean;
  wasPlaying: boolean;
  lastElapsed: number | null;
}

export function createCassetteModel(): CassetteModel {
  return {
    phase: [0, 0],
    shownPhase: [0, 0],
    shownQ: 0,
    rewinding: false,
    rewindStartQ: 0,
    rewindOffset: [0, 0],
    rewindAudio0: 0,
    rewindWall0: 0,
    anchorElapsed: 0,
    trackKey: '',
    haveAnchor: false,
    wasPlaying: false,
    lastElapsed: null,
  };
}

function clamp(value: number, low: number, high: number): number {
  return value < low ? low : value > high ? high : value;
}

/** The pack's outer diameter for a fill: the device's square-root law, rounded even. */
export function packDiameter(fill: number): number {
  const f = clamp(fill, 0, 1000) / 1000;
  const d = Math.sqrt(PACK_MIN_D ** 2 + (PACK_MAX_D ** 2 - PACK_MIN_D ** 2) * f);
  return Math.round(d) & ~1;
}

/** The two fills for a position, exactly as `cassette.c` splits them. */
export function packFills(permille: number): [number, number] {
  const q = Math.round(clamp(permille, 0, 1000));
  const load = 1000 - MIN_FILL;
  return [
    MIN_FILL + Math.trunc((load * (1000 - q)) / 1000),
    MIN_FILL + Math.trunc((load * q) / 1000),
  ];
}

/** The radii of a pack's rings, outer first, stopping at the hub rim it sits under. */
export function packRings(diameter: number): number[] {
  const rings: number[] = [];
  for (let r = diameter / 2; r > HUB_RING_D / 2; r -= PACK_RING_GAP) rings.push(r);
  return rings;
}

/**
 * One side's tape path: the outer tangent from the pack, then the wrap round its guide to
 * the run. The arithmetic is `strand_points()` on the panel, with the pose at 1.
 */
export function strandPoints(index: 0 | 1, diameter: number): Array<{ x: number; y: number }> {
  const hub = HUB_CENTRES[index];
  const guide = GUIDE[index];
  const radius = diameter / 2;
  const dx = guide.x - hub.x;
  const dy = guide.y - hub.y;
  const len = Math.hypot(dx, dy);
  const phi = Math.atan2(dy, dx);
  const alpha = Math.acos((radius - GUIDE_R) / len);
  const a = phi + (index === 1 ? -alpha : alpha);

  let sweep = Math.PI / 2 - a;
  if (sweep > Math.PI) sweep -= TWO_PI;
  if (sweep < -Math.PI) sweep += TWO_PI;

  const points = [
    { x: hub.x + radius * Math.cos(a), y: hub.y + radius * Math.sin(a) },
    { x: guide.x + GUIDE_R * Math.cos(a), y: guide.y + GUIDE_R * Math.sin(a) },
  ];
  for (let k = 1; k <= 10; k++) {
    const t = a + (sweep * k) / 10;
    points.push({ x: guide.x + GUIDE_R * Math.cos(t), y: guide.y + GUIDE_R * Math.sin(t) });
  }
  return points;
}

/** The frame a phase selects, 0..35. Only the fraction matters: a rewind runs past 1. */
export function hubIndex(phase: number): number {
  const frac = phase - Math.floor(phase);
  return Math.floor(frac * HUB_FRAME_COUNT) % HUB_FRAME_COUNT;
}

/** A fill's pack radius in millimetres, for the reel speed. */
function packRadiusMm(fill: number): number {
  const f = clamp(fill, 0, 1000) / 1000;
  return Math.sqrt(HUB_RADIUS_MM ** 2 + (FULL_RADIUS_MM ** 2 - HUB_RADIUS_MM ** 2) * f);
}

/** The position for an elapsed time, as the wire's own times define it. */
export function positionAt(elapsedMs: number, durationMs: number): number {
  const elapsed = Math.max(0, elapsedMs);
  if (durationMs > 0) return Math.min(1000, Math.trunc((elapsed * 1000) / durationMs));
  return Math.min(1000, Math.trunc((elapsed * 1000) / NOMINAL_SIDE_MS));
}

/** The elapsed the screen shows: the anchor, plus the wall time since it arrived. */
function elapsedAt(input: CassetteInput): number {
  const anchor = Math.max(0, input.elapsedMs);
  const elapsed = input.playing ? anchor + Math.max(0, input.nowMs - input.receivedAt) : anchor;
  return input.durationMs > 0 ? Math.min(elapsed, input.durationMs) : elapsed;
}

/** The rewind's shape: constant for all but its tail, which falls to a stop. */
function rewindEase(p: number): number {
  const tail = REWIND_EASE_MS / REWIND_MS;
  const hold = 1 - tail;
  const total = hold + (tail * 2) / 3;
  if (p <= 0) return 0;
  if (p >= 1) return 1;
  if (p <= hold) return p / total;
  const u = (p - hold) / tail;
  return (hold + tail * (u - (u * u * u) / 3)) / total;
}

/** The position a rewind draws, in the device's fixed number of steps. */
function rewindPackQ(from: number, to: number, left: number): number {
  if (from === to) return to;
  const done = 1 - left;
  if (done <= 0) return from;
  if (done >= 1) return to;
  const k = Math.round(done * REWIND_PACK_STEPS);
  return from + Math.trunc(((to - from) * k) / REWIND_PACK_STEPS);
}

/**
 * Advance the model to one rendered frame.
 *
 * The panel calls its model with the status's own elapsed; here the status arrives every
 * few seconds, so the wall clock carries the elapsed between answers. The model still
 * integrates the tape speed over the elapsed delta, which is what makes the two reels turn
 * at different rates as their packs fill.
 */
export function stepCassette(model: CassetteModel, input: CassetteInput): CassetteFrame {
  const elapsed = elapsedAt(input);
  const position = positionAt(elapsed, input.durationMs);

  /* A status that rewound its own clock, or one that names another track, is a track
   * change. The 2 s guard keeps a jitter, or a broadcast that starves for a moment, from
   * opening an 8 s window. */
  const trackChange =
    input.playing &&
    model.wasPlaying &&
    model.haveAnchor &&
    (input.trackKey !== model.trackKey ||
      input.elapsedMs + CLOCK_RESET_MS < model.anchorElapsed);

  const fromPhase: [number, number] = [model.shownPhase[0], model.shownPhase[1]];
  const fromQ = model.shownQ;

  if (input.playing) {
    if (!model.wasPlaying || trackChange) {
      model.phase = [0, 0];
    } else if (model.lastElapsed !== null) {
      const dt = Math.max(0, elapsed - model.lastElapsed) / 1000;
      if (dt > 0) {
        const fills = packFills(position);
        for (let i = 0; i < 2; i++) {
          const rev = TAPE_SPEED_MM_S / (TWO_PI * packRadiusMm(fills[i]));
          const phase = model.phase[i] + rev * dt;
          model.phase[i] = phase - Math.floor(phase);
        }
      }
    }
  }

  if (trackChange) {
    model.rewinding = true;
    model.rewindAudio0 = input.elapsedMs;
    model.rewindWall0 = input.nowMs;
    model.rewindStartQ = fromQ;
    for (let i = 0; i < 2; i++) {
      const f = fromPhase[i] - model.phase[i];
      model.rewindOffset[i] = f + Math.round(REWIND_REVS - f);
    }
  }

  const sinceAudio = elapsed > model.rewindAudio0 ? elapsed - model.rewindAudio0 : 0;
  const sinceWall = Math.max(0, input.nowMs - model.rewindWall0);
  const since = Math.max(sinceAudio, sinceWall);
  if (model.rewinding && (!input.playing || since >= REWIND_MS)) model.rewinding = false;

  let shownQ = position;
  const shownPhase: [number, number] = [model.phase[0], model.phase[1]];
  if (model.rewinding) {
    const p = Math.min(1, since / REWIND_MS);
    const left = 1 - rewindEase(p);
    shownQ = rewindPackQ(model.rewindStartQ, position, left);
    for (let i = 0; i < 2; i++) shownPhase[i] = model.phase[i] + model.rewindOffset[i] * left;
  }

  model.shownQ = shownQ;
  model.shownPhase = shownPhase;
  model.anchorElapsed = input.elapsedMs;
  model.trackKey = input.trackKey;
  model.haveAnchor = true;
  model.wasPlaying = input.playing;
  model.lastElapsed = elapsed;

  return {
    packFill: packFills(shownQ),
    hubFrame: [hubIndex(shownPhase[0]), hubIndex(shownPhase[1])],
    permille: shownQ,
    rewinding: model.rewinding,
  };
}

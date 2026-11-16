import { useEffect, useLayoutEffect, useRef, useState, type RefObject } from 'react';
import { cn } from 'cn';

import {
  createCassetteModel,
  DESIGN_H,
  DESIGN_W,
  GUIDE,
  GUIDE_R,
  HUB_CENTRES,
  HUB_RING_D,
  HUB_SPOKE_R0,
  HUB_SPOKE_R1,
  PACK_RING_MAX,
  packDiameter,
  packRings,
  RUN_Y,
  stepCassette,
  strandPoints,
  type CassetteFrame,
  type CassetteInput,
} from './cassette-model';

/**
 * The device's cassette screen, drawn as SVG and animated by the panel's own model.
 *
 * The geometry is `cassette_tapes.c` at the entry pose: the two frames, the three chips,
 * the two packs of concentric rings, the guides and the run, and the two hubs - a disc, a
 * 2 px rim and two opposite dividers. The animation is `cassette.c`'s: `cassette-model.ts`
 * owns the frame, and this file only paints the circles, the strands and the two dividers.
 *
 * The paints are written straight to the elements rather than through React state: the
 * reels move at up to 25 frames a second, and a state update per frame would re-render the
 * page's title and progress along with them. The one resting frame under reduced motion is
 * the same paint, drawn once.
 */

const REDUCED_QUERY = '(prefers-reduced-motion: reduce)';

interface CassetteScreenProps {
  /** True while the broadcast carries a track. */
  playing: boolean;
  /** The wire's `elapsed_ms`. */
  elapsedMs: number;
  /** The wire's `duration_ms`; 0 when the device does not know it. */
  durationMs: number;
  /** The track's identity; a change rewinds the reels as the panel does. */
  trackKey: string;
  className?: string;
}

interface Scene {
  rings: SVGCircleElement[][];
  strands: SVGPolylineElement[];
  spokes: SVGGElement[];
  /** The diameter and hub frame last painted, so a still frame writes nothing. */
  diameters: [number, number];
  frames: [number, number];
}

function useReducedMotion(): boolean {
  const [reduced, setReduced] = useState(
    () => typeof window !== 'undefined' && window.matchMedia(REDUCED_QUERY).matches,
  );

  useEffect(() => {
    const media = window.matchMedia(REDUCED_QUERY);
    const update = () => setReduced(media.matches);
    media.addEventListener('change', update);
    return () => media.removeEventListener('change', update);
  }, []);

  return reduced;
}

function useInView(ref: RefObject<Element | null>): boolean {
  const [inView, setInView] = useState(true);

  useEffect(() => {
    const element = ref.current;
    if (!element || typeof IntersectionObserver === 'undefined') return;
    const observer = new IntersectionObserver(([entry]) => setInView(entry.isIntersecting), {
      threshold: 0,
    });
    observer.observe(element);
    return () => observer.disconnect();
  }, [ref]);

  return inView;
}

/** The elements the model's frame moves: ten rings per pack, a strand, two dividers. */
function buildScene(svg: SVGSVGElement): Scene | null {
  const rings = [0, 1].map((i) =>
    Array.from(svg.querySelectorAll<SVGCircleElement>(`[data-pack="${i}"] [data-ring]`)),
  );
  const strands = [0, 1].map((i) => svg.querySelector<SVGPolylineElement>(`[data-strand="${i}"]`));
  const spokes = [0, 1].map((i) => svg.querySelector<SVGGElement>(`[data-spokes="${i}"]`));

  if (
    rings.some((pack) => pack.length !== PACK_RING_MAX) ||
    strands.some((strand) => strand === null) ||
    spokes.some((spoke) => spoke === null)
  ) {
    return null;
  }

  return {
    rings,
    strands: strands as SVGPolylineElement[],
    spokes: spokes as SVGGElement[],
    diameters: [-1, -1],
    frames: [-1, -1],
  };
}

/** Paint one frame, touching only what moved. */
function paint(scene: Scene, frame: CassetteFrame): void {
  for (let i = 0; i < 2; i++) {
    const diameter = packDiameter(frame.packFill[i]);
    if (diameter !== scene.diameters[i]) {
      const radii = packRings(diameter);
      scene.rings[i].forEach((circle, k) => {
        if (k < radii.length) {
          circle.setAttribute('r', String(radii[k]));
          circle.style.display = '';
        } else {
          circle.setAttribute('r', '0');
          circle.style.display = 'none';
        }
      });
      scene.strands[i].setAttribute(
        'points',
        strandPoints(i as 0 | 1, diameter)
          .map((point) => `${point.x.toFixed(2)},${point.y.toFixed(2)}`)
          .join(' '),
      );
      scene.diameters[i] = diameter;
    }

    if (frame.hubFrame[i] !== scene.frames[i]) {
      const hub = HUB_CENTRES[i];
      scene.spokes[i].setAttribute(
        'transform',
        `rotate(${-frame.hubFrame[i] * 10} ${hub.x} ${hub.y})`,
      );
      scene.frames[i] = frame.hubFrame[i];
    }
  }
}

export function CassetteScreen({
  playing,
  elapsedMs,
  durationMs,
  trackKey,
  className,
}: CassetteScreenProps) {
  const svgRef = useRef<SVGSVGElement | null>(null);
  const sceneRef = useRef<Scene | null>(null);
  const reduced = useReducedMotion();
  const inView = useInView(svgRef);

  /* The status's clock is the anchor; the loop adds the wall time since it arrived. The
   * ref is the loop's input, so a poll replaces it without restarting the animation. */
  const inputRef = useRef<CassetteInput | null>(null);
  if (inputRef.current === null) {
    const now = typeof performance === 'undefined' ? 0 : performance.now();
    inputRef.current = { playing, elapsedMs, durationMs, trackKey, receivedAt: now, nowMs: now };
  }

  useEffect(() => {
    const now = performance.now();
    inputRef.current = { playing, elapsedMs, durationMs, trackKey, receivedAt: now, nowMs: now };
  }, [playing, elapsedMs, durationMs, trackKey]);

  /* The scene is the SVG's own children; it never changes shape, only its attributes. The
   * first frame is painted before the browser can show the empty circles. */
  useLayoutEffect(() => {
    const svg = svgRef.current;
    if (!svg) return;
    const scene = buildScene(svg);
    sceneRef.current = scene;
    const input = inputRef.current;
    if (!scene || !input) return;
    const model = createCassetteModel();
    paint(
      scene,
      stepCassette(model, {
        ...input,
        playing: reduced ? false : input.playing,
        nowMs: performance.now(),
      }),
    );
  }, []);

  /* The same frame, held: the current fill with the dividers at rest. A reduced-motion
   * screen still says how much tape is on each side; it does not turn. */
  useEffect(() => {
    const scene = sceneRef.current;
    if (!reduced || !inView || !scene) return;
    const input = inputRef.current;
    if (!input) return;
    const model = createCassetteModel();
    paint(
      scene,
      stepCassette(model, { ...input, playing: false, nowMs: performance.now() }),
    );
  }, [reduced, inView, playing, elapsedMs, durationMs, trackKey]);

  /* The moving frame. One rAF loop for the mount, paused offscreen. */
  useEffect(() => {
    const scene = sceneRef.current;
    if (reduced || !inView || !scene) return;
    const model = createCassetteModel();
    let raf = 0;

    const draw = (now: number) => {
      const input = inputRef.current;
      if (input) paint(scene, stepCassette(model, { ...input, nowMs: now }));
      raf = requestAnimationFrame(draw);
    };

    draw(performance.now());
    return () => cancelAnimationFrame(raf);
  }, [reduced, inView]);

  return (
    <svg
      ref={svgRef}
      viewBox={`0 0 ${DESIGN_W} ${DESIGN_H}`}
      preserveAspectRatio="xMidYMid meet"
      role="img"
      aria-label="The K7 screen: the broadcast’s cassette, with its two reels"
      className={cn('block h-auto w-full', className)}
    >
      <rect width={DESIGN_W} height={DESIGN_H} fill="var(--k7-device-bg)" />

      {/* The two frames the panel's RGB path masks its overscan with. They are part of the
          screen's drawing, and the inner one is the cassette's window. */}
      <g fill="none" stroke="var(--k7-device-frame)" strokeWidth={1}>
        <rect x={16.5} y={16.5} width={767} height={359} rx={15.5} />
        <rect x={122.5} y={122.5} width={555} height={167} rx={15.5} />
      </g>

      {/* The three chips along the bottom: the run passes over their filled boxes. */}
      <g fill="var(--k7-device-bg)" stroke="var(--k7-device-tape)" strokeWidth={1}>
        <rect x={120.5} y={440.5} width={49} height={19} rx={7.5} />
        <rect x={360.5} y={430.5} width={79} height={29} rx={7.5} />
        <rect x={680.5} y={440.5} width={49} height={19} rx={7.5} />
      </g>

      {/* The packs: the device's ring stack, its outer circle at the pack's diameter. */}
      <g fill="none" stroke="var(--k7-device-tape)" strokeWidth={1}>
        {[0, 1].map((i) => (
          <g key={i} data-pack={i}>
            {Array.from({ length: PACK_RING_MAX }, (_, k) => (
              <circle
                key={k}
                data-ring={k}
                cx={HUB_CENTRES[i].x}
                cy={HUB_CENTRES[i].y}
                r={0}
                style={{ display: 'none' }}
              />
            ))}
          </g>
        ))}
      </g>

      {/* The guides, and the run under the tape. */}
      <g fill="none" stroke="var(--k7-device-tape)" strokeWidth={1}>
        {GUIDE.map((guide, i) => (
          <circle key={i} cx={guide.x} cy={guide.y} r={GUIDE_R - 0.5} />
        ))}
      </g>
      <line
        x1={GUIDE[0].x}
        y1={RUN_Y}
        x2={GUIDE[1].x}
        y2={RUN_Y}
        stroke="var(--k7-device-tape)"
        strokeWidth={2}
        strokeLinecap="butt"
      />

      {/* Each side's tape path, over its guide. */}
      <g
        fill="none"
        stroke="var(--k7-device-tape)"
        strokeWidth={2}
        strokeLinecap="butt"
        strokeLinejoin="round"
      >
        <polyline data-strand={0} points="" />
        <polyline data-strand={1} points="" />
      </g>

      {/* The hubs, over the rings they cover: the plastic disc, the playing rim, and the
          two opposite dividers that turn inside it. */}
      <g>
        {[0, 1].map((i) => (
          <g key={i}>
            <circle
              cx={HUB_CENTRES[i].x}
              cy={HUB_CENTRES[i].y}
              r={HUB_RING_D / 2}
              fill="var(--k7-device-hub)"
            />
            <circle
              cx={HUB_CENTRES[i].x}
              cy={HUB_CENTRES[i].y}
              r={HUB_RING_D / 2 - 1}
              fill="none"
              stroke="var(--k7-device-tape)"
              strokeWidth={2}
            />
            <g data-spokes={i}>
              <line
                x1={HUB_CENTRES[i].x + HUB_SPOKE_R0}
                y1={HUB_CENTRES[i].y}
                x2={HUB_CENTRES[i].x + HUB_SPOKE_R1}
                y2={HUB_CENTRES[i].y}
                stroke="var(--k7-device-hub-ink)"
                strokeWidth={4}
                strokeLinecap="round"
              />
              <line
                x1={HUB_CENTRES[i].x - HUB_SPOKE_R0}
                y1={HUB_CENTRES[i].y}
                x2={HUB_CENTRES[i].x - HUB_SPOKE_R1}
                y2={HUB_CENTRES[i].y}
                stroke="var(--k7-device-hub-ink)"
                strokeWidth={4}
                strokeLinecap="round"
              />
            </g>
          </g>
        ))}
      </g>
    </svg>
  );
}

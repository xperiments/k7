import {
  useCallback,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
  type CSSProperties,
  type PointerEvent as ReactPointerEvent,
} from "react";

import { Jcard } from "../jcard/face";
import type { JcardDesign, JcardFields } from "../jcard/model";

/**
 * The App's album shelf, holding this J-card: web/src/ui/album-shelf.tsx's row, drawn with
 * the same `.k7-shelf` markup and the same `placeAt` geometry, with this design where the
 * media page has an album. The case turned to the reader shows the cover part; the one
 * beside it, spine out, shows the spine part — which is how an album's J-card is drawn on
 * the shelf (k7art.md §5).
 *
 * The browsing chrome around the row is not copied: the arrows, the album's name, the index
 * rail, the wheel and the arrow keys are the media page's, and this is a preview of a design
 * rather than of a library. The dwell before a case turns, the drag along the row and the
 * click that turns a case are.
 */

/** How far either side of the turned case the row is built; this preview has two. */
const WINDOW = 10;
/** A case turns only after the pointer has rested on it this long, as it does on the shelf. */
const HOVER_DELAY = 200;
/** How long a turn is held after the pointer leaves, so crossing the gap between two cases
 *  does not swing the shelf back to straight in between. */
const HOVER_OUT_DELAY = 260;
/** The same geometry as `--cw`, `--cd`, `--cg` and the selected gap in `index.css`, in rem. */
const FIRST_STEP = 9.7;
const SPINE_STEP = 3.25;

/** The shelf the preview fills: three cases either side of the turned one. The design is
 *  drawn on all seven; only the middle one is turned, so it shows the cover and the six
 *  beside it show their spines. */
const CASES = [0, 1, 2, 3, 4, 5, 6];
/** The turned case: the middle, so three spines flank it each way. */
const CENTRE = 3;


/** Where a case at distance `d` from the selection sits. The terms are the stylesheet's
 *  `--cw`, `--cd`, `--cg` and `--csg`, so the row scales with the case when the stage is
 *  short. */
function placeAt(distance: number): string {
  if (distance === 0) return "translate3d(0, 0, 0)";
  const steps = Math.abs(distance) - 1;
  const dir = distance > 0 ? 1 : -1;
  const x = [
    `var(--cw) * ${0.5 * dir}`,
    `var(--cg) * ${dir}`,
    `var(--csg) * ${dir}`,
    `var(--cd) * ${dir * steps}`,
    `var(--cg) * ${dir * steps}`,
    `var(--cd) * ${0.5 * dir}`,
  ].join(" + ");
  const z = `var(--cw) * ${(-Math.min(Math.abs(distance), 8) * 0.12).toFixed(3)}`;
  return `translate3d(calc(${x}), 0, calc(${z}))`;
}

export function JcardShelf({ design, fields }: { design: JcardDesign; fields: JcardFields }) {
  const section = useRef<HTMLElement>(null);
  /* The shelf has a natural height; when the stage is shorter than it, the whole carousel is
   * scaled down together so nothing is pushed under the toolbar. `zoom` (not transform) so
   * the layout shrinks too. */
  const [fit, setFit] = useState(1);
  /* Which case is turned; the shelf's own fact, as it is on the media page. */
  const [at, setAt] = useState(CENTRE);
  /* Which case the pointer is on, read from the flat hit layer below. It is deliberately not
   * the case's own :hover: the case turns and rises while the pointer stays put, and a hover
   * that moved with the 3D box would keep dropping out from under the pointer. */
  const [hovered, setHovered] = useState<number | null>(null);
  /* The pending turn and the pending release: a case is turned on dwell, and held across a
   * short leave so moving to its neighbour does not pass through "nothing hovered". */
  const hoverIn = useRef<number | null>(null);
  const hoverOut = useRef<number | null>(null);
  useEffect(
    () => () => {
      if (hoverIn.current !== null) window.clearTimeout(hoverIn.current);
      if (hoverOut.current !== null) window.clearTimeout(hoverOut.current);
    },
    [],
  );
  useLayoutEffect(() => {
    const el = section.current;
    const parent = el?.parentElement;
    if (!el || !parent) return;
    const fitToStage = () => {
      if (parent.clientHeight === 0) return;
      setFit((current) => {
        const box = el.getBoundingClientRect();
        const natural = box.height / current;
        /* The row is centred and the case beside the turned one is a spine, seen obliquely:
         * it reaches further than the turned case does. Both ends have to stay inside the
         * window, so the fit is the smaller of what the height allows and what the width
         * does — on a wide stage the height is the only bound, as it is on the media page. */
        const centre = box.left + box.width / 2;
        let reach = 0;
        for (const face of el.querySelectorAll<HTMLElement>(".k7-front, .k7-left")) {
          const rect = face.getBoundingClientRect();
          reach = Math.max(reach, Math.abs(rect.left - centre), Math.abs(rect.right - centre));
        }
        const byHeight = parent.clientHeight / natural;
        /* A little room, so the case's frame and its sheen are not cut by the shelf's clip. */
        const byWidth =
          reach === 0 ? Infinity : (parent.clientWidth * 0.48) / (reach / current);
        return Math.max(0.25, Math.min(1.4, byHeight, byWidth));
      });
    };
    const observer = new ResizeObserver(fitToStage);
    observer.observe(parent);
    fitToStage();
    return () => observer.disconnect();
  }, []);

  const count = CASES.length;
  const select = useCallback(
    (index: number) => setAt(Math.min(Math.max(0, index), Math.max(0, count - 1))),
    [count],
  );

  /* The pointer drags the row *through* the cases: every case crossed turns the selection,
   * so the 3D follows the hand; the release lands on the nearest one. A press that never
   * moved is a click on the case. */
  const dragStart = useRef<number | null>(null);
  const dragStartAt = useRef(0);
  const dragTarget = useRef(0);
  const dragRem = useRef(16);
  const dragged = useRef(false);
  const stepsFor = (dx: number) => {
    const r = -dx / dragRem.current;
    if (Math.abs(r) < FIRST_STEP / 2) return 0;
    return Math.sign(r) * (1 + Math.round((Math.abs(r) - FIRST_STEP) / SPINE_STEP));
  };

  /* The pointer may be released outside the shelf, outside the window, or not at all; one
   * end path serves the shelf's pointerup, a window-level pointerup/cancel/blur and a lost
   * capture, so a release can never leave the row in hand. */
  const endRef = useRef<(clientX: number | null) => void>(() => {});
  endRef.current = (clientX) => {
    if (dragStart.current === null) return;
    const from = dragStart.current;
    dragStart.current = null;
    if (clientX === null) return;
    const target = Math.min(
      Math.max(0, dragStartAt.current + stepsFor(clientX - from)),
      Math.max(0, count - 1),
    );
    if (target !== dragTarget.current) {
      dragTarget.current = target;
      select(target);
    }
  };
  useEffect(() => {
    const end = (event: PointerEvent) => endRef.current(event.clientX);
    const cancel = () => endRef.current(null);
    window.addEventListener("pointerup", end);
    window.addEventListener("pointercancel", cancel);
    window.addEventListener("blur", cancel);
    return () => {
      window.removeEventListener("pointerup", end);
      window.removeEventListener("pointercancel", cancel);
      window.removeEventListener("blur", cancel);
    };
  }, []);

  /* A carousel drag can begin on any case, not only the turned one: the row moves under the
   * hand from wherever it was taken. The hover goes off for the duration, so the cases do not
   * lift and turn under the hand that is moving the row. */
  const beginDrag = (event: ReactPointerEvent) => {
    if (event.button !== 0) return;
    setHovered(null);
    /* No pointer capture: it would retarget the click to the shelf and the case would never
     * turn. The window's pointerup still ends a release that lands outside. */
    const root = parseFloat(getComputedStyle(document.documentElement).fontSize) || 16;
    dragRem.current = root * fit;
    dragStart.current = event.clientX;
    dragStartAt.current = at;
    dragTarget.current = at;
    dragged.current = false;
  };

  const hoverSide =
    hovered === null ? undefined : hovered < at ? "left" : hovered > at ? "right" : "active";
  const label = (fields.title ?? "").trim() || (fields.artist ?? "").trim() || "The J-card";

  return (
    <section
      ref={section}
      style={{ zoom: fit } as CSSProperties}
      className="flex select-none flex-col items-center gap-3"
      aria-label="The J-card on the shelf"
    >
      <div className="flex w-full min-w-0 items-stretch gap-2">
        <div
          className="k7-shelf min-w-0 flex-1"
          data-hover-side={hoverSide}
          role="group"
          aria-roledescription="J-card shelf"
          tabIndex={0}
          onDragStart={(event) => event.preventDefault()}
          onPointerMove={(event) => {
            if (dragStart.current === null) return;
            if ((event.buttons & 1) === 0) {
              endRef.current(event.clientX);
              return;
            }
            const dx = event.clientX - dragStart.current;
            if (Math.abs(dx) > 10) dragged.current = true;
            const target = Math.min(
              Math.max(0, dragStartAt.current + stepsFor(dx)),
              Math.max(0, count - 1),
            );
            if (target === dragTarget.current) return;
            dragTarget.current = target;
            select(target);
          }}
          onPointerUp={(event) => endRef.current(event.clientX)}
          onPointerCancel={() => endRef.current(null)}
        >
          <div className="k7-shelf-row">
            {CASES.map((index) => {
              const distance = index - at;
              if (Math.abs(distance) > WINDOW) return null;
              const activeCase = distance === 0;
              return (
                <div
                  key={index}
                  className="k7-shelf-item"
                  data-active={activeCase}
                  data-hover={hovered === index}
                  style={{ transform: placeAt(distance) }}
                  onPointerEnter={() => {
                    if (dragStart.current !== null) return;
                    if (hoverIn.current !== null) window.clearTimeout(hoverIn.current);
                    if (hoverOut.current !== null) {
                      window.clearTimeout(hoverOut.current);
                      hoverOut.current = null;
                    }
                    hoverIn.current = window.setTimeout(() => {
                      hoverIn.current = null;
                      setHovered(index);
                    }, HOVER_DELAY);
                  }}
                  onPointerLeave={() => {
                    if (hoverIn.current !== null) {
                      window.clearTimeout(hoverIn.current);
                      hoverIn.current = null;
                    }
                    if (hoverOut.current !== null) window.clearTimeout(hoverOut.current);
                    hoverOut.current = window.setTimeout(() => {
                      hoverOut.current = null;
                      setHovered((current) => (current === index ? null : current));
                    }, HOVER_OUT_DELAY);
                  }}
                >
                  <button
                    type="button"
                    className="k7-case"
                    data-active={activeCase}
                    aria-label={label}
                    aria-current={activeCase ? "true" : undefined}
                    onPointerDown={beginDrag}
                    onClick={() => {
                      if (dragged.current) {
                        dragged.current = false;
                        return;
                      }
                      select(index);
                    }}
                  >
                    <Faces design={design} fields={fields} />
                  </button>
                </div>
              );
            })}
          </div>
        </div>
      </div>
    </section>
  );
}

/**
 * The four faces of a case, as the shelf draws them: the cover on the front, the flap on the
 * back (folded around the spine, so it sits at the back's spine edge), and the two sides -
 * the spine on the left, the case's own on the right.
 */
function Faces({ design, fields }: { design: JcardDesign; fields: JcardFields }) {
  return (
    <>
      <span className="k7-face k7-front">
        <Jcard design={design} fields={fields} part="cover" />
        <span className="k7-frame" aria-hidden="true" />
        <span className="k7-sheen" aria-hidden="true" />
      </span>
      <span className="k7-face k7-back" aria-hidden="true">
        <span className="k7-back-flap">
          <Jcard design={design} fields={fields} part="flap" />
        </span>
        <span className="k7-sheen" aria-hidden="true" />
      </span>
      <span className="k7-face k7-side k7-right" aria-hidden="true">
        <span className="k7-sheen" aria-hidden="true" />
      </span>
      <span className="k7-face k7-side k7-left" aria-hidden="true">
        <Jcard design={design} fields={fields} part="spine" />
        <span className="k7-frame" />
        <span className="k7-sheen" aria-hidden="true" />
      </span>
    </>
  );
}

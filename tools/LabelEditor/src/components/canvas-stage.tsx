import { useEffect, useRef, useState } from "react";
import {
  Monitor,
  Scan,
  Frame as FrameIcon,
} from "lucide-react";

import { Badge } from "@/components/reui/badge";
import { Button } from "@/components/ui/button";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
} from "@/components/ui/select";
import { Separator } from "@/components/ui/separator";
import { Skeleton } from "@/components/ui/skeleton";
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip";
import { useEditor } from "@/hooks/use-editor";
import { alignOf, aperturePath } from "@/engine/tpl";
import { PRINT_H, PRINT_W, PRINT_X, PRINT_Y, SCREEN_H, SCREEN_W, WIN } from "@/engine/vocab";
import type { Obj } from "@/engine/types";

const PASTEBOARD = 48;
/** The line under the canvas (its text and the gap above it), which the fit leaves room
 *  for so the caption is not what makes the stage scroll. */
const CAPTION = 32;

/** The print's stage: the design at its one real size, on the screen it prints from. */
export function CanvasStage() {
  const ed = useEditor();
  const wrapRef = useRef<HTMLDivElement>(null);
  const printRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const captured = useRef<number | null>(null);
  const [hover, setHover] = useState<number | null>(null);

  const zoom = ed.zoom;
  const run = ed.run;

  // The canvas fits the stage it is in: a print is 800 x 480 at 100%, and the panels leave
  // less room than that in most windows. When the stage is smaller than the chosen zoom the
  // zoom comes down to the size that fits - and stays there, because a stage that grows is
  // not a reason to change the size the author picked.
  useEffect(() => {
    const box = wrapRef.current;
    if (!box) return;
    const fitToStage = () => {
      if (!box.clientWidth || !box.clientHeight) return;
      const z = Math.min(
        (box.clientWidth - PASTEBOARD * 2) / SCREEN_W,
        (box.clientHeight - PASTEBOARD * 2 - CAPTION) / SCREEN_H,
      );
      const fit = Math.max(0.25, Math.floor(z * 20) / 20);
      if (fit < ed.zoom - 0.001) ed.setZoom(fit);
    };
    const observer = new ResizeObserver(fitToStage);
    observer.observe(box);
    fitToStage();
    return () => observer.disconnect();
  }, [ed]);

  // Static text hugs its ink: measure once the faces are in, and settle.
  useEffect(() => {
    if (!run && ed.fitStaticText()) ed.emit();
  }, [ed, run, ed.getVersion(), ed.fonts.length]);

  // The preview's canvas is real LVGL output: hand it to the engine when it mounts.
  useEffect(() => {
    ed.attachCanvas(canvasRef.current);
    return () => ed.attachCanvas(null);
  }, [ed, run]);

  const toDesign = (e: React.PointerEvent) => {
    const r = printRef.current?.getBoundingClientRect();
    if (!r) return { x: 0, y: 0 };
    return { x: (e.clientX - r.left) / zoom, y: (e.clientY - r.top) / zoom };
  };

  const onPointerDown = (e: React.PointerEvent) => {
    if (run) return;
    const target = e.target as HTMLElement;
    const handle = target.dataset.handle as "rotate" | "resize" | "scale" | undefined;
    const objEl = target.closest<HTMLElement>("[data-obj-id]");
    const hit =
      handle !== undefined
        ? { kind: handle }
        : objEl
          ? { kind: "obj" as const, id: Number(objEl.dataset.objId) }
          : { kind: "empty" as const };
    ed.pointerDown(hit, toDesign(e), {
      add: e.metaKey || e.ctrlKey || e.shiftKey,
    });
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
    captured.current = e.pointerId;
  };

  const onPointerMove = (e: React.PointerEvent) => {
    if (captured.current === null) return;
    ed.pointerMove(toDesign(e), e.shiftKey);
  };

  const end = () => {
    captured.current = null;
    ed.pointerUp();
  };


  const sel = ed.sel;
  const members = ed.selMembers();
  const handles =
    sel && !run
      ? (() => {
          const mem = members.length ? members : [sel];
          const group = mem.length > 1;
          const sc = sel.type === "text" ? sel.s || 1 : 1;
          const hh = sel.type === "text" ? Math.round(ed.pxOf(sel) * 1.2) : sel.h || 0;
          const r = ((sel.rot || 0) * Math.PI) / 180;
          const cos = Math.cos(r);
          const sin = Math.sin(r);
          const hw = (sc * sel.w) / 2;
          const hhs = (sc * hh) / 2;
          const cx = sel.x + sel.w / 2;
          const cy = sel.y + hh / 2;
          const rt = (vx: number, vy: number) => ({
            x: cx + vx * cos - vy * sin,
            y: cy + vx * sin + vy * cos,
          });
          // One layer turns and resizes its own box. A group does both about its bounds, so
          // every member follows: the rotate handle sits over the bounds' middle, the scale
          // handle on their far corner.
          const b = ed.selBounds(mem);
          return {
            mem,
            group,
            resize: group ? { x: b.x1, y: b.y1 } : rt(hw, hhs),
            rotate: group ? { x: b.cx, y: b.y0 - 26 } : rt(0, -hhs - 26),
          };
        })()
      : null;

  const layerEl = (o: Obj, i: number) => {
    if (o.hidden) return null;
    const selected = o === sel;
    const multi = o !== sel && ed.selSet.has(o.id);
    const out = ed.outOfSpace(o);
    const sc = o.type === "text" ? o.s || 1 : 1;
    const colour = run ? ed.eff(o).color : ed.fillOf(o);
    const text = run ? ed.eff(o).text : ed.simText(o);
    const isHover = hover === o.id && !selected && !multi;
    return (
      <div
        key={o.id}
        data-obj-id={o.id}
        className="absolute"
        onPointerEnter={() => setHover(o.id)}
        onPointerLeave={() => setHover((h) => (h === o.id ? null : h))}
        style={{
          left: o.x,
          top: o.y,
          width: o.w,
          zIndex: i + 1,
          transform: `rotate(${o.rot || 0}deg) scale(${sc})`,
          transformOrigin: "50% 50%",
          outline: selected
            ? "1.5px solid var(--k7-accent)"
            : multi
              ? "1px dashed var(--k7-accent)"
              : out
                ? "1.5px dashed #858787"
                : isHover
                  ? "1px solid color-mix(in srgb, var(--k7-ink) 55%, transparent)"
                  : undefined,
          outlineOffset: selected ? 2 : out ? 2 : 1,
          cursor: o.locked || run ? "default" : "move",
          ...(o.type === "text"
            ? {
                color: colour,
                fontSize: ed.pxOf(o),
                fontFamily: `"${ed.ffOf(o)}", sans-serif`,
                textAlign: alignOf(o),
                lineHeight: 1.2,
                // The chrome's tracking is not the print's: the sheet draws the face's own
                // spacing, and the fit below measures without letter-spacing.
                letterSpacing: "normal",
                whiteSpace: "pre-wrap",
                textShadow: "none",
              }
            : {
                height: o.h,
                background: colour,
                borderRadius: o.type === "circle" ? "50%" : (o.r ?? 0),
              }),
        }}
      >
        {o.type === "text" ? text : null}
      </div>
    );
  };

  /* The print always has paper: every layer is painted on it, and the clip to its rounded
   * corners and the Card's window cut are the same for all of them. */
  const nodes = (
    <div
      key="paperbox"
      style={{
        position: "absolute",
        left: 0,
        top: 0,
        width: PRINT_W,
        height: PRINT_H,
        overflow: ed.outside ? "visible" : "hidden",
        borderRadius: ed.mask.radius,
        background: ed.mask.color,
        clipPath: `path(evenodd, "${aperturePath()}")`,
      }}
    >
      {ed.objs.map((o, i) => layerEl(o, i))}
    </div>
  );

  /* The Card's window is fixed and always shown: an opaque black layer on top of the print,
   * so nothing drawn under it is seen, and it is not a layer to select or move. */
  const windowLayer = (
    <div
      data-window="1"
      aria-hidden
      style={{
        position: "absolute",
        left: WIN.x,
        top: WIN.y,
        width: WIN.w,
        height: WIN.h,
        background: "#000000",
        borderRadius: WIN.r,
        pointerEvents: "none",
        zIndex: ed.objs.length + 1,
      }}
    />
  );

  return (
    <section className="flex min-h-0 min-w-0 flex-col bg-background h-full" aria-label="Canvas">
      <div className="flex h-11 shrink-0 items-center gap-2 border-b border-border px-3">
        <Badge variant="outline" size="sm" className="gap-1.5 tabnum">
          <FrameIcon className="size-3" />
          Print {PRINT_W} × {PRINT_H}
        </Badge>
        <Badge
          variant="outline"
          size="sm"
          className="hidden gap-1.5 tabnum text-muted-foreground lg:inline-flex"
        >
          <Monitor className="size-3" />
          Screen {SCREEN_W} × {SCREEN_H}
        </Badge>
        <span className="hidden whitespace-nowrap text-xs text-muted-foreground tabnum 2xl:inline">
          {run ? "LVGL 9.6 · live" : `at ${PRINT_X}, ${PRINT_Y}`}
        </span>
        <div className="ml-auto flex items-center gap-1.5">
          <Tooltip>
            <TooltipTrigger
              render={
                <Button
                  variant={ed.outside ? "outline" : "ghost"}
                  size="icon-sm"
                  aria-pressed={ed.outside}
                  onClick={() => ed.toggleOutside()}
                >
                  <Scan />
                </Button>
              }
            />
            <TooltipContent>
              {ed.outside ? "Show what lies outside the print" : "Clip to the print"} (O)
            </TooltipContent>
          </Tooltip>
          <Separator orientation="vertical" className="h-5" />
          <Select
            value={String(zoom)}
            onValueChange={(v) => ed.setZoom(Number(v) || 1)}
            items={[
              { value: "0.5", label: "50%" },
              { value: "0.75", label: "75%" },
              { value: "1", label: "100%" },
              { value: "1.5", label: "150%" },
            ]}
          >
            <SelectTrigger size="sm" className="w-20 tabnum" aria-label="Zoom">
              <span>{Math.round(zoom * 100)}%</span>
            </SelectTrigger>
            <SelectContent alignItemWithTrigger={false}>
              <SelectItem value="0.5">50%</SelectItem>
              <SelectItem value="0.75">75%</SelectItem>
              <SelectItem value="1">100%</SelectItem>
              <SelectItem value="1.5">150%</SelectItem>
            </SelectContent>
          </Select>
        </div>
      </div>

      <div
        ref={wrapRef}
        className="relative min-h-0 flex-1 overflow-auto bg-k7-well"
        style={{
          backgroundImage:
            "radial-gradient(circle at 1px 1px, color-mix(in srgb, var(--k7-line) 70%, transparent) 1px, transparent 0)",
          backgroundSize: "16px 16px",
        }}
      >
        <div
          className="flex h-max min-h-full w-max min-w-full items-center justify-center"
          style={{ padding: PASTEBOARD }}
        >
          {run ? (
            <div className="flex flex-col items-center gap-3">
              <div
                className="relative shrink-0 border border-k7-line"
                style={{
                  width: SCREEN_W * zoom,
                  height: SCREEN_H * zoom,
                  borderRadius: 10 * zoom,
                  background: ed.bg,
                  overflow: "hidden",
                }}
              >
                <div
                  style={{
                    position: "absolute",
                    left: PRINT_X * zoom,
                    top: PRINT_Y * zoom,
                    width: PRINT_W * zoom,
                    height: PRINT_H * zoom,
                    background: ed.bg,
                  }}
                >
                  <canvas
                    ref={canvasRef}
                    style={{
                      width: PRINT_W,
                      height: PRINT_H,
                      transform: `scale(${zoom})`,
                      transformOrigin: "0 0",
                      display: "block",
                    }}
                  />
                </div>
                {!ed.lvgl.module ? (
                  <div className="absolute inset-0 flex flex-col items-center justify-center gap-2 bg-background/85">
                    <Skeleton className="h-24 w-48" />
                    <p className="max-w-64 text-center text-xs text-muted-foreground">
                      {ed.lvgl.error
                        ? "The LVGL preview could not load. Reload to try again."
                        : "Loading the LVGL preview…"}
                    </p>
                  </div>
                ) : null}
              </div>
              <p className="text-xs text-muted-foreground tabnum">
                {SCREEN_W} × {SCREEN_H} · the print at {PRINT_X}, {PRINT_Y}
              </p>
            </div>
          ) : (
            <div
              className="relative shrink-0"
              style={{
                width: SCREEN_W * zoom,
                height: SCREEN_H * zoom,
                background: ed.bg,
                borderRadius: 6 * zoom,
                outline: "1px solid var(--k7-line)",
              }}
            >
              <div
                ref={printRef}
                className="canvas-surface absolute"
                onPointerDown={onPointerDown}
                onPointerMove={onPointerMove}
                onPointerUp={end}
                onPointerCancel={end}
                style={{
                  left: PRINT_X * zoom,
                  top: PRINT_Y * zoom,
                  width: PRINT_W * zoom,
                  height: PRINT_H * zoom,
                  transformOrigin: "0 0",
                  cursor:
                    ed.tool === "select" ? (sel ? "default" : "default") : "crosshair",
                }}
              >
                <div
                  className="absolute"
                  style={{
                    width: PRINT_W,
                    height: PRINT_H,
                    transform: `scale(${zoom})`,
                    transformOrigin: "0 0",
                    outline: ed.outside
                      ? "1px solid color-mix(in srgb, var(--k7-accent) 55%, transparent)"
                      : "1px solid var(--k7-line)",
                    outlineOffset: 0,
                  }}
                >
                  {nodes}
                  {windowLayer}
                  {handles && sel && !handles.mem.some((o) => o.locked) ? (
                    <>
                      <div
                        data-handle="rotate"
                        title="Drag to rotate · hold Shift for 15° steps"
                        aria-label="Rotate"
                        style={handleStyle(handles.rotate, 1 / zoom, "grab")}
                      />
                      <div
                        data-handle={handles.group ? "scale" : "resize"}
                        title={handles.group ? "Drag to scale the selection" : "Drag to resize"}
                        aria-label={handles.group ? "Scale the selection" : "Resize"}
                        style={{
                          ...handleStyle(handles.resize, 1 / zoom, "nwse-resize"),
                          borderRadius: 2,
                        }}
                      />
                    </>
                  ) : null}
                </div>
              </div>
            </div>
          )}
        </div>
      </div>
    </section>
  );
}

function handleStyle(p: { x: number; y: number }, inv: number, cursor: string) {
  return {
    position: "absolute" as const,
    left: p.x,
    top: p.y,
    width: 11,
    height: 11,
    transform: `translate(-50%, -50%) scale(${Math.max(inv, 0.5)})`,
    background: "var(--k7-accent)",
    border: "1px solid #111",
    borderRadius: 2,
    cursor,
    zIndex: 9999,
    touchAction: "none" as const,
  };
}

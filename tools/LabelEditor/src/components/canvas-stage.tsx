import { useEffect, useRef, useState } from "react";
import {
  Maximize2,
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
import { PRINT_H, PRINT_W, PRINT_X, PRINT_Y, SCREEN_H, SCREEN_W } from "@/engine/vocab";
import type { Obj } from "@/engine/types";

const PASTEBOARD = 48;

/** The print's stage: the design at its one real size, on the screen it prints from. */
export function CanvasStage() {
  const ed = useEditor();
  const wrapRef = useRef<HTMLDivElement>(null);
  const printRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const captured = useRef<number | null>(null);
  const fitted = useRef(false);
  const [hover, setHover] = useState<number | null>(null);

  const zoom = ed.zoom;
  const run = ed.run;

  // Open on a canvas that fits: a print is 800 x 480 at 100%, and a 1440 window is
  // narrower than the panels allow. The user's own zoom wins from then on.
  useEffect(() => {
    if (fitted.current) return;
    const box = wrapRef.current;
    if (!box || !box.clientWidth || !box.clientHeight) return;
    fitted.current = true;
    const z = Math.min(
      (box.clientWidth - PASTEBOARD * 2) / SCREEN_W,
      (box.clientHeight - PASTEBOARD * 2) / SCREEN_H,
    );
    if (z < 0.95) ed.setZoom(Math.max(0.25, Math.floor(z * 20) / 20));
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
    const handle = target.dataset.handle as "rotate" | "resize" | undefined;
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

  const fit = () => {
    const box = wrapRef.current;
    if (!box) return;
    const z = Math.min(
      (box.clientWidth - PASTEBOARD * 2) / SCREEN_W,
      (box.clientHeight - PASTEBOARD * 2) / SCREEN_H,
    );
    ed.setZoom(Math.max(0.25, Math.round(z * 20) / 20));
  };

  const ap = ed.apOf();
  const mask = ed.mask;
  const maskRange = ed.maskRange();
  const sel = ed.sel;
  const members = ed.selMembers();
  const handles =
    sel && !run
      ? (() => {
          const mem = members;
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
          const resize = rt(hw, hhs);
          const rotate = rt(0, -hhs - 26);
          const b = ed.selBounds(mem);
          return { mem, resize, rotate, groupRotate: { x: b.cx, y: b.y0 - 26 } };
        })()
      : null;

  const layerEl = (o: Obj, i: number) => {
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
                whiteSpace: "pre-wrap",
                textShadow: "none",
              }
            : {
                height: o.h,
                background: o.ap ? "transparent" : colour,
                borderRadius: o.type === "circle" ? "50%" : (o.r ?? 0),
              }),
        }}
      >
        {o.type === "text" ? text : null}
      </div>
    );
  };

  const maskNode = (children: React.ReactNode) => (
    <div
      key="maskbox"
      style={{
        position: "absolute",
        left: 0,
        top: 0,
        width: PRINT_W,
        height: PRINT_H,
        overflow: mask && !ed.outside ? "hidden" : "visible",
        borderRadius: mask ? mask.radius : 0,
        background: mask ? mask.color : "transparent",
        ...(mask && ap
          ? { clipPath: `path(evenodd, "${aperturePath(ap)}")` }
          : {}),
      }}
    >
      {children}
    </div>
  );

  const nodes: React.ReactNode[] = [];
  if (maskRange) {
    for (let i = 0; i < maskRange.lo; i++) nodes.push(layerEl(ed.objs[i], i));
    nodes.push(
      maskNode(
        ed.objs
          .slice(maskRange.lo, maskRange.hi + 1)
          .map((o, k) => layerEl(o, maskRange.lo + k)),
      ),
    );
    for (let i = maskRange.hi + 1; i < ed.objs.length; i++) nodes.push(layerEl(ed.objs[i], i));
  } else {
    if (mask) nodes.push(maskNode(null));
    ed.objs.forEach((o, i) => nodes.push(layerEl(o, i)));
  }

  return (
    <section className="flex min-h-0 min-w-0 flex-col bg-background" aria-label="Canvas">
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
          <Tooltip>
            <TooltipTrigger
              render={
                <Button variant="ghost" size="icon-sm" onClick={fit} aria-label="Fit to window">
                  <Maximize2 />
                </Button>
              }
            />
            <TooltipContent>Fit to window</TooltipContent>
          </Tooltip>
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
                  {handles && handles.mem.length > 1 && !handles.mem.some((o) => o.locked) ? (
                    <div
                      data-handle="rotate"
                      title="Drag to rotate · hold Shift for 15° steps"
                      aria-label="Rotate"
                      style={{
                        position: "absolute",
                        left: handles.groupRotate.x,
                        top: handles.groupRotate.y,
                        width: 12,
                        height: 12,
                        transform: "translate(-50%, -50%)",
                        background: "var(--k7-accent)",
                        border: "1px solid var(--k7-text, #111)",
                        borderRadius: 2,
                        cursor: "grab",
                        zIndex: 9999,
                      }}
                    />
                  ) : handles && sel && !sel.locked ? (
                    <>
                      <div
                        data-handle="rotate"
                        title="Drag to rotate · hold Shift for 15° steps"
                        aria-label="Rotate"
                        style={handleStyle(handles.rotate, 1 / zoom, "grab")}
                      />
                      <div
                        data-handle="resize"
                        title="Drag to resize"
                        aria-label="Resize"
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

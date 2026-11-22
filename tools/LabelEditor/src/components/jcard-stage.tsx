import { useEffect, useRef, useState } from "react";
import { Scan } from "lucide-react";

import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
} from "@/components/ui/select";
import { Separator } from "@/components/ui/separator";
import { ToggleGroup, ToggleGroupItem } from "@/components/ui/toggle-group";
import { useEditor } from "@/hooks/use-editor";
import { JCardPreview } from "@/components/jcard-preview";
import { alignOf } from "@/engine/tpl";
import { JCARD_H, JCARD_PARTS, JCARD_W } from "@/engine/jcard";
import type { JCardFocus, Obj } from "@/engine/types";

const PASTEBOARD = 48;
/** The line under the canvas (its text and the gap above it), which the fit leaves room
 *  for so the caption is not what makes the stage scroll. */
const CAPTION = 32;

const FOCUS_LABELS: Array<{ value: JCardFocus; label: string; width: number | null }> = [
  { value: "sheet", label: "Sheet", width: JCARD_W },
  { value: "flap", label: "Flap", width: JCARD_PARTS.flap.w },
  { value: "spine", label: "Spine", width: JCARD_PARTS.spine.w },
  { value: "cover", label: "Cover", width: JCARD_PARTS.cover.w },
];

/**
 * The J-card's stage: one 1300x1280 sheet at 320 units per inch, drawn in the DOM because
 * the webapp draws it and there is no firmware rendering of a paper design. The fold guides
 * are the case's, and the focus dims the part that is not being authored. The album card's
 * square is not drawn: the canvas is the sheet, and where the card crops is the App's fact.
 */
export function JCardStage() {
  const ed = useEditor();
  const wrapRef = useRef<HTMLDivElement>(null);
  const sheetRef = useRef<HTMLDivElement>(null);
  const captured = useRef<number | null>(null);
  const [hover, setHover] = useState<number | null>(null);

  const zoom = ed.zoom;
  const list = ed.jcard.prims as unknown as Obj[];
  const sel = ed.sel;
  const members = ed.selMembers();

  /** The zoom is relative to a 50% sheet: this stage's 100% is half the old stage's. */
  const JCARD_DISPLAY = 0.5;
  const display = zoom * JCARD_DISPLAY;

  /* The sheet fits the stage it is in: when the stage is smaller than the chosen zoom the
   * zoom comes down to the size that fits, and stays there - a stage that grows is not a
   * reason to change the size the author picked. */
  useEffect(() => {
    const box = wrapRef.current;
    if (!box) return;
    const fitToStage = () => {
      if (!box.clientWidth || !box.clientHeight) return;
      const z = Math.min(
        (box.clientWidth - PASTEBOARD * 2) / (JCARD_W * JCARD_DISPLAY),
        (box.clientHeight - PASTEBOARD * 2 - CAPTION) / (JCARD_H * JCARD_DISPLAY),
      );
      const fit = Math.max(0.1, Math.floor(z * 20) / 20);
      if (fit < ed.zoom - 0.001) ed.setZoom(fit);
    };
    const observer = new ResizeObserver(fitToStage);
    observer.observe(box);
    fitToStage();
    return () => observer.disconnect();
  }, [ed]);

  if (ed.run) return <JCardPreview />;

  const toDesign = (e: React.PointerEvent) => {
    const r = sheetRef.current?.getBoundingClientRect();
    if (!r) return { x: 0, y: 0 };
    return { x: (e.clientX - r.left) / display, y: (e.clientY - r.top) / display };
  };

  const onPointerDown = (e: React.PointerEvent) => {
    if (ed.run) return;
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


  const handles =
    sel && !ed.run
      ? (() => {
          const mem = members.length ? members : [sel];
          const group = mem.length > 1;
          const hh = sel.type === "text" ? Math.round(ed.pxOf(sel) * 1.2) : sel.h || 0;
          const r = ((sel.rot || 0) * Math.PI) / 180;
          const cos = Math.cos(r);
          const sin = Math.sin(r);
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
            resize: group ? { x: b.x1, y: b.y1 } : rt(sel.w / 2, hh / 2),
            rotate: group ? { x: b.cx, y: b.y0 - 26 } : rt(0, -hh / 2 - 26),
          };
        })()
      : null;

  const layerEl = (o: Obj, i: number) => {
    if (o.hidden) return null;
    const selected = o === sel;
    const multi = o !== sel && ed.selSet.has(o.id);
    const out = ed.outOfSpace(o);
    const isHover = hover === o.id && !selected && !multi;
    const colour = ed.fillOf(o);
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
          transform: `rotate(${o.rot || 0}deg)`,
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
          cursor: o.locked || ed.run ? "default" : "move",
          ...(o.type === "text"
            ? {
                color: colour,
                fontSize: ed.pxOf(o),
                fontFamily: `"${ed.ffOf(o)}", sans-serif`,
                textAlign: alignOf(o),
                lineHeight: 1.2,
                letterSpacing: "normal",
                whiteSpace: "pre-wrap",
                overflow: "hidden",
                textShadow: "none",
              }
            : {
                height: o.h,
                background: colour,
                borderRadius: o.type === "circle" ? "50%" : (o.r ?? 0),
              }),
        }}
      >
        {o.type === "text" ? ed.simText(o) : null}
      </div>
    );
  };

  const focused = ed.focus === "sheet" ? null : JCARD_PARTS[ed.focus];
  const focusLabel =
    ed.focus === "sheet"
      ? "Whole sheet"
      : `${ed.focus[0].toUpperCase()}${ed.focus.slice(1)} · ${JCARD_PARTS[ed.focus].w} × ${JCARD_H}`;

  return (
    <section className="flex min-h-0 min-w-0 flex-col bg-background h-full" aria-label="J-card canvas">
      <div className="flex h-11 shrink-0 items-center gap-2 border-b border-border px-3">
        <div className="ml-auto flex items-center gap-1.5">
          <ToggleGroup
            value={[ed.focus]}
            onValueChange={(v) => ed.setFocus((v[0] as JCardFocus) || "sheet")}
            variant="outline"
            size="sm"
            spacing={0}
            aria-label="J-card focus"
          >
            {FOCUS_LABELS.map((f) => (
              <ToggleGroupItem key={f.value} value={f.value} className="px-2 tabnum">
                {f.label}
              </ToggleGroupItem>
            ))}
          </ToggleGroup>
          <Separator orientation="vertical" className="h-5" />
          <Select
            value={String(zoom)}
            onValueChange={(v) => ed.setZoom(Number(v) || 1)}
            items={[
              { value: "0.25", label: "25%" },
              { value: "0.5", label: "50%" },
              { value: "0.75", label: "75%" },
              { value: "1", label: "100%" },
            ]}
          >
            <SelectTrigger size="sm" className="w-20 tabnum" aria-label="Zoom">
              <span>{Math.round(zoom * 100)}%</span>
            </SelectTrigger>
            <SelectContent alignItemWithTrigger={false}>
              <SelectItem value="0.25">25%</SelectItem>
              <SelectItem value="0.5">50%</SelectItem>
              <SelectItem value="0.75">75%</SelectItem>
              <SelectItem value="1">100%</SelectItem>
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
          <div className="flex flex-col items-center gap-3">
            <div
              ref={sheetRef}
              className="canvas-surface relative shrink-0"
              onPointerDown={onPointerDown}
              onPointerMove={onPointerMove}
              onPointerUp={end}
              onPointerCancel={end}
              style={{
                width: JCARD_W * display,
                height: JCARD_H * display,
                outline: "1px solid var(--k7-line)",
                background: ed.jcard.background,
                cursor: ed.tool === "select" ? "default" : "crosshair",
              }}
            >
              {/* The sheet clips what bleeds past it. The handles are its siblings, outside
                  that clip, so the layer that fills the sheet can still be turned and scaled. */}
              <div className="absolute inset-0 overflow-hidden">
                <div
                  className="absolute"
                  style={{
                    width: JCARD_W,
                    height: JCARD_H,
                    transform: `scale(${display})`,
                    transformOrigin: "0 0",
                    background: ed.jcard.background,
                  }}
                >
                  {list.map((o, i) => layerEl(o, i))}

                  {/* The fold guides: the case's, not the design's. */}
                  <div
                    className="pointer-events-none absolute inset-0"
                    style={{ zIndex: 5000 }}
                    aria-hidden
                  >
                    {[JCARD_PARTS.spine.x, JCARD_PARTS.cover.x].map((x) => (
                      <div
                        key={x}
                        className="absolute top-0 h-full"
                        style={{
                          left: x,
                          width: 1,
                          background: "color-mix(in srgb, var(--k7-accent) 70%, transparent)",
                        }}
                      />
                    ))}
                    {/* The focus dims everything outside the part being authored. */}
                    {focused ? (
                      <div
                        className="absolute"
                        style={{
                          left: focused.x,
                          top: focused.y,
                          width: focused.w,
                          height: focused.h,
                          outline: "2px solid var(--k7-accent)",
                          boxShadow: "0 0 0 9999px rgba(0, 0, 0, 0.22)",
                        }}
                      />
                    ) : null}
                  </div>
                </div>
              </div>

              {handles && sel && !handles.mem.some((o) => o.locked) ? (
                <>
                  <div
                    data-handle="rotate"
                    title="Drag to rotate · hold Shift for 15° steps"
                    aria-label="Rotate"
                    style={gripStyle(handles.rotate, display, "grab")}
                  />
                  <div
                    data-handle={handles.group ? "scale" : "resize"}
                    title={handles.group ? "Drag to scale the selection" : "Drag to resize"}
                    aria-label={handles.group ? "Scale the selection" : "Resize"}
                    style={{
                      ...gripStyle(handles.resize, display, "nwse-resize"),
                      borderRadius: 2,
                    }}
                  />
                </>
              ) : null}
            </div>
            <p className="flex items-center gap-2 text-xs text-muted-foreground tabnum">
              <Scan className="size-3" />
              {focusLabel} · folds at 320 and 480
            </p>
          </div>
        </div>
      </div>
    </section>
  );
}

/** A handle's screen position, from the design point it marks: the sheet's handles are drawn
 *  over it rather than inside its clip, so they are in screen pixels already. */
function gripStyle(p: { x: number; y: number }, display: number, cursor: string) {
  return {
    position: "absolute" as const,
    left: p.x * display,
    top: p.y * display,
    width: 11,
    height: 11,
    transform: "translate(-50%, -50%)",
    background: "var(--k7-accent)",
    border: "1px solid #111",
    borderRadius: 2,
    pointerEvents: "auto" as const,
    cursor,
    zIndex: 10_000,
    touchAction: "none" as const,
  };
}

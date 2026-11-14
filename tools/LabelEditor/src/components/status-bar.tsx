import { CheckCircle2, CircleDot, RotateCw, TriangleAlert } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Separator } from "@/components/ui/separator";
import { useEditor } from "@/hooks/use-editor";
import { PRINT_H, PRINT_W, SCREEN_H, SCREEN_W } from "@/engine/vocab";

export function StatusBar() {
  const ed = useEditor();
  const sel = ed.sel;
  const fields = ed.objs.filter((o) => o.type === "text" && !o.static).length;
  const lv = ed.lvgl;

  return (
    <footer className="flex h-6 shrink-0 items-center gap-2 border-t border-border bg-background px-3 text-xs text-muted-foreground">
      <span className="tabnum">
        {SCREEN_W} × {SCREEN_H} panel
      </span>
      <Separator orientation="vertical" className="hidden h-3 xl:block" />
      <span className="hidden tabnum xl:inline">
        print {PRINT_W} × {PRINT_H}
      </span>
      <Separator orientation="vertical" className="h-3" />
      <span className="tabnum">{ed.objs.length} layers</span>
      <Separator orientation="vertical" className="hidden h-3 xl:block" />
      <span className="hidden tabnum xl:inline">{fields} fields</span>
      {ed.mask ? (
        <>
          <Separator orientation="vertical" className="hidden h-3 xl:block" />
          <span className="hidden xl:inline">LayerMask</span>
        </>
      ) : null}

      {sel ? (
        <>
          <Separator orientation="vertical" className="hidden h-3 xl:block" />
          <span className="hidden truncate tabnum xl:inline">
            {sel.type} · {Math.round(sel.x)}, {Math.round(sel.y)} · {Math.round(sel.w)}×
            {Math.round(sel.h ?? ed.pxOf(sel) * 1.2)}
            {sel.rot ? ` · ${Math.round(sel.rot)}°` : ""}
          </span>
        </>
      ) : null}

      <span className="ml-auto flex items-center gap-3">
        <span className="hidden items-center gap-1 xl:flex">
          <CheckCircle2 className="size-3 text-primary" />
          autosaved here
        </span>
        <Separator orientation="vertical" className="h-3" />
        <span className="flex items-center gap-1">
          {lv.error ? (
            <>
              <TriangleAlert className="size-3" />
              preview unavailable
              <Button
                variant="ghost"
                size="icon-xs"
                onClick={() => ed.retryLvgl()}
                aria-label="Retry the LVGL preview"
              >
                <RotateCw className="size-3" />
              </Button>
            </>
          ) : lv.module ? (
            <>
              <CircleDot className="size-3 text-primary" />
              LVGL ready
            </>
          ) : (
            <>
              <CircleDot className="size-3 animate-pulse" />
              loading LVGL…
            </>
          )}
        </span>
        <Separator orientation="vertical" className="h-3" />
        <span className="tabnum">{Math.round(ed.zoom * 100)}%</span>
      </span>
    </footer>
  );
}

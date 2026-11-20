import { Bot, CheckCircle2, CircleDot, RotateCw, TriangleAlert } from "lucide-react";

import { Button } from "@/components/ui/button";
import { Separator } from "@/components/ui/separator";
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip";
import { useEditor } from "@/hooks/use-editor";
import { PRINT_H, PRINT_W, SCREEN_H, SCREEN_W } from "@/engine/vocab";
import { JCARD_H, JCARD_W } from "@/engine/jcard";

export function StatusBar({ onAgentTools }: { onAgentTools: () => void }) {
  const ed = useEditor();
  const sel = ed.sel;
  const active = ed.activeObjs();
  const jcard = ed.isJCard();
  const fields = active.filter((o) => o.type === "text" && !o.static).length;
  const lv = ed.lvgl;

  return (
    <footer className="flex h-6 shrink-0 items-center gap-2 border-t border-border bg-background px-3 text-xs text-muted-foreground">
      <span className="tabnum">
        {jcard ? `J-card ${JCARD_W} × ${JCARD_H}` : `${SCREEN_W} × ${SCREEN_H} panel`}
      </span>
      <Separator orientation="vertical" className="hidden h-3 xl:block" />
      <span className="hidden tabnum xl:inline">
        {jcard ? "flap | spine | cover" : `print ${PRINT_W} × ${PRINT_H}`}
      </span>
      <Separator orientation="vertical" className="h-3" />
      <span className="tabnum">{active.length} layers</span>
      <Separator orientation="vertical" className="hidden h-3 xl:block" />
      <span className="hidden tabnum xl:inline">
        {fields} {jcard ? "J-card fields" : "fields"}
      </span>
      {!jcard && ed.mask ? (
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
            {Math.round(sel.type === "text" ? ed.pxOf(sel) * 1.2 : (sel.h ?? 0))}
            {sel.rot ? ` · ${Math.round(sel.rot)}°` : ""}
          </span>
        </>
      ) : null}

      <span className="ml-auto flex items-center gap-3">
        <Tooltip>
          <TooltipTrigger
            render={
              <Button
                variant="ghost"
                size="xs"
                className="gap-1 text-muted-foreground"
                aria-label="Agent tools (WebMCP)"
                onClick={onAgentTools}
              >
                <Bot className="size-3" />
                <span className="hidden xl:inline">WebMCP</span>
              </Button>
            }
          />
          <TooltipContent>Agent tools (WebMCP)</TooltipContent>
        </Tooltip>
        <Separator orientation="vertical" className="h-3" />
        <span className="hidden items-center gap-1 xl:flex">
          <CheckCircle2 className="size-3 text-primary" />
          autosaved here
        </span>
        <Separator orientation="vertical" className="h-3" />
        <span className="flex items-center gap-1">
          {jcard ? (
            <>
              <CircleDot className="size-3 text-primary" />
              J-card · DOM
            </>
          ) : lv.error ? (
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

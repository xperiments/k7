import { Circle, MousePointer2, RectangleHorizontal, Square, Type } from "lucide-react";

import { ToggleGroup, ToggleGroupItem } from "@/components/ui/toggle-group";
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip";
import { useEditor } from "@/hooks/use-editor";
import type { Tool } from "@/engine/types";

const TOOLS: Array<{ value: Tool; label: string; hint: string; icon: typeof Square }> = [
  { value: "select", label: "Select", hint: "V", icon: MousePointer2 },
  { value: "rect", label: "Rectangle", hint: "R", icon: RectangleHorizontal },
  { value: "square", label: "Square", hint: "S", icon: Square },
  { value: "circle", label: "Circle", hint: "O", icon: Circle },
  { value: "text", label: "Text", hint: "T", icon: Type },
];

/** The tools, as a rail: one pressed item, and a cursor back to Select after every add. */
export function ToolRail() {
  const ed = useEditor();
  return (
    <nav
      aria-label="Tools"
      className="flex w-12 shrink-0 flex-col items-center gap-2 border-r border-border bg-background py-2"
    >
      <ToggleGroup
        value={[ed.tool]}
        onValueChange={(v) => ed.setTool((v[0] as Tool) || "select")}
        orientation="vertical"
        spacing={2}
        aria-label="Drawing tool"
        className="flex flex-col"
      >
        {TOOLS.map((t) => {
          const Icon = t.icon;
          return (
            <Tooltip key={t.value}>
              <TooltipTrigger
                render={
                  <ToggleGroupItem
                    value={t.value}
                    aria-label={`${t.label} (${t.hint})`}
                    className="size-8 aria-pressed:border aria-pressed:border-primary/60 aria-pressed:text-primary"
                  >
                    <Icon className="size-4" />
                  </ToggleGroupItem>
                }
              />
              <TooltipContent side="right">
                {t.label} <span className="text-muted-foreground">{t.hint}</span>
              </TooltipContent>
            </Tooltip>
          );
        })}
      </ToggleGroup>

    </nav>
  );
}

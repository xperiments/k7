import { Bot } from "lucide-react";

import { Button } from "@/components/ui/button";
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip";

/**
 * The bar along the bottom: one button, the editor's agent tools. The readouts it used to
 * carry - the sheet's size, its resolution, the panel widths, the selection's numbers, the
 * LVGL state - are the panels' now, where the thing they describe is being edited.
 */
export function StatusBar({ onAgentTools }: { onAgentTools: () => void }) {
  return (
    <footer className="flex h-6 shrink-0 items-center gap-2 border-t border-border bg-background px-3 text-xs text-muted-foreground">
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
      </span>
    </footer>
  );
}

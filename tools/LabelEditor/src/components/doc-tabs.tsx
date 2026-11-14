import { useRef, useState } from "react";
import { Plus, X } from "lucide-react";

import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip";
import { useEditor } from "@/hooks/use-editor";

/** One tab per open design: the editor holds several at once, as the builder did. */
export function DocTabs() {
  const ed = useEditor();
  const [renaming, setRenaming] = useState<number | null>(null);
  const [closing, setClosing] = useState<number | null>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  return (
    <div className="flex h-9 shrink-0 items-stretch gap-1 border-b border-border bg-background px-2">
      <div
        role="tablist"
        aria-label="Open designs"
        className="flex min-w-0 flex-1 items-stretch gap-1 overflow-x-auto"
      >
        {ed.docs.map((d, i) => {
          const active = i === ed.active;
          return (
            <div
              key={d.id}
              role="tab"
              aria-selected={active}
              tabIndex={active ? 0 : -1}
              onKeyDown={(e) => {
                if (e.key === "Enter" || e.key === " ") {
                  e.preventDefault();
                  ed.switchDoc(i);
                  return;
                }
                if (e.key === "ArrowRight" || e.key === "ArrowLeft") {
                  e.preventDefault();
                  const next =
                    (i + (e.key === "ArrowRight" ? 1 : -1) + ed.docs.length) %
                    ed.docs.length;
                  ed.switchDoc(next);
                  requestAnimationFrame(() =>
                    document
                      .querySelectorAll<HTMLElement>('[role="tab"]')
                      [next]?.focus(),
                  );
                }
              }}
              onClick={() => ed.switchDoc(i)}
              onDoubleClick={() => {
                setRenaming(i);
                requestAnimationFrame(() => inputRef.current?.select());
              }}
              className={`group/tab relative flex min-w-0 max-w-52 items-center gap-1.5 border border-b-0 px-2.5 text-xs transition-colors ${
                active
                  ? "border-border bg-k7-surface text-foreground"
                  : "border-transparent text-muted-foreground hover:bg-k7-surface/50 hover:text-foreground"
              }`}
            >
              {renaming === i ? (
                <Input
                  ref={inputRef}
                  autoFocus
                  defaultValue={d.name}
                  className="h-6 w-32 px-1 text-xs"
                  onClick={(e) => e.stopPropagation()}
                  onKeyDown={(e) => {
                    e.stopPropagation();
                    if (e.key === "Enter") {
                      ed.renameDoc(i, e.currentTarget.value);
                      setRenaming(null);
                    }
                    if (e.key === "Escape") setRenaming(null);
                  }}
                  onBlur={(e) => {
                    ed.renameDoc(i, e.target.value);
                    setRenaming(null);
                  }}
                />
              ) : (
                <span className="truncate">{d.name}</span>
              )}
              <Tooltip>
                <TooltipTrigger
                  render={
                    <Button
                      variant="ghost"
                      size="icon-xs"
                      aria-label={`Close ${d.name}`}
                      className={`ml-auto ${active ? "opacity-60 hover:opacity-100" : "opacity-0 group-hover/tab:opacity-100"} focus-visible:opacity-100`}
                      onClick={(e) => {
                        e.stopPropagation();
                        setClosing(i);
                      }}
                    >
                      <X className="size-3" />
                    </Button>
                  }
                />
                <TooltipContent>Close design</TooltipContent>
              </Tooltip>
            </div>
          );
        })}
      </div>
      <Tooltip>
        <TooltipTrigger
          render={
            <Button
              variant="ghost"
              size="icon-sm"
              className="self-center"
              aria-label="New design"
              onClick={() => ed.newDoc()}
            >
              <Plus />
            </Button>
          }
        />
        <TooltipContent>New design</TooltipContent>
      </Tooltip>

      <AlertDialog open={closing !== null} onOpenChange={(o) => !o && setClosing(null)}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>
              Delete “{closing !== null ? ed.docs[closing]?.name : ""}”?
            </AlertDialogTitle>
            <AlertDialogDescription>
              The design and its undo history are removed from this browser. A saved
              document or an exported template is not affected.
            </AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel>Keep it</AlertDialogCancel>
            <AlertDialogAction
              onClick={() => {
                if (closing !== null) ed.closeDoc(closing);
                setClosing(null);
              }}
            >
              Delete
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}

import { useState } from "react";
import { AlertTriangle, Check, Download, FileJson } from "lucide-react";
import { toast } from "sonner";

import { Alert, AlertDescription, AlertTitle } from "@/components/reui/alert";
import { Badge } from "@/components/reui/badge";
import { Frame, FramePanel } from "@/components/reui/frame";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Kbd } from "@/components/ui/kbd";
import { Separator } from "@/components/ui/separator";
import { Spinner } from "@/components/ui/spinner";
import { useEditor } from "@/hooks/use-editor";
import { downloadBlob, exportAll } from "@/engine/files";
import { exportTemplate } from "@/engine/actions";
import { PRINT_H, PRINT_W } from "@/engine/vocab";

/** Export is a decision: every open design is checked before one byte is written. */
export function ExportDialog({
  open,
  onOpenChange,
  onProblems,
}: {
  open: boolean;
  onOpenChange: (v: boolean) => void;
  onProblems: (title: string, errors: string[]) => void;
}) {
  const ed = useEditor();
  const [running, setRunning] = useState(false);
  const rows = open ? ed.validateAll() : [];
  const anyErr = rows.some((r) => r.errs.length);
  const lvglReady = !!ed.lvgl.module;

  const onOne = () => {
    const r = exportTemplate(ed);
    if (r.errs.length) {
      onProblems("This design cannot be exported yet", r.errs);
      return;
    }
    toast.success("Template exported", { description: r.name });
  };

  const onAll = async () => {
    setRunning(true);
    try {
      const r = await exportAll(ed);
      if (r.errors.length) {
        onProblems("Export refused", r.errors);
        return;
      }
      if (!r.blob) return;
      downloadBlob("label-templates.zip", r.blob);
      toast.success("Templates exported", {
        description: `${r.filenames.length} files · label-templates.zip`,
      });
      onOpenChange(false);
    } catch (err) {
      onProblems("Export failed", [(err as Error)?.message || String(err)]);
    } finally {
      setRunning(false);
    }
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-xl">
        <DialogHeader>
          <DialogTitle>Export templates</DialogTitle>
          <DialogDescription>
            One JSON per design, a PNG preview each and a <code>labels.json</code> manifest,
            in one zip. The sheet is {PRINT_W} × {PRINT_H}; every design is checked first.
          </DialogDescription>
        </DialogHeader>

        <Frame stacked spacing="sm" className="max-h-64 overflow-y-auto">
          {rows.length ? (
            rows.map((r, i) => (
              <FramePanel key={i} className="py-2">
                <div className="flex items-center gap-2 px-1">
                  <span className="truncate text-sm font-medium">{r.name}</span>
                  {r.errs.length ? (
                    <Badge variant="destructive-light" className="ml-auto shrink-0">
                      {r.errs.length} issue{r.errs.length === 1 ? "" : "s"}
                    </Badge>
                  ) : (
                    <Badge variant="outline" className="ml-auto shrink-0 text-primary">
                      <Check className="size-3" />
                      ready
                    </Badge>
                  )}
                  <Badge variant="outline" className="shrink-0 tabnum">
                    {i === ed.active ? "open" : "tab"}
                  </Badge>
                </div>
                {r.errs.length ? (
                  <ul className="mt-2 flex flex-col gap-1 px-1">
                    {r.errs.map((e, k) => (
                      <li key={k} className="flex gap-1.5 text-xs text-muted-foreground">
                        <span className="mt-1 size-1 shrink-0 rounded-full bg-destructive" />
                        <span>{e}</span>
                      </li>
                    ))}
                  </ul>
                ) : null}
              </FramePanel>
            ))
          ) : (
            <FramePanel>
              <p className="text-xs text-muted-foreground">No designs are open.</p>
            </FramePanel>
          )}
        </Frame>

        {!lvglReady ? (
          <Alert variant="warning">
            <AlertTriangle />
            <AlertTitle>The LVGL preview is still loading</AlertTitle>
            <AlertDescription>
              A design's PNG is the preview's own output. Wait a moment and open Export again.
            </AlertDescription>
          </Alert>
        ) : null}

        <DialogFooter className="flex-row items-center justify-between gap-2 sm:justify-between">
          <Button variant="ghost" size="sm" onClick={() => onOpenChange(false)}>
            Cancel
          </Button>
          <div className="flex items-center gap-1.5">
            <Button variant="outline" size="sm" onClick={onOne}>
              <FileJson />
              Template JSON
            </Button>
            <Button
              size="sm"
              onClick={() => void onAll()}
              disabled={running || anyErr || !lvglReady}
            >
              {running ? <Spinner className="size-3.5" /> : <Download />}
              {running ? "Packing…" : "Export all as ZIP"}
            </Button>
          </div>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

export function ProblemsDialog({
  title,
  errors,
  onClose,
}: {
  title: string;
  errors: string[];
  onClose: () => void;
}) {
  return (
    <Dialog open onOpenChange={(o) => !o && onClose()}>
      <DialogContent className="sm:max-w-lg">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <AlertTriangle className="size-4 text-destructive" />
            {title}
          </DialogTitle>
          <DialogDescription>
            {errors.length} thing{errors.length === 1 ? "" : "s"} the packer would refuse.
            Fix them and try again.
          </DialogDescription>
        </DialogHeader>
        <div className="max-h-72 overflow-y-auto rounded-lg border border-border bg-k7-well p-2">
          <ul className="flex flex-col gap-1.5">
            {errors.map((e, i) => (
              <li key={i} className="flex gap-2 text-xs text-muted-foreground">
                <span className="tabnum text-muted-foreground/60">{String(i + 1).padStart(2, "0")}</span>
                <span>{e}</span>
              </li>
            ))}
          </ul>
        </div>
        <DialogFooter>
          <Button size="sm" onClick={onClose}>
            Close
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

const SHORTCUTS: Array<[string, string[]]> = [
  ["Select · Rectangle · Square · Circle · Text", ["V", "R", "S", "O", "T"]],
  ["Undo", ["⌘", "Z"]],
  ["Redo", ["⇧", "⌘", "Z"]],
  ["Copy · Paste", ["⌘", "C", "/", "⌘", "V"]],
  ["Group · Ungroup", ["⌘", "G", "/", "⇧", "⌘", "G"]],
  ["Delete the selection", ["⌫"]],
  ["Nudge by 1 px · 10 px", ["←↑→↓", "/", "⇧", "+", "←↑→↓"]],
  ["Save the document", ["⌘", "S"]],
  ["Import a JSON file", ["⌘", "O"]],
  ["Export the template", ["⌘", "E"]],
  ["Preview · Design", ["⌘", "↵"]],
  ["Deselect · leave Preview", ["Esc"]],
  ["This sheet", ["?"]],
];

export function ShortcutsDialog({ open, onOpenChange }: { open: boolean; onOpenChange: (v: boolean) => void }) {
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-md">
        <DialogHeader>
          <DialogTitle>Keyboard</DialogTitle>
          <DialogDescription>Every edit is undoable: the history holds the last 200.</DialogDescription>
        </DialogHeader>
        <div className="flex flex-col">
          {SHORTCUTS.map(([what, keys], i) => (
            <div key={what}>
              {i ? <Separator /> : null}
              <div className="flex items-center justify-between gap-3 py-1.5">
                <span className="text-xs text-muted-foreground">{what}</span>
                <span className="flex shrink-0 items-center gap-0.5">
                  {keys.map((k, j) =>
                    k === "/" || k === "+" || k === "·" ? (
                      <span key={j} className="px-0.5 text-xs text-muted-foreground">
                        {k}
                      </span>
                    ) : (
                      <Kbd key={j} className="h-5 min-w-5 px-1 text-xs">
                        {k}
                      </Kbd>
                    ),
                  )}
                </span>
              </div>
            </div>
          ))}
        </div>
        <DialogFooter>
          <Button size="sm" onClick={() => onOpenChange(false)}>
            Done
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}


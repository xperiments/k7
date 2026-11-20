import { useCallback, useEffect, useRef, useState } from "react";
import { toast } from "sonner";

import { FileJson } from "lucide-react";

import { Toaster } from "@/components/ui/sonner";
import { TooltipProvider } from "@/components/ui/tooltip";
import { AppBar } from "@/components/app-bar";
import { CanvasStage } from "@/components/canvas-stage";
import { DocTabs } from "@/components/doc-tabs";
import { ExportDialog, ProblemsDialog, ShortcutsDialog } from "@/components/dialogs";
import { JCardStage } from "@/components/jcard-stage";
import { PublishDialog } from "@/components/publish-dialog";
import { AgentToolsDialog } from "@/components/agent-tools-dialog";
import { Inspector, InspectorRail } from "@/components/inspector";
import { LayersPanel } from "@/components/layers-panel";
import { StatusBar } from "@/components/status-bar";
import { ToolRail } from "@/components/tool-rail";
import { useEditor } from "@/hooks/use-editor";
import { exportTemplate, saveDocument } from "@/engine/actions";
import { useImport } from "@/hooks/use-import";

export function App() {
  const ed = useEditor();
  const [exportOpen, setExportOpen] = useState(false);
  const [publishOpen, setPublishOpen] = useState(false);
  const [shortcutsOpen, setShortcutsOpen] = useState(false);
  const [agentToolsOpen, setAgentToolsOpen] = useState(false);
  const [problems, setProblems] = useState<{ title: string; errors: string[] } | null>(null);
  const [dragDepth, setDragDepth] = useState(0);
  const [inspectorOpen, setInspectorOpen] = useState(() => {
    try {
      return localStorage.getItem("label-editor.inspector") !== "0";
    } catch {
      return true;
    }
  });
  const openRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    try {
      localStorage.setItem("label-editor.inspector", inspectorOpen ? "1" : "0");
    } catch {
      /* a private window has no storage; the collapse still works for the session */
    }
  }, [inspectorOpen]);

  const onProblems = useCallback((title: string, errors: string[]) => {
    setProblems({ title, errors });
  }, []);
  const { importFiles } = useImport(ed, onProblems);

  const carriesFiles = (e: React.DragEvent) =>
    Array.from(e.dataTransfer.types).includes("Files");

  useEffect(() => {
    const typing = (el: EventTarget | null) => {
      const t = el as HTMLElement | null;
      if (!t) return false;
      return (
        t.tagName === "INPUT" ||
        t.tagName === "TEXTAREA" ||
        t.tagName === "SELECT" ||
        t.isContentEditable
      );
    };
    const onKey = (e: KeyboardEvent) => {
      if (typing(e.target)) return;
      // Only an open modal owns the keyboard. A closed popover stays mounted with
      // role="dialog" (Base UI), and must not swallow every shortcut.
      if (document.querySelector('[role="dialog"][data-open], [role="alertdialog"][data-open]'))
        return;
      const mod = e.metaKey || e.ctrlKey;
      const k = e.key.toLowerCase();

      if (mod && k === "s") {
        e.preventDefault();
        const name = saveDocument(ed);
        toast.success("Document saved", { description: name });
        return;
      }
      if (mod && k === "e") {
        e.preventDefault();
        const r = exportTemplate(ed);
        if (r.errs.length) onProblems("This design cannot be exported yet", r.errs);
        else toast.success("K7Art exported", { description: r.name });
        return;
      }
      if (mod && k === "o") {
        e.preventDefault();
        openRef.current?.click();
        return;
      }
      if (mod && e.key === "Enter") {
        e.preventDefault();
        ed.setRun(!ed.run);
        return;
      }
      if (mod && k === "z") {
        e.preventDefault();
        if (e.shiftKey) ed.redoAct();
        else ed.undoAct();
        return;
      }
      if (mod && k === "y") {
        e.preventDefault();
        ed.redoAct();
        return;
      }
      if (mod && k === "c") {
        ed.copyAct();
        return;
      }
      if (mod && k === "v") {
        e.preventDefault();
        ed.pasteAct();
        return;
      }
      if (mod && k === "g") {
        e.preventDefault();
        if (e.shiftKey) ed.ungroupSel();
        else ed.groupSel();
        return;
      }
      if (e.key === "Escape") {
        if (ed.run) ed.setRun(false);
        else ed.clearSelection();
        return;
      }
      if (!ed.run && !mod) {
        const tools: Record<string, "select" | "rect" | "square" | "circle" | "text"> = {
          v: "select",
          r: "rect",
          s: "square",
          o: "circle",
          t: "text",
        };
        if (tools[k]) {
          e.preventDefault();
          ed.setTool(tools[k]);
          return;
        }
      }
      if (e.key === "?") {
        e.preventDefault();
        setShortcutsOpen(true);
        return;
      }
      if (ed.run) return;
      if (e.key.startsWith("Arrow") && ed.sel) {
        e.preventDefault();
        const d = e.shiftKey ? 10 : 1;
        ed.nudgeSel(
          e.key === "ArrowLeft" ? -d : e.key === "ArrowRight" ? d : 0,
          e.key === "ArrowUp" ? -d : e.key === "ArrowDown" ? d : 0,
        );
        return;
      }
      if (e.key === "Delete" || e.key === "Backspace") {
        if (ed.sel) {
          e.preventDefault();
          ed.delSel();
        }
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [ed, onProblems]);

  return (
    <TooltipProvider>
      <div
        className="relative flex h-full min-h-0 flex-col overflow-hidden bg-background"
        onDragEnter={(e) => {
          if (!carriesFiles(e)) return;
          e.preventDefault();
          setDragDepth((d) => d + 1);
        }}
        onDragOver={(e) => {
          if (!carriesFiles(e)) return;
          e.preventDefault();
          e.dataTransfer.dropEffect = "copy";
        }}
        onDragLeave={(e) => {
          if (!carriesFiles(e)) return;
          setDragDepth((d) => Math.max(0, d - 1));
        }}
        onDrop={(e) => {
          if (!e.dataTransfer.files.length) return;
          e.preventDefault();
          setDragDepth(0);
          void importFiles(e.dataTransfer.files);
        }}
      >
        <AppBar
          onExport={() => setExportOpen(true)}
          onPublish={() => setPublishOpen(true)}
          onShortcuts={() => setShortcutsOpen(true)}
          onProblems={onProblems}
          onImport={() => openRef.current?.click()}
        />
        <DocTabs />
        <div className="flex min-h-0 min-w-0 flex-1">
          <ToolRail />
          <div className="hidden w-56 shrink-0 xl:flex">
            <LayersPanel />
          </div>
          <div className="min-w-0 flex-1">
            {ed.isJCard() ? <JCardStage /> : <CanvasStage />}
          </div>
          {inspectorOpen ? (
            <div className="hidden w-72 shrink-0 lg:flex">
              <Inspector onCollapse={() => setInspectorOpen(false)} />
            </div>
          ) : (
            <InspectorRail onExpand={() => setInspectorOpen(true)} />
          )}
        </div>
        <StatusBar onAgentTools={() => setAgentToolsOpen(true)} />
        {dragDepth > 0 ? (
          <div className="pointer-events-none absolute inset-0 z-50 flex items-center justify-center bg-background/80">
            <div className="flex flex-col items-center gap-2 rounded-lg border-2 border-dashed border-primary bg-background px-8 py-6 text-center">
              <FileJson className="size-6 text-primary" />
              <p className="text-sm font-medium tracking-tight">Drop to import</p>
              <p className="text-xs text-muted-foreground">
                A K7Art, a retired label template or an editor document (.lvgl.json)
              </p>
            </div>
          </div>
        ) : null}
      </div>

      <input
        ref={openRef}
        type="file"
        accept=".json,application/json"
        multiple
        className="hidden"
        onChange={(e) => {
          // Copy first: clearing the input empties the live FileList.
          const files = e.target.files ? Array.from(e.target.files) : [];
          e.target.value = "";
          if (files.length) void importFiles(files);
        }}
      />

      <AgentToolsDialog open={agentToolsOpen} onOpenChange={setAgentToolsOpen} />
      <ExportDialog open={exportOpen} onOpenChange={setExportOpen} onProblems={onProblems} />
      <PublishDialog open={publishOpen} onOpenChange={setPublishOpen} onProblems={onProblems} />
      <ShortcutsDialog open={shortcutsOpen} onOpenChange={setShortcutsOpen} />
      {problems ? (
        <ProblemsDialog
          title={problems.title}
          errors={problems.errors}
          onClose={() => setProblems(null)}
        />
      ) : null}
      <Toaster theme="dark" position="bottom-right" />
    </TooltipProvider>
  );
}

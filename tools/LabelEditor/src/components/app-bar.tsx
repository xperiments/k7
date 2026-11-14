import { useRef } from "react";
import {
  CassetteTape,
  ClipboardPaste,
  Copy,
  Download,
  Eye,
  FileJson,
  FilePlus2,
  FolderOpen,
  Keyboard,
  PencilRuler,
  Redo2,
  Save,
  Undo2,
  Upload,
} from "lucide-react";
import { toast } from "sonner";

import { Badge } from "@/components/reui/badge";
import { Button } from "@/components/ui/button";
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuGroup,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu";
import { Separator } from "@/components/ui/separator";
import { ToggleGroup, ToggleGroupItem } from "@/components/ui/toggle-group";
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip";
import { useEditor } from "@/hooks/use-editor";
import { exportTemplate, saveDocument } from "@/engine/actions";

export function AppBar({
  onExport,
  onShortcuts,
  onProblems,
  onImport,
}: {
  onExport: () => void;
  onShortcuts: () => void;
  onProblems: (title: string, errors: string[]) => void;
  onImport: () => void;
}) {
  const ed = useEditor();
  const fontRef = useRef<HTMLInputElement>(null);

  const docName = ed.doc?.name ?? "Untitled";

  const onSave = () => {
    const name = saveDocument(ed);
    toast.success("Document saved", { description: name });
  };

  const onExportOne = () => {
    const r = exportTemplate(ed);
    if (r.errs.length) {
      onProblems("This design cannot be exported yet", r.errs);
      return;
    }
    toast.success("Template exported", { description: r.name });
  };

  return (
    <header className="flex h-12 shrink-0 items-center gap-2 border-b border-border bg-background px-3">
      <div className="flex shrink-0 items-center gap-2 pr-1">
        <span
          className="grid size-7 place-items-center rounded-md border border-primary/60 text-primary"
          aria-hidden
        >
          <CassetteTape className="size-4" />
        </span>
        <div className="flex flex-col leading-none">
          <span className="whitespace-nowrap text-sm font-medium tracking-tight text-foreground">
            Label Editor
          </span>
          <span className="mt-0.5 hidden whitespace-nowrap text-xs uppercase tracking-[0.14em] text-muted-foreground xl:block">
            K7 · cassette print
          </span>
        </div>
      </div>

      <Separator orientation="vertical" className="mx-1 h-6" />

      <div className="flex min-w-0 items-center gap-1.5">
        <span className="max-w-56 truncate text-xs font-medium text-foreground">{docName}</span>
        <Badge variant="outline" className="tabnum">
          {ed.objs.length} layers
        </Badge>
      </div>

      <div className="ml-auto flex shrink-0 items-center gap-1">
        <div className="flex items-center gap-0.5">
          <IconButton label="Undo (⌘Z)" disabled={!ed.undo.length} onClick={() => ed.undoAct()}>
            <Undo2 />
          </IconButton>
          <IconButton label="Redo (⇧⌘Z)" disabled={!ed.redo.length} onClick={() => ed.redoAct()}>
            <Redo2 />
          </IconButton>
          <Separator orientation="vertical" className="mx-0.5 h-5" />
          <IconButton label="Copy (⌘C)" disabled={!ed.sel} onClick={() => ed.copyAct()}>
            <Copy />
          </IconButton>
          <IconButton label="Paste in place (⌘V)" disabled={!ed.clip} onClick={() => ed.pasteAct()}>
            <ClipboardPaste />
          </IconButton>
        </div>

        <Separator orientation="vertical" className="mx-1 h-6" />

        <ToggleGroup
          value={[ed.run ? "preview" : "design"]}
          onValueChange={(v) => ed.setRun(v.includes("preview"))}
          variant="outline"
          size="sm"
          spacing={0}
          aria-label="Mode"
        >
          <ToggleGroupItem value="design" className="gap-1.5 px-2.5">
            <PencilRuler className="size-3.5" />
            Design
          </ToggleGroupItem>
          <ToggleGroupItem value="preview" className="gap-1.5 px-2.5">
            <Eye className="size-3.5" />
            Preview
          </ToggleGroupItem>
        </ToggleGroup>

        <Separator orientation="vertical" className="mx-1 h-6" />

        <Tooltip>
          <TooltipTrigger
            render={
              <Button
                variant="ghost"
                size="icon-sm"
                onClick={onShortcuts}
                aria-label="Keyboard shortcuts"
              >
                <Keyboard />
              </Button>
            }
          />
          <TooltipContent>Keyboard shortcuts</TooltipContent>
        </Tooltip>

        <Tooltip>
          <TooltipTrigger
            render={
              <Button
                variant="outline"
                size="sm"
                className="gap-1.5"
                onClick={onImport}
                aria-label="Import a label template or an editor document"
              >
                <FolderOpen className="size-3.5" />
                <span className="hidden xl:inline">Import</span>
              </Button>
            }
          />
          <TooltipContent>
            Import a label-template JSON or an editor document
          </TooltipContent>
        </Tooltip>

        <DropdownMenu>
          <DropdownMenuTrigger
            render={
              <Button variant="outline" size="sm" className="gap-1.5">
                <Download className="size-3.5" />
                Export
              </Button>
            }
          />
          <DropdownMenuContent align="end" className="w-60">
            <DropdownMenuGroup>
              <DropdownMenuLabel>Export</DropdownMenuLabel>
              <DropdownMenuItem onClick={onExportOne}>
                <FileJson />
                Template JSON
                <span className="ml-auto text-xs text-muted-foreground">⌘E</span>
              </DropdownMenuItem>
              <DropdownMenuItem onClick={onExport}>
                <Download />
                All open designs…
                <span className="ml-auto text-xs text-muted-foreground">zip</span>
              </DropdownMenuItem>
            </DropdownMenuGroup>
            <DropdownMenuSeparator />
            <DropdownMenuGroup>
              <DropdownMenuLabel>Document</DropdownMenuLabel>
              <DropdownMenuItem onClick={onSave}>
                <Save />
                Save document
              </DropdownMenuItem>
              <DropdownMenuItem onClick={onImport}>
                <FolderOpen />
                Import a JSON file…
                <span className="ml-auto text-xs text-muted-foreground">⌘O</span>
              </DropdownMenuItem>
              <DropdownMenuItem onClick={() => ed.newDoc()}>
                <FilePlus2 />
                New document
              </DropdownMenuItem>
            </DropdownMenuGroup>
            <DropdownMenuSeparator />
            <DropdownMenuItem onClick={() => fontRef.current?.click()}>
              <Upload />
              Upload a font…
            </DropdownMenuItem>
          </DropdownMenuContent>
        </DropdownMenu>

        <input
          ref={fontRef}
          type="file"
          accept=".ttf,.otf"
          className="hidden"
          onChange={async (e) => {
            const f = e.target.files?.[0];
            e.target.value = "";
            if (!f) return;
            await ed.addFont(f);
            toast.success("Font added", { description: f.name });
          }}
        />
      </div>
    </header>
  );
}

function IconButton({
  label,
  disabled,
  onClick,
  children,
}: {
  label: string;
  disabled?: boolean;
  onClick?: () => void;
  children: React.ReactNode;
}) {
  return (
    <Tooltip>
      <TooltipTrigger
        render={
          <Button
            variant="ghost"
            size="icon-sm"
            disabled={disabled}
            onClick={onClick}
            aria-label={label}
          >
            {children}
          </Button>
        }
      />
      <TooltipContent>{label}</TooltipContent>
    </Tooltip>
  );
}

import { useMemo } from "react";
import { Bot, Check, Copy, Info } from "lucide-react";
import { toast } from "sonner";

import { Alert, AlertDescription, AlertTitle } from "@/components/reui/alert";
import { Badge } from "@/components/reui/badge";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { useEditor } from "@/hooks/use-editor";
import { labelEditorTools } from "@/webmcp/tools";
import { webmcpStatus } from "@/webmcp/register";

/**
 * What this page exposes to an agent. It is both the human's receipt (what can be done
 * from outside the UI) and the place the fallback bridge is named, for browsers without
 * the WebMCP API yet.
 */
export function AgentToolsDialog({
  open,
  onOpenChange,
}: {
  open: boolean;
  onOpenChange: (v: boolean) => void;
}) {
  const ed = useEditor();
  const tools = useMemo(() => labelEditorTools(ed), [ed]);
  const status = webmcpStatus;
  const bridge =
    typeof window !== "undefined" ? window.__labelEditorMcp : undefined;

  const copy = async () => {
    const list = tools.map(({ name, description, inputSchema }) => ({
      name,
      description,
      inputSchema,
    }));
    try {
      await navigator.clipboard.writeText(JSON.stringify(list, null, 2));
      toast.success("Tool list copied", { description: `${tools.length} tools` });
    } catch {
      toast.error("The clipboard is not available");
    }
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-lg">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <Bot className="size-4 text-primary" />
            Agent tools
          </DialogTitle>
          <DialogDescription>
            The editor's actions as tools an agent can call: read the design, add and change
            layers, set the colourway, import and export templates. Every call is an ordinary
            edit of the open design, and undoable.
          </DialogDescription>
        </DialogHeader>

        <div className="flex flex-wrap items-center gap-2">
          {status.available ? (
            <Badge variant="primary-light" className="gap-1">
              <Check className="size-3" />
              WebMCP · {status.registered.length} tools registered
            </Badge>
          ) : (
            <Badge variant="outline">WebMCP unavailable</Badge>
          )}
          <span className="text-xs text-muted-foreground">
            {status.tools} tools
            {status.via ? ` · ${status.via}.modelContext` : ""}
          </span>
        </div>

        {!status.available ? (
          <Alert variant="info">
            <Info />
            <AlertTitle>This browser does not offer the tools natively</AlertTitle>
            <AlertDescription>
              WebMCP needs Chrome 146 or newer with{" "}
              <code className="text-foreground">#enable-webmcp-testing</code> enabled. In any
              browser the same tools are on{" "}
              <code className="text-foreground">window.__labelEditorMcp</code>:{" "}
              <code className="text-foreground">
                __labelEditorMcp.call("label-editor-read")
              </code>
              .
              {status.error ? ` ${status.error}` : ""}
            </AlertDescription>
          </Alert>
        ) : (
          <Alert variant="success">
            <Check />
            <AlertTitle>The browser is offering these tools to its agent</AlertTitle>
            <AlertDescription>
              Registered through <code className="text-foreground">{status.via}.modelContext</code>
              . They are also on{" "}
              <code className="text-foreground">window.__labelEditorMcp</code> for a page
              script or extension.
            </AlertDescription>
          </Alert>
        )}

        <div className="max-h-[42vh] overflow-y-auto rounded-lg border border-border">
          <ul className="divide-y divide-border/70">
            {tools.map((tool) => (
              <li key={tool.name} className="flex flex-col gap-0.5 px-3 py-2">
                <code className="font-mono text-xs font-medium text-foreground">
                  {tool.name}
                </code>
                <p className="text-xs text-muted-foreground">{tool.description}</p>
              </li>
            ))}
          </ul>
        </div>

        <DialogFooter className="flex-row items-center justify-between gap-2 sm:justify-between">
          <span className="text-xs text-muted-foreground">
            {bridge ? "Bridge installed" : "Bridge unavailable"}
          </span>
          <div className="flex items-center gap-1.5">
            <Button variant="outline" size="sm" onClick={() => void copy()}>
              <Copy />
              Copy tool list
            </Button>
            <Button size="sm" onClick={() => onOpenChange(false)}>
              Done
            </Button>
          </div>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

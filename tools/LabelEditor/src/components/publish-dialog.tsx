import { useEffect, useMemo, useState } from "react";
import { Download, Package } from "lucide-react";
import { toast } from "sonner";

import { Badge } from "@/components/reui/badge";
import { Frame, FramePanel } from "@/components/reui/frame";
import { Button } from "@/components/ui/button";
import { Checkbox } from "@/components/ui/checkbox";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Field, FieldLabel } from "@/components/ui/field";
import { Input } from "@/components/ui/input";
import { useEditor } from "@/hooks/use-editor";
import { downloadPack, publishPack } from "@/engine/actions";
import { PACK_ID_MAX, PACK_NAME_MAX, PACK_AUTHOR_MAX } from "@/engine/labelPack";

const slug = (id: string) => id.replace(/^k7\./, "").replace(/[^a-z0-9-]/g, "-");

/**
 * Publish (docs/specs/art-packs.md §3): choose any subset of the open K7Arts, name the
 * pack, and build it in the browser. The editor has no device client; the download is the
 * file the App's Art packs page installs.
 */
export function PublishDialog({
  open,
  onOpenChange,
  onProblems,
}: {
  open: boolean;
  onOpenChange: (v: boolean) => void;
  onProblems: (title: string, errors: string[]) => void;
}) {
  const ed = useEditor();
  const [chosen, setChosen] = useState<Set<number>>(new Set());
  const [packId, setPackId] = useState("");
  const [name, setName] = useState("");
  const [author, setAuthor] = useState("");

  useEffect(() => {
    if (!open) return;
    setChosen(new Set(ed.docs.map((_, i) => i)));
    setPackId(slug(ed.tpl.id).slice(0, PACK_ID_MAX) || "k7art");
    setName(`${ed.tpl.name} pack`.slice(0, PACK_NAME_MAX));
    setAuthor((prev) => prev || "The Analog Sons");
    // Only when the dialog opens: typing in it must not be reset by a re-render.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open]);

  const rows = useMemo(
    () => (open ? ed.validateAll() : []),
    // The version is the editor's change counter; validateAll reads every document.
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [open, ed.getVersion()],
  );
  const anyErr = rows.some((r, i) => chosen.has(i) && r.errs.length > 0);

  const toggle = (i: number, checked: boolean) => {
    setChosen((prev) => {
      const next = new Set(prev);
      if (checked) next.add(i);
      else next.delete(i);
      return next;
    });
  };

  const publish = () => {
    const indices = [...chosen].sort((a, b) => a - b);
    if (!indices.length) {
      onProblems("Publish refused", ["Choose at least one design for the pack."]);
      return;
    }
    const r = publishPack(ed, indices, {
      packId: packId.trim(),
      name: name.trim(),
      author: author.trim(),
    });
    if (r.errs.length) {
      onProblems("Publish refused", r.errs);
      return;
    }
    if (!r.pack) return;
    const file = downloadPack(r.pack);
    toast.success("Pack published", {
      description: `${file} · ${r.pack.designCount} design${r.pack.designCount === 1 ? "" : "s"} · ${r.pack.totalSize} B`,
    });
    onOpenChange(false);
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-xl">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2">
            <Package className="size-4 text-primary" />
            Publish a K7Art pack
          </DialogTitle>
          <DialogDescription>
            One <code>.k7artpack</code>: the label sheet the K7 prints, the designs&apos; J-cards
            the App draws, and a header with the pack&apos;s id, name and author. Built in this
            browser and downloaded; the App installs it.
          </DialogDescription>
        </DialogHeader>

        <div className="grid grid-cols-2 gap-3">
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Pack id</FieldLabel>
            <Input
              value={packId}
              onChange={(e) => setPackId(e.target.value)}
              className="h-8 font-mono text-xs"
              spellCheck={false}
              maxLength={PACK_ID_MAX}
              aria-label="Pack id"
            />
            <p className="text-xs text-muted-foreground">
              A slug: lowercase letters, digits and dashes, up to {PACK_ID_MAX} characters.
            </p>
          </Field>
          <Field className="gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Name</FieldLabel>
            <Input
              value={name}
              onChange={(e) => setName(e.target.value)}
              className="h-8"
              maxLength={PACK_NAME_MAX}
              aria-label="Pack name"
            />
            <p className="text-xs text-muted-foreground">
              Shown in the App&apos;s pack list, up to {PACK_NAME_MAX} bytes.
            </p>
          </Field>
          <Field className="col-span-2 gap-1.5">
            <FieldLabel className="text-xs text-muted-foreground">Author</FieldLabel>
            <Input
              value={author}
              onChange={(e) => setAuthor(e.target.value)}
              className="h-8"
              maxLength={PACK_AUTHOR_MAX}
              aria-label="Pack author"
            />
          </Field>
        </div>

        <div className="flex items-center justify-between">
          <span className="text-xs text-muted-foreground">
            Designs in the pack ({chosen.size} of {rows.length})
          </span>
          <div className="flex items-center gap-1.5">
            <Button
              variant="ghost"
              size="sm"
              onClick={() => setChosen(new Set(ed.docs.map((_, i) => i)))}
            >
              All
            </Button>
            <Button variant="ghost" size="sm" onClick={() => setChosen(new Set())}>
              None
            </Button>
          </div>
        </div>

        <Frame stacked spacing="sm" className="max-h-64 overflow-y-auto">
          {rows.length ? (
            rows.map((r, i) => {
              const doc = ed.docs[i];
              const checked = chosen.has(i);
              return (
                <FramePanel key={i} className="py-2">
                  <label className="flex cursor-default items-center gap-2 px-1">
                    <Checkbox
                      checked={checked}
                      onCheckedChange={(v) => toggle(i, v === true)}
                      aria-label={`Include ${r.name}`}
                    />
                    <span className="truncate text-sm font-medium">{r.name}</span>
                    {r.errs.length ? (
                      <Badge variant="destructive-light" className="ml-auto shrink-0">
                        {r.errs.length} issue{r.errs.length === 1 ? "" : "s"}
                      </Badge>
                    ) : (
                      <Badge variant="outline" className="ml-auto shrink-0 text-primary">
                        ready
                      </Badge>
                    )}
                    <Badge variant="outline" className="shrink-0 tabnum">
                      {doc?.data.tpl?.id ?? "k7.untitled"}
                    </Badge>
                  </label>
                  {r.errs.length ? (
                    <ul className="mt-2 flex flex-col gap-1 px-1">
                      {r.errs.slice(0, 4).map((e, k) => (
                        <li key={k} className="flex gap-1.5 text-xs text-muted-foreground">
                          <span className="mt-1 size-1 shrink-0 rounded-full bg-destructive" />
                          <span>{e}</span>
                        </li>
                      ))}
                    </ul>
                  ) : null}
                </FramePanel>
              );
            })
          ) : (
            <FramePanel>
              <p className="text-xs text-muted-foreground">No designs are open.</p>
            </FramePanel>
          )}
        </Frame>

        <DialogFooter className="flex-row items-center justify-between gap-2 sm:justify-between">
          <Button variant="ghost" size="sm" onClick={() => onOpenChange(false)}>
            Cancel
          </Button>
          <Button size="sm" onClick={publish} disabled={!chosen.size || anyErr}>
            <Download />
            Publish and download
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

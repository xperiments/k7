import { useCallback, useState } from "react";
import { toast } from "sonner";

import { openFile } from "@/engine/actions";
import type { Editor } from "@/engine/editor";

/**
 * Import one or more `.json` files: a `label-template v1` design, or an editor document.
 * A file the editor refuses is reported and nothing is left behind; several files open in
 * order, each in its own tab.
 */
export function useImport(
  ed: Editor,
  onProblems: (title: string, errors: string[]) => void,
) {
  const [busy, setBusy] = useState(false);

  const importFiles = useCallback(
    async (files: FileList | File[]) => {
      const list = [...files].filter((f) => /\.json$/i.test(f.name));
      if (!list.length) {
        toast.error("That is not a JSON file", {
          description:
            "Import a label template (label-template v1) or an editor document (.lvgl.json).",
        });
        return;
      }
      setBusy(true);
      try {
        for (const f of list) {
          try {
            const what = await openFile(ed, f);
            toast.success(what, { description: f.name });
          } catch (err) {
            onProblems(`“${f.name}” could not be imported`, [
              (err as Error)?.message || String(err),
            ]);
            return;
          }
        }
        if (list.length > 1)
          toast.success(`${list.length} designs imported`, {
            description: list.map((f) => f.name).join(", "),
          });
      } finally {
        setBusy(false);
      }
    },
    [ed, onProblems],
  );

  return { importFiles, busy };
}

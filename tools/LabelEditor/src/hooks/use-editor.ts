import { useSyncExternalStore } from "react";
import { editor } from "@/engine";

/** Read the editor: it is one store, and every view of it re-renders on the same version. */
export function useEditor() {
  useSyncExternalStore(editor.subscribe, editor.getVersion, editor.getVersion);
  return editor;
}

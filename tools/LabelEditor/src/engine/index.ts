import { Editor } from "./editor";

/**
 * One editor per page, created before the first render: it owns the documents in
 * localStorage, the fonts and the LVGL module, so it must outlive any component.
 */
export const editor = new Editor();

// In development the store is reachable from the console (and from the parity checks).
if (import.meta.env.DEV) {
  (window as unknown as { __editor?: Editor }).__editor = editor;
}

export type { Editor };
export * from "./types";

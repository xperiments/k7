import { createRoot } from "react-dom/client";
import "@/index.css";
import { App } from "@/App";
import { editor } from "@/engine";
import { registerLabelEditorTools } from "@/webmcp/register";

// The editor's actions, offered to a browser agent as WebMCP tools (and always on
// window.__labelEditorMcp). Registration is best-effort: no API, no noise, the app works.
void registerLabelEditorTools(editor);

createRoot(document.getElementById("root")!).render(<App />);

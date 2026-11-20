import { readFileSync } from "node:fs";
import { fileURLToPath, URL } from "node:url";

import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import type { Plugin, ViteDevServer } from "vite";
import { defineConfig } from "vitest/config";

/**
 * The agent skill is the app's `skills/k7-art/SKILL.md`, served as
 * `/skill.md` so an agent that reaches the page can read how to use its tools, and emitted
 * into `dist/` so a built copy carries it too. Read at request time, never bundled: one
 * source of truth for the guide and the page.
 */
const skillFile = fileURLToPath(
  new URL("./skills/k7-art/SKILL.md", import.meta.url),
);

function labelSkill(): Plugin {
  return {
    name: "label-editor-skill",
    configureServer(server: ViteDevServer) {
      server.middlewares.use((req, res, next) => {
        const path = (req.url ?? "").split("?")[0];
        if (path !== "/skill.md") return next();
        res.setHeader("content-type", "text/markdown; charset=utf-8");
        res.end(readFileSync(skillFile));
      });
    },
    generateBundle() {
      this.emitFile({ type: "asset", fileName: "skill.md", source: readFileSync(skillFile) });
    },
  };
}

export default defineConfig(({ command, isPreview }) => ({
  /**
   * The editor is the Site's third document (k7-site.md D1, ADR 0112). The build is served
   * from the Site's `/k7art` directory, so its hashed assets and public files resolve under
   * `/k7art/`; the Site's build copies `dist/` to `web/dist/k7art/`. The preview of that
   * build serves it at the same base. The 5178 development server is the editor on its own,
   * at the root.
   */
  base: command === "build" || isPreview ? "/k7art/" : "/",
  plugins: [react(), tailwindcss(), labelSkill()],
  resolve: {
    alias: {
      "@": fileURLToPath(new URL("./src", import.meta.url)),
    },
  },
  server: {
    port: 5178,
  },
  test: {
    include: ["src/**/*.test.ts"],
    environment: "node",
  },
}));

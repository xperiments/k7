import { readFileSync } from "node:fs";
import { fileURLToPath, URL } from "node:url";

import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import type { Plugin, ViteDevServer } from "vite";
import { defineConfig } from "vitest/config";

/**
 * The agent skill is the app's `skills/k7-labels/SKILL.md`, served as
 * `/skill.md` so an agent that reaches the page can read how to use its tools, and emitted
 * into `dist/` so a built copy carries it too. Read at request time, never bundled: one
 * source of truth for the guide and the page.
 */
const skillFile = fileURLToPath(
  new URL("./skills/k7-labels/SKILL.md", import.meta.url),
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

export default defineConfig({
  base: "./",
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
});

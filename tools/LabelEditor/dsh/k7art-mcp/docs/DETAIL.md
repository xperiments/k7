# K7Art editor

The K7Art editor designs a cassette's artwork: the **label** the K7 prints in its two-finger
pose, and the **J-card** the App draws on an album with no cover — the cover on the case front,
the spine on the shelf, the flap on the back. It runs as a page at `/k7art` and offers what it
can do as sixteen tools, `k7art-read` through `k7art-history`.

This bundle connects those tools to DSH over MCP, through the bridge in the same checkout:
`src/webmcp/mcp-server.ts`. The bridge is what holds the browser — it opens the editor in Chrome,
reads `window.__labelEditorMcp` from the page, and forwards each call there, so the tools the
client sees are the page's own registry and not a copy.

## Running it

The bridge has to be up before the session that uses it:

```bash
cd tools/LabelEditor
npm run mcp -- --http 8787
```

It prints `add it to a client as http://127.0.0.1:8787/mcp` and keeps a Chrome window on the
editor. Add this directory in **Plugins → Add plugin**, then open a new session: the tools
arrive as `mcp__k7art__k7art-read`, `mcp__k7art__k7art-add-layer`, `mcp__k7art__k7art-publish-pack`
and the rest.

`--attach http://127.0.0.1:9222` drives a Chrome you already run on a debugging port;
`--url` points the bridge at another editor (the Site's `/k7art`, for instance).

## Using it

Call **`k7art-guide`** first: it returns the designing guide — the label's print and the Card's
window, the J-card's sheet and its three panels, the colour and type roles, what the exporter
refuses. Then `k7art-read`, then the edit tools. A refused call throws, and the message names the
argument.

Designs are exported as a K7Art JSON (`k7art-export-template`) or packed into a `.k7artpack`
(`k7art-publish-pack`) that the App installs on the device under Settings → Art packs.

## Limits

- The bridge needs a Google Chrome and a Node that can run the TypeScript source; it reads the
  editor from `K7ART_URL`, default `http://localhost:5178`.
- The session's tool list is fixed when it opens: start the bridge, then open a new session.
- No authentication and no remote bind: the server listens on `127.0.0.1` only. `--host 0.0.0.0`
  exists for a deliberate case, and it exposes a browser-driving endpoint to the network.
- The editor's Preview is the editor's own; the tools edit the design, not the view.

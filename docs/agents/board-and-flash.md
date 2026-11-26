# Board and flash

One board, one port, one build directory: two sessions doing any of these at once corrupt
the result silently. This is the record; the `board-and-flash` skill is a pointer to it and
is installed per machine (`.agents/` is gitignored here).

| Resource | Why it admits one session |
|---|---|
| `build/<variant>/` | cmake caches absolute paths and the generated config |
| `web/node_modules/` | a concurrent `npm ci` or `npm install` corrupts it |
| `/dev/cu.usbserial-8344440` | one board; `build.sh` kills whatever holds the port before flashing, so a second `flash` silently ends the first session's `monitor` |
| the dev server on `8000` | one origin on one port; the dev certificates assume it |

Check first with `lsof -t <port>`, then lock it: `mkdir -p .scratch/locks && lockf -k
.scratch/locks/<name>` around anything the tool does not guard itself. macOS has no `flock`;
an atomic `mkdir` under the same directory does for a shell that is not going through `lockf`.

Build and flash with `./tools/build.sh <variant> build flash monitor`; the variants are the
`sdkconfig.*` files, and `package.json` names the common ones (`flash`, `flash:dev`,
`monitor`, `release`). The K7Player has its own: `./player/build.sh build`.

Never leave a `monitor` attached — `<board>.local` and the serial port are shared with
whoever else is flashing. The port name above is this machine's; `ls /dev/cu.*` when the
board enumerates differently.

## The skill, on another machine

`.agents/` is gitignored ("installed, not vendored"), so the pointer skill is recreated:

```bash
mkdir -p .agents/skills/board-and-flash
cat > .agents/skills/board-and-flash/SKILL.md <<'EOF'
---
name: board-and-flash
description: Use before building, flashing or monitoring the ESP32 board, or when two sessions might share a build directory, the serial port or the dev server. Read docs/agents/board-and-flash.md first.
---
Read `docs/agents/board-and-flash.md` in the repo root before building, flashing or
monitoring: it lists what admits one session at a time and how to take it.
EOF
```

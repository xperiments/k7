# 01 — Host build and fixture corpus for the broadcast path

**What to build:** A developer can build and run the real server on macOS with no board
attached, point it at a generated corpus of test tracks, and pull a live stream from it with an
ordinary HTTP client. A second client can connect while a first one sits silent and idle, and
still gets served.

This is the first vertical slice because the two-tier acceptance rule makes the host build the
test layer every later ticket depends on — and because it cannot be built at all today: the
decoder's vendored headers are missing from the workspace, and the include directories the
build registers do not exist.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] Vendor the three missing decoder headers (public domain / MIT-0) so the decode layer compiles, and repair the include-directory registration so the build no longer references directories that do not exist.
- [ ] The host build target compiles with one command and produces a runnable server.
- [ ] The host build compiles the **same production sources** as the firmware. No forked copy of any component source, and no target conditional compilation anywhere in a production component — host-only behaviour is supplied by the shim.
- [ ] The server takes its music directory and port as runtime values, binds to the loopback interface on a non-privileged port, and needs no elevation.
- [ ] `GET /stream` returns continuous audio at the station rate.
- [ ] A client that connects and sends no request cannot delay or block a second client. Measured: the second client receives response headers after it connects, not several seconds later.
- [ ] A fixture generator regenerates the corpus with a single command. The corpus covers all three source formats and includes at least one input rate different from the station rate.
- [ ] A measurement harness reports encoding cost per frame and free heap in both memory classes, runnable on the host; this is the harness later tickets reuse.

Spec: `docs/specs/sd-stream-server.md` §10 (H1–H4, H6), §5 D1, §9 T4.

## Comments

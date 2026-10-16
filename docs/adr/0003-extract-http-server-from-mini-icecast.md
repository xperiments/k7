# Extract the HTTP surface from the audio producer

`resources/mini_icecast.c` currently both produces audio and owns the listening socket, the
accept loop, request parsing and routing. Adding a file index turns one HTTP consumer into
three, so we extract a `http_server` component that owns the socket and routes, leaving
`mini_icecast` as pure audio production plus a narrow fd-registration entry point.

## Considered Options

- **Keep everything in `mini_icecast.c`.** Rejected. Its accept thread calls `handle_new()`
  inline and blocks on a 2 s receive timeout, so a client that connects and says nothing
  already stalls every new connection. Bolting a download read loop onto that same thread
  makes the defect worse rather than fixing it.
- **Extract a shared HTTP layer now.** Chosen. The extraction is the fix for the accept-loop
  defect, not a refactor undertaken for tidiness.

## Consequences

Routing happens on a short-lived per-connection task, capped by a connection limit. Only the
`GET /stream` route hands its socket onward: `mini_icecast` registers it under the same
broadcast lock that already carries the burst handoff, so the client gets history and then the
next live chunk with no gap and no duplicate. Index and download are served entirely inside the
routing task.

The dependency direction is one-way and must stay that way: `mini_files` depends on
`http_server`, and nothing depends on `mini_files`. `mini_icecast` must never learn about files
or HTML — if it does, the seam has been crossed.

This is the largest structural change to code that currently works, which is why it is
sequenced before the encoder work rather than alongside it.

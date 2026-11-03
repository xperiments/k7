# The library has two sources

Every library view in the App is device-only today: `Ask` parses a sentence and then calls
`library_search` over `/mcp`; Media lists the device's tree; Player asks for its stats. So
standalone mode is the Card area and Settings, and the phrase "works independently of the
ESP32" is true only of measuring and building an image.

That collides with the local Provider. A WebLLM model running in the tab turns a sentence into
filters with no key and no network — and then has nothing to search, because the search is the
device's. The feature that motivated the secure origin would be a parser attached to nothing.

The App already holds what it needs: the **Last image**, a few kilobytes in `k7-snapshots`
carrying every path and measurement of the last run, joined to `k7-media` for titles and
covers.

## Decision

- **`LibrarySource` is a seam** with four methods — `search`, `list`, `stats`, `track` — and two
  implementations: the **device** (MCP over the bridge, or direct on the Device copy) and the
  **image** (the Last image's rows joined to the media store).
- **Local-first resolution.** With a link, the device answers, because it reflects the card as
  it is now. Without one, the image answers, for the scope it was built for. With neither, the
  surface says which is missing.
- **The surface names its source.** "The player · 4 120 tracks" and "Last image · 11 February ·
  4 118 tracks" are different facts and must not look alike.
- **One conformance corpus** — rows, queries, expected results — runs against both
  implementations, in the App's vitest and the device's host tests.

## Consequences

- **Two implementations of the filter semantics** is a standing drift risk; the corpus is the
  mitigation, and it is mandatory rather than nice to have.
- **The image's library is only as complete as the last run.** A partial run is a partial
  library, and the surface has to say so rather than look empty.
- **Standalone means measure, browse, ask and draft**; the device adds send, play, live status
  and the stream. That is the division the product's premise describes.
- **The bridge keeps `library.*`** for the connected case, now with a second caller instead of
  being the only way to see a library.

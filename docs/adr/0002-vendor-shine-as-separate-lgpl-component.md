# Keep shine a separate vendored component, never linked into our sources

`shine` is **LGPL-2.0-only**, and LGPL-2.0 §6 is the clause that permits using it under terms
of your own choosing — but only if the recipient can relink against a modified version of the
library. So `shine` is vendored as an unmodified source tree in its own component directory
and built from source in the consumer's build tree, never copied into `mini_icecast/` or
`main/`, and never pre-built into a blob.

## Considered Options

- **Paste the sources into `src/`.** Rejected. It destroys the relink path, and the licensing
  position degrades to something that has to be argued rather than merely complied with.
- **Vendor as a separate source component.** Chosen. §6a's relink path then exists naturally,
  our own components' licence is unaffected, and compliance reduces to shipping the licence
  text plus a notice.

## Consequences

`fknrdcls/mp3_shine_esp32` is **GPL-3.0** and must not be used at all — it would impose GPL on
the whole project. Its Xtensa optimisations are tempting given the encoder's CPU cost; if they
are ever wanted they must be obtained from a differently-licensed source or written fresh.

The vendored-source arrangement is also what keeps the host tests possible: the same sources
build with a host `gcc`, which is how the encoder's frame granularity and bit-rate accuracy
were measured before any hardware was involved.

# tls_material

The certificate and key the device serves, as bytes in the image.

`tls/xtape.local.pem` and `tls/xtape.local.key` are checked in at the repository
root and signed once by the CA in `tls/ca.pem`; `tls/ca.key` is never embedded and
never flashed. This component embeds the leaf and its key with CMake's
`EMBED_TXTFILES` and hands them to `http_server` as `const uint8_t[]` + length, so
every image serves the same pair and rotating it is one file and one flash
(front door T1, T1a; ADR 0048).

Nothing here is generated at runtime and nothing is written to flash by the device.

**No pair in the image.** If the two files are absent the component still compiles,
`tls_material_available()` is false, and the http server logs the reason and starts
only the plain listener: the stream on port 80 must never depend on TLS (front
door §6).

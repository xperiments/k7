# Packaging & licensing constraints (verified against sources, not recalled)

These are facts that gate the library's *shape* and its *legal ability to be depended
on*. Each was checked directly against the registry or the upstream repository on
**2026-10-16**, because both facts here are the kind that are commonly misremembered.
(Version numbers and licenses therefore reflect that date and will drift.)

## 1. Espressif's audio stack is under a non-standard, restricted license

Every Espressif audio component in the registry reports the license as **`Custom`**,
not Apache-2.0 or MIT. Verified via the registry API
(`https://components.espressif.com/api/components?q=<name>`):

| Component | Version | Registry license |
|---|---|---|
| `espressif/esp_audio_codec` | 2.6.2 | Custom |
| `espressif/esp_hls_stream` | 1.0.4 | Custom |
| `espressif/esp_extractor` | 1.0.7 | Custom |
| `espressif/esp_audio_render` | 1.1.0 | Custom |
| `espressif/gmf_audio` | 1.1.0 | Custom |
| `espressif/esp_codec_dev` | 1.6.2 | Custom |

The `Custom` label is not a registry artefact. The actual license text in
`espressif/esp-adf-libs` is the **"Espressif Modified MIT License"**, which is MIT with
three added clauses. Quoting `esp_audio_codec/LICENSE` verbatim:

> Permission is hereby granted for use **EXCLUSIVELY with Espressif Systems products**.
> […]
> 1. This Software MUST BE USED IN CONJUNCTION WITH ESPRESSIF SYSTEMS PRODUCTS.
> 2. The above copyright notice and this permission notice shall be included in all copies
>    or substantial portions of the Software.
> 3. Redistribution of the Software in source or binary form **FOR USE WITH NON-ESPRESSIF
>    PRODUCTS is strictly prohibited**.

Source: [`esp_audio_codec/LICENSE`](https://raw.githubusercontent.com/espressif/esp-adf-libs/master/esp_audio_codec/LICENSE)
and identically-worded [`esp_hls_stream/LICENSE`](https://raw.githubusercontent.com/espressif/esp-adf-libs/master/esp_hls_stream/LICENSE).
By contrast `esp-adf` itself is the older **"Espressif MIT License"** ("Permission is
hereby granted for use on all ESPRESSIF SYSTEMS products…"), see
[`esp-adf/LICENSE`](https://raw.githubusercontent.com/espressif/esp-adf/master/LICENSE).
Note that `https://raw.githubusercontent.com/espressif/esp-adf-libs/master/LICENSE` (a
single top-level LICENSE) returns **404** — the license is per-component, so each
dependency must be checked individually.

### What this means for xtape — and what it does *not*

- **It does not block the stated goal.** xtape targets ESP32-S3, i.e. an Espressif
  product, so clause 1 is satisfied by construction. The library may depend on these
  components and use them freely.
- **It does block vendoring them into the library.** Clause 3 forbids redistributing
  that code for use with non-Espressif products. If xtape ever supports a non-Espressif
  target (or a host/DSP simulation build), the Espressif audio components cannot be
  bundled into it. They must stay **external registry dependencies**, not copied trees.
- **Practical consequence — keep the dependency optional.** The library should not
  *require* an Espressif-licensed decoder. The clean resolution is the one a HAL/backend
  seam gives for free: define the decoder as a pluggable backend, ship a permissively
  licensed default (e.g. a public-domain/MIT MP3 decoder), and offer the Espressif
  `esp_audio_codec`/AAC path as an opt-in component for those who want AAC and accept
  the terms. That keeps the core redistributable and the AAC capability available.
- **Attribution obligation.** Clause 2 is plain MIT-style attribution: any binary
  distribution must carry the notice and permission text. The library needs a
  `LICENSES/` directory (or an `idf_component.yml` `license` field plus a `LICENSE`
  file) enumerating the third-party licenses it pulls in.
- The `esp_hls_stream` component also confirms Espressif's own model: it is
  `Custom`-licensed and implemented in `esp-adf-libs`, so HLS support is an
  **optional add-on to compose with**, not something xtape should reimplement or absorb.

## 2. Component registry naming and packaging rules

Verified against the official manifest reference,
[`idf_component.yml` Manifest File](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html):

- The manifest must sit in the **component root**. Registry identity is
  `namespace/component_name`; the namespace may be omitted only for the default
  `espressif` namespace.
- `version` is **required to upload** (it may instead come from a git tag or
  `compote component upload --version`).
- **Either** a `license` field with a valid SPDX identifier **or** a `LICENSE` /
  `LICENSE.txt` file must be present. If neither can be determined the registry records
  the component as `Custom` — which is how the Espressif components above ended up
  labelled. For xtape this is a deliberate choice to make, not a default to fall into.
- `targets:` is optional and may be omitted when the component is target-agnostic; for
  xtape listing `esp32s3` (and any other genuinely supported targets) is the honest
  declaration, and the component should `#error` on unsupported targets rather than
  fail obscurely at runtime.
- Dependencies support `require: public|private|no`, mapping onto
  `REQUIRES`/`PRIV_REQUIRES` in `idf_component_register`. A network+codec library
  should keep its decoder and transport dependencies **private** wherever the public
  headers do not expose their types — which is a design constraint on the public
  headers: if `xtape.h` never names an `esp_http_client_handle_t` or a decoder struct,
  the library keeps its dependencies private and its ABI small.
- `files.exclude` / `include` control what is archived for the registry, and the default
  exclusion list already drops `build/`, `managed_components/`, `sdkconfig`, and
  `.DS_Store`. Note the tree currently contains a stray `.DS_Store` in the workspace
  root.
- `examples/` is auto-discovered, so example apps belong in `examples/` and need no
  manifest entry.

## 3. Name availability

`https://components.espressif.com/api/components?q=xtape` returns
`{"error":"ComponentNotFoundError"}` — the name is **unclaimed** on the registry. That
is an availability fact only, not a reservation; the namespace under which it would be
published is a separate decision that the user owns (`<namespace>/xtape`).

## Reproduction

```bash
curl -s "https://components.espressif.com/api/components?q=esp_audio_codec" | \
  python3 -m json.tool | grep -i -A2 license
curl -sL https://raw.githubusercontent.com/espressif/esp-adf-libs/master/esp_audio_codec/LICENSE
```
